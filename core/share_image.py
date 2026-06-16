"""Render a static social-format JPEG: letterboxed photo + EXIF tag overlay (Pillow only)."""

from __future__ import annotations

from pathlib import Path
from typing import Literal

from PIL import Image, ImageDraw, ImageFont

from core.exif_tags import extract_photo_tags

SocialFormat = Literal["story", "feed", "square"]
ExportTier = Literal["hq", "ig"]

# “Ít nhất 2K”: cạnh ngắn của canvas (chiều ngang khung dọc) ≥ 2048 px.
# `ig` = sàn 2K + JPEG nhẹ hơn; `hq` = cạnh ngắn 2560 px + JPEG cao hơn.
_MIN_SHORT_EDGE_2K = 2048
_SHORT_EDGE: dict[ExportTier, int] = {"hq": 2560, "ig": _MIN_SHORT_EDGE_2K}
_DEFAULT_QUALITY: dict[ExportTier, int] = {"hq": 97, "ig": 92}


def _even_dim(n: int) -> int:
    return n + (n % 2)


def _canvas_pixels(short_edge: int, social_format: SocialFormat) -> tuple[int, int]:
    """Portrait canvas: width = short edge; height từ tỉ lệ khung."""
    w = max(_MIN_SHORT_EDGE_2K, int(short_edge))
    if social_format == "square":
        return w, w
    if social_format == "feed":
        # 4:5
        h = _even_dim(int(round(w * 5 / 4)))
        return w, h
    # story 9:16
    h = _even_dim(int(round(w * 16 / 9)))
    return w, h


def _font_candidates_bold() -> list[Path]:
    return [
        Path("/System/Library/Fonts/Supplemental/Arial Bold.ttf"),
        Path("/System/Library/Fonts/Helvetica.ttc"),
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
        Path("C:/Windows/Fonts/arialbd.ttf"),
    ]


def _font_candidates_regular() -> list[Path]:
    return [
        Path("/System/Library/Fonts/Supplemental/Arial.ttf"),
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    ]


def _load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    paths = _font_candidates_bold() if bold else _font_candidates_regular()
    for p in paths:
        if p.exists():
            try:
                return ImageFont.truetype(str(p), size=size)
            except OSError:
                continue
    return ImageFont.load_default()


def _text_width(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont) -> int:
    bbox = draw.textbbox((0, 0), text, font=font)
    return bbox[2] - bbox[0]


def _wrap_line(
    draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont, max_w: int
) -> list[str]:
    words = text.split()
    if not words:
        return []
    lines: list[str] = []
    cur: list[str] = []
    for w in words:
        trial = " ".join(cur + [w])
        if _text_width(draw, trial, font) <= max_w:
            cur.append(w)
        else:
            if cur:
                lines.append(" ".join(cur))
            cur = [w]
    if cur:
        lines.append(" ".join(cur))
    return lines


def _bottom_gradient_rgba(canvas_w: int, strip_h: int, strength: int = 210) -> Image.Image:
    strip = Image.new("RGBA", (canvas_w, strip_h))
    px = strip.load()
    for y in range(strip_h):
        a = int(strength * (y + 1) / max(strip_h, 1))
        for x in range(canvas_w):
            px[x, y] = (0, 0, 0, a)
    return strip


