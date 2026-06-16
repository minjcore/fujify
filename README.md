# Fuji-Fy — pipeline xử lý ảnh

**Mục tiêu sản phẩm:** hai trụ **Edit ảnh** (WB, tone, preset, export) và **Social** (xuất ảnh/video + tag EXIF, chất lượng nguồn trên chuẩn IG/Threads) — chi tiết: [docs/principles.md](docs/principles.md).

**Phạm vi repo:** nghiệp vụ và luồng xử lý hình ảnh (WB, tone, preset, export). Hosting, domain, object storage, IAM, v.v. do bạn quản lý bên ngoài; tài liệu lõi nằm ở [docs/pipeline.md](docs/pipeline.md).

**Mô hình sản phẩm (khẳng định):** thu phí / quota ở **Storage**; **phần còn lại** (công cụ & pipeline) **free cho người dùng** — chi tiết ngắn: [docs/product-model.md](docs/product-model.md).

**Nguyên tắc chất lượng (chia sẻ xịn hơn IG/Threads ở tầng file nguồn):** [docs/principles.md](docs/principles.md).

CLI ban đầu cho bài toán AWB/cảnh Sony NEX-5N; engine dùng lại cho JPEG/RAW qua `core/loader`.

## Cấu trúc

- `core/`: engine — loader, white balance, tone, export, presets.
- `pipeline.py`: CLI.
- `core/api.py`: JSON API cho bridge/script.
- `desktop/bridge/`: Node gọi engine.
- `apps/mobile/`: Expo, adapter JPEG (V1).
- `apps/server/`: HTTP mỏng quanh `process_image` (dev / tích hợp tùy chọn).
- `apps/shared-ui/`: compare / preset UI dùng chung.
- `shared/contracts/`: schema tham số cho client.
- `tests/`, `tools/`: parity cross-platform.
- `docs/release.md`: ghi chú bản build.
- `docs/software-design.md`: **bảng thiết kế phần mềm** (kiến trúc, engine, Studio desktop, threading, i18n).
- `docs/market-research.md`: **nghiên cứu thị trường** (quy mô, đối thủ, giá, định vị, SWOT).
- `docs/pipeline.md`: **flow pipeline** (mermaid + entry points).
- `docs/principles.md`: **nguyên tắc** — social ≥2K, JPEG 4:4:4, v.v.
- `docs/third-party.md`: license / `photo-editor` (MIT) khi tích hợp sau.
- `docs/storage-swap-vision.md`: tầm nhìn Storage “tráo hình” (R2/proxy/pHash) — triển khai ngoài repo.
- `docs/full-studio-mvp.md`: flow MVP nhanh nhất để gom Edit + Social + Storage.

## Install

```bash
python3 -m pip install -r requirements.txt
```

## Single image

```bash
python3 pipeline.py DSC00038.JPG -o DSC00038_processed.jpg \
  --temp 5200 \
  --tint 5 \
  --wb-shift-b 8 \
  --brightness 0.05 \
  --contrast 0.15 \
  --shadows 0.1 \
  --highlights -0.1 \
  --compare
```

## Built-in preset (saved use-case)

```bash
python3 pipeline.py DSC00038.JPG --preset case01_flower_warm_fix --compare
```

Available presets:

- `case01_flower_warm_fix`
- `case02_indoor_neon_neutral`
- `default_indoor`
- `nex5n_auto_keep_vibe`

## Pick neutral point

Use `--wb-pick X,Y` to neutralize a sampled area (white petal, gray card, etc.):

```bash
python3 pipeline.py DSC00038.JPG --wb-pick 1030,705 --wb-pick-radius 8
```

## Batch mode

```bash
python3 pipeline.py --batch . -o ./out --temp 5200 --wb-shift-b 10
```

## Engine JSON API (for wrappers)

```bash
python3 -m core.api <<'EOF'
{
  "input_path": "DSC00038.JPG",
  "preset": "case01_flower_warm_fix",
  "compare": true
}
EOF
```

## Desktop bridge

```bash
node desktop/bridge/process-job-cli.mjs payload.json
```

See `desktop/README.md` for payload format.

## Mobile app (Android, offline JPEG-first)

**Mobile-first — bắt đầu từ đây:** [docs/mobile-first-start.md](docs/mobile-first-start.md) (thứ tự bước 0 → 3).

```bash
cd apps/mobile
npm install
npm run android
```

## Server API (tùy chọn — bọc quanh engine)

Chỉ để gọi pipeline qua HTTP khi cần; không bắt buộc cho nghiệp vụ lõi.

```bash
python3 -m pip install -r apps/server/requirements.txt
npm run server:dev
```

```bash
curl -X POST "http://127.0.0.1:8000/process/upload" \
  -F "file=@DSC00043.JPG" \
  -F "preset=default_indoor" \
  -F "compare=true"
```

Chi tiết cấu trúc service: [apps/server/README.md](apps/server/README.md).

## Validation

```bash
python3 tools/check_preset_parity.py
python3 tools/validate_cross_platform.py --config tests/cross_platform_pairs.json --out tests/cross_platform_report.json
```

## Video chia sẻ (tag máy / khẩu / tốc / ISO trên ảnh)

Cần **ffmpeg** trên PATH (build có libass). Chi tiết: mục *Tag EXIF + share video* trong [docs/pipeline.md](docs/pipeline.md).

```bash
python3 tools/render_share_video.py DSC00041.JPG -o share.mp4
python3 tools/render_share_video.py DSC00041.JPG --tags-json
```

**Ảnh social (JPEG, không cần ffmpeg):** tối thiểu **2K** (ngang ≥2048px), mặc định **hq** 2560px — Story 9:16, feed 4:5, vuông; khung đen + chữ EXIF đáy.

```bash
python3 tools/render_social_image.py DSC00041.JPG -o ig_story.jpg
python3 tools/render_social_image.py DSC00041.JPG --format feed -o ig_feed.jpg
python3 tools/render_social_image.py DSC00041.JPG --format square --no-brand
# Bản sàn 2K (2048px ngang), file nhỏ hơn hq: --tier ig
```

