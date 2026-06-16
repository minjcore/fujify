"""Check parity between Python presets and shared JSON presets."""

from __future__ import annotations

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from core.presets import PRESETS


def normalize(value):
    if isinstance(value, float):
        return round(value, 6)
    if isinstance(value, dict):
        return {k: normalize(v) for k, v in value.items()}
    if isinstance(value, list):
        return [normalize(v) for v in value]
    return value


def main() -> int:
    shared_path = ROOT / "shared/contracts/presets.json"
    shared = json.loads(shared_path.read_text(encoding="utf-8"))

    py_norm = normalize(PRESETS)
    shared_norm = normalize(shared)

    if py_norm != shared_norm:
        print("Preset mismatch detected.")
        py_keys = set(py_norm.keys())
        shared_keys = set(shared_norm.keys())
        print("Only in python:", sorted(py_keys - shared_keys))
        print("Only in shared:", sorted(shared_keys - py_keys))
        for key in sorted(py_keys & shared_keys):
            if py_norm[key] != shared_norm[key]:
                print(f"- Different values in preset: {key}")
        return 1

    print("Preset parity OK.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

