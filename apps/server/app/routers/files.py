from __future__ import annotations

from pathlib import Path

from fastapi import APIRouter, HTTPException, Request
from fastapi.responses import FileResponse

router = APIRouter(tags=["files"])


def _safe_file_path(folder: Path, file_name: str) -> Path:
    candidate = (folder / file_name).resolve()
    if folder.resolve() not in candidate.parents and candidate != folder.resolve():
        raise HTTPException(status_code=400, detail="Invalid file path")
    return candidate


@router.get("/files/{kind}/{name}")
def get_file(request: Request, kind: str, name: str) -> FileResponse:
    paths = request.app.state.paths
    if kind == "outputs":
        folder = paths.output_dir
    elif kind == "compares":
        folder = paths.compare_dir
    else:
        raise HTTPException(status_code=404, detail="Unknown file kind")
    path = _safe_file_path(folder, name)
    if not path.exists() or not path.is_file():
        raise HTTPException(status_code=404, detail="File not found")
    return FileResponse(path)
