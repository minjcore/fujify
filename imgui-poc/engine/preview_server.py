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

import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path
from time import perf_counter

# Cloud goes through the fujify-cdn Worker: /auth/* for accounts, PUT /library/<name> to store.
_WORKER = os.environ.get("FUJIFY_WORKER", "https://fujify-cdn.caokhang91.workers.dev")
UPLOAD_BASE = os.environ.get("FUJIFY_UPLOAD_URL", _WORKER + "/library/")
AUTH_BASE = os.environ.get("FUJIFY_AUTH_URL", _WORKER + "/auth/")


def _resolve_ffmpeg() -> str:
    """Pick an ffmpeg that actually runs (the default brew one may be dylib-broken)."""
    for c in (os.environ.get("FUJIFY_FFMPEG"),
              "/opt/homebrew/opt/ffmpeg-full/bin/ffmpeg",
              shutil.which("ffmpeg"), "ffmpeg"):
        if not c:
            continue
        try:
            if subprocess.run([c, "-version"], capture_output=True).returncode == 0:
                return c
        except Exception:
            pass
    return "ffmpeg"


FFMPEG = _resolve_ffmpeg()

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]   # imgui-poc/engine/ → project root
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

VIDEO_EXTS = {".mp4", ".mov", ".m4v", ".avi", ".mkv", ".webm"}

# Cache only the most recent image (keeps memory to one proxy). Key = (path, mtime, max_dim).
_cache: dict = {}


def _is_video(path) -> bool:
    return Path(path).suffix.lower() in VIDEO_EXTS


def _video_frame(path: str) -> str:
    """Extract a representative frame from a video → temp JPEG, return its path (cached)."""
    p = Path(path).expanduser().resolve()
    out = Path(tempfile.gettempdir()) / f"fujify_vframe_{abs(hash((str(p), p.stat().st_mtime)))}.jpg"
    if not out.exists():
        # seek ~1s in; fall back to first frame if shorter
        cmd = [FFMPEG, "-y", "-ss", "1", "-i", str(p), "-frames:v", "1",
               "-q:v", "2", str(out)]
        r = subprocess.run(cmd, capture_output=True)
        if r.returncode != 0 or not out.exists():
            cmd = [FFMPEG, "-y", "-i", str(p), "-frames:v", "1", "-q:v", "2", str(out)]
            r = subprocess.run(cmd, capture_output=True)
            if r.returncode != 0:
                raise PreviewError(ERR_DECODE_FAILED,
                                   "ffmpeg frame extract failed: " + r.stderr.decode("utf-8", "ignore")[-200:])
    return str(out)


def _ffmpeg_look_filters(s) -> str:
    """Map engine settings → an ffmpeg filter chain (approximate look for video v1)."""
    parts = []
    if s.temp:                               # warm/cool toward target Kelvin
        parts.append(f"colortemperature=temperature={int(s.temp)}:mix=1.0")
    b = float(s.brightness)                  # eq: brightness -1..1, contrast around 1.0
    c = max(0.0, 1.0 + float(s.contrast))
    parts.append(f"eq=brightness={b:.3f}:contrast={c:.3f}:gamma=1.0")
    return ",".join(parts)


