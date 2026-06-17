// Fujify CDN Worker — serves the R2 content bucket over HTTP.
//
// Routes (bind on cdn.fujify.app/*):
//   GET /content/<page>.md   → markdown for the site (app.js fetches these)
//   GET /download/<file>     → release binaries (DMG, zip…) with Range support
//   GET /                    → tiny index (health check)
//
// The R2 bucket stays PRIVATE — public access is NOT enabled. This Worker is the only
// reader (via the BUCKET binding) and the sole public gateway, so it controls exactly
// what is exposed. Adds CORS, caching, ETag/304, and Range (resumable downloads).
// Served content is meant to be publicly readable, so Access-Control-Allow-Origin is "*".

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, HEAD, PUT, OPTIONS",
  "Access-Control-Allow-Headers": "Range, If-None-Match, If-Modified-Since, Authorization, Content-Type",
  "Access-Control-Expose-Headers": "Content-Length, Content-Range, ETag, Accept-Ranges",
};

const TYPES = {
  md: "text/markdown; charset=utf-8",
  json: "application/json; charset=utf-8",
  txt: "text/plain; charset=utf-8",
  png: "image/png",
  jpg: "image/jpeg",
  jpeg: "image/jpeg",
  svg: "image/svg+xml",
  dmg: "application/x-apple-diskimage",
  zip: "application/zip",
};

function contentType(key, meta) {
  if (meta && meta.contentType) return meta.contentType;
  const ext = key.split(".").pop().toLowerCase();
  return TYPES[ext] || "application/octet-stream";
}

// Cache longer for binaries, shorter for editable markdown.
function cacheControl(key) {
  if (key.startsWith("download/")) return "public, max-age=86400, immutable";
  if (key.endsWith(".md")) return "public, max-age=60, stale-while-revalidate=300";
  return "public, max-age=3600";
}

