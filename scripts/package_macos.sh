#!/usr/bin/env bash
# =============================================================================
# scripts/package_macos.sh  — StreamMix macOS .app + .dmg packager
# =============================================================================
# Requirements:
#   • Clang++ (Xcode Command Line Tools)
#   • CMake 3.27+ (optional for Qt GUI)
#   • hdiutil, codesign (standard macOS tools)
# Usage:
#   ./scripts/package_macos.sh [Release|Debug]
# =============================================================================
set -euo pipefail

BUILD_TYPE="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-macos"
DIST_DIR="$ROOT_DIR/dist/macos"

APP_NAME="StreamMix"
APP_BUNDLE="$APP_NAME.app"
DMG_NAME="${APP_NAME}.dmg"
VERSION=$(awk -F'"' '/STREAMMIX_VERSION/ {print $2}' "$ROOT_DIR/libstreammix/include/streammix/version.h" 2>/dev/null || echo "1.1.0")

echo "══════════════════════════════════════════"
echo "  StreamMix macOS Packager v$VERSION"
echo "  Build type: $BUILD_TYPE"
echo "══════════════════════════════════════════"

mkdir -p "$BUILD_DIR"
mkdir -p "$DIST_DIR"

APP_SRC="$BUILD_DIR/$APP_BUNDLE"

# ── 1. Configure + Build Application ──────────────────────────────────────────
BUILD_SUCCESS=false

# Try CMake Qt6 build first if Qt6 is installed
if command -v cmake &>/dev/null && cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 2>/dev/null; then
    if cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$(sysctl -n hw.logicalcpu)" 2>/dev/null; then
        if [ -d "$BUILD_DIR/streammix-app/$APP_BUNDLE" ]; then
            APP_SRC="$BUILD_DIR/streammix-app/$APP_BUNDLE"
            BUILD_SUCCESS=true
        elif [ -d "$BUILD_DIR/$APP_BUNDLE" ]; then
            APP_SRC="$BUILD_DIR/$APP_BUNDLE"
            BUILD_SUCCESS=true
        fi
    fi
fi

# Fallback / Native Standalone Engine Build
if [ "$BUILD_SUCCESS" = false ]; then
    echo "ℹ️   Building native StreamMix production bundle with clang++ C++20..."
    mkdir -p "$BUILD_DIR/bin"

    clang++ -std=c++20 -O3 \
        -I"$ROOT_DIR/libstreammix/include" \
        -I"$ROOT_DIR/libstreammix/src" \
        "$ROOT_DIR/src/preview_main.cpp" \
        "$ROOT_DIR/libstreammix/src/core/context.cpp" \
        "$ROOT_DIR/libstreammix/src/core/data_source.cpp" \
        "$ROOT_DIR/libstreammix/src/core/preset_manager.cpp" \
        "$ROOT_DIR/libstreammix/src/capture/source.cpp" \
        "$ROOT_DIR/libstreammix/src/capture/title_source.cpp" \
        "$ROOT_DIR/libstreammix/src/compositor/compositor.cpp" \
        "$ROOT_DIR/libstreammix/src/compositor/chroma_key.cpp" \
        "$ROOT_DIR/libstreammix/src/compositor/virtual_set.cpp" \
        "$ROOT_DIR/libstreammix/src/compositor/shader_pipeline.cpp" \
        "$ROOT_DIR/libstreammix/src/audio/audio_mixer.cpp" \
        "$ROOT_DIR/libstreammix/src/audio/audio_dsp.cpp" \
        "$ROOT_DIR/libstreammix/src/control/control_surface.cpp" \
        "$ROOT_DIR/libstreammix/src/control/ptz_controller.cpp" \
        "$ROOT_DIR/libstreammix/src/replay/instant_replay.cpp" \
        "$ROOT_DIR/libstreammix/src/scene/scene.cpp" \
        "$ROOT_DIR/libstreammix/src/scene/scene_manager.cpp" \
        "$ROOT_DIR/libstreammix/src/output/rtmp_output.cpp" \
        "$ROOT_DIR/libstreammix/src/output/stream_manager.cpp" \
        "$ROOT_DIR/libstreammix/src/output/multicorder.cpp" \
        "$ROOT_DIR/libstreammix/src/output/web_server.cpp" \
        -o "$BUILD_DIR/bin/StreamMix"

    # Assemble macOS .app bundle directory hierarchy
    echo "📦  Assembling $APP_BUNDLE structure..."
    rm -rf "$APP_SRC"
    mkdir -p "$APP_SRC/Contents/MacOS"
    mkdir -p "$APP_SRC/Contents/Resources"

    cp "$BUILD_DIR/bin/StreamMix" "$APP_SRC/Contents/MacOS/StreamMix"
    chmod +x "$APP_SRC/Contents/MacOS/StreamMix"

    # Copy Web Controller / Telestrator assets
    cp "$ROOT_DIR/index.html" "$APP_SRC/Contents/Resources/" 2>/dev/null || true
    cp "$ROOT_DIR/style.css"  "$APP_SRC/Contents/Resources/" 2>/dev/null || true
    cp "$ROOT_DIR/app.js"     "$APP_SRC/Contents/Resources/" 2>/dev/null || true

    # Generate Info.plist
    cat <<EOF > "$APP_SRC/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>StreamMix</string>
    <key>CFBundleIdentifier</key>
    <string>live.streammix.app</string>
    <key>CFBundleName</key>
    <string>StreamMix</string>
    <key>CFBundleDisplayName</key>
    <string>StreamMix Live Production</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSCameraUsageDescription</key>
    <string>StreamMix requires camera access for live video production.</string>
    <key>NSMicrophoneUsageDescription</key>
    <string>StreamMix requires microphone access for live audio mixing.</string>
