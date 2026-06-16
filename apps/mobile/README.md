# Fuji-Fy Mobile (React Native / Expo)

V1 mobile app shell focused on Android offline flow.

Implemented:

- image import (gallery)
- preset selection + parameter edits
- Android JPEG-first processing adapter
- before/after compare view
- **Save to gallery** + **Share** (processed JPEG) via `expo-media-library` / `expo-sharing`
- **Export Social** presets: Story 9:16 / Feed 4:5 / Square 1:1 (2K+ width), then share
- **Video B (V1)** — hai cách:
  - **On-device:** `Export Video (on-device)` — `ffmpeg-kit-react-native` (cần **`expo run:android`**, không chạy trong **Expo Go**). Chữ overlay từ EXIF lúc Import (`expo-image-picker` `asset.exif`).
  - **Server:** `Export Video (server)` — upload lên `POST /social/share-video` → tải MP4 → Share / Save (`EXPO_PUBLIC_FUJIFY_API` trong `.env`).

Server phải chạy `uvicorn` và có **ffmpeg** (libass). Android emulator: `http://10.0.2.2:8000`.

### Mobile upload (Video B)

`Export Video B` dùng `fetch(..., { method: "POST", body: FormData })`:

- Trường **`file`**: object `{ uri, type, name }` — React Native đọc file cục bộ từ `uri` (thường `file://` sau `expo-image-manipulator`) và gửi multipart giống trình duyệt.
- Trường **`duration`**, **`layout`**: form text (`vertical` / `horizontal`).
- Ảnh gửi lên: **social export** nếu đã có, không thì **ảnh đã edit** (`outputUri`).

Sau khi server trả JSON `files.video`, app **GET** `EXPO_PUBLIC_FUJIFY_API + files.video` qua `expo-file-system` `downloadAsync` (không nhét toàn bộ MP4 vào RAM dạng base64).

Third-party editor code in repo root `photo-editor/` is **not** used by this Expo app by default; it is **MIT** (Utkarsh Tiwari) — see [docs/third-party.md](../../docs/third-party.md) if you integrate it later.

## Run

```bash
cd apps/mobile
npm install
npm run android
```

Notes:

- Adapter is intentionally JPEG-first for V1.
- Full parity with Python engine WB/tone math can be added in a native module in V2.

