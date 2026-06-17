#!/usr/bin/env bash
# deploy-pages.sh — deploy the site to Cloudflare Pages (project "fujify" → fujify.app).
# Excludes download/ (the DMG lives on R2 via the cdn.fujify.app Worker, and exceeds the
# Pages 25 MiB per-file limit). Content (*.md) is served from R2, not Pages.
set -euo pipefail
cd "$(dirname "$0")"

STAGE="$(mktemp -d)"
cp -R index.html app.js favicon.svg og.jpg content "$STAGE/" 2>/dev/null || true
npx wrangler pages deploy "$STAGE" --project-name fujify --branch=main --commit-dirty=true
rm -rf "$STAGE"
