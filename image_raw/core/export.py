"""Image export utilities."""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image


def to_uint8(rgb: np.ndarray) -> np.ndarray:
    """Convert normalized float image in [0, 1] to uint8."""
    clipped = np.clip(rgb, 0.0, 1.0)
    return np.round(clipped * 255.0).astype(np.uint8)


def save_jpeg(
    rgb: np.ndarray,
    output_path: str | Path,
    quality: int = 95,
    exif_bytes: bytes | None = None,
) -> Path:
    """Write RGB float image to JPEG with optional EXIF preservation."""
    out_path = Path(output_path).expanduser().resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    image = Image.fromarray(to_uint8(rgb), mode="RGB")
    save_kwargs = {
        "format": "JPEG",
        "quality": int(np.clip(quality, 1, 100)),
        "subsampling": 0,
    }
    if exif_bytes:
        save_kwargs["exif"] = exif_bytes
    image.save(out_path, **save_kwargs)
    return out_path


def save_side_by_side(
    original_rgb: np.ndarray,
    processed_rgb: np.ndarray,
    output_path: str | Path,
    quality: int = 95,
) -> Path:
    """Save a left-right comparison image: original | processed."""
    o = to_uint8(original_rgb)
    p = to_uint8(processed_rgb)
    combo = np.concatenate([o, p], axis=1)
    image = Image.fromarray(combo, mode="RGB")

    out_path = Path(output_path).expanduser().resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(out_path, format="JPEG", quality=int(np.clip(quality, 1, 100)), subsampling=0)
    return out_path

