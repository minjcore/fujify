"""Image Processing Pipeline CLI."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from core.engine import ProcessImageRequest, process_image
from core.loader import RAW_EXTENSIONS
from core.presets import PRESETS


def _parse_xy(value: str) -> tuple[int, int]:
    try:
        x_str, y_str = value.split(",", maxsplit=1)
        return int(x_str.strip()), int(y_str.strip())
    except Exception as exc:  # noqa: BLE001
        raise argparse.ArgumentTypeError("Expected format: X,Y (e.g. 1024,680)") from exc


def _iter_batch_inputs(directory: Path) -> list[Path]:
    exts = {".jpg", ".jpeg", ".png", ".tif", ".tiff"} | RAW_EXTENSIONS
    files = [p for p in directory.iterdir() if p.is_file() and p.suffix.lower() in exts]
    return sorted(files)


def _output_path_for(input_path: Path, output_arg: str | None, batch: bool) -> str | None:
    if output_arg is None:
        return None
    if output_arg:
        out = Path(output_arg).expanduser()
        if batch:
            out.mkdir(parents=True, exist_ok=True)
            return str(out / f"{input_path.stem}_processed.jpg")
        if out.suffix:
            return str(out)
        out.mkdir(parents=True, exist_ok=True)
        return str(out / f"{input_path.stem}_processed.jpg")
    return None


def _effective_params(args: argparse.Namespace) -> dict[str, float | bool | tuple[int, int] | None]:
    params: dict[str, float | bool | tuple[int, int] | None] = {
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
        "wb_pick": args.wb_pick,
        "wb_pick_radius": args.wb_pick_radius,
    }

    if args.preset:
        params.update(PRESETS[args.preset])

    manual_overrides = {
        "temp": args.temp,
        "tint": args.tint,
        "wb_shift_a": args.wb_shift_a,
        "wb_shift_b": args.wb_shift_b,
        "wb_shift_g": args.wb_shift_g,
        "wb_shift_m": args.wb_shift_m,
        "wb_auto": args.wb_auto,
        "brightness": args.brightness,
        "contrast": args.contrast,
        "shadows": args.shadows,
        "highlights": args.highlights,
    }
    for key, value in manual_overrides.items():
        if value is not None:
            params[key] = value

    return params


def process_one(input_path: Path, args: argparse.Namespace, batch: bool = False) -> Path:
    params = _effective_params(args)
    output = _output_path_for(input_path, args.output, batch=batch)
    request = ProcessImageRequest(
        input_path=str(input_path),
        output_path=output,
        preset=args.preset,
        compare=args.compare,
        quality=args.quality,
        settings=params,
        include_info=args.info,
    )
    result = process_image(request)
    if not result.ok:
        raise RuntimeError(result.error or "Processing failed")
    if args.info and result.info is not None:
        print(json.dumps(result.info, indent=2))
    if args.info:
        print(
            json.dumps(
                {
                    "preset": result.preset,
                    "effective_params": result.effective_settings,
                },
                indent=2,
            )
        )
    print(f"Saved: {result.output_path}")
    if result.compare_output_path:
        print(f"Saved compare: {result.compare_output_path}")
    return Path(result.output_path)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Image Processing Pipeline CLI")
    parser.add_argument("input", nargs="?", help="Input file path (.ARW/.JPG/...)")
    parser.add_argument("-o", "--output", help="Output file path (or output directory in batch mode)")
    parser.add_argument("--batch", help="Process all supported images in this directory")
    parser.add_argument("--preset", choices=sorted(PRESETS.keys()), help="Apply built-in processing preset")

    parser.add_argument("--temp", type=float, default=None, help="White balance color temperature in Kelvin")
    parser.add_argument("--tint", type=float, default=None, help="Tint adjustment (magenta/green)")
    parser.add_argument("--wb-auto", action="store_true", default=None, help="Enable gray-world auto white balance")
    parser.add_argument("--wb-pick", type=_parse_xy, help="Neutral sample coordinate X,Y")
    parser.add_argument("--wb-pick-radius", type=int, default=6, help="Sampling radius for --wb-pick")
    parser.add_argument("--wb-shift-a", type=float, default=None, help="Fine WB amber shift")
    parser.add_argument("--wb-shift-b", type=float, default=None, help="Fine WB blue shift")
    parser.add_argument("--wb-shift-g", type=float, default=None, help="Fine WB green shift")
    parser.add_argument("--wb-shift-m", type=float, default=None, help="Fine WB magenta shift")

    parser.add_argument("--brightness", type=float, default=None, help="Brightness slider [-1..1]")
    parser.add_argument("--contrast", type=float, default=None, help="Contrast slider [-1..1]")
    parser.add_argument("--shadows", type=float, default=None, help="Shadows slider [-1..1]")
    parser.add_argument("--highlights", type=float, default=None, help="Highlights slider [-1..1]")

    parser.add_argument("--quality", type=int, default=95, help="JPEG quality [1..100]")
    parser.add_argument("--compare", action="store_true", help="Save side-by-side original/processed comparison")
    parser.add_argument("--info", action="store_true", help="Print loaded image metadata as JSON")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if not args.input and not args.batch:
        parser.error("Provide either an input file or --batch INPUT_DIR.")
    if args.input and args.batch:
        parser.error("Use either a single input file or --batch, not both.")

    if args.batch:
        batch_dir = Path(args.batch).expanduser().resolve()
        if not batch_dir.exists() or not batch_dir.is_dir():
            parser.error(f"Batch directory not found: {batch_dir}")
        files = _iter_batch_inputs(batch_dir)
        if not files:
            parser.error(f"No supported image files found in: {batch_dir}")
        for path in files:
            process_one(path, args, batch=True)
        return 0

    input_path = Path(args.input).expanduser().resolve()
    process_one(input_path, args, batch=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

