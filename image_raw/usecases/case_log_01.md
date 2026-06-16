# CASE LOG 01

- CaseID: `01`
- Camera: `Sony NEX-5N`
- Lens: `7Artisans 56mm f/1.8`
- File: `DSC00038.JPG`

## Scene

- Subject hoa mau tim, co vung sang gan trang.
- Nen la xanh + vang olive.
- Nguon sang am gay yellow cast.

## Observation

- AWB giu ambient warm.
- Hoa/chi tiet sang bi ngam kem nhe.
- Tim cua bong chua "thuan tim" nhu mat nguoi thay.

## Confirmed Cause

- Global AWB bias theo tong mau scene.
- Khong phai loi sensor; la warm illuminant + old-school AWB behavior.

## Locked Preset

Preset key: `case01_flower_warm_fix`

- temp: `5100`
- tint: `+3`
- wb_shift_b: `+8`
- contrast: `+0.12`
- shadows: `+0.08`
- highlights: `-0.10`

## One-line Run

```bash
python3 pipeline.py DSC00038.JPG --preset case01_flower_warm_fix --compare
```

