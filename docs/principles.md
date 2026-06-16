# Nguyên tắc Fuji-Fy

Tài liệu này gắn **mục tiêu sản phẩm** và **định hướng kỹ thuật** trong repo. Khác với [product-model.md](product-model.md) (chỗ thu phí vs free), phần dưới giải thích **vì sao** bản xuất Social phải **xịn hơn Instagram và Threads** ở tầng **file nguồn** trước khi upload.

---

## Mục tiêu sản phẩm: hai trụ **Edit ảnh** và **Social**

Đây là **north star** của Fuji-Fy — mọi roadmap trong repo nên kiểm tra có phục vụ một trong hai trụ (hoặc cả hai).

| Trụ | Ý nghĩa cho người dùng | Phần chính trong repo |
|-----|-------------------------|------------------------|
| **Edit ảnh** | Chỉnh màu, WB, tone, preset; so sánh trước/sau; xuất ảnh đã xử lý chất lượng cao. | `core/` (loader, white_balance, tone, export, presets, engine), `pipeline.py`, `core/api.py`, `apps/mobile` adapter xử lý. |
| **Social** | Sau khi có ảnh (gốc hoặc đã edit): xuất **ảnh / video** để đăng mạng xã hội — khung đẹp, **tag EXIF** (máy, lens, khẩu độ…), **độ phân giải và JPEG vượt chuẩn app mạng xã hội** theo các mục dưới. | `core/exif_tags.py`, `core/share_image.py`, `core/share_video.py`, CLI `tools/render_*`. |

**Hành trình mong muốn:** *chọn ảnh → chỉnh (Edit) → (tuỳ chọn) xuất bản Social* — hai trụ **liền mạch**, không tách thành hai app vô nghĩa trong đầu người dùng.

**Storage (trụ thương mại, triển khai ngoài repo lõi):** mô hình “tráo hình” — gốc trên cloud, proxy nhỏ + metadata trên máy, checksum trước khi gợi ý xóa local — [storage-swap-vision.md](storage-swap-vision.md), [product-model.md](product-model.md).

**Roadmap thực thi nhanh (A+B+C):** [full-studio-mvp.md](full-studio-mvp.md).

---

## 1. IG / Threads không phải chuẩn chất lượng

Instagram và Threads **luôn** tái mã hóa ảnh/video sau khi nhận file: giảm bitrate, đổi kích thước, có thể giảm độ phân giải hiển thị theo thiết bị và feed. Điều đó **không thể** bị Fuji-Fy kiểm soát.

**Nguyên tắc:** Fuji-Fy **không** coi “đủ bằng IG” là đủ. Output dùng để chia sẻ phải là **điểm xuất phát tốt nhất hợp lý**: nhiều pixel hơn khổ quen thấy trên app, ít tổn hao màu/chi tiết hơn JPEG “tiết kiệm” kiểu tin nhắn.

Người dùng vẫn có thể thấy nén sau upload; **cam kết của chúng ta** là: file **trước** bước đó **rõ và giàu thông tin hơn** so với export mặc định 1080p + nén mạnh từ điện thoại hay template app mạng xã hội.

---

## 2. Ảnh social (static) — bắt buộc “ít nhất 2K”

**Quy ước 2K trong repo:** cạnh **ngắn** của canvas (chiều ngang khung dọc) **≥ 2048 px**. Không xuất bản social card ở 1080px ngang làm “bản chính” trong tooling Fuji-Fy.

| Tier | Cạnh ngắn | JPEG mặc định | Ghi chú |
|------|-----------|----------------|---------|
| **hq** (mặc định) | **2560 px** | ~97 | Trên sàn 2K, ưu tiên độ sạch |
| **ig** (tên lịch sử) | **2048 px** | ~92 | Vẫn đạt sàn 2K, file nhỏ hơn hq |

**Kỹ thuật bắt buộc trong `core/share_image`:**

- Resize ảnh vào khung: **LANCZOS** (không nearest / bilinear cho bước cuối).
- JPEG: **`subsampling=0`** (4:4:4) để giảm vỡ màu cạnh chữ và chi tiết.
- **`optimize=True`** khi lưu JPEG (tối ưu entropy, không làm mềm ảnh).

Implementation: `core/share_image.py`, CLI `tools/render_social_image.py`. Chi tiết luồng: [pipeline.md](pipeline.md).

---

## 3. Video chia sẻ

- Encode qua **ffmpeg** với pipeline rõ ràng (scale + pad + phụ đề ASS), độ phân giải có thể cấu hình; tránh coi 720p là đủ cho “bản giới thiệu” nếu mục tiêu là cảm giác premium.
- Yêu cầu môi trường: **ffmpeg** có **libass** khi burn text.

`core/share_video.py`, `tools/render_share_video.py`.

---

## 4. Metadata / tag (EXIF)

- Đọc tag máy, lens, khẩu, tốc, ISO, tiêu cự khi EXIF chuẩn có mặt (`core/exif_tags.py`).
- Overlay chữ phải **đọc được** trên nền ảnh (gradient, stroke, contrast) — ưu tiên khả năng đọc hơn “minimal mờ nhạt” kiểu watermark app.

---

## 5. Khi implement tính năng mới

- Tính năng mới phải **gắn rõ** vào trụ **Edit**, **Social**, hoặc **cầu nối** giữa hai (ví dụ nút “Xuất social” sau khi apply preset).
- Bất kỳ preset “xuất để post mạng xã hội” nào: **kiểm tra** có đạt **sàn 2K** và **JPEG 4:4:4** (hoặc định dạng lossless nếu có lý do) trước khi merge.
- Nếu thêm tùy chọn “nhỏ / nhanh”, phải **đặt tên và help rõ** (ví dụ preview-only), không đặt làm mặc định thay cho bản chất lượng.
- Mobile / server sau này nên **tái sử dụng** cùng nguyên tắc (độ phân giải tối thiểu, subsampling, resampling), không duplicate logic suy giảm chất lượng im lặng.

---

## 6. Tham chiếu nhanh

| Mục | Vị trí |
|-----|--------|
| Edit (pipeline) | `core/engine.py`, `core/loader.py`, `pipeline.md` |
| Ảnh social 2K+ | `core/share_image.py`, `tools/render_social_image.py` |
| Video + tag | `core/share_video.py`, `tools/render_share_video.py` |
| EXIF | `core/exif_tags.py` |
| Mô hình thu phí / free | [product-model.md](product-model.md) |
