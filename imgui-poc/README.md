# ImGui Desktop POC

Proof-of-concept desktop app dùng [Dear ImGui](https://github.com/ocornut/imgui) trên macOS.
Backend: **GLFW + OpenGL3**.

## Build & Run

```bash
brew install glfw
git clone --depth 1 https://github.com/ocornut/imgui vendor/imgui   # vendored, gitignored
make
./imgui_poc
```

## Cấu trúc

```
src/      main.cpp · fujify_studio.cpp (GL) · fujify_vk.cpp (Vulkan) · fujify_engine.h (shared)
vendor/   stb_image.h · imgui/ (clone)
engine/   preview_server.py (Python engine daemon)
assets/   fonts/Inter-Regular.otf · sample.ARW (gitignored)
Makefile · package.sh · notarize.sh   (ở root)
```

- `src/main.cpp` — POC hello-world ImGui (input, slider, color picker, demo toggle).
- `src/fujify_engine.h` — `StudioUI` + engine daemon/queue/fonts/histogram/recents (dùng chung GL+VK).

## Ghi chú

- macOS yêu cầu OpenGL Core profile 3.2 + forward-compat (đã set trong `main.cpp`).
- Bật checkbox "Show ImGui demo window" để xem toàn bộ widget gallery của ImGui.

---

# Fuji-fy Studio (`fujify`) — tích hợp engine xử lý ảnh

POC editor desktop: load **RAW/JPEG → hiển thị trên GL texture**, tái dùng đúng engine
Python của project (`core/api.py`).

## Luồng

```
GUI (C++/ImGui)
  -> ghi payload JSON (input_path, settings WB/tone, preset)
  -> python3 -m core.api   (cd về project root)
       core/loader.py  -> rawpy/libraw decode RAW  (hoac PIL cho JPEG)
       white_balance + tone
       -> ghi JPEG /tmp/fuji_studio_preview.jpg
  -> stb_image decode JPEG -> glTexImage2D -> ImGui::Image
```

## Build & Run

```bash
brew install glfw rawpy           # glfw (C++) + rawpy qua: pip install rawpy Pillow numpy
make fujify
./fujify                          # tu dong load imgui-poc/sample.ARW khi khoi dong
```

`sample.ARW` = Sony NEX-5N (raw.pixls.us, 4920×3276). Đổi đường dẫn trong ô **Input**
rồi bấm **Load / Apply** để xử lý ảnh khác. Chỉnh WB/tone/preset → Apply để re-process.

## Quyết định thiết kế

- **Không decode RAW trong C++.** Gọi thẳng engine Python (rawpy/libraw + WB + tone) — đúng
  một code path với CLI/server, không phân nhánh logic xử lý ảnh.
- **RAW nặng** (file mẫu 17MB; full-res 4920×3276 ≈ 64MB RGBA). Hợp với product-model:
  Storage là revenue feature (giữ RAW gốc trên cloud), editor làm trên proxy nhẹ.
  → bước tiếp theo nên cho engine xuất **preview downscale** thay vì full-res.
- Storage + Ads là revenue features; **Premium tắt ads** — chỉ là tầng monetize, không
  đụng tới core pipeline.

## Export creative (IG / Threads)

Panel "Export creative" gọi `tools/render_social_image.py` trên ảnh **đã WB/tone**:

- `format`: story (9:16) · feed (4:5) · square (1:1)
- `tier`: hq (2560px) · ig (2048px)
- watermark Fuji-Fy bật/tắt
- **Export (format đang chọn)** → 1 file `creative_<fmt>_<tier>.jpg`
- **Export ALL 3 formats** → cả story + feed + square trong 1 click (đủ bộ tỉ lệ cho ad creative)

Output ví dụ (tier hq): story 2560×4552, feed 2560×3200, square 2560×2560 — khung đen + chữ EXIF.

## Threading & progress

Mô hình **producer / 1 consumer** (`JobSystem` trong `fujify_engine.h`):

- UI thread = **producer**: `submit(Task)` đẩy job vào `std::queue<Task>` (mutex + condvar).
- **1 consumer thread** pop & xử lý tuần tự (đúng với daemon — chỉ xử lý 1 request/lần).
- **Preview coalescing**: mỗi preview mang `seq`; consumer bỏ qua preview cũ đã bị preview
  mới thay → kéo slider liên tục không dồn hàng đợi job cũ.
- Consumer chỉ làm CPU/IO; UI thread **drain results** mỗi frame rồi mới upload texture
  (đúng thread giữ GL/Vulkan context).
- UI hiển thị `queue: N job` + bar *indeterminate* khi bận; có thể enqueue thêm khi đang bận.

## Zoom / Pan

Viewport hỗ trợ **cuộn chuột = zoom**, **kéo chuột trái = pan**, nút **Fit** (vừa khung)
và **100%** (1:1 pixel), kèm slider zoom 0.1–8×. Ảnh vẽ bằng draw-list có clip nên
pan/zoom mượt, không tạo lại texture.

## i18n (Latin / Tiếng Việt / 日本語)

UI dùng font **Inter** (đúng family IntelliJ dùng cho UI) cho Latin + Tiếng Việt, và
**merge Hiragino** cho glyph Nhật (Inter không có CJK). `setup_fonts()` tìm Inter theo thứ tự:
bản copy local `fonts/Inter-Regular.otf` → bundle IntelliJ/Android Studio; fallback cuối
là `Arial Unicode.ttf`. Nhờ vậy hiển thị được cả `để tắt ads` lẫn `写真編集 / 書き出し`.

## Backend Vulkan (`fujify_vk`) — MoltenVK trên macOS

Bản Vulkan của cùng app (`fujify_vk.cpp`), chứng minh swap backend ImGui từ OpenGL3 sang
Vulkan. macOS không có Vulkan native → chạy qua **MoltenVK** (Vulkan→Metal).

```bash
brew install molten-vk vulkan-loader vulkan-headers   # + vulkan-tools (optional)
make fujify_vk
./fujify_vk
```

- **Toàn bộ state + UI + engine** nằm trong `fujify_engine.h` (class `StudioUI`), dùng chung
  giữa GL và VK. Mỗi `.cpp` chỉ còn: dựng backend + 1 hàm upload-texture + vòng lặp gọi
  `ui.draw()`. Texture trừu tượng qua `TextureOps{ upload, id }` (callback backend-specific).
- **Texture Vulkan**: staging buffer → `vkCmdCopyBufferToImage` → layout transition →
  `ImGui_ImplVulkan_AddTexture` (VkDescriptorSet làm `ImTextureID`). Upload chạy trên main
  thread (sau khi worker xong), `vkQueueWaitIdle` đồng bộ.
- App tự set `VK_ICD_FILENAMES`/`VK_DRIVER_FILES` trỏ tới MoltenVK ICD (Makefile truyền
  `-DMOLTENVK_ICD`), bật `VK_KHR_portability_enumeration` + `portability_subset`.
- Verify: Apple M5, Vulkan 1.4 qua MoltenVK; cùng luồng RAW→proxy→texture, live preview.

## Đóng gói (.app / DMG)

```bash
./package.sh        # → dist/Fuji-fy Studio.app + dist/Fuji-fy-Studio-macOS.dmg (~35MB)
```

`package.sh` gói **standalone**, không phụ thuộc project/python trên máy đích:
- **Engine** freeze bằng **PyInstaller** (`preview_server.py` + `core/` + rawpy/libraw + numpy
  + Pillow) → `Contents/Resources/engine/fujify-engine`. Sample RAW → `Resources/sample.ARW`.
- **`libglfw`** vào `Frameworks` + relink `@rpath`.
- **Ad-hoc codesign inside-out** (bắt buộc Apple Silicon sau `install_name_tool`).
- DMG (có symlink `/Applications`).

App tự định vị engine qua `_NSGetExecutablePath` → set `FUJIFY_ENGINE`/`FUJIFY_SAMPLE` (dev:
env trống → dùng `python3 imgui-poc/preview_server.py` ở source tree). Engine = 1 tiến trình
daemon lo cả preview/full/**social** (export không cần process phụ). Export ảnh **cạnh file gốc**.

Verify: chạy `.app` từ `/tmp` → engine process là `…/Resources/engine/fujify-engine` (không
phải python3), preview ra bình thường.

**Phát hành không cảnh báo Gatekeeper:** dùng `./notarize.sh` (cần cert Apple của bạn) —
ký **Developer ID + hardened runtime + entitlements** (entitlements cần cho engine
PyInstaller: allow-jit / unsigned-executable-memory / disable-library-validation), rebuild
DMG, `notarytool submit --wait` rồi `stapler staple`. Set env trước khi chạy:
```bash
export DEV_ID="Developer ID Application: Tên (TEAMID)"
export NOTARY_PROFILE="fujify"   # hoặc APPLE_ID + TEAM_ID + APP_PWD
./package.sh && ./notarize.sh
```
Chưa chạy notarize → DMG vẫn tải/chạy được nhưng lần đầu phải chuột phải → Open.

DMG lên R2: `cp dist/*.dmg ../web/download/ && node ../web/upload-r2.mjs download` → nút
Download ở `fujify.app` trỏ `cdn.fujify.app/download/...`.

## Video (Studio)

Studio mở luôn **video** (`.mp4/.mov/.m4v/...`): engine trích 1 frame qua **ffmpeg** để
preview + chỉnh look (temp/brightness/contrast) như ảnh; nút **Export video (.mp4)** áp look
cho cả clip bằng ffmpeg (`colortemperature` + `eq`) → `<tên>_fujify.mp4` cạnh file gốc.

- Cần **ffmpeg** trên máy (engine tự tìm `ffmpeg-full` rồi tới `ffmpeg` trong PATH; override
  bằng env `FUJIFY_FFMPEG`). Bản brew `ffmpeg` mặc định trên máy này đang lỗi dylib → dùng `ffmpeg-full`.
- Look video là **xấp xỉ** (filter ffmpeg), không phải color science y hệt engine ảnh — v1.

## Hạn chế POC (chưa làm)

- Queue 1 consumer (daemon serial); chưa có cancel job đang chạy. Hiển thị proxy, export full-res.
