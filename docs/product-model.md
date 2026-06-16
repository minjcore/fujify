# Nguyên tắc mô hình sản phẩm (không thuộc triển khai kỹ thuật trong repo)

**Khẳng định:** phần **Storage** (lưu trữ ảnh/file trên hạ tầng bạn vận hành) là trục **có thể thu phí hoặc giới hạn quota**. **Phần còn lại** — công cụ xử lý, pipeline, preset, chỉnh WB/tone (logic trong `core/`), và trải nghiệm “dùng thử” không gắn bắt buộc với lưu trữ — **hướng tới miễn phí cho người dùng** (free cho họ).

## Tóm tắt

| Thành phần | Định hướng |
|------------|------------|
| **Engine / pipeline** (`core/`, CLI, API JSON) | Dùng miễn phí (theo license repo của bạn). |
| **Lưu trữ cloud / portal / bản gốc lâu dài** | Thu phí hoặc gói quota — bạn lo phía hạ tầng & billing bên ngoài repo. |
| **HTTP server mỏng** (`apps/server`) | Chỉ là cách gọi engine; không định nghĩa mô hình giá trong code. |

## Ghi chú

- Repo **Fuji-fy** giữ **nghiệp vụ xử lý ảnh**; không nhúng Stripe/R2/bảng giá vào đây trừ khi bạn chủ động mở rộng.
- Câu chữ “**free cho họ**” = người dùng không trả tiền cho **tính năng chỉnh/sửa cơ bản** nếu bạn giữ đúng hướng đó; **storage** là ranh giới kinh doanh rõ ràng.

## Tầm nhìn Storage (tráo hình / cloud + proxy)

Một hướng sản phẩm khả thi cho **Storage có phí**: backup bản gốc lên object storage (ví dụ R2), giữ **ảnh proxy nhỏ** + metadata phong phú trên máy, checksum trước khi gợi ý xóa gốc local — chi tiết kiến trúc, giới hạn iOS/Android, gợi ý pHash và schema: [storage-swap-vision.md](storage-swap-vision.md).
