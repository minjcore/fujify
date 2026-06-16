"""White balance controls and correction utilities."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class WhiteBalanceSettings:
    """White balance controls similar to in-camera settings."""

    temp: float | None = None
    tint: float = 0.0
    wb_shift_a: float = 0.0
    wb_shift_b: float = 0.0
    wb_shift_g: float = 0.0
    wb_shift_m: float = 0.0
    auto_gray_world: bool = False
    pick_xy: tuple[int, int] | None = None
    pick_radius: int = 6


def _apply_gains(rgb: np.ndarray, gains: np.ndarray) -> np.ndarray:
    out = rgb * gains.reshape(1, 1, 3)
    return np.clip(out, 0.0, 1.0)


def _temperature_tint_gains(temp: float | None, tint: float) -> np.ndarray:
    """
    Convert manual WB controls to channel gains.

    - temp reference: 5200K (daylight).
    - tint > 0 pushes magenta (reduce green), tint < 0 pushes green.
    """
    gains = np.ones(3, dtype=np.float64)

    if temp is not None:
        temp_norm = np.clip((temp - 5200.0) / 3000.0, -1.0, 1.0)
        gains[0] *= 1.0 + (0.30 * temp_norm)  # Red
        gains[2] *= 1.0 - (0.30 * temp_norm)  # Blue

    tint_norm = np.clip(tint / 100.0, -1.0, 1.0)
    gains[1] *= 1.0 - (0.25 * tint_norm)  # Green channel compensation

    return gains


def _shift_gains(
    wb_shift_a: float,
    wb_shift_b: float,
    wb_shift_g: float,
    wb_shift_m: float,
) -> np.ndarray:
    """
    Fine WB shift controls (camera-style).

    Positive values:
    - A: amber
    - B: blue
    - G: green
    - M: magenta
    """
    gains = np.ones(3, dtype=np.float64)

    # Amber/Blue axis
    gains[0] *= 1.0 + (0.02 * wb_shift_a)
    gains[2] *= 1.0 + (0.02 * wb_shift_b)

    # Green/Magenta axis
    gains[1] *= 1.0 + (0.02 * wb_shift_g)
    gains[1] *= 1.0 - (0.02 * wb_shift_m)

    return np.clip(gains, 0.3, 3.0)


def gray_world_gains(rgb: np.ndarray) -> np.ndarray:
    """Compute per-channel gains using gray-world auto white balance."""
    means = rgb.reshape(-1, 3).mean(axis=0)
    mean_luma = float(np.mean(means))
    safe_means = np.clip(means, 1e-6, None)
    gains = mean_luma / safe_means
    return np.clip(gains, 0.2, 5.0)


def neutral_pick_gains(rgb: np.ndarray, x: int, y: int, radius: int = 6) -> np.ndarray:
    """Compute gains by forcing a sampled region around (x, y) to neutral gray."""
    h, w, _ = rgb.shape
    cx = int(np.clip(x, 0, w - 1))
    cy = int(np.clip(y, 0, h - 1))
    r = max(1, int(radius))

    x0, x1 = max(0, cx - r), min(w, cx + r + 1)
    y0, y1 = max(0, cy - r), min(h, cy + r + 1)
    patch = rgb[y0:y1, x0:x1]

    means = patch.reshape(-1, 3).mean(axis=0)
    target = float(np.mean(means))
    safe_means = np.clip(means, 1e-6, None)
    gains = target / safe_means
    return np.clip(gains, 0.2, 5.0)


def apply_white_balance(rgb: np.ndarray, settings: WhiteBalanceSettings) -> np.ndarray:
    """Apply manual/automatic white balance controls to an RGB image."""
    out = rgb.copy()

    if settings.auto_gray_world:
        out = _apply_gains(out, gray_world_gains(out))

    if settings.pick_xy is not None:
        x, y = settings.pick_xy
        out = _apply_gains(out, neutral_pick_gains(out, x=x, y=y, radius=settings.pick_radius))

    manual_gains = _temperature_tint_gains(settings.temp, settings.tint)
    shift = _shift_gains(
        wb_shift_a=settings.wb_shift_a,
        wb_shift_b=settings.wb_shift_b,
        wb_shift_g=settings.wb_shift_g,
        wb_shift_m=settings.wb_shift_m,
    )
    out = _apply_gains(out, manual_gains * shift)

    return out

