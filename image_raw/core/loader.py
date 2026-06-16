"""Image loading utilities for RAW and JPEG inputs."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image


RAW_EXTENSIONS = {".arw", ".dng", ".nef", ".cr2", ".raf", ".rw2"}


@dataclass
class LoadedImage:
    """Normalized RGB image and source metadata."""

    rgb: np.ndarray
    source_path: Path
    is_raw: bool
    exif_bytes: bytes | None


def _normalize_to_float01(array: np.ndarray) -> np.ndarray:
    """Convert integer/floating arrays to float64 in [0, 1]."""
    if np.issubdtype(array.dtype, np.floating):
        normalized = array.astype(np.float64, copy=False)
        if normalized.max(initial=0.0) > 1.0:
            normalized = normalized / 255.0
        return np.clip(normalized, 0.0, 1.0)

    info = np.iinfo(array.dtype)
    normalized = array.astype(np.float64) / float(info.max)
    return np.clip(normalized, 0.0, 1.0)


def _postprocess_raw(path: Path) -> np.ndarray:
    """Read and demosaic RAW image into RGB using rawpy."""
    try:
        import rawpy  # Lazy import so JPEG-only workflows do not require rawpy.
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "rawpy is required for RAW files. Install dependencies with "
            "`python3 -m pip install -r requirements.txt`."
        ) from exc

    with rawpy.imread(str(path)) as raw:
        rgb16 = raw.postprocess(
            use_camera_wb=False,
            no_auto_bright=True,
            output_bps=16,
            gamma=(1.0, 1.0),
        )
    return _normalize_to_float01(rgb16)


def _load_jpeg(path: Path) -> tuple[np.ndarray, bytes | None]:
    """Read a non-RAW image and return normalized RGB and EXIF bytes."""
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        exif_bytes = image.info.get("exif")
        arr = np.asarray(rgb)
    return _normalize_to_float01(arr), exif_bytes


def load_image(path: str | Path) -> LoadedImage:
    """Load an image from disk into normalized RGB float64 array."""
    source_path = Path(path).expanduser().resolve()
    if not source_path.exists():
        raise FileNotFoundError(f"Input file not found: {source_path}")

    suffix = source_path.suffix.lower()
    if suffix in RAW_EXTENSIONS:
        rgb = _postprocess_raw(source_path)
        return LoadedImage(
            rgb=rgb,
            source_path=source_path,
            is_raw=True,
            exif_bytes=None,
        )

    rgb, exif = _load_jpeg(source_path)
    return LoadedImage(
        rgb=rgb,
        source_path=source_path,
        is_raw=False,
        exif_bytes=exif,
    )


def image_info(loaded: LoadedImage) -> dict[str, Any]:
    """Build a serializable summary of loaded image metadata."""
    h, w, c = loaded.rgb.shape
    return {
        "path": str(loaded.source_path),
        "is_raw": loaded.is_raw,
        "width": w,
        "height": h,
        "channels": c,
        "dtype": str(loaded.rgb.dtype),
        "min": float(loaded.rgb.min(initial=0.0)),
        "max": float(loaded.rgb.max(initial=0.0)),
    }