def render_social_image(
    image_path: str | Path,
    output_path: str | Path,
    *,
    social_format: SocialFormat = "story",
    tier: ExportTier = "hq",
    quality: int | None = None,
    show_brand: bool = True,
) -> Path:
    """
    Build a share-ready JPEG: black letterbox, bottom gradient, camera/exposure lines.

    ``tier="hq"`` (default): cạnh ngắn **2560 px** (trên mức 2K) + JPEG cao.
    ``tier="ig"``: cạnh ngắn **2048 px** (sàn 2K) + JPEG ~92 — file nhỏ hơn hq cùng tỉ lệ.
    Instagram/Threads vẫn có thể nén lại khi upload.

    Input: same as EXIF reader (JPEG/TIFF with metadata when possible).
    Output: new JPEG (metadata not copied — composite file).
    """
    image_path = Path(image_path).expanduser().resolve()
    output_path = Path(output_path).expanduser().resolve()
    if not image_path.is_file():
        raise FileNotFoundError(image_path)

    short = _SHORT_EDGE[tier]
    cw, ch = _canvas_pixels(short, social_format)
    q = int(_DEFAULT_QUALITY[tier] if quality is None else quality)
    q = max(1, min(100, q))

    # Scale typography từ thiết kế gốc 1080 px ngang
    s = cw / 1080.0
    tags = extract_photo_tags(image_path)

    with Image.open(image_path) as src:
        src_rgb = src.convert("RGB")
        iw, ih = src_rgb.size

    scale = min(cw / iw, ch / ih)
    nw, nh = max(1, int(iw * scale)), max(1, int(ih * scale))
    photo = src_rgb.resize((nw, nh), Image.Resampling.LANCZOS)

    canvas = Image.new("RGB", (cw, ch), (12, 12, 14))
    ox, oy = (cw - nw) // 2, (ch - nh) // 2
    canvas.paste(photo, (ox, oy))

    strip_h = min(int(round(340 * s)), ch // 3)
    strip_h = max(strip_h, int(round(200 * s)))
    grad = _bottom_gradient_rgba(cw, strip_h)
    overlay = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
    overlay.paste(grad, (0, ch - strip_h))
    composed = Image.alpha_composite(canvas.convert("RGBA"), overlay).convert("RGB")

    draw = ImageDraw.Draw(composed)
    margin_x = int(round(44 * s))
    max_text_w = cw - 2 * margin_x

    if social_format == "square":
        title_size, meta_size = 34, 24
    elif social_format == "feed":
        title_size, meta_size = 38, 26
    else:
        title_size, meta_size = 44, 30
    title_size = max(8, int(round(title_size * s)))
    meta_size = max(8, int(round(meta_size * s)))

    font_title = _load_font(title_size, bold=True)
    font_meta = _load_font(meta_size, bold=False)

    line1 = tags.line_camera()
    line2 = tags.line_exposure()

    title_lines = _wrap_line(draw, line1, font_title, max_text_w)
    meta_lines = _wrap_line(draw, line2, font_meta, max_text_w) if line2 else []

    line_gap_title = max(2, int(round(6 * s)))
    line_gap_meta = max(2, int(round(4 * s)))
    block_meta_h = 0
    if meta_lines:
        for ln in meta_lines:
            bbox = draw.textbbox((0, 0), ln, font=font_meta)
            block_meta_h += bbox[3] - bbox[1] + line_gap_meta
        block_meta_h -= line_gap_meta

    block_title_h = 0
    for ln in title_lines:
        bbox = draw.textbbox((0, 0), ln, font=font_title)
        block_title_h += bbox[3] - bbox[1] + line_gap_title
    block_title_h -= line_gap_title

    gap_between = (max(2, int(round(14 * s))) if meta_lines else 0)
    bottom_pad = int(round(56 * s))
    y = ch - bottom_pad - block_meta_h - gap_between - block_title_h

    stroke_fill = (0, 0, 0)
    sw_title = max(2, int(round(2 * s)))
    sw_meta = max(1, int(round(1 * s)))
    for ln in title_lines:
        draw.text(
            (margin_x, y),
            ln,
            font=font_title,
            fill=(245, 245, 247),
            stroke_width=sw_title,
            stroke_fill=stroke_fill,
        )
        bbox = draw.textbbox((0, 0), ln, font=font_title)
        y += bbox[3] - bbox[1] + line_gap_title

    y += gap_between - line_gap_title if meta_lines else 0

    for ln in meta_lines:
        draw.text(
            (margin_x, y),
            ln,
            font=font_meta,
            fill=(210, 210, 215),
            stroke_width=sw_meta,
            stroke_fill=stroke_fill,
        )
        bbox = draw.textbbox((0, 0), ln, font=font_meta)
        y += bbox[3] - bbox[1] + line_gap_meta

    if show_brand:
        brand = "Fuji-Fy"
        font_brand = _load_font(max(10, int(round(20 * s))), bold=False)
        bb = draw.textbbox((0, 0), brand, font=font_brand)
        bw = bb[2] - bb[0]
        draw.text(
            (cw - margin_x - bw, int(round(36 * s))),
            brand,
            font=font_brand,
            fill=(140, 140, 148),
            stroke_width=sw_meta,
            stroke_fill=(20, 20, 22),
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    composed.save(
        output_path,
        format="JPEG",
        quality=q,
        subsampling=0,
        optimize=True,
    )
    return output_path
