import { app, BrowserWindow, ipcMain } from "electron";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { runProcessingJob } from "../../desktop/bridge/runProcessingJob.mjs";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

function createWindow() {
  const win = new BrowserWindow({
    width: 1200,
    height: 820,
    webPreferences: {
      preload: path.join(__dirname, "preload.mjs"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.loadFile(path.join(__dirname, "renderer/index.html"));
}

ipcMain.handle("fujify:process", async (_event, request) => {
  return runProcessingJob(request, {
    projectDir: path.resolve(__dirname, "../.."),
    pythonBin: "python3",
    timeoutMs: 180000,
  });
});

app.whenReady().then(createWindow);

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});

