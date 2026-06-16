/**
 * ASS subtitle content aligned with `core/share_video.build_ass` (vertical / horizontal).
 * Used for on-device ffmpeg subtitle burn.
 */

export type ShareVideoLayout = "vertical" | "horizontal";

export type ShareAssTags = {
  lineCamera: string;
  lineExposure: string;
};

function escapeAss(text: string): string {
  return text
    .replace(/\\/g, "\\\\")
    .replace(/\{/g, "\\{")
    .replace(/\}/g, "\\}")
    .replace(/\n/g, " ");
}

function formatAssTime(seconds: number): string {
  const s = Math.max(0, seconds);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  return `${h}:${String(m).padStart(2, "0")}:${sec.toFixed(2).padStart(5, "0")}`;
}

export function buildShareAss(
  tags: ShareAssTags,
  durationSec: number,
  layout: ShareVideoLayout,
): string {
  const end = formatAssTime(durationSec);
  const line1 = escapeAss(tags.lineCamera || "Photo");
  const line2 = (tags.lineExposure || "").trim();

  let rx: number;
  let ry: number;
  let titleSize: number;
  let metaSize: number;
  let mvTitle: number;
  let mvMeta: number;
  if (layout === "vertical") {
    rx = 1080;
    ry = 1920;
    titleSize = 52;
    metaSize = 36;
    mvTitle = 88;
    mvMeta = 160;
  } else {
    rx = 1920;
    ry = 1080;
    titleSize = 44;
    metaSize = 30;
    mvTitle = 56;
    mvMeta = 118;
  }

  let header = `[Script Info]
Title: Fuji-Fy Share
ScriptType: v4.00+
PlayResX: ${rx}
PlayResY: ${ry}
WrapStyle: 0

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Title,Arial,${titleSize},&H00F5F5F5,&H000000FF,&H80000000,&H80000000,-1,0,0,0,100,100,0,0,1,2,2,2,80,80,${mvTitle},1
Style: Meta,Arial,${metaSize},&H00DDDDDD,&H000000FF,&H80000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,80,80,${mvMeta},1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,${end},Title,,0,0,0,,{\\fad(400,600)}${line1}
`;
  if (line2) {
    header += `Dialogue: 0,0:00:00.20,${end},Meta,,0,0,0,,{\\fad(600,600)}${escapeAss(line2)}\n`;
  }
  return header;
}
