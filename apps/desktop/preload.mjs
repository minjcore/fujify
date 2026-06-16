import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("fujify", {
  processImage: (request) => ipcRenderer.invoke("fujify:process", request),
});

