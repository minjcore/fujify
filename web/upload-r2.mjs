// upload-r2.mjs — đẩy cả thư mục content/ (và download/) lên R2 bucket private.
//
//   node upload-r2.mjs                 # đẩy ./content
//   node upload-r2.mjs content download # đẩy nhiều thư mục
//   R2_BUCKET=other node upload-r2.mjs  # đổi bucket (mặc định "fujify-production")
//
// Dùng wrangler có sẵn (không cần npm dep). Key R2 = "<tên-thư-mục>/<đường-dẫn>",
// ví dụ content/home.md → key "content/home.md" (khớp app.js + Worker).
import { spawn } from "node:child_process";
import { readdir, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const BUCKET = process.env.R2_BUCKET || "fujify-production";
const DIRS = process.argv.slice(2).length ? process.argv.slice(2) : ["content"];
const CONCURRENCY = 4;

const TYPES = {
  ".md": "text/markdown; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".txt": "text/plain; charset=utf-8",
  ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
  ".svg": "image/svg+xml", ".dmg": "application/x-apple-diskimage", ".zip": "application/zip",
};

async function walk(dir) {
  const out = [];
  for (const entry of await readdir(dir, { withFileTypes: true })) {
    if (entry.name.startsWith(".")) continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...(await walk(full)));
    else out.push(full);
  }
  return out;
}

function put(file, key) {
  const ct = TYPES[path.extname(file).toLowerCase()] || "application/octet-stream";
  const args = ["r2", "object", "put", `${BUCKET}/${key}`,
                `--file=${file}`, `--content-type=${ct}`, "--remote"];
  return new Promise((resolve, reject) => {
    const p = spawn("npx", ["wrangler", ...args], { stdio: ["ignore", "ignore", "inherit"] });
    p.on("close", (code) => (code === 0 ? resolve() : reject(new Error(`put ${key} → exit ${code}`))));
  });
}

async function main() {
  // gom (file, key) cho mọi thư mục
  const jobs = [];
  for (const d of DIRS) {
    const base = path.resolve(HERE, d);
    let files;
    try { files = await walk(base); }
    catch { console.error(`bỏ qua: không thấy thư mục ${d}`); continue; }
    for (const f of files) {
      const key = `${d}/${path.relative(base, f).split(path.sep).join("/")}`;
      jobs.push({ f, key });
    }
  }
  if (!jobs.length) { console.error("Không có file để đẩy."); process.exit(1); }

  console.log(`Đẩy ${jobs.length} file → bucket "${BUCKET}" (remote)…`);
  let i = 0, ok = 0;
  async function worker() {
    while (i < jobs.length) {
      const { f, key } = jobs[i++];
      try { await put(f, key); ok++; console.log(`  ✓ ${key}`); }
      catch (e) { console.error(`  ✗ ${key}: ${e.message}`); }
    }
  }
  await Promise.all(Array.from({ length: CONCURRENCY }, worker));
  console.log(`Xong: ${ok}/${jobs.length} file.`);
  if (ok < jobs.length) process.exit(1);
}

main();
