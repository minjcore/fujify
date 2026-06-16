"""Extract human-readable camera tags from JPEG/TIFF EXIF (for overlays / share cards)."""

from __future__ import annotations

import re
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, cast

from PIL import Image
from PIL.ExifTags import IFD, TAGS


def _rational_to_float(val: Any) -> float | None:
    if val is None:
        return None
    if isinstance(val, tuple) and len(val) == 2:
        num, den = val[0], val[1]
        if den:
            return float(num) / float(den)
        return float(num) if num else None
    if isinstance(val, (int, float)):
        return float(val)
    return None


def _clean_str(val: Any) -> str | None:
    if val is None:
        return None
    s = str(val).strip()
    if not s or s.lower() in ("unknown", "none"):
        return None
    # Remove nulls / junk from some bodies
    s = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", s)
    return s or None


@dataclass
class PhotoTags:
    """Tags suitable for on-screen labels (camera line + exposure line)."""

    make: str | None = None
    model: str | None = None
    lens: str | None = None
    aperture: str | None = None  # e.g. f/2.8
    shutter: str | None = None  # e.g. 1/500s or 2s
    iso: str | None = None
    focal_length: str | None = None  # e.g. 35mm

    def line_camera(self) -> str:
        parts: list[str] = []
        mm = " ".join(x for x in (self.make, self.model) if x).strip()
        if mm:
            parts.append(mm)
        if self.lens:
            parts.append(self.lens)
        return "  ·  ".join(parts) if parts else "Photo"

    def line_exposure(self) -> str:
        chunks: list[str] = []
        if self.aperture:
            chunks.append(self.aperture)
        if self.shutter:
            chunks.append(self.shutter)
        if self.iso:
            chunks.append(self.iso)
        if self.focal_length:
            chunks.append(self.focal_length)
        return "  ·  ".join(chunks) if chunks else ""

    def to_dict(self) -> dict[str, Any]:
        return {k: v for k, v in asdict(self).items() if v is not None}


def _format_shutter(sec: float | None) -> str | None:
    if sec is None or sec <= 0:
        return None
    if sec >= 1.0:
        if abs(sec - round(sec)) < 0.05:
            return f"{int(round(sec))}s"
        return f"{sec:.1f}s".rstrip("0").rstrip(".") + "s"
    t = 1.0 / sec
    rounded = int(round(t))
    if abs(t - rounded) < 0.1:
        return f"1/{rounded}s"
    return f"1/{t:.0f}s"


def _ifd_flat(exif: Any) -> dict[int, Any]:
    """Root + Exif IFD tags by numeric id."""
    out: dict[int, Any] = {}
    for k, v in exif.items():
        out[int(k)] = v
    try:
        sub = cast(Any, exif).get_ifd(IFD.Exif)
        for k, v in sub.items():
            out[int(k)] = v
    except Exception:
        pass
    return out


def extract_photo_tags(path: str | Path) -> PhotoTags:
    """Read EXIF from a file (JPEG/TIFF with metadata). RAW files are not supported here."""
    path = Path(path).expanduser().resolve()
    with Image.open(path) as im:
        exif = im.getexif()
        if not exif:
            return PhotoTags()
        raw = _ifd_flat(exif)

    def tag_name(k: int) -> str:
        return TAGS.get(k, str(k))

    by_name: dict[str, Any] = {tag_name(k): v for k, v in raw.items()}

    make = _clean_str(by_name.get("Make"))
    model = _clean_str(by_name.get("Model"))
    lens = _clean_str(by_name.get("LensModel")) or _clean_str(by_name.get("LensMake"))

    fn = _rational_to_float(by_name.get("FNumber"))
    aperture = f"f/{fn:.1f}".replace(".0", "") if fn is not None else None

    et = _rational_to_float(by_name.get("ExposureTime"))
    shutter = _format_shutter(et)

    iso_val = by_name.get("ISOSpeedRatings") or by_name.get("PhotographicSensitivity")
    if isinstance(iso_val, tuple):
        iso_val = iso_val[0]
    iso = f"ISO {int(iso_val)}" if iso_val is not None else None

    fl = _rational_to_float(by_name.get("FocalLength"))
    focal = f"{fl:.0f}mm" if fl is not None else None

    return PhotoTags(
        make=make,
        model=model,
        lens=lens,
        aperture=aperture,
        shutter=shutter,
        iso=iso,
        focal_length=focal,
    )
