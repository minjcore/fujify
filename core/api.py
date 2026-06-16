"""JSON API entrypoint for app bridges (desktop/mobile wrappers)."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

from core.engine import ProcessImageRequest, process_image


def _load_payload(path: str | None) -> dict[str, Any]:
    if path:
        data = Path(path).read_text(encoding="utf-8")
    else:
        data = sys.stdin.read()
    return json.loads(data)


def main() -> int:
    payload_path = sys.argv[1] if len(sys.argv) > 1 else None
    payload = _load_payload(payload_path)
    request = ProcessImageRequest(
        input_path=payload["input_path"],
        output_path=payload.get("output_path"),
        preset=payload.get("preset"),
        compare=bool(payload.get("compare", False)),
        quality=int(payload.get("quality", 95)),
        settings=payload.get("settings"),
        include_info=bool(payload.get("include_info", False)),
    )
    result = process_image(request)
    output = result.to_dict()
    if result.ok:
        print(json.dumps(output, indent=2))
        return 0
    print(json.dumps(output, indent=2), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

