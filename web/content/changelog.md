# Changelog

## v0.1 — Preview · 2026-06-16

First public preview of **Fuji-fy Studio**.

### Desktop app
- Native macOS editor (Dear ImGui) — **OpenGL** + **Vulkan** (MoltenVK) backends
- RAW & JPEG → GPU texture; white balance, tone, presets
- **Live preview** — drag a slider, see it instantly (debounced)
- Zoom / pan viewport (Fit · 100% · 0.1–8×)
- Cached RAW decode + proxy engine → fast re-edits
- One-click social export — Story 9:16 · Feed 4:5 · Square 1:1
- Vietnamese + 日本語 UI (Inter + Hiragino)
- Standalone `.app` + DMG — engine bundled, no Python needed

### Web
- New Markdown-driven site — content served from R2 via an edge Worker
- Pricing page

*Known limits:* preview build is unsigned (right-click → Open on first launch);
macOS 12+ · Apple Silicon.
