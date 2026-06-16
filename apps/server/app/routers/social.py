"""Social export endpoints (share video, etc.)."""

from __future__ import annotations

import shutil
import uuid
from pathlib import Path
from typing import Any, Literal

from fastapi import APIRouter, File, Form, HTTPException, Request, UploadFile

router = APIRouter(tags=["social"])


@router.post("/social/share-video")
async def social_share_video(
    request: Request,
    file: UploadFile = File(...),
    duration: float = Form(default=5.0),
    layout: str = Form(default="vertical"),
) -> dict[str, Any]:
    """
    Upload a JPEG/PNG, run ``core.share_video.render_share_video`` (ffmpeg + ASS), return path to MP4.

    Requires **ffmpeg** on the server host (with libass). Mobile V1 downloads via ``GET /files/outputs/...``.
    """
    if layout not in ("vertical", "horizontal"):
        raise HTTPException(status_code=400, detail="layout must be vertical or horizontal")
    if duration <= 0 or duration > 120:
        raise HTTPException(status_code=400, detail="duration must be in (0, 120] seconds")

    paths = request.app.state.paths
    ext = Path(file.filename or "upload.jpg").suffix or ".jpg"
    uid = uuid.uuid4().hex
    input_path = paths.input_dir / f"{uid}_share_video{ext}"
    video_name = f"{uid}_share_video.mp4"
    output_path = paths.output_dir / video_name

    with input_path.open("wb") as out:
        shutil.copyfileobj(file.file, out)

    layout_lit: Literal["vertical", "horizontal"] = "vertical" if layout == "vertical" else "horizontal"

    try:
        from core.share_video import render_share_video

        render_share_video(
            input_path,
            output_path,
            duration=float(duration),
            layout=layout_lit,
        )
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc
    finally:
        input_path.unlink(missing_ok=True)

    if not output_path.is_file():
        raise HTTPException(status_code=500, detail="Video output missing")

    return {
        "ok": True,
        "files": {
            "video": f"/files/outputs/{video_name}",
        },
    }


@router.post("/social/image")
async def social_image(
    request: Request,
    file: UploadFile = File(...),
    format: str = Form(default="story"),
) -> dict[str, Any]:
    """
    Upload a JPEG, add EXIF overlay bar, return social-ready image (2560px HQ).
    Reads EXIF from the uploaded file automatically.
    """
    if format not in ("story", "feed", "square"):
        raise HTTPException(status_code=400, detail="format must be story, feed, or square")

    paths = request.app.state.paths
    ext = Path(file.filename or "upload.jpg").suffix or ".jpg"
    uid = uuid.uuid4().hex
    input_path = paths.input_dir / f"{uid}_social{ext}"
    output_name = f"{uid}_social.jpg"
    output_path = paths.output_dir / output_name

    with input_path.open("wb") as out:
        shutil.copyfileobj(file.file, out)

    try:
        from core.share_image import render_social_image
        from typing import Literal

        fmt: Literal["story", "feed", "square"] = format  # type: ignore
        render_social_image(input_path, output_path, social_format=fmt, tier="hq")
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc
    finally:
        input_path.unlink(missing_ok=True)

    if not output_path.is_file():
        raise HTTPException(status_code=500, detail="Output image missing")

    return {
        "ok": True,
        "files": {
            "image": f"/files/outputs/{output_name}",
        },
    }
