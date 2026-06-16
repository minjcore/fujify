"""Render a short MP4: image + styled EXIF tags (ASS subtitles + ffmpeg)."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Literal

from core.exif_tags import PhotoTags, extract_photo_tags


def _ass_escape(text: str) -> str:
    """Escape characters that break ASS override syntax."""
    return (
        text.replace("\\", "\\\\")
        .replace("{", "\\{")
        .replace("}", "\\}")
        .replace("\n", " ")
    )


def build_ass(
    tags: PhotoTags, duration_sec: float, layout: Literal["vertical", "horizontal"] = "vertical"
) -> str:
    """Minimal ASS with one or two bottom lines, fade in/out, soft shadow."""
    end = _format_ass_time(duration_sec)
    line1 = _ass_escape(tags.line_camera())
    line2 = tags.line_exposure()
    if layout == "vertical":
        rx, ry = 1080, 1920
        title_size, meta_size = 52, 36
        mv_title, mv_meta = 88, 160
    else:
        rx, ry = 1920, 1080
        title_size, meta_size = 44, 30
        mv_title, mv_meta = 56, 118
    # Arial: common on macOS/Windows; many Linux images alias Liberation Sans to Arial.
    header = f"""[Script Info]
Title: Fuji-Fy Share
ScriptType: v4.00+
PlayResX: {rx}
PlayResY: {ry}
WrapStyle: 0

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Title,Arial,{title_size},&H00F5F5F5,&H000000FF,&H80000000,&H80000000,-1,0,0,0,100,100,0,0,1,2,2,2,80,80,{mv_title},1
Style: Meta,Arial,{meta_size},&H00DDDDDD,&H000000FF,&H80000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,80,80,{mv_meta},1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,{end},Title,,0,0,0,,{{\\fad(400,600)}}{line1}
"""
    if line2:
        header += f"Dialogue: 0,0:00:00.20,{end},Meta,,0,0,0,,{{\\fad(600,600)}}{_ass_escape(line2)}\n"
    return header


def _format_ass_time(seconds: float) -> str:
    if seconds < 0:
        seconds = 0
    h = int(seconds // 3600)
    m = int((seconds % 3600) // 60)
    s = seconds % 60
    return f"{h}:{m:02d}:{s:05.2f}"


def _ffmpeg_escape_path(path: Path) -> str:
    # ffmpeg subtitles= filter: escape special chars
    s = path.resolve().as_posix()
    return s.replace("\\", "\\\\").replace(":", "\\:").replace("'", r"\'")


def render_share_video(
    image_path: str | Path,
    output_path: str | Path,
    *,
    duration: float = 5.0,
    layout: Literal["vertical", "horizontal"] = "vertical",
    ffmpeg_bin: str = "ffmpeg",
) -> Path:
    """
    Create an H.264 MP4 with the image (letterboxed) and two tag lines.

    Requires ``ffmpeg`` on PATH (with libass for subtitles).
    """
    image_path = Path(image_path).expanduser().resolve()
    output_path = Path(output_path).expanduser().resolve()
    if not image_path.is_file():
        raise FileNotFoundError(image_path)

    if not shutil.which(ffmpeg_bin):
        raise RuntimeError(
            f"'{ffmpeg_bin}' not found on PATH. Install ffmpeg (e.g. brew install ffmpeg)."
        )

    tags = extract_photo_tags(image_path)
    ass_body = build_ass(tags, duration, layout)

    with tempfile.NamedTemporaryFile(
        suffix=".ass", mode="w", encoding="utf-8-sig", delete=False
    ) as ass_f:
        ass_f.write(ass_body)
        ass_path = Path(ass_f.name)

    try:
        if layout == "vertical":
            w, h = 1080, 1920
        else:
            w, h = 1920, 1080

        vf = (
            f"scale={w}:{h}:force_original_aspect_ratio=decrease,"
            f"pad={w}:{h}:(ow-iw)/2:(oh-ih)/2:color=black,"
            f"subtitles={_ffmpeg_escape_path(ass_path)}"
        )

        cmd = [
            ffmpeg_bin,
            "-y",
            "-loop",
            "1",
            "-i",
            str(image_path),
            "-t",
            str(duration),
            "-vf",
            vf,
            "-pix_fmt",
            "yuv420p",
            "-c:v",
            "libx264",
            "-movflags",
            "+faststart",
            str(output_path),
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if proc.returncode != 0:
            raise RuntimeError(
                f"ffmpeg failed ({proc.returncode}): {proc.stderr[-2000:] or proc.stdout[-2000:]}"
            )
    finally:
        ass_path.unlink(missing_ok=True)

    return output_path
