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

# ── 1. Locate Qt6 and Universal Dependencies ──────────────────────────────────
QT6_PREFIX=""
DEPS_PREFIX=""

if [ -d "/Applications/MAMP/htdocs/obs-vmix/obs-studio/.deps/obs-deps-qt6-2026-07-15-universal" ]; then
    QT6_PREFIX="/Applications/MAMP/htdocs/obs-vmix/obs-studio/.deps/obs-deps-qt6-2026-07-15-universal"
fi
if [ -d "/Applications/MAMP/htdocs/obs-vmix/obs-studio/.deps/obs-deps-2026-07-15-universal" ]; then
    DEPS_PREFIX="/Applications/MAMP/htdocs/obs-vmix/obs-studio/.deps/obs-deps-2026-07-15-universal"
fi

CMAKE_PREFIX=""
if [ -n "$QT6_PREFIX" ] && [ -n "$DEPS_PREFIX" ]; then
    CMAKE_PREFIX="-DCMAKE_PREFIX_PATH=$QT6_PREFIX;$DEPS_PREFIX"
elif [ -n "$QT6_PREFIX" ]; then
    CMAKE_PREFIX="-DCMAKE_PREFIX_PATH=$QT6_PREFIX"
fi

# ── 2. Configure + Build Native C++20 Qt6 Application ─────────────────────────
echo "⚙️   Configuring CMake with native Qt6 & FFmpeg..."
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    $CMAKE_PREFIX \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0

echo "🔨  Compiling native StreamMix broadcast engine..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$(sysctl -n hw.logicalcpu)"

# Locate built .app bundle
if [ -d "$BUILD_DIR/bin/$APP_BUNDLE" ]; then
    APP_SRC="$BUILD_DIR/bin/$APP_BUNDLE"
elif [ -d "$BUILD_DIR/streammix-app/$APP_BUNDLE" ]; then
    APP_SRC="$BUILD_DIR/streammix-app/$APP_BUNDLE"
elif [ -d "$BUILD_DIR/$APP_BUNDLE" ]; then
    APP_SRC="$BUILD_DIR/$APP_BUNDLE"
else
    echo "❌ Error: Could not locate built $APP_BUNDLE in $BUILD_DIR"
    exit 1
fi

echo "📦  Located native app bundle: $APP_SRC"

# ── 3. Bundle Cocoa Platform Plugins & qt.conf ────────────────────────────────
mkdir -p "$APP_SRC/Contents/PlugIns/platforms"
mkdir -p "$APP_SRC/Contents/Resources"

if [ -n "$QT6_PREFIX" ] && [ -f "$QT6_PREFIX/plugins/platforms/libqcocoa.dylib" ]; then
    cp "$QT6_PREFIX/plugins/platforms/libqcocoa.dylib" "$APP_SRC/Contents/PlugIns/platforms/"
fi

cat <<EOF > "$APP_SRC/Contents/Resources/qt.conf"
[Paths]
Plugins = PlugIns
EOF

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
