#!/usr/bin/env python3
"""CLI: image + EXIF tags → short MP4 (vertical 1080×1920 by default)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from core.exif_tags import extract_photo_tags  # noqa: E402
from core.share_video import render_share_video  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser(description="Render share video with camera/exposure tags.")
    p.add_argument("image", type=Path, help="JPEG/TIFF with EXIF (not raw sensor files)")
    p.add_argument("-o", "--output", type=Path, help="Output .mp4 path")
    p.add_argument(
        "--duration", type=float, default=5.0, help="Clip length in seconds (default 5)"
    )
    p.add_argument(
        "--landscape",
        action="store_true",
        help="1920×1080 instead of vertical 1080×1920",
    )
    p.add_argument(
        "--tags-json",
        action="store_true",
        help="Print extracted tags as JSON and exit (no ffmpeg)",
    )
    p.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg binary name or path")
    args = p.parse_args()

    img = args.image.expanduser().resolve()
    if not img.is_file():
        print(f"Not found: {img}", file=sys.stderr)
        return 1

    tags = extract_photo_tags(img)
    if args.tags_json:
        print(json.dumps(tags.to_dict(), indent=2, ensure_ascii=False))
        print()
        print(tags.line_camera())
        print(tags.line_exposure())
        return 0

    out = args.output
    if out is None:
        out = img.with_suffix(".share.mp4")
    else:
        out = out.expanduser().resolve()

    layout = "horizontal" if args.landscape else "vertical"
    try:
        render_share_video(
            img,
            out,
            duration=args.duration,
            layout=layout,
            ffmpeg_bin=args.ffmpeg,
        )
    except Exception as e:
        print(str(e), file=sys.stderr)
        return 1
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
