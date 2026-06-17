#!/usr/bin/env bash
# package.sh — build Fuji-fy Studio (OpenGL) into a .app bundle + .dmg.
#
# Bundles libglfw into Contents/Frameworks and relinks it; ad-hoc signs (required on
# Apple Silicon after install_name_tool rewrites the binary); then makes a DMG.
#
# NOTE: this is a PREVIEW build. The app still shells out to the Python engine at its
# compiled-in project root (needs python3 + `pip install rawpy Pillow numpy`). A fully
# standalone build must bundle the engine — see README.
set -euo pipefail
cd "$(dirname "$0")"

APP_NAME="Fuji-fy Studio"
BIN="fujify"
DIST="dist"
APP="$DIST/$APP_NAME.app"
DMG="$DIST/Fuji-fy-Studio-macOS.dmg"

echo "==> build $BIN"
make "$BIN" >/dev/null

echo "==> lay out $APP"
rm -rf "$APP" "$DMG"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp "$BIN" "$APP/Contents/MacOS/$BIN"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleDisplayName</key><string>$APP_NAME</string>
  <key>CFBundleIdentifier</key><string>app.fujify.studio</string>
  <key>CFBundleVersion</key><string>0.1.0</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>CFBundleExecutable</key><string>$BIN</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
</dict></plist>
PLIST

echo "==> bundle Python engine (PyInstaller onedir)"
ENGINE_SRC="engine-dist/fujify-engine"
if [ ! -x "$ENGINE_SRC/fujify-engine" ]; then
  ( cd .. && python3 -m PyInstaller --noconfirm --onedir --name fujify-engine \
      --paths . --collect-all rawpy --collect-submodules core \
      --distpath imgui-poc/engine-dist --workpath /tmp/fujify-pyi --specpath /tmp/fujify-pyi \
      imgui-poc/engine/preview_server.py >/tmp/fujify-pyi.log 2>&1 ) \
    || { echo "engine build failed — see /tmp/fujify-pyi.log"; exit 1; }
fi
cp -R "$ENGINE_SRC" "$APP/Contents/Resources/engine"
[ -f assets/sample.ARW ] && cp assets/sample.ARW "$APP/Contents/Resources/sample.ARW"

echo "==> bundle + relink GLFW"
GLFW_REF="$(otool -L "$APP/Contents/MacOS/$BIN" | awk '/libglfw/{print $1; exit}')"
cp -L "$GLFW_REF" "$APP/Contents/Frameworks/libglfw.3.dylib"
chmod u+w "$APP/Contents/Frameworks/libglfw.3.dylib"
install_name_tool -id @rpath/libglfw.3.dylib "$APP/Contents/Frameworks/libglfw.3.dylib"
install_name_tool -change "$GLFW_REF" @rpath/libglfw.3.dylib "$APP/Contents/MacOS/$BIN"
install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/$BIN"

echo "==> ad-hoc codesign (inside-out; Apple Silicon needs valid signatures)"
codesign --force --timestamp=none --sign - "$APP/Contents/Frameworks/libglfw.3.dylib"
codesign --force --timestamp=none --sign - "$APP/Contents/Resources/engine/fujify-engine"
codesign --force --timestamp=none --sign - "$APP/Contents/MacOS/$BIN"
codesign --force --timestamp=none --sign - "$APP"          # seal the bundle
codesign --verify --strict "$APP" && echo "   signature ok"

echo "==> create DMG"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"

echo "==> done"
echo "   app: $APP"
echo "   dmg: $DMG ($(du -h "$DMG" | cut -f1))"
