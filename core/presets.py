"""Built-in presets for common NEX-5N scenarios."""

from __future__ import annotations

from typing import Any


PRESETS: dict[str, dict[str, Any]] = {
    # Case log 01: warm ambient scene with purple flowers.
    "case01_flower_warm_fix": {
        "temp": 5100.0,
        "tint": 3.0,
        "wb_shift_a": 0.0,
        "wb_shift_b": 8.0,
        "wb_shift_g": 0.0,
        "wb_shift_m": 0.0,
        "wb_auto": False,
        "brightness": 0.0,
        "contrast": 0.12,
        "shadows": 0.08,
        "highlights": -0.10,
    },
    # Keep ambient vibe and only reduce yellow slightly.
    "nex5n_auto_keep_vibe": {
        "temp": 5200.0,
        "tint": 1.0,
        "wb_shift_a": 0.0,
        "wb_shift_b": 4.0,
        "wb_shift_g": 0.0,
        "wb_shift_m": 0.0,
        "wb_auto": False,
        "brightness": 0.0,
        "contrast": 0.06,
        "shadows": 0.03,
        "highlights": -0.05,
    },
    # Case log 02: indoor white-neon scene, slight cool cast.
    "case02_indoor_neon_neutral": {
        "temp": 5600.0,
        "tint": 2.0,
        "wb_shift_a": 0.0,
        "wb_shift_b": -2.0,
        "wb_shift_g": 0.0,
        "wb_shift_m": 0.0,
        "wb_auto": False,
        "brightness": -0.01,
        "contrast": 0.05,
        "shadows": 0.02,
        "highlights": -0.12,
    },
    # Locked default indoor preset (alias of case02).
    "default_indoor": {
        "temp": 5600.0,
        "tint": 2.0,
        "wb_shift_a": 0.0,
        "wb_shift_b": -2.0,
        "wb_shift_g": 0.0,
        "wb_shift_m": 0.0,
        "wb_auto": False,
        "brightness": -0.01,
        "contrast": 0.05,
        "shadows": 0.02,
        "highlights": -0.12,
    },
}

