#!/usr/bin/env python3
"""CLI: ảnh gốc → JPEG social (Story / feed 4:5 / vuông) + tag EXIF."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from core.share_image import render_social_image  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser(
        description="Tạo ảnh social: khung đen, ảnh giữ tỉ lệ, chữ máy/lens/exposure ở đáy."
    )
    p.add_argument("image", type=Path, help="JPEG/PNG/TIFF nguồn")
    p.add_argument("-o", "--output", type=Path, help="File .jpg ra (mặc định: tên.social.jpg)")
    p.add_argument(
        "--format",
        dest="social_format",
        choices=("story", "feed", "square"),
        default="story",
        help="story / feed (4:5) / square — kích thước pixel theo --tier",
    )
    p.add_argument(
        "--tier",
        choices=("hq", "ig"),
        default="hq",
        help="hq=cạnh ngắn 2560px + JPEG ~97 (mặc định); ig=sàn 2K 2048px + JPEG ~92",
    )
    p.add_argument(
        "--quality",
        type=int,
        default=None,
        metavar="N",
        help="Ghi đè JPEG quality 1–100 (mặc định: 97 nếu hq, 92 nếu ig)",
    )
    p.add_argument("--no-brand", action="store_true", help="Ẩn watermark Fuji-Fy góc phải")
    args = p.parse_args()

    img = args.image.expanduser().resolve()
    if not img.is_file():
        print(f"Not found: {img}", file=sys.stderr)
        return 1

    out = args.output
    if out is None:
        out = img.with_name(f"{img.stem}.social.jpg")
    else:
        out = out.expanduser().resolve()

    try:
        render_social_image(
            img,
            out,
            social_format=args.social_format,
            tier=args.tier,
            quality=args.quality,
            show_brand=not args.no_brand,
        )
    except Exception as e:
        print(str(e), file=sys.stderr)
        return 1

    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
