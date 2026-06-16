# Fuji-Fy Server

HTTP mỏng (FastAPI) bọc `core.engine.process_image`. **Triển khai production** (Docker, reverse proxy, storage) nằm ngoài repo — xem [docs/pipeline.md](../../docs/pipeline.md) cho luồng xử lý ảnh.

Cấu trúc: `app/` (config, routers), `main.py` (entry `uvicorn apps.server.main:app`).

## Install

```bash
cd /path/to/Fuji-fy
python3 -m pip install -r requirements.txt
python3 -m pip install -r apps/server/requirements.txt
```

## Configuration (local)

Copy `.env.example` → `.env` hoặc export `FUJIFY_*`. `FUJIFY_CORS_ORIGINS` chỉ cần khi gọi API từ trình duyệt khác origin.

## Run

Từ root repo:

```bash
uvicorn apps.server.main:app --reload --host 0.0.0.0 --port 8000
```

Hoặc: `npm run server:dev`

## Endpoints

- `GET /health`
- `GET /presets`
- `POST /process/upload` (multipart upload)
- `GET /files/outputs/{name}`
- `GET /files/compares/{name}`
- `POST /social/share-video` (multipart `file`, optional `duration`, `layout=vertical|horizontal`) — cần **ffmpeg** trên máy chạy server; trả JSON có `files.video` để client `GET /files/outputs/...`

## Example

```bash
curl -X POST "http://127.0.0.1:8000/process/upload" \
  -F "file=@DSC00043.JPG" \
  -F "preset=default_indoor" \
  -F "compare=true"
```

