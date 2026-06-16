"""Tone controls: brightness, contrast, shadows, highlights."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class ToneSettings:
    """Simple tone controls with camera-like sliders."""

    brightness: float = 0.0  # Suggested range: [-1.0, 1.0]
    contrast: float = 0.0  # Suggested range: [-1.0, 1.0]
    shadows: float = 0.0  # Suggested range: [-1.0, 1.0]
    highlights: float = 0.0  # Suggested range: [-1.0, 1.0]


def _apply_contrast(rgb: np.ndarray, contrast: float) -> np.ndarray:
    # Centered linear contrast around middle gray.
    factor = 1.0 + np.clip(contrast, -0.95, 2.0)
    out = (rgb - 0.5) * factor + 0.5
    return np.clip(out, 0.0, 1.0)


def _apply_shadows(rgb: np.ndarray, shadows: float) -> np.ndarray:
    # Affect darker values more than bright values.
    amt = np.clip(shadows, -1.0, 1.0)
    if amt == 0:
        return rgb
    shadow_mask = (1.0 - rgb) ** 2
    out = rgb + (amt * 0.6 * shadow_mask)
    return np.clip(out, 0.0, 1.0)


def _apply_highlights(rgb: np.ndarray, highlights: float) -> np.ndarray:
    # Affect brighter values more than dark values.
    amt = np.clip(highlights, -1.0, 1.0)
    if amt == 0:
        return rgb
    highlight_mask = rgb**2
    out = rgb + (amt * 0.6 * highlight_mask)
    return np.clip(out, 0.0, 1.0)


def apply_tone(rgb: np.ndarray, settings: ToneSettings) -> np.ndarray:
    """Apply basic tone controls in a stable order."""
    out = rgb.copy()

    out = out + float(np.clip(settings.brightness, -1.0, 1.0))
    out = np.clip(out, 0.0, 1.0)

    out = _apply_contrast(out, settings.contrast)
    out = _apply_shadows(out, settings.shadows)
    out = _apply_highlights(out, settings.highlights)

    return np.clip(out, 0.0, 1.0)

