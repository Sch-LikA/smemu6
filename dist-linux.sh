#!/usr/bin/env bash
# dist-linux.sh — Build and package a Linux AppImage of smemu6
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2024-2026 Marcel Prisi
#
# Usage:
#   ./dist-linux.sh [--skip-build]
#
#   --skip-build   Skip CMake configure+build (reuse build/ output)
#
# Output:  dist/smemu6-linux-x86_64-<version>.AppImage
#
# Prerequisites:
#   sudo apt install libsdl2-dev cmake make
#   FUSE: sudo apt install libfuse2   (needed to run AppImages)
#
# linuxdeploy is downloaded automatically to tools/ on first run.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ARCH="$(uname -m)"
BUILD_DIR="$SCRIPT_DIR/build"
APPDIR="$SCRIPT_DIR/build/AppDir"
DIST_ROOT="$SCRIPT_DIR/dist"
TOOLS_DIR="$SCRIPT_DIR/tools"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"

VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo "dev")"
OUTPUT_NAME="smemu6-linux-${ARCH}-${VERSION}.AppImage"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
SKIP_BUILD=0
for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Download linuxdeploy if needed
# ---------------------------------------------------------------------------
if [[ ! -x "$LINUXDEPLOY" ]]; then
    echo "==> Downloading linuxdeploy for ${ARCH}…"
    mkdir -p "$TOOLS_DIR"
    wget -q --show-progress -O "$LINUXDEPLOY" "$LINUXDEPLOY_URL"
    chmod +x "$LINUXDEPLOY"
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
if [[ $SKIP_BUILD -eq 0 ]]; then
    echo "==> Configuring (default preset)…"
    cmake --preset default -DCMAKE_INSTALL_PREFIX=/usr

    echo "==> Building…"
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

if [[ ! -f "$BUILD_DIR/smemu6" ]]; then
    echo "ERROR: $BUILD_DIR/smemu6 not found. Run without --skip-build first."
    exit 1
fi

# ---------------------------------------------------------------------------
# Install into AppDir
# ---------------------------------------------------------------------------
echo "==> Installing into AppDir…"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

# ---------------------------------------------------------------------------
# Run linuxdeploy to bundle libraries and create AppImage
# ---------------------------------------------------------------------------
echo "==> Building AppImage…"
mkdir -p "$DIST_ROOT"

# linuxdeploy needs ARCH and OUTPUT set
export ARCH="$ARCH"
export OUTPUT="$DIST_ROOT/$OUTPUT_NAME"

# Run with APPIMAGE_EXTRACT_AND_RUN=1 in case FUSE is not available in the
# build environment (e.g. CI containers).
APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$BUILD_DIR/smemu6" \
    --desktop-file "$SCRIPT_DIR/smemu6.desktop" \
    --icon-file "$SCRIPT_DIR/web/smaky6-256.png" \
    --output appimage

echo ""
echo "Done: $OUTPUT"
ls -lh "$OUTPUT"