// ---- auth helpers (stateless HMAC tokens; passwords hashed in KV) ----
const ENC = new TextEncoder();
function b64url(buf) {
  return btoa(String.fromCharCode(...new Uint8Array(buf)))
    .replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
function unb64url(s) {
  s = s.replace(/-/g, "+").replace(/_/g, "/");
  const bin = atob(s), u = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
  return u;
}
function jsonRes(obj, status = 200) {
  return new Response(JSON.stringify(obj),
    { status, headers: { ...CORS, "Content-Type": "application/json" } });
}
function userKey(email) { return email.toLowerCase().replace(/[^a-z0-9]+/g, "_"); }
async function hmacKey(secret) {
  return crypto.subtle.importKey("raw", ENC.encode(secret),
    { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
}
async function makeToken(env, email) {
  const payload = b64url(ENC.encode(JSON.stringify({ e: email, exp: Date.now() + 30 * 864e5 })));
  const sig = b64url(await crypto.subtle.sign("HMAC", await hmacKey(env.AUTH_SECRET), ENC.encode(payload)));
  return payload + "." + sig;
}
async function verifyToken(env, token) {
  if (!token || !env.AUTH_SECRET) return null;
  const [payload, sig] = token.split(".");
  if (!payload || !sig) return null;
  const ok = await crypto.subtle.verify("HMAC", await hmacKey(env.AUTH_SECRET),
    unb64url(sig), ENC.encode(payload));
  if (!ok) return null;
  try {
    const p = JSON.parse(new TextDecoder().decode(unb64url(payload)));
    return p.exp > Date.now() ? p.e : null;
  } catch { return null; }
}
async function pbkdf2(password, salt) {
  const base = await crypto.subtle.importKey("raw", ENC.encode(password), "PBKDF2", false, ["deriveBits"]);
  const bits = await crypto.subtle.deriveBits(
    { name: "PBKDF2", salt, iterations: 100000, hash: "SHA-256" }, base, 256);
  return b64url(bits);
}

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS });
    }

    const url = new URL(request.url);
    let key = decodeURIComponent(url.pathname.replace(/^\/+/, ""));

    // --- accounts: POST /auth/signup | /auth/login → { ok, token, email } ---
    if (request.method === "POST" && (key === "auth/signup" || key === "auth/login")) {
      if (!env.AUTH_SECRET) return jsonRes({ ok: false, error: "server not configured" }, 500);
      let body; try { body = await request.json(); } catch { body = null; }
      const email = (body?.email || "").trim().toLowerCase();
      const password = body?.password || "";
      if (!email || !password) return jsonRes({ ok: false, error: "email & password required" }, 400);
      const existing = await env.USERS.get(email);
      if (key === "auth/signup") {
        if (existing) return jsonRes({ ok: false, error: "email already registered" }, 409);
        const salt = crypto.getRandomValues(new Uint8Array(16));
        await env.USERS.put(email, JSON.stringify({ salt: b64url(salt), hash: await pbkdf2(password, salt) }));
      } else {
        if (!existing) return jsonRes({ ok: false, error: "no such account" }, 401);
        const rec = JSON.parse(existing);
        if (await pbkdf2(password, unb64url(rec.salt)) !== rec.hash)
          return jsonRes({ ok: false, error: "wrong password" }, 401);
      }
      return jsonRes({ ok: true, token: await makeToken(env, email), email });
    }

    // --- authenticated upload: PUT /library/<name> (Bearer login token) → user's folder ---
    if (request.method === "PUT") {
      if (!key.startsWith("library/") || key.includes(".."))
        return new Response("Forbidden", { status: 403, headers: CORS });
      const email = await verifyToken(env, (request.headers.get("Authorization") || "").replace(/^Bearer\s+/, ""));
      if (!email) return new Response("Unauthorized", { status: 401, headers: CORS });
      const scoped = "library/" + userKey(email) + "/" + key.slice("library/".length);
      const obj = await env.BUCKET.put(scoped, request.body, {
        httpMetadata: { contentType: contentType(scoped) },
      });
      return jsonRes({ ok: true, key: scoped, size: obj.size }, 201);
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response("Method Not Allowed", { status: 405, headers: CORS });
    }

    // --- list the signed-in user's library: GET /library (Bearer login token) ---
    if (request.method === "GET" && (key === "library" || key === "library/")) {
      const email = await verifyToken(env, (request.headers.get("Authorization") || "").replace(/^Bearer\s+/, ""));
      if (!email) return new Response("Unauthorized", { status: 401, headers: CORS });
      const prefix = "library/" + userKey(email) + "/";
      const listed = await env.BUCKET.list({ prefix, limit: 1000 });
      const items = listed.objects.map((o) => ({ key: o.key, name: o.key.slice(prefix.length), size: o.size }));
      return jsonRes({ ok: true, items });
    }

    if (key === "" || key === "index.html") {
      return new Response("Fujify CDN — ok\n", {
        headers: { ...CORS, "Content-Type": "text/plain" },
      });
    }

    // Guard against path traversal / odd keys.
    if (key.includes("..")) return new Response("Bad Request", { status: 400, headers: CORS });

    // Range (resumable downloads for big binaries).
    const rangeHeader = request.headers.get("Range");
    let range;
    if (rangeHeader) {
      const m = /^bytes=(\d*)-(\d*)$/.exec(rangeHeader.trim());
      if (m) {
        const start = m[1] ? parseInt(m[1], 10) : undefined;
        const end = m[2] ? parseInt(m[2], 10) : undefined;
        if (start !== undefined && end !== undefined) range = { offset: start, length: end - start + 1 };
        else if (start !== undefined) range = { offset: start };
        else if (end !== undefined) range = { suffix: end };
      }
    }

    const obj = await env.BUCKET.get(key, {
      range,
      onlyIf: request.headers,   // honours If-None-Match / If-Modified-Since → 304
    });

    if (obj === null) {
      return new Response("Not Found: " + key, { status: 404, headers: CORS });
    }

    const headers = new Headers(CORS);
    obj.writeHttpMetadata(headers);              // ETag, Last-Modified, etc.
    headers.set("Content-Type", contentType(key, obj.httpMetadata));
    headers.set("Cache-Control", cacheControl(key));
    headers.set("Accept-Ranges", "bytes");
    if (obj.httpEtag) headers.set("ETag", obj.httpEtag);

    // Conditional request matched → 304 (R2 returns a body-less object).
    if (!obj.body) return new Response(null, { status: 304, headers });

    // Range satisfied → 206 with Content-Range (only when the client asked for a range;
    // R2 populates obj.range even on full GETs, so gate on the request header).
    if (rangeHeader && obj.range && typeof obj.range.offset === "number") {
      const start = obj.range.offset;
      const len = obj.range.length ?? obj.size - start;
      headers.set("Content-Range", `bytes ${start}-${start + len - 1}/${obj.size}`);
      headers.set("Content-Length", String(len));
      return new Response(request.method === "HEAD" ? null : obj.body, { status: 206, headers });
    }

    headers.set("Content-Length", String(obj.size));
    return new Response(request.method === "HEAD" ? null : obj.body, { status: 200, headers });
  },
};
