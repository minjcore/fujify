"""Environment-driven settings for the API service."""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


def _default_repo_root() -> Path:
    # apps/server/app/config.py -> parents[3] = repository root
    return Path(__file__).resolve().parents[3]


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_prefix="FUJIFY_",
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    service_name: str = "Fuji-Fy Server"
    api_version: str = "0.1.0"
    repo_root: Path = Field(default_factory=_default_repo_root)
    # Comma-separated origins for browser clients, e.g. "http://localhost:3000"
    cors_origins: str = ""

    @field_validator("repo_root", mode="before")
    @classmethod
    def coerce_repo_root(cls, v: object) -> Path:
        if v is None or v == "":
            return _default_repo_root()
        return Path(v).resolve()

    @property
    def cors_origin_list(self) -> list[str]:
        if not self.cors_origins.strip():
            return []
        return [o.strip() for o in self.cors_origins.split(",") if o.strip()]


@dataclass(frozen=True)
class RuntimePaths:
    input_dir: Path
    output_dir: Path
    compare_dir: Path

    @classmethod
    def under_repo(cls, root: Path) -> "RuntimePaths":
        base = root / "apps" / "server" / "runtime"
        return cls(
            input_dir=base / "inputs",
            output_dir=base / "outputs",
            compare_dir=base / "compares",
        )

    def ensure(self) -> None:
        for p in (self.input_dir, self.output_dir, self.compare_dir):
            p.mkdir(parents=True, exist_ok=True)


@lru_cache
def get_settings() -> Settings:
    return Settings()
