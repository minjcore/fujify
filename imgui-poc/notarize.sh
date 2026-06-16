#!/usr/bin/env bash
# notarize.sh — release signing + notarization for Fuji-fy Studio.
#
# Re-signs the .app from package.sh with a Developer ID + hardened runtime + entitlements
# (required because the bundled PyInstaller engine loads its own dylibs), builds a DMG,
# submits to Apple notary, and staples the ticket. After this the DMG opens with no
# Gatekeeper warning.
#
# Prereqs (your Apple Developer account):
#   export DEV_ID="Developer ID Application: Your Name (TEAMID)"
#   # notary credentials — either a stored keychain profile…
#   export NOTARY_PROFILE="fujify"        # from: xcrun notarytool store-credentials
#   # …or Apple ID + team + app-specific password:
#   export APPLE_ID="you@example.com" TEAM_ID="TEAMID" APP_PWD="abcd-efgh-ijkl-mnop"
#
#   ./package.sh && ./notarize.sh
set -euo pipefail
cd "$(dirname "$0")"

APP_NAME="Fuji-fy Studio"
APP="dist/$APP_NAME.app"
DMG="dist/Fuji-fy-Studio-macOS.dmg"
ENT="/tmp/fujify-entitlements.plist"

[ -d "$APP" ] || { echo "Missing $APP — run ./package.sh first."; exit 1; }
[ -n "${DEV_ID:-}" ] || { echo "Set DEV_ID='Developer ID Application: … (TEAMID)'"; exit 1; }

# notarytool auth: keychain profile, or Apple ID creds.
if [ -n "${NOTARY_PROFILE:-}" ]; then
  NOTARY_AUTH=(--keychain-profile "$NOTARY_PROFILE")
elif [ -n "${APPLE_ID:-}" ] && [ -n "${TEAM_ID:-}" ] && [ -n "${APP_PWD:-}" ]; then
  NOTARY_AUTH=(--apple-id "$APPLE_ID" --team-id "$TEAM_ID" --password "$APP_PWD")
else
  echo "Set NOTARY_PROFILE, or APPLE_ID + TEAM_ID + APP_PWD."; exit 1
fi

echo "==> entitlements (hardened runtime needs these for the frozen Python engine)"
cat > "$ENT" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>com.apple.security.cs.allow-jit</key><true/>
  <key>com.apple.security.cs.allow-unsigned-executable-memory</key><true/>
  <key>com.apple.security.cs.disable-library-validation</key><true/>
</dict></plist>
PLIST

SIGN=(codesign --force --timestamp --options runtime --entitlements "$ENT" --sign "$DEV_ID")

echo "==> sign nested code inside-out (Developer ID)"
# every Mach-O the engine loads, then the engine exe, glfw, main binary, finally the app
find "$APP/Contents/Resources/engine" -type f \( -name "*.dylib" -o -name "*.so" \) -print0 \
  | while IFS= read -r -d '' lib; do "${SIGN[@]}" "$lib"; done
"${SIGN[@]}" "$APP/Contents/Resources/engine/fujify-engine"
"${SIGN[@]}" "$APP/Contents/Frameworks/libglfw.3.dylib"
"${SIGN[@]}" "$APP/Contents/MacOS/fujify"
"${SIGN[@]}" "$APP"
codesign --verify --strict --verbose=2 "$APP" && echo "   app signature ok"

echo "==> rebuild DMG from signed app"
rm -f "$DMG"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"; ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"
codesign --force --timestamp --sign "$DEV_ID" "$DMG"

echo "==> notarize (submit + wait)"
xcrun notarytool submit "$DMG" "${NOTARY_AUTH[@]}" --wait

echo "==> staple"
xcrun stapler staple "$DMG"
xcrun stapler validate "$DMG" && echo "   stapled ok"
spctl -a -t open --context context:primary-signature -v "$DMG" || true

echo "==> done → $DMG (signed + notarized + stapled)"
echo "   upload: cp '$DMG' ../web/download/ && node ../web/upload-r2.mjs download"
