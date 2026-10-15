#!/usr/bin/env bash
# =============================================================================
# scripts/package_linux.sh  — StreamMix Linux AppImage packager
# =============================================================================
# Requirements:
#   • Qt6 dev packages (qt6-base-dev, etc.)
#   • CMake 3.27+
#   • linuxdeploy + linuxdeploy-plugin-qt  (downloaded automatically below)
#   • AppImageTool  (downloaded automatically below)
#   • FUSE or AppImage runtime support
# Usage:
#   ./scripts/package_linux.sh [Release|Debug]
# =============================================================================
set -euo pipefail

BUILD_TYPE="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-linux"
DIST_DIR="$ROOT_DIR/dist/linux"
TOOLS_DIR="$ROOT_DIR/build-linux-tools"

APP_NAME="StreamMix"
VERSION=$(grep -oP '(?<=STREAMMIX_VERSION ")[^"]+' "$ROOT_DIR/libstreammix/include/streammix/version.h" 2>/dev/null || echo "1.0.0")

ARCH="$(uname -m)"

echo "══════════════════════════════════════════"
echo "  StreamMix Linux Packager v$VERSION"
echo "  Build type: $BUILD_TYPE | Arch: $ARCH"
echo "══════════════════════════════════════════"

# ── 1. Download linuxdeploy + Qt plugin if not present ───────────────────────
mkdir -p "$TOOLS_DIR"
cd "$TOOLS_DIR"

LINUXDEPLOY="linuxdeploy-$ARCH.AppImage"
LINUXDEPLOY_QT="linuxdeploy-plugin-qt-$ARCH.AppImage"

if [ ! -f "$LINUXDEPLOY" ]; then
    echo "📥  Downloading linuxdeploy..."
    curl -fsSL -o "$LINUXDEPLOY" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
    chmod +x "$LINUXDEPLOY"
fi

if [ ! -f "$LINUXDEPLOY_QT" ]; then
    echo "📥  Downloading linuxdeploy-plugin-qt..."
    curl -fsSL -o "$LINUXDEPLOY_QT" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage"
    chmod +x "$LINUXDEPLOY_QT"
fi

cd "$ROOT_DIR"

# ── 2. CMake configure + build ───────────────────────────────────────────────
mkdir -p "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="/usr"

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$(nproc)"

# ── 3. Install into AppDir ───────────────────────────────────────────────────
APPDIR="$BUILD_DIR/AppDir"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

# ── 4. Desktop entry + icon ──────────────────────────────────────────────────
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# Write .desktop file
cat > "$APPDIR/usr/share/applications/streammix.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=StreamMix
Comment=Professional Live Production Software (vMix 29 Parity)
Exec=StreamMix
Icon=streammix
Categories=AudioVideo;Video;
Keywords=streaming;live;production;switcher;vmix;
EOF

# Copy icon (fall back to placeholder if not present)
ICON_SRC="$ROOT_DIR/resources/icons/streammix_256.png"
ICON_DST="$APPDIR/usr/share/icons/hicolor/256x256/apps/streammix.png"
if [ -f "$ICON_SRC" ]; then
    cp "$ICON_SRC" "$ICON_DST"
else
    # Create minimal 1x1 placeholder so linuxdeploy doesn't fail
    python3 -c "
import struct, zlib
def png1x1():
    sig = b'\x89PNG\r\n\x1a\n'
    ihdr_data = struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0)
    ihdr_crc  = zlib.crc32(b'IHDR' + ihdr_data) & 0xffffffff
    ihdr = struct.pack('>I', 13) + b'IHDR' + ihdr_data + struct.pack('>I', ihdr_crc)
    raw  = b'\x00\xff\x00\x00'
    comp = zlib.compress(raw)
    idat_crc = zlib.crc32(b'IDAT' + comp) & 0xffffffff
    idat = struct.pack('>I', len(comp)) + b'IDAT' + comp + struct.pack('>I', idat_crc)
    iend_crc = zlib.crc32(b'IEND') & 0xffffffff
    iend = struct.pack('>I', 0) + b'IEND' + struct.pack('>I', iend_crc)
    return sig + ihdr + idat + iend
open('$ICON_DST','wb').write(png1x1())
" 2>/dev/null || touch "$ICON_DST"
fi

# ── 5. Run linuxdeploy + Qt plugin ───────────────────────────────────────────
mkdir -p "$DIST_DIR"

export OUTPUT="$DIST_DIR/${APP_NAME}-${VERSION}-${ARCH}.AppImage"
export QMAKE="${QMAKE:-qmake6}"

echo "🔧  Running linuxdeploy + Qt plugin..."
"$TOOLS_DIR/$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage \
    --desktop-file "$APPDIR/usr/share/applications/streammix.desktop"

echo ""
echo "✅  Package ready:"
echo "    $OUTPUT"
