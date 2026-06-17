#!/usr/bin/env bash
# package.sh — build Fuji-fy Studio (Vulkan/MoltenVK) into a standalone .app + .dmg.
#
# Bundles libglfw + the Vulkan loader + libMoltenVK (with an ICD manifest) and the frozen
# Python engine into the .app, relinks dylibs to @rpath, ad-hoc signs (required on Apple
# Silicon after install_name_tool), then makes a DMG. Runs with no system Vulkan/Python.
#
# Release signing + notarization: see notarize.sh.
set -euo pipefail
cd "$(dirname "$0")"

APP_NAME="Fuji-fy Studio"
BIN="fujify_vk"
DIST="dist"
APP="$DIST/$APP_NAME.app"
DMG="$DIST/Fuji-fy-Studio-macOS.dmg"
FW="$APP/Contents/Frameworks"

VK_LOADER="$(brew --prefix vulkan-loader)/lib/libvulkan.1.dylib"
MOLTENVK="$(brew --prefix molten-vk 2>/dev/null)/lib/libMoltenVK.dylib"
[ -f "$MOLTENVK" ] || MOLTENVK="/opt/homebrew/lib/libMoltenVK.dylib"

echo "==> build $BIN"
make "$BIN" >/dev/null

echo "==> lay out $APP"
rm -rf "$APP" "$DMG"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/vulkan/icd.d" "$FW"
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
# Refreeze when the frozen binary is missing OR the engine source is newer (stale cache
# silently shipped an old engine before — e.g. a build predating the rotate feature).
if [ ! -x "$ENGINE_SRC/fujify-engine" ] || [ engine/preview_server.py -nt "$ENGINE_SRC/fujify-engine" ]; then
  ( cd .. && python3 -m PyInstaller --noconfirm --onedir --name fujify-engine \
      --paths . --collect-all rawpy --collect-submodules core \
      --distpath imgui-poc/engine-dist --workpath /tmp/fujify-pyi --specpath /tmp/fujify-pyi \
      imgui-poc/engine/preview_server.py >/tmp/fujify-pyi.log 2>&1 ) \
    || { echo "engine build failed — see /tmp/fujify-pyi.log"; exit 1; }
fi
cp -R "$ENGINE_SRC" "$APP/Contents/Resources/engine"
[ -f assets/sample.ARW ] && cp assets/sample.ARW "$APP/Contents/Resources/sample.ARW"

echo "==> bundle dylibs + relink (@rpath)"
GLFW_REF="$(otool -L "$APP/Contents/MacOS/$BIN" | awk '/libglfw/{print $1; exit}')"
VK_REF="$(otool -L "$APP/Contents/MacOS/$BIN" | awk '/libvulkan/{print $1; exit}')"
cp -L "$GLFW_REF" "$FW/libglfw.3.dylib"
cp -L "$VK_LOADER" "$FW/libvulkan.1.dylib"
cp -L "$MOLTENVK" "$FW/libMoltenVK.dylib"
chmod u+w "$FW"/*.dylib
install_name_tool -id @rpath/libglfw.3.dylib   "$FW/libglfw.3.dylib"
install_name_tool -id @rpath/libvulkan.1.dylib "$FW/libvulkan.1.dylib"
install_name_tool -id @rpath/libMoltenVK.dylib "$FW/libMoltenVK.dylib"
install_name_tool -change "$GLFW_REF" @rpath/libglfw.3.dylib   "$APP/Contents/MacOS/$BIN"
install_name_tool -change "$VK_REF"   @rpath/libvulkan.1.dylib "$APP/Contents/MacOS/$BIN"
install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/$BIN"

# ICD manifest: loader → bundled MoltenVK (path relative to this JSON).
cat > "$APP/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'ICD'
{
  "file_format_version": "1.0.0",
  "ICD": { "library_path": "../../../Frameworks/libMoltenVK.dylib", "api_version": "1.2.0" }
}
ICD

echo "==> ad-hoc codesign (inside-out; Apple Silicon needs valid signatures)"
codesign --force --timestamp=none --sign - "$FW/libglfw.3.dylib"
codesign --force --timestamp=none --sign - "$FW/libvulkan.1.dylib"
codesign --force --timestamp=none --sign - "$FW/libMoltenVK.dylib"
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
