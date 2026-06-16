# Cross-platform Validation

## 1) Engine parity checks (desktop paths)

Run:

```bash
python3 tools/check_preset_parity.py
python3 tools/validate_cross_platform.py --config tests/cross_platform_pairs.json --out tests/cross_platform_report.json
```

This validates that:

- shared JSON presets match Python presets
- case outputs from CLI/API/desktop bridge stay within drift threshold

## 2) Android drift checks

1. Export processed Android outputs to project root:
   - `DSC00038_android_case01.jpg`
   - `DSC00043_android_case02.jpg`
2. Copy `tests/android_pairs.template.json` to `tests/android_pairs.json`.
3. Run:

```bash
python3 tools/validate_cross_platform.py --config tests/android_pairs.json --out tests/android_report.json
```

