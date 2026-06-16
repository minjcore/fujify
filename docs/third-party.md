# Thành phần bên thứ ba trong repo

## `photo-editor/` (Android / JNI)

- **License:** [MIT](../photo-editor/LICENSE.md) — Copyright (c) 2020 **Utkarsh Tiwari**.
- **Ý nghĩa pháp lý (MIT):** được phép dùng, sửa, gộp, phân phối miễn là **giữ nguyên bản quyền và đoạn license** trong bản copy / phần lớn mã nguồn bạn phát hành.
- **Ý nghĩa xã giao:** nếu bạn **nhúng đáng kể** mã từ project này vào Fuji-Fy (đặc biệt bản build thương mại), nên **báo / xin phép lịch sự** tác giả (email/GitHub) — không bắt buộc bởi MIT, nhưng minh bạch và lịch sự.

**Lưu ý:** app **Expo** trong `apps/mobile` hiện dùng `expo-image-manipulator` + **lưu/chia sẻ** qua `expo-media-library` / `expo-sharing` — **không** tự động kéo mã từ `photo-editor/`. Chỉ khi bạn chủ động tích hợp editor native từ thư mục đó mới cần tuân MIT + ghi nhận tác giả như trên.

## `ffmpeg-kit-react-native` (Video B on-device)

- **Mục đích:** encode MP4 + burn ASS **trên máy** Android (không upload server).
- **License:** theo gói FFmpeg Kit (thường **LGPL 3.0** mặc định; một số biến thể có thành phần GPL) — xem [FFmpegKit license](https://github.com/arthenica/ffmpeg-kit/wiki/License) và README của package trên npm.
- **Gói npm:** `ffmpeg-kit-react-native` (upstream đã đánh dấu deprecated trên npm; vẫn dùng được cho V1, sau này nên đánh giá fork thay thế).
- **Cách chạy:** cần **native dev build** (`expo run:android`), **không** có trong Expo Go.

## Expo packages

Xem `apps/mobile/package.json` và license từng gói trên npm (Expo / MIT / BSD tùy package).
