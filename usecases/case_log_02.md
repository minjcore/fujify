# CASE LOG 02

- CaseID: `02`
- Camera: `Sony NEX-5N`
- Lens: `7Artisans 56mm f/1.8`
- File: `DSC00043.JPG`

## Scene

- Indoor, white-neon style lighting.
- Subject has white paper cup and blue pen.
- User feedback: blue pen looks brighter than real life.

## Observation

- Overall scene looks slightly cool.
- White objects can drift a bit blue/cyan.
- Blue object luminance feels a little boosted.

## Hypothesis

- Cool illuminant + global AWB interaction.
- Blue channel appears too strong for perceived reality.

## Locked Preset (Expected)

Preset key: `case02_indoor_neon_neutral`
Locked alias: `default_indoor`

- temp: `5600`
- tint: `+2`
- wb_shift_b: `-2`
- brightness: `-0.01`
- contrast: `+0.05`
- shadows: `+0.02`
- highlights: `-0.12`

## One-line Run

```bash
python3 pipeline.py DSC00043.JPG --preset case02_indoor_neon_neutral --compare
```

```bash
python3 pipeline.py DSC00043.JPG --preset default_indoor --compare
```

