# Fuji-Fy Desktop Bridge

Desktop wrapper bridge that runs local processing jobs via Python API.

## Usage

1. Create a payload file:

```json
{
  "projectDir": "/absolute/path/to/Fuji-fy",
  "request": {
    "input_path": "/absolute/path/to/image.jpg",
    "output_path": "/absolute/path/to/output.jpg",
    "preset": "default_indoor",
    "compare": true
  }
}
```

2. Run:

```bash
node bridge/process-job-cli.mjs payload.json
```

Returns a structured JSON `ProcessImageResult` from `core.api`.

