# Tầm nhìn Storage “tráo hình” (cloud + proxy)

Tài liệu này cố định **ý tưởng sản phẩm** cho trụ **Storage** — khớp [product-model.md](product-model.md): đây là chỗ **thu phí / quota**. **Không** có triển khai R2, Go worker hay DB trong repo Fuji-Fy hiện tại; đây là **kiến trúc mục tiêu** để bạn (hoặc team) xây bên ngoài.

---

## 1. Vấn đề người dùng

- Ảnh gốc (RAW/JPEG lớn) chiếm dung lượng máy; iCloud/Photos đôi khi trải nghiệm “chờ tải” khi xem lại.
- Fuji-Fy muốn: **bản gốc an toàn trên object storage**, trên máy giữ **bản xem nhanh nhỏ** (proxy / “ghost”) + **metadata giàu** (EXIF, recipe, tag) để lướt và tìm kiếm vẫn mượt.

---

## 2. Quy trình “tráo” (Swapping) — logic nghiệp vụ

| Bước | Việc | Ghi chú |
|------|------|--------|
| **Analyze** | Quét / import danh sách ảnh (từ app hoặc album được chọn), đọc metadata: ngày chụp, kích thước, EXIF, (sau này) film simulation nếu đọc được. | Phần EXIF/tag có thể tái sử dụng hướng [exif_tags](../core/exif_tags.py) trên server hoặc port sang native. |
| **Upload** | Đẩy **bản gốc** (hoặc bản lossless bạn chọn) lên object storage (ví dụ **Cloudflare R2**). | Cần **checksum** (SHA-256) ngay khi upload. |
| **Generate proxy** | Worker tạo **ảnh xem**: WebP / AVIF / JPEG chất lượng thấp hơn nhiều so với gốc, đủ nét cho màn hình điện thoại. | Mục tiêu ~1–5% dung lượng gốc tùy ảnh; không thay thế bản in / chỉnh pixel-level. |
| **Replace (UX)** | Chỉ sau khi **xác nhận** object trên cloud **khớp checksum** với file local, mới gợi ý user **xóa bản gốc khỏi máy** và giữ proxy trong app cache / album ảo của Fuji-Fy. | Xem mục 5 — giới hạn Apple/Google. |

---

## 3. Ghost / proxy — kỹ thuật

- **WebP / AVIF**: tốt cho kích thước nhỏ; cần decode trên iOS/Android (hỗ trợ tốt trên bản OS mới).
- **HEIC**: có thể nhỏ nhưng ecosystem và pipeline worker cần cân nhắc (license/tooling).
- Proxy lưu **cạnh** metadata trong DB; app ưu tiên load proxy từ **cache cục bộ**, chỉ kéo **gốc** khi user bấm “Tải gốc” / “Chỉnh đầy đủ” / “Xuất in”.

---

## 4. Chỉ mục thông minh (gợi ý)

- **Trùng lặp / burst**: **perceptual hash (pHash)** để gom ảnh gần giống → gợi ý “giữ tốt nhất, còn lại chỉ cloud hoặc xóa local”.
- **Tag**: film simulation, máy, lens — từ EXIF / maker note khi đọc được; đồng bộ với [principles.md](principles.md) (Edit + Social).

### pHash: Go hay thư viện ngoài?

- **Go:** thư viện kiểu [`goimagehash`](https://github.com/corona10/goimagehash) (và các fork tương tự) — đủ cho worker độc lập, xử lý batch.
- **Python (gần `core/`):** có thể dùng `ImageHash` / pipeline Pillow trong **job worker** tách repo — hợp V1 nếu team đã quen stack ảnh Python.
- **Native mobile:** chỉ nên tính pHash nhẹ khi cần real-time; batch nặng nên để **server/worker**.

Không bắt buộc một ngôn ngữ: quan trọng là **cùng thuật toán + khoảng cách Hamming** trong DB để so khớp ổn định.

---

## 5. Giới hạn nền tảng (quan trọng)

- **iOS Photos:** Apple **không** cho app “thay byte” trực tiếp một `PHAsset` trong Thư viện ảnh hệ thống bằng file ghost giữ nguyên ID. Luồng thực tế thường là: **album / thư viện trong app Fuji-Fy** + người dùng **tự xóa** ảnh gốc trong Photos sau khi đã backup, hoặc dùng extension/flow được Apple cho phép — cần tư vấn pháp lý & guideline App Store.
- **Android:** linh hoạt hơn nhưng scoped storage và quyền đọc/ghi vẫn phải thiết kế rõ.

Tài liệu marketing “tráo hình” nên **khớp** với hành vi thật trên từng OS để tránh từ chối duyệt app.

---

## 6. Gợi ý schema (bên ngoài repo)

| Cột | Kiểu | Mô tả |
|-----|------|--------|
| `original_object_key` | string | Key bản gốc trên R2/S3 |
| `proxy_object_key` | string | Key ảnh xem (WebP/…) |
| `checksum_sha256` | string | Xác minh trước khi cho phép “đã an toàn trên cloud” |
| `phash` | bytes / string | Hash nhận diện gần trùng |
| `exif_json` | JSONB | ISO, khẩu, lens, … |
| `local_state` | enum | ví dụ `original_only` / `uploaded` / `swapped` (đã xóa gốc local sau verify) |

---

## 7. Niềm tin người dùng

- **Checksum + xác nhận upload** trước khi khuyến khích xóa local.
- **Khôi phục gốc** một lần bấm (pull từ R2) nếu trong quota.
- **Minh bạch** dung lượng đã tiết kiệm và số ảnh chỉ còn proxy trên máy.

---

## 8. Liên hệ với repo hiện tại

| Trong Fuji-Fy repo | Việc |
|---------------------|------|
| `core/` | Chỉnh ảnh, preset — free theo product-model. |
| `core/exif_tags.py` | Hướng đọc tag cho indexing (port hoặc gọi API). |
| `apps/mobile` | V1: edit + save/share local; Storage “tráo hình” là **lớp sau**. |

Khi bạn bắt đầu code worker + R2, có thể mở repo/service **riêng** và tham chiếu lại file này làm contract nghiệp vụ.