</dict>
</plist>
EOF
fi

# ── 2. Bundle Qt frameworks if macdeployqt is present ─────────────────────────
if command -v macdeployqt &>/dev/null; then
    echo "🔧  Running macdeployqt..."
    macdeployqt "$APP_SRC" -always-overwrite -verbose=1
fi

# ── 3. Code signing (Hardened Runtime or ad-hoc) ──────────────────────────────
SIGN_IDENTITY="${MACOS_SIGN_IDENTITY:-}"
if [ -n "$SIGN_IDENTITY" ]; then
    echo "🔏  Signing with identity: $SIGN_IDENTITY"
    codesign --deep --force --options=runtime \
        --entitlements "$ROOT_DIR/scripts/entitlements.plist" \
        --sign "$SIGN_IDENTITY" "$APP_SRC"
else
    echo "⚠️   No MACOS_SIGN_IDENTITY set — applying ad-hoc code signature"
    codesign --deep --force --sign - "$APP_SRC"
fi

# Verify codesign
echo "🔍  Verifying code signature..."
codesign --verify --deep --strict "$APP_SRC" && echo "✔  Signature verified"

# ── 4. Create .dmg Disk Image ─────────────────────────────────────────────────
DMG_OUT="$DIST_DIR/${APP_NAME}-${VERSION}.dmg"
rm -f "$DMG_OUT"

if command -v create-dmg &>/dev/null && [ -f "$ROOT_DIR/resources/icons/StreamMix.icns" ]; then
    echo "📦  Building DMG with create-dmg..."
    create-dmg \
        --volname "$APP_NAME $VERSION" \
        --volicon "$ROOT_DIR/resources/icons/StreamMix.icns" \
        --window-pos 200 120 \
        --window-size 660 400 \
        --icon-size 128 \
        --icon "$APP_BUNDLE" 160 175 \
        --hide-extension "$APP_BUNDLE" \
        --app-drop-link 500 175 \
        "$DMG_OUT" \
        "$APP_SRC"
else
    echo "📦  Building compressed UDZO DMG with hdiutil..."
    TMP_DMG_DIR="$(mktemp -d)"
    cp -R "$APP_SRC" "$TMP_DMG_DIR/"
    ln -s /Applications "$TMP_DMG_DIR/Applications"
    hdiutil create \
        -volname "$APP_NAME $VERSION" \
        -srcfolder "$TMP_DMG_DIR" \
        -ov -format UDZO \
        "$DMG_OUT"
    rm -rf "$TMP_DMG_DIR"
fi

echo ""
echo "══════════════════════════════════════════"
echo "  ✅  StreamMix macOS Deployment Complete!"
echo "══════════════════════════════════════════"
echo "  • App Bundle : $APP_SRC"
echo "  • DMG Image  : $DMG_OUT"
echo "  • Size       : $(du -sh "$DMG_OUT" | awk '{print $1}')"
echo ""
