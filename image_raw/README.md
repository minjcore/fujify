# Image Processing Pipeline CLI

Custom pipeline to override in-camera AWB behavior for Sony NEX-5N images.

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

