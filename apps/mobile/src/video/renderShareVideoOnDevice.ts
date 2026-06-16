/**
 * On-device share video (static image + ASS) via ffmpeg-kit-react-native.
 * Requires a **development build** (`expo run:android`); not available in Expo Go.
 */

import * as FileSystem from "expo-file-system";
import { Platform } from "react-native";

import { buildShareAss, type ShareAssTags, type ShareVideoLayout } from "./shareAss";

function stripFileScheme(uri: string): string {
  return uri.replace(/^file:\/\//, "");
}

/** Escape path for ffmpeg `subtitles=` filter (Android POSIX paths). */
function escapeSubtitlesFilterPath(path: string): string {
  return path.replace(/\\/g, "\\\\").replace(/:/g, "\\:").replace(/'/g, "\\'");
}

export async function renderShareVideoOnDevice(opts: {
  inputImageUri: string;
  durationSec: number;
  layout: ShareVideoLayout;
  tags: ShareAssTags;
}): Promise<string> {
  if (Platform.OS !== "android") {
    throw new Error("Video export is only supported on Android.");
  }

  // eslint-disable-next-line @typescript-eslint/no-var-requires
  const { FFmpegKit, ReturnCode } = require("ffmpeg-kit-react-native") as typeof import("ffmpeg-kit-react-native");

  const cacheRoot = FileSystem.cacheDirectory;
  if (!cacheRoot) {
    throw new Error("cacheDirectory is null");
  }

  const inputPath = stripFileScheme(opts.inputImageUri);
  const stamp = Date.now();
  const assPath = `${cacheRoot}fujify_share_${stamp}.ass`;
  const outPath = `${cacheRoot}fujify_share_${stamp}.mp4`;

  const assBody = buildShareAss(opts.tags, opts.durationSec, opts.layout);
  await FileSystem.writeAsStringAsync(assPath, `\uFEFF${assBody}`, {
    encoding: FileSystem.EncodingType.UTF8,
  });

  const w = opts.layout === "vertical" ? 1080 : 1920;
  const h = opts.layout === "vertical" ? 1920 : 1080;
  const subEsc = escapeSubtitlesFilterPath(assPath);
  const vf = `scale=${w}:${h}:force_original_aspect_ratio=decrease,pad=${w}:${h}:(ow-iw)/2:(oh-ih)/2:color=black,subtitles='${subEsc}'`;

  const args = [
    "-y",
    "-loop",
    "1",
    "-i",
    inputPath,
    "-t",
    String(opts.durationSec),
    "-vf",
    vf,
    "-pix_fmt",
    "yuv420p",
    "-c:v",
    "libx264",
    "-movflags",
    "+faststart",
    outPath,
  ];

  const session = await (FFmpegKit as any).executeWithArguments(args);
  const rc = await session.getReturnCode();
  try {
    await FileSystem.deleteAsync(assPath, { idempotent: true });
  } catch {
    // ignore cleanup errors
  }

  if (!(ReturnCode as any).isSuccess(rc)) {
    const logs = await session.getAllLogsAsString(5000);
    throw new Error(logs?.slice(-1200) || "ffmpeg failed");
  }

  return `file://${outPath}`;
}
