"""FastAPI application entry."""

from __future__ import annotations

import sys
from pathlib import Path

# Repository root must be on sys.path before routers import `core.*`
_REPO_ROOT = Path(__file__).resolve().parents[3]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from apps.server.app.config import RuntimePaths, get_settings
from apps.server.app.routers import files, health, presets, process, social


def create_app() -> FastAPI:
    settings = get_settings()
    root = settings.repo_root
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))

    paths = RuntimePaths.under_repo(root)
    paths.ensure()

    app = FastAPI(title=settings.service_name, version=settings.api_version)
    app.state.settings = settings
    app.state.paths = paths

    if settings.cors_origin_list:
        app.add_middleware(
            CORSMiddleware,
            allow_origins=settings.cors_origin_list,
            allow_credentials=True,
            allow_methods=["*"],
            allow_headers=["*"],
        )

    app.include_router(health.router)
    app.include_router(presets.router)
    app.include_router(process.router)
    app.include_router(files.router)
    app.include_router(social.router)

    return app


app = create_app()