def _build_lut(s, size: int = 33) -> str:
    """Bake the engine's exact WB+tone transform into a 3D .cube LUT (identity grid →
    apply_white_balance + apply_tone). Image-dependent steps (auto WB / pick) are off, so
    the LUT captures the manual/preset look exactly — same color science as the photo path."""
    ramp = np.linspace(0.0, 1.0, size)
    idx = np.arange(size ** 3)
    ri, gi, bi = idx % size, (idx // size) % size, (idx // (size * size)) % size   # R fastest
    grid = np.stack([ramp[ri], ramp[gi], ramp[bi]], axis=1).reshape(size ** 3, 1, 3)
    wb = WhiteBalanceSettings(
        temp=s.temp, tint=float(s.tint),
        wb_shift_a=float(s.wb_shift_a), wb_shift_b=float(s.wb_shift_b),
        wb_shift_g=float(s.wb_shift_g), wb_shift_m=float(s.wb_shift_m),
        auto_gray_world=False, pick_xy=None, pick_radius=int(s.wb_pick_radius))
    tone = ToneSettings(brightness=float(s.brightness), contrast=float(s.contrast),
                        shadows=float(s.shadows), highlights=float(s.highlights))
    out = np.clip(apply_tone(apply_white_balance(grid, wb), tone), 0.0, 1.0).reshape(size ** 3, 3)
    path = str(Path(tempfile.gettempdir()) / "fujify_look.cube")
    with open(path, "w") as f:
        f.write(f"LUT_3D_SIZE {size}\n")
        for px in out:
            f.write(f"{px[0]:.6f} {px[1]:.6f} {px[2]:.6f}\n")
    return path


def _video_export(req: dict) -> dict:
    t0 = perf_counter()
    src = Path(req["input_path"]).expanduser().resolve()
    out = Path(req["output_path"]).expanduser().resolve()
    if not src.is_file():
        raise PreviewError(ERR_FILE_NOT_FOUND, f"not found: {src}")
    r = ProcessImageRequest(input_path=str(src), preset=req.get("preset") or None,
                            settings=req.get("settings") or None)
    s = _effective_settings(r)
    # Exact look via a baked 3D LUT; fall back to approximate ffmpeg filters if it fails.
    try:
        vf = "lut3d=" + _build_lut(s)
    except Exception:
        vf = _ffmpeg_look_filters(s)
    cmd = [FFMPEG, "-y", "-i", str(src), "-vf", vf,
           "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p",
           "-c:a", "copy", str(out)]
    res = subprocess.run(cmd, capture_output=True)
    if res.returncode != 0:
        # retry dropping audio (some inputs have none / incompatible audio)
        cmd = [FFMPEG, "-y", "-i", str(src), "-vf", vf,
               "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", "-an", str(out)]
        res = subprocess.run(cmd, capture_output=True)
        if res.returncode != 0:
            raise PreviewError(ERR_SAVE_FAILED,
                               "ffmpeg export failed: " + res.stderr.decode("utf-8", "ignore")[-300:])
    return {"ok": True, "ms": int((perf_counter() - t0) * 1000), "mode": "video_export", "vf": vf}


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
        if _is_video(path):                  # video → preview a single extracted frame
            path = _video_frame(path)
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


def _save_target(req: dict) -> dict:
    """Save the processed full-res image at ~target_kb: binary-search JPEG quality, then
    shrink if even low quality overshoots. Keeps 4:4:4 (subsampling=0)."""
    t0 = perf_counter()
    out = Path(req["output_path"]).expanduser().resolve()
    target = max(10, int(req.get("target_kb", 500))) * 1024
    wb, tone = _wb_tone(req)
    rgb, exif = _load_rgb("full", req)
    processed = np.clip(apply_tone(apply_white_balance(rgb, wb), tone), 0.0, 1.0)
    base = Image.fromarray((processed * 255.0).astype(np.uint8))
    exif_b = exif if isinstance(exif, (bytes, bytearray)) else None

    def encode(im, q):
        buf = io.BytesIO()
        kw = {"quality": int(q), "subsampling": 0, "optimize": True}
        if exif_b:
            kw["exif"] = exif_b
        im.save(buf, "JPEG", **kw)
        return buf.getvalue()

    best, scale = None, 1.0
    for _ in range(5):                       # shrink if min quality still overshoots
        im = base if scale == 1.0 else base.resize(
            (max(1, int(base.width * scale)), max(1, int(base.height * scale))), Image.LANCZOS)
        lo, hi, cand = 30, 95, None
        for _ in range(7):                   # binary-search quality
            q = (lo + hi) // 2
            data = encode(im, q)
            if len(data) <= target:
                cand, lo = (q, data), q + 1
            else:
                hi = q - 1
        if cand is not None:
            best = cand; break
        data = encode(im, 30)
        if len(data) <= target or scale < 0.35:
            best = (30, data); break
        scale *= 0.8

    q, data = best
    try:
        out.write_bytes(data)
    except Exception as exc:  # noqa: BLE001
        raise PreviewError(ERR_SAVE_FAILED, f"{type(exc).__name__}: {exc}") from exc
    return {"ok": True, "ms": int((perf_counter() - t0) * 1000), "mode": "save_target",
            "bytes": len(data), "kb": round(len(data) / 1024), "quality": q}


def _auth(mode: str, req: dict) -> dict:
    """Account signup/login via the Worker → returns a signed token for uploads."""
    t0 = perf_counter()
    kind = "signup" if mode == "auth_signup" else "login"
    email = (req.get("email") or "").strip().lower()
    password = req.get("password") or ""
    if not email or not password:
        raise PreviewError(ERR_BAD_REQUEST, "email & password required")
    body = json.dumps({"email": email, "password": password}).encode()
    rq = urllib.request.Request(AUTH_BASE + kind, data=body, method="POST",
        headers={"Content-Type": "application/json", "User-Agent": "fujify-studio/0.1"})
    try:
        with urllib.request.urlopen(rq, timeout=30) as resp:
            data = json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        try: data = json.loads(e.read().decode())
        except Exception: data = {"ok": False, "error": f"HTTP {e.code}"}
    except Exception as exc:  # noqa: BLE001
        raise PreviewError(ERR_SAVE_FAILED, f"auth: {exc}") from exc
    if not data.get("ok"):
        raise PreviewError(ERR_BAD_REQUEST, data.get("error", "auth failed"))
    return {"ok": True, "mode": mode, "ms": int((perf_counter() - t0) * 1000),
            "token": data.get("token", ""), "email": data.get("email", email)}


def _library_list(req: dict) -> dict:
    """List the signed-in user's cloud library (GET /library + token)."""
    t0 = perf_counter()
    token = req.get("token") or os.environ.get("FUJIFY_UPLOAD_TOKEN")
    if not token:
        raise PreviewError(ERR_BAD_REQUEST, "not logged in (no token)")
    rq = urllib.request.Request(_WORKER + "/library", method="GET",
        headers={"Authorization": "Bearer " + token, "User-Agent": "fujify-studio/0.1"})
    try:
        with urllib.request.urlopen(rq, timeout=30) as resp:
            data = json.loads(resp.read().decode())
    except Exception as exc:  # noqa: BLE001
        raise PreviewError(ERR_SAVE_FAILED, f"list: {exc}") from exc
    items = data.get("items", [])
    return {"ok": True, "mode": "library_list", "ms": int((perf_counter() - t0) * 1000),
            "names": "|".join(i["name"] for i in items),
            "keys": "|".join(i["key"] for i in items), "count": len(items)}


def _library_get(req: dict) -> dict:
    """Download a library object (by full key) to output_path."""
    t0 = perf_counter()
    key = req.get("key", "")
    if not key or not req.get("output_path"):
        raise PreviewError(ERR_BAD_REQUEST, "key & output_path required")
    out = Path(req["output_path"]).expanduser().resolve()
    rq = urllib.request.Request(_WORKER + "/" + key, method="GET",
        headers={"User-Agent": "fujify-studio/0.1"})
    try:
        with urllib.request.urlopen(rq, timeout=120) as resp:
            data = resp.read()
    except Exception as exc:  # noqa: BLE001
        raise PreviewError(ERR_SAVE_FAILED, f"download: {exc}") from exc
    out.write_bytes(data)
    return {"ok": True, "mode": "library_get", "ms": int((perf_counter() - t0) * 1000),
            "output_path": str(out), "kb": round(len(data) / 1024)}


def _upload(req: dict) -> dict:
    """PUT a local file to the cloud (Worker → private R2) under the user's library/."""
    t0 = perf_counter()
    src = Path(req["input_path"]).expanduser().resolve()
    if not src.is_file():
        raise PreviewError(ERR_FILE_NOT_FOUND, f"not found: {src}")
    token = req.get("token") or os.environ.get("FUJIFY_UPLOAD_TOKEN")
    if not token:
        raise PreviewError(ERR_BAD_REQUEST, "not logged in (no token)")
    name = (req.get("name") or src.name).replace("/", "_")
    url = UPLOAD_BASE + name
    data = src.read_bytes()
    rq = urllib.request.Request(url, data=data, method="PUT", headers={
        "Authorization": "Bearer " + token, "Content-Type": "application/octet-stream",
        "User-Agent": "fujify-studio/0.1"})   # Cloudflare 403s the default Python-urllib UA
    try:
        with urllib.request.urlopen(rq, timeout=300) as resp:
            resp.read()
    except Exception as exc:  # noqa: BLE001
        raise PreviewError(ERR_SAVE_FAILED, f"upload: {exc}") from exc
    return {"ok": True, "ms": int((perf_counter() - t0) * 1000), "mode": "upload",
            "url": url, "kb": round(len(data) / 1024)}


def _handle(req: dict) -> dict:
    t0 = perf_counter()
    mode = req.get("mode", "preview")
    if mode not in ("preview", "full", "social", "video_export", "save_target", "upload",
                    "auth_login", "auth_signup", "library_list", "library_get"):
        raise PreviewError(ERR_BAD_REQUEST, f"unknown mode '{mode}'")
    if mode in ("auth_login", "auth_signup"):
        return _auth(mode, req)
    if mode == "library_list":
        return _library_list(req)
    if mode == "library_get":
        return _library_get(req)
    if mode == "upload":
        return _upload(req)
    if not req.get("output_path"):
        raise PreviewError(ERR_BAD_REQUEST, "missing 'output_path'")

    if mode == "video_export":
        return _video_export(req)
    if mode == "save_target":
        return _save_target(req)

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
