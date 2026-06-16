# Full Studio MVP (A + B + C trong 1 flow)

Mục tiêu: gom **Edit ảnh (A)** + **Social xuất bản (B)** + **Storage tráo hình (C)** thành một trải nghiệm liền mạch, nhưng vẫn ra bản chạy được nhanh.

Nguyên tắc triển khai:
- **Ship nhanh theo thứ tự A -> B -> C**, không chờ C hoàn chỉnh mới phát hành.
- **C chỉ cần “stub contract” ở MVP**: ghi trạng thái backup/swap giả lập để UI flow hoàn chỉnh, backend thật làm sau.

---

## Flow người dùng (MVP nhanh nhất)

1. **Import ảnh**
2. **Edit** (preset + WB/tone, before/after)
3. **Social Export**
   - Ảnh social 2K+ (`tools/render_social_image.py` logic)
   - Video share có tag EXIF (`tools/render_share_video.py` logic)
4. **Storage CTA (stub)**
   - Nút: `Backup original` / `Mark as cloud-safe`
   - Badge trạng thái: `local_only` -> `uploaded` -> `swapped`
5. **Save/Share**

Kết quả: user thấy 1 studio duy nhất, dù storage thật chưa bật.

---

## Scope theo tuần (đề xuất)

### Phase 1 (2-4 ngày): A -> B chạy thật
- Mobile: Import -> Edit -> Compare -> Save -> Share (đã có nền).
- **Social ảnh HQ 2K+** trong app (crop theo Story / Feed / Square).
- **Video B (V1):** (1) **on-device** — `ffmpeg-kit-react-native` + ASS trong app (`expo run:android`); hoặc (2) **server** — `POST /social/share-video` + `EXPO_PUBLIC_FUJIFY_API` + ffmpeg trên host.
- Chuẩn preset đầu ra:
  - `Photo HQ` (2K+, JPEG 4:4:4)
  - `Video Vertical` (có EXIF overlay)

### Phase 2 (1-2 ngày): C dạng stub (không cần R2 thật)
- Thêm model cục bộ cho mỗi ảnh:
  - `storage_state`: `local_only | uploaded | swapped`
  - `checksum_local` (nếu có)
- UI:
  - CTA backup/swap
  - Dialog cảnh báo rõ đây là bản preview workflow

### Phase 3 (sau MVP): C backend thật
- Worker upload + checksum verify + proxy generation
- Đồng bộ trạng thái thật từ API
- Chính sách xóa local theo platform guideline

---

## Contract tối thiểu cho C (stub trước, backend dùng lại sau)

```json
{
  "asset_id": "string",
  "storage_state": "local_only | uploaded | swapped",
  "original_checksum_sha256": "string | null",
  "proxy_uri": "string | null",
  "cloud_key": "string | null",
  "updated_at": "iso8601"
}
```

MVP có thể lưu contract này ở local JSON/SQLite trong app.

---

## Definition of Done cho “Full Studio MVP”

- User đi được 1 vòng duy nhất:
  - Import -> Edit -> Export Social -> Save/Share
- Trong cùng màn/chi tiết ảnh, user thấy được trạng thái Storage (dù là stub)
- Không vi phạm principles:
  - Social image >= 2K
  - JPEG 4:4:4
  - Video pipeline có overlay EXIF đọc được

---

## File liên quan

- Product/quality: `docs/principles.md`
- Storage vision: `docs/storage-swap-vision.md`
- Mobile execution: `docs/mobile-first-start.md`
