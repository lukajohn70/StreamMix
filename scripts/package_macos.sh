#!/usr/bin/env bash
# =============================================================================
# scripts/package_macos.sh  — StreamMix macOS .app + .dmg packager
# =============================================================================
# Requirements:
#   • Qt6 installed via Homebrew or qt.io installer (macdeployqt in PATH)
#   • CMake 3.27+
#   • Xcode Command Line Tools (codesign, hdiutil, create-dmg optional)
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
VERSION=$(grep -oP '(?<=STREAMMIX_VERSION ")[^"]+' "$ROOT_DIR/libstreammix/include/streammix/version.h" 2>/dev/null || echo "1.0.0")

echo "══════════════════════════════════════════"
echo "  StreamMix macOS Packager v$VERSION"
echo "  Build type: $BUILD_TYPE"
echo "══════════════════════════════════════════"

# ── 1. CMake configure + build ───────────────────────────────────────────────
mkdir -p "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$(sysctl -n hw.logicalcpu)"

# ── 2. Locate .app bundle ────────────────────────────────────────────────────
APP_SRC="$BUILD_DIR/streammix-app/$APP_BUNDLE"
if [ ! -d "$APP_SRC" ]; then
    APP_SRC="$BUILD_DIR/$APP_BUNDLE"
fi
if [ ! -d "$APP_SRC" ]; then
    echo "❌  Could not find $APP_BUNDLE in build directory." >&2
    exit 1
fi

# ── 3. macdeployqt — bundle Qt frameworks ───────────────────────────────────
echo "🔧  Running macdeployqt..."
macdeployqt "$APP_SRC" \
    -always-overwrite \
    -verbose=1

# ── 4. Code signing (ad-hoc if no identity provided) ────────────────────────
SIGN_IDENTITY="${MACOS_SIGN_IDENTITY:-}"
if [ -n "$SIGN_IDENTITY" ]; then
    echo "🔏  Signing with identity: $SIGN_IDENTITY"
    codesign --deep --force --options=runtime \
        --entitlements "$ROOT_DIR/scripts/entitlements.plist" \
        --sign "$SIGN_IDENTITY" "$APP_SRC"
else
    echo "⚠️   No MACOS_SIGN_IDENTITY set — applying ad-hoc signature"
    codesign --deep --force --sign - "$APP_SRC"
fi

# ── 5. Create .dmg ───────────────────────────────────────────────────────────
mkdir -p "$DIST_DIR"
DMG_OUT="$DIST_DIR/${APP_NAME}-${VERSION}.dmg"

if command -v create-dmg &>/dev/null; then
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
    echo "📦  Building DMG with hdiutil (no create-dmg)..."
    TMP_DMG_DIR="$(mktemp -d)"
    cp -R "$APP_SRC" "$TMP_DMG_DIR/"
    hdiutil create \
        -volname "$APP_NAME $VERSION" \
        -srcfolder "$TMP_DMG_DIR" \
        -ov -format UDZO \
        "$DMG_OUT"
    rm -rf "$TMP_DMG_DIR"
fi

echo ""
echo "✅  Package ready:"
echo "    $DMG_OUT"
