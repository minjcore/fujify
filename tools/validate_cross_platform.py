"""Validate visual drift between processing outputs."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image


@dataclass
class PairResult:
    name: str
    ref_path: str
    candidate_path: str
    rmse: float
    mean_abs_diff: float
    channel_shift: list[float]
    pass_threshold: bool


def _read_rgb(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        arr = np.asarray(image.convert("RGB"), dtype=np.float64) / 255.0
    return arr


def _compare_pair(name: str, ref_path: Path, cand_path: Path, max_rmse: float) -> PairResult:
    ref = _read_rgb(ref_path)
    cand = _read_rgb(cand_path)
    if ref.shape != cand.shape:
        raise ValueError(f"Shape mismatch for {name}: {ref.shape} != {cand.shape}")

    diff = cand - ref
    rmse = float(np.sqrt(np.mean(diff**2)))
    mean_abs_diff = float(np.mean(np.abs(diff)))
    channel_shift = [float(x) for x in np.mean(diff, axis=(0, 1))]

    return PairResult(
        name=name,
        ref_path=str(ref_path),
        candidate_path=str(cand_path),
        rmse=rmse,
        mean_abs_diff=mean_abs_diff,
        channel_shift=channel_shift,
        pass_threshold=rmse <= max_rmse,
    )


def run_validation(config_path: Path) -> dict[str, Any]:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    base_dir = config_path.parent.parent.resolve()
    max_rmse = float(config.get("max_rmse", 0.02))
    pairs = config["pairs"]

    results: list[PairResult] = []
    for pair in pairs:
        ref = (base_dir / pair["reference"]).resolve()
        cand = (base_dir / pair["candidate"]).resolve()
        results.append(_compare_pair(pair["name"], ref, cand, max_rmse=max_rmse))

    failed = [r for r in results if not r.pass_threshold]
    return {
        "ok": len(failed) == 0,
        "max_rmse": max_rmse,
        "pairs": [r.__dict__ for r in results],
        "failed": [r.name for r in failed],
    }


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description="Validate cross-platform output drift")
    parser.add_argument(
        "--config",
        default="tests/cross_platform_pairs.json",
        help="Path to validation config JSON",
    )
    parser.add_argument("--out", default="tests/cross_platform_report.json", help="Output report path")
    args = parser.parse_args()

    config_path = Path(args.config).expanduser().resolve()
    report = run_validation(config_path)

    out_path = Path(args.out).expanduser().resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))

    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

