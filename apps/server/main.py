"""ASGI entry for uvicorn: `uvicorn apps.server.main:app`."""

from apps.server.app.main import app

__all__ = ["app"]
