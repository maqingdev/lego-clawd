#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PACKAGE_DIR="$PROJECT_ROOT/macos/LegoClawdBar"
BUILD_DIR="$PROJECT_ROOT/macos/build"
APP_DIR="$BUILD_DIR/Lego Clawd Bar.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
SOURCE_RESOURCES_DIR="$PACKAGE_DIR/Sources/LegoClawdBar/Resources"

cd "$PACKAGE_DIR"
swift build -c release

rm -rf "$APP_DIR"
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"
cp "$PACKAGE_DIR/.build/release/LegoClawdBar" "$MACOS_DIR/LegoClawdBar"
cp "$SOURCE_RESOURCES_DIR/AppIcon.icns" "$RESOURCES_DIR/AppIcon.icns"
cp "$SOURCE_RESOURCES_DIR/MenuBarIconTemplate.png" "$RESOURCES_DIR/MenuBarIconTemplate.png"

cat > "$CONTENTS_DIR/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>LegoClawdBar</string>
  <key>CFBundleIdentifier</key>
  <string>dev.maqing.lego-clawd-bar</string>
  <key>CFBundleName</key>
  <string>Lego Clawd Bar</string>
  <key>CFBundleIconFile</key>
  <string>AppIcon</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1.0</string>
  <key>CFBundleVersion</key>
  <string>1</string>
  <key>LSMinimumSystemVersion</key>
  <string>14.0</string>
</dict>
</plist>
PLIST

printf '%s\n' "$APP_DIR"
