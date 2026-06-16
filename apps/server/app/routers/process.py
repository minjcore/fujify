from __future__ import annotations

import json
import shutil
import uuid
from pathlib import Path
from typing import Any, Optional

from fastapi import APIRouter, File, Form, HTTPException, Request, UploadFile

from core.engine import ProcessImageRequest, process_image  # noqa: E402
from core.presets import PRESETS  # noqa: E402

router = APIRouter(tags=["process"])


def _parse_settings(settings_json: str | None) -> dict[str, Any] | None:
    if not settings_json:
        return None
    try:
        parsed = json.loads(settings_json)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=400, detail=f"Invalid settings_json: {exc}") from exc
    if not isinstance(parsed, dict):
        raise HTTPException(status_code=400, detail="settings_json must be a JSON object")
    return parsed


@router.post("/process/upload")
async def process_upload(
    request: Request,
    file: UploadFile = File(...),
    preset: Optional[str] = Form(default=None),
    compare: bool = Form(default=False),
    quality: int = Form(default=95),
    settings_json: Optional[str] = Form(default=None),
) -> dict[str, Any]:
    paths = request.app.state.paths
    if preset and preset not in PRESETS:
        raise HTTPException(status_code=400, detail=f"Unknown preset: {preset}")

    settings = _parse_settings(settings_json)
    ext = Path(file.filename or "upload.jpg").suffix or ".jpg"
    uid = uuid.uuid4().hex
    input_path = paths.input_dir / f"{uid}{ext}"
    output_path = paths.output_dir / f"{uid}.jpg"

    with input_path.open("wb") as out:
        shutil.copyfileobj(file.file, out)

    result = process_image(
        ProcessImageRequest(
            input_path=str(input_path),
            output_path=str(output_path),
            preset=preset,
            compare=compare,
            quality=quality,
            settings=settings,
            include_info=True,
        )
    )
    if not result.ok:
        raise HTTPException(status_code=500, detail=result.error or "Processing failed")

    compare_name: str | None = None
    if result.compare_output_path:
        compare_src = Path(result.compare_output_path)
        compare_name = f"{uid}_compare.jpg"
        compare_dst = paths.compare_dir / compare_name
        compare_dst.write_bytes(compare_src.read_bytes())

    return {
        "ok": True,
        "result": result.to_dict(),
        "files": {
            "output": f"/files/outputs/{output_path.name}",
            "compare": f"/files/compares/{compare_name}" if compare_name else None,
        },
    }
