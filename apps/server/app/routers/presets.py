from __future__ import annotations

from typing import Any

from fastapi import APIRouter

router = APIRouter(tags=["presets"])

from core.presets import PRESETS  # noqa: E402 — core after sys.path in main


@router.get("/presets")
def presets() -> dict[str, Any]:
    return {"ok": True, "presets": PRESETS}
