# Bắt đầu mobile-first — thứ tự làm (không bị loãng)

Bạn đã có **lõi**: `apps/mobile` (Expo) + gallery → chỉnh preset → xử lý JPEG trên Android → before/after. **Mục tiêu sản phẩm** đầy đủ là **Edit ảnh + Social** ([principles.md](principles.md)); mobile hiện ưu tiên trụ **Edit**, trụ Social nối sau (CLI máy tính hoặc port vào app). Phần “bên ngoài” (hosting, storage thu phí) làm sau; **bắt đầu từ đây** để não không bị trống.

## Bước 0 — Chạy được trên máy thật (1 buổi)

1. Cài Android Studio / emulator **hoặc** bật USB debug điện thoại.
2. Từ root repo:
   ```bash
   cd apps/mobile && npm install && npm run android
   ```
3. Trên app: **Import** → **Run Fuji-Fy** → **Save to gallery** / **Share** → compare là xong vòng lặp đầu tiên.

*Nếu kẹt:* chỉ cần app build + 1 ảnh JPEG chạy qua pipeline — đừng nhảy sang iOS, server, hay R2.

## Bước 1 — Hiểu luồng code (nửa ngày)

Đọc theo thứ tự:

| File | Việc |
|------|------|
| `apps/mobile/App.tsx` | UI: chọn ảnh, state, gọi adapter |
| `apps/mobile/src/adapters/AndroidProcessingAdapter.ts` | Chỗ “xử lý ảnh” trên máy |
| `apps/mobile/src/contracts.ts` | Preset + `ProcessingSettings` (phải khớp ý với Python) |
| `shared/contracts/processing.schema.json` | Schema chung (sau này sync chặt với `core/`) |
| [docs/pipeline.md](pipeline.md) | Engine Python làm gì (WB → tone → export) |

Mục tiêu: biết **chỗ nào sửa** khi muốn thêm slider hoặc preset mới trên mobile.

## Bước 2 — Một cải tiến nhỏ có ý nghĩa (1–3 ngày)

Chọn **một** việc, làm xong mới sang việc khác:

- **UX:** nút “Reset preset”; lưu/chia sẻ sau xử lý đã có (**Save to gallery** / **Share**). Thư mục `photo-editor/` (MIT, Utkarsh Tiwari) là project riêng — nếu gộp mã native, xem [third-party.md](third-party.md).
- **Social ảnh (máy tính):** pipeline đã có CLI `tools/render_social_image.py` (Story / feed / vuông + tag EXIF) — sau có thể gọi cùng logic từ app hoặc server.
- **Preset:** thêm 1 preset trong `contracts` + test với ảnh thật của bạn.
- **So khớp:** chạy cùng tham số trên `pipeline.py` và trên app, so mắt (parity V1 không cần pixel-perfect nếu adapter đơn giản).

## Bước 3 — Chuẩn bị “sản phẩm” chứ chưa làm hạ tầng

- Màn **Onboarding** 3 dòng: chọn ảnh → chỉnh → xem before/after.
- **Empty state** rõ ràng khi chưa có ảnh.
- (Tùy chọn) **iOS build** khi Android đã ổn — vẫn mobile-first, không cần web trước.

## Việc *chưa* làm ở giai đoạn này

- Login, backend, R2, thanh toán — theo [product-model.md](product-model.md), để sau khi **một vòng dùng thử trên điện thoại** đã mượt.
- Đồng bộ 100% số học với Python — có thể là **V2** (native module hoặc gọi API).

## Câu hỏi gốc: “Bắt đầu từ đâu?”

**Trả lời ngắn:** mở `apps/mobile`, chạy `npm run android`, đi hết flow Import → Run → Compare. Mọi thứ khác nhánh từ đó.

Nếu mục tiêu là gom nhanh Full Studio (Edit + Social + Storage), đi theo checklist này: [full-studio-mvp.md](full-studio-mvp.md).
