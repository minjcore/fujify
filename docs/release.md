# Fuji-Fy V1 Packaging

## Targets

- macOS desktop app (Electron bundle)
- Windows desktop app (Electron bundle)
- Android app (Expo/React Native)

## Local build commands

### Desktop

```bash
npm install
npm run desktop:dist
```

Artifacts are produced by `electron-builder` for macOS/Windows.

### Android

```bash
cd apps/mobile
npm install
npx expo export --platform android
```

For signed APK/AAB, connect EAS profile in a follow-up step.

## CI pipeline

Workflow: `.github/workflows/release-v1.yml`

- matrix desktop builds on macOS + Windows
- Android export job on Ubuntu

