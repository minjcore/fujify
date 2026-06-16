import { spawn } from "node:child_process";

/**
 * Run a Fuji-Fy processing job through the Python engine API.
 * @param {object} request ProcessImageRequest-like object
 * @param {object} options bridge options
 * @param {string} [options.projectDir] absolute path to Fuji-fy project
 * @param {string} [options.pythonBin] python binary name/path
 * @param {number} [options.timeoutMs] timeout in milliseconds
 * @returns {Promise<object>} ProcessImageResult
 */
export function runProcessingJob(
  request,
  { projectDir = process.cwd(), pythonBin = "python3", timeoutMs = 120000 } = {},
) {
  return new Promise((resolve, reject) => {
    const child = spawn(pythonBin, ["-m", "core.api"], {
      cwd: projectDir,
      stdio: ["pipe", "pipe", "pipe"],
    });

    let stdout = "";
    let stderr = "";
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      reject(new Error(`Fuji-Fy processing timed out after ${timeoutMs}ms`));
    }, timeoutMs);

    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString();
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
    });
    child.on("error", (err) => {
      clearTimeout(timer);
      reject(err);
    });
    child.on("close", (code) => {
      clearTimeout(timer);
      const text = (code === 0 ? stdout : stderr).trim();
      if (!text) {
        reject(new Error(`No API response from python process (exit=${code})`));
        return;
      }
      let parsed;
      try {
        parsed = JSON.parse(text);
      } catch (err) {
        reject(new Error(`Invalid JSON from python API: ${err}\n${text}`));
        return;
      }
      if (code !== 0 || !parsed.ok) {
        reject(new Error(parsed.error || `Processing failed (exit=${code})`));
        return;
      }
      resolve(parsed);
    });

    child.stdin.write(JSON.stringify(request));
    child.stdin.end();
  });
}

