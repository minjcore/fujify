#!/usr/bin/env python3
"""Persistent preview engine for Fuji-fy Studio.

Reads one JSON request per line on stdin, writes one JSON status line on stdout.
Decodes a RAW/JPEG once and caches a downscaled **proxy** in memory, so live preview
re-applies WB/tone on the small array (tens of ms) instead of re-running rawpy each time.

Request:  {"mode":"preview"|"full","input_path":..,"output_path":..,"max_dim":1600,
           "quality":90,"preset":..,"settings":{temp,tint,wb_auto,brightness,...}}
Response: {"ok":true,"ms":..,"w":..,"h":..,"mode":..}  or  {"ok":false,"error":..}

Reuses core/ so color science stays identical to the CLI/server path.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from time import perf_counter

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

# --- defined errors -------------------------------------------------------
# Stable codes so the C++ client can branch on the failure kind, not parse prose.
ERR_BAD_REQUEST = "BAD_REQUEST"      # missing/invalid fields in the JSON request
ERR_FILE_NOT_FOUND = "FILE_NOT_FOUND"
ERR_DECODE_FAILED = "DECODE_FAILED"  # rawpy/libraw could not decode the input
ERR_SAVE_FAILED = "SAVE_FAILED"      # could not write the output JPEG
ERR_INTERNAL = "INTERNAL"


class PreviewError(Exception):
    """Engine error carrying a stable code (see ERR_* above)."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code

from core.engine import ProcessImageRequest, _effective_settings  # noqa: E402
from core.export import save_jpeg  # noqa: E402
from core.loader import load_image  # noqa: E402
from core.share_image import render_social_image  # noqa: E402
from core.tone import ToneSettings, apply_tone  # noqa: E402
from core.white_balance import WhiteBalanceSettings, apply_white_balance  # noqa: E402

# Cache only the most recent image (keeps memory to one proxy). Key = (path, mtime, max_dim).
_cache: dict = {}


def _downscale(arr: np.ndarray, max_dim: int) -> np.ndarray:
    h, w = arr.shape[:2]
    if max_dim <= 0 or max(h, w) <= max_dim:
        return arr
    s = max_dim / float(max(h, w))
    nw, nh = max(1, round(w * s)), max(1, round(h * s))
    img = Image.fromarray((np.clip(arr, 0.0, 1.0) * 255.0).astype(np.uint8))
    img = img.resize((nw, nh), Image.LANCZOS)
    return np.asarray(img).astype(np.float64) / 255.0


def _proxy_for(path: str, max_dim: int):
    p = Path(path).expanduser().resolve()
    key = (str(p), p.stat().st_mtime, max_dim)
    ent = _cache.get(key)
    if ent is None:
        loaded = load_image(p)
        ent = {"rgb": _downscale(loaded.rgb, max_dim), "exif": None}
        _cache.clear()  # drop older entries → bounded memory
        _cache[key] = ent
    return ent["rgb"], ent["exif"]


def _wb_tone(req: dict):
    r = ProcessImageRequest(
        input_path=req.get("input_path", ""),
        preset=req.get("preset") or None,
        settings=req.get("settings") or None,
    )
    s = _effective_settings(r)
    wb = WhiteBalanceSettings(
        temp=s.temp, tint=float(s.tint),
        wb_shift_a=float(s.wb_shift_a), wb_shift_b=float(s.wb_shift_b),
        wb_shift_g=float(s.wb_shift_g), wb_shift_m=float(s.wb_shift_m),
        auto_gray_world=bool(s.wb_auto), pick_xy=s.wb_pick, pick_radius=int(s.wb_pick_radius),
    )
    tone = ToneSettings(
        brightness=float(s.brightness), contrast=float(s.contrast),
        shadows=float(s.shadows), highlights=float(s.highlights),
    )
    return wb, tone


def _load_rgb(mode: str, req: dict):
    """Decode input → (rgb, exif), translating low-level failures into PreviewError."""
    path = req.get("input_path")
    if not path:
        raise PreviewError(ERR_BAD_REQUEST, "missing 'input_path'")
    try:
        if mode == "full":
            loaded = load_image(Path(path).expanduser().resolve())
            return loaded.rgb, loaded.exif_bytes
        return _proxy_for(path, int(req.get("max_dim", 1600)))
    except FileNotFoundError as exc:
        raise PreviewError(ERR_FILE_NOT_FOUND, str(exc)) from exc
    except PreviewError:
        raise
    except Exception as exc:  # rawpy/libraw decode, unsupported format, etc.
        raise PreviewError(ERR_DECODE_FAILED, f"{type(exc).__name__}: {exc}") from exc


def _handle(req: dict) -> dict:
    t0 = perf_counter()
    mode = req.get("mode", "preview")
    if mode not in ("preview", "full", "social"):
        raise PreviewError(ERR_BAD_REQUEST, f"unknown mode '{mode}'")
    if not req.get("output_path"):
        raise PreviewError(ERR_BAD_REQUEST, "missing 'output_path'")

    # Social export: render a share-ready crop from an already-processed full-res JPEG.
    if mode == "social":
        src = req.get("input_path")
        if not src:
            raise PreviewError(ERR_BAD_REQUEST, "missing 'input_path'")
        try:
            render_social_image(
                Path(src).expanduser().resolve(),
                Path(req["output_path"]).expanduser().resolve(),
                social_format=req.get("social_format", "story"),
                tier=req.get("tier", "hq"),
                quality=req.get("quality"),
                show_brand=bool(req.get("brand", True)),
            )
        except FileNotFoundError as exc:
            raise PreviewError(ERR_FILE_NOT_FOUND, str(exc)) from exc
        except Exception as exc:  # noqa: BLE001
            raise PreviewError(ERR_SAVE_FAILED, f"{type(exc).__name__}: {exc}") from exc
        return {"ok": True, "ms": int((perf_counter() - t0) * 1000), "mode": "social"}

    out = Path(req["output_path"]).expanduser().resolve()
    quality = int(req.get("quality", 90))
    wb, tone = _wb_tone(req)

    rgb, exif = _load_rgb(mode, req)
    processed = apply_tone(apply_white_balance(rgb, wb), tone)
    try:
        save_jpeg(processed, out, quality=quality, exif_bytes=exif)
    except Exception as exc:  # noqa: BLE001
        raise PreviewError(ERR_SAVE_FAILED, f"{type(exc).__name__}: {exc}") from exc

    h, w = processed.shape[:2]
    return {"ok": True, "ms": int((perf_counter() - t0) * 1000), "w": w, "h": h, "mode": mode}


def main() -> int:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            resp = _handle(json.loads(line))
        except PreviewError as exc:
            resp = {"ok": False, "code": exc.code, "error": str(exc)}
        except json.JSONDecodeError as exc:
            resp = {"ok": False, "code": ERR_BAD_REQUEST, "error": f"bad JSON: {exc}"}
        except Exception as exc:  # noqa: BLE001 — last-resort guard, never crash the daemon
            resp = {"ok": False, "code": ERR_INTERNAL, "error": f"{type(exc).__name__}: {exc}"}
        sys.stdout.write(json.dumps(resp) + "\n")
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
