#!/usr/bin/env bash
# dist-win.sh — Build and package a Windows distribution of smemu6
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2024-2026 Marcel Prisi
#
# Usage:
#   ./dist-win.sh [--skip-build]
#
#   --skip-build   Skip CMake configure+build and just re-package build-win/
#
# Output:  dist/smaky6-win64-<version>.zip
#
# Prerequisites:
#   sudo apt install mingw-w64 zip
#   SDL2 MinGW dev package at ~/dev/windows/SDL2-2.30.3/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
# SDL2_ROOT can be overridden via the environment (useful in CI).
# Default: ~/dev/windows/SDL2-2.30.3/x86_64-w64-mingw32
SDL2_ROOT="${SDL2_ROOT:-$HOME/dev/windows/SDL2-2.30.3/x86_64-w64-mingw32}"
SDL2_BIN="$SDL2_ROOT/bin"
BUILD_DIR="$SCRIPT_DIR/build-win"
DIST_ROOT="$SCRIPT_DIR/dist"

# Derive version from git (e.g. v1.2-3-gabcdef → 1.2-3-gabcdef)
VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo "dev")"
DIST_NAME="smemu6-win64-${VERSION}"
DIST_DIR="$DIST_ROOT/$DIST_NAME"

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
# Build
# ---------------------------------------------------------------------------
if [[ $SKIP_BUILD -eq 0 ]]; then
    echo "==> Configuring (win64 preset)…"
    cmake --preset win64 -DSDL2_DIR="$SDL2_ROOT/lib/cmake/SDL2"

    echo "==> Building…"
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

if [[ ! -f "$BUILD_DIR/smemu6.exe" ]]; then
    echo "ERROR: $BUILD_DIR/smemu6.exe not found. Run without --skip-build first."
    exit 1
fi

# ---------------------------------------------------------------------------
# Assemble distribution directory
# ---------------------------------------------------------------------------
echo "==> Assembling $DIST_DIR …"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/roms"

# Main executable
cp "$BUILD_DIR/smemu6.exe" "$DIST_DIR/"

# SDL2 runtime DLL
if [[ ! -f "$SDL2_BIN/SDL2.dll" ]]; then
    echo "ERROR: SDL2.dll not found at $SDL2_BIN — check your SDL2 installation."
    exit 1
fi
cp "$SDL2_BIN/SDL2.dll" "$DIST_DIR/"

# ROM (required for boot)
cp "$SCRIPT_DIR/roms/samos_sys17.rom" "$DIST_DIR/roms/"

# Floppy disk image bundled with the release
FLOPPY_SRC="$SCRIPT_DIR/floppies/1 Systeme_1HComplet.dsk"
if [[ -f "$FLOPPY_SRC" ]]; then
    mkdir -p "$DIST_DIR/floppies"
    cp "$FLOPPY_SRC" "$DIST_DIR/floppies/"
else
    echo "WARNING: $FLOPPY_SRC not found — skipping floppy."
fi

# License and readme
cp "$SCRIPT_DIR/LICENSE"    "$DIST_DIR/"
cp "$SCRIPT_DIR/README.md"  "$DIST_DIR/"

# ---------------------------------------------------------------------------
# Zip
# ---------------------------------------------------------------------------
mkdir -p "$DIST_ROOT"
ZIP_FILE="$DIST_ROOT/${DIST_NAME}.zip"
echo "==> Creating $ZIP_FILE …"
(cd "$DIST_ROOT" && zip -r "${DIST_NAME}.zip" "$DIST_NAME")

echo ""
echo "Done: $ZIP_FILE"
echo "Contents:"
unzip -l "$ZIP_FILE" | tail -n +4 | head -n -2
