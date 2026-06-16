#!/usr/bin/env node
import { readFile } from "node:fs/promises";
import { runProcessingJob } from "./runProcessingJob.mjs";

async function main() {
  const payloadPath = process.argv[2];
  if (!payloadPath) {
    throw new Error("Usage: node desktop/bridge/process-job-cli.mjs <payload.json>");
  }

  const payloadRaw = await readFile(payloadPath, "utf8");
  const payload = JSON.parse(payloadRaw);
  const projectDir = payload.projectDir || process.cwd();
  const result = await runProcessingJob(payload.request, {
    projectDir,
    pythonBin: payload.pythonBin || "python3",
    timeoutMs: payload.timeoutMs || 120000,
  });
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
}

main().catch((err) => {
  process.stderr.write(`${err.stack || err.message}\n`);
  process.exit(1);
});

