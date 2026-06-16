"""Callable processing engine API for CLI and app bridges."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import Any

from core.export import save_jpeg, save_side_by_side
from core.loader import image_info, load_image
from core.presets import PRESETS
from core.tone import ToneSettings, apply_tone
from core.white_balance import WhiteBalanceSettings, apply_white_balance


@dataclass
class EngineSettings:
    temp: float | None = None
    tint: float = 0.0
    wb_shift_a: float = 0.0
    wb_shift_b: float = 0.0
    wb_shift_g: float = 0.0
    wb_shift_m: float = 0.0
    wb_auto: bool = False
    brightness: float = 0.0
    contrast: float = 0.0
    shadows: float = 0.0
    highlights: float = 0.0
    wb_pick: tuple[int, int] | None = None
    wb_pick_radius: int = 6


@dataclass
class ProcessImageRequest:
    input_path: str | Path
    output_path: str | Path | None = None
    preset: str | None = None
    compare: bool = False
    quality: int = 95
    settings: dict[str, Any] | None = None
    include_info: bool = False


@dataclass
class ProcessImageResult:
    ok: bool
    input_path: str
    output_path: str
    effective_settings: dict[str, Any]
    elapsed_ms: int
    engine: str = "python"
    compare_output_path: str | None = None
    preset: str | None = None
    info: dict[str, Any] | None = None
    error: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "ok": self.ok,
            "input_path": self.input_path,
            "output_path": self.output_path,
            "compare_output_path": self.compare_output_path,
            "preset": self.preset,
            "effective_settings": self.effective_settings,
            "elapsed_ms": self.elapsed_ms,
            "engine": self.engine,
            "info": self.info,
            "error": self.error,
        }


def _output_path_for(input_path: Path, output_path: str | Path | None) -> Path:
    if output_path is None:
        return input_path.with_name(f"{input_path.stem}_processed.jpg")

    out = Path(output_path).expanduser().resolve()
    if out.suffix:
        return out
    out.mkdir(parents=True, exist_ok=True)
    return out / f"{input_path.stem}_processed.jpg"


def _effective_settings(request: ProcessImageRequest) -> EngineSettings:
    base: dict[str, Any] = {
        "temp": None,
        "tint": 0.0,
        "wb_shift_a": 0.0,
        "wb_shift_b": 0.0,
        "wb_shift_g": 0.0,
        "wb_shift_m": 0.0,
        "wb_auto": False,
        "brightness": 0.0,
        "contrast": 0.0,
        "shadows": 0.0,
        "highlights": 0.0,
        "wb_pick": None,
        "wb_pick_radius": 6,
    }
    if request.preset:
        if request.preset not in PRESETS:
            raise ValueError(f"Unknown preset: {request.preset}")
        base.update(PRESETS[request.preset])
    if request.settings:
        for key, value in request.settings.items():
            if value is not None:
                base[key] = value
    return EngineSettings(**base)


def process_image(request: ProcessImageRequest) -> ProcessImageResult:
    start = perf_counter()
    input_path = Path(request.input_path).expanduser().resolve()
    output_path = _output_path_for(input_path, request.output_path).resolve()
    try:
        loaded = load_image(input_path)
        settings = _effective_settings(request)

        wb_settings = WhiteBalanceSettings(
            temp=settings.temp,
            tint=float(settings.tint),
            wb_shift_a=float(settings.wb_shift_a),
            wb_shift_b=float(settings.wb_shift_b),
            wb_shift_g=float(settings.wb_shift_g),
            wb_shift_m=float(settings.wb_shift_m),
            auto_gray_world=bool(settings.wb_auto),
            pick_xy=settings.wb_pick,
            pick_radius=int(settings.wb_pick_radius),
        )
        tone_settings = ToneSettings(
            brightness=float(settings.brightness),
            contrast=float(settings.contrast),
            shadows=float(settings.shadows),
            highlights=float(settings.highlights),
        )

        processed = apply_white_balance(loaded.rgb, wb_settings)
        processed = apply_tone(processed, tone_settings)

        saved = save_jpeg(processed, output_path, quality=request.quality, exif_bytes=loaded.exif_bytes)

        compare_output_path: Path | None = None
        if request.compare:
            compare_output_path = saved.with_name(f"{saved.stem}_compare.jpg")
            save_side_by_side(loaded.rgb, processed, compare_output_path, quality=request.quality)

        elapsed_ms = int((perf_counter() - start) * 1000)
        info = image_info(loaded) if request.include_info else None
        return ProcessImageResult(
            ok=True,
            input_path=str(input_path),
            output_path=str(saved),
            compare_output_path=str(compare_output_path) if compare_output_path else None,
            preset=request.preset,
            effective_settings=settings.__dict__,
            elapsed_ms=elapsed_ms,
            info=info,
        )
    except Exception as exc:  # noqa: BLE001
        elapsed_ms = int((perf_counter() - start) * 1000)
        return ProcessImageResult(
            ok=False,
            input_path=str(input_path),
            output_path=str(output_path),
            compare_output_path=None,
            preset=request.preset,
            effective_settings=(request.settings or {}),
            elapsed_ms=elapsed_ms,
            error=str(exc),
        )

