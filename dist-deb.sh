#!/usr/bin/env bash
# dist-deb.sh — Build and package a Debian/Ubuntu .deb of smemu6
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2024-2026 Marcel Prisi
#
# Usage:
#   ./dist-deb.sh [--skip-build]
#
#   --skip-build   Skip CMake configure+build (reuse build/ output)
#
# Output:  dist/smemu6_<version>_<arch>.deb
#
# Prerequisites:
#   sudo apt install libsdl2-dev cmake make

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="$SCRIPT_DIR/build"
DIST_ROOT="$SCRIPT_DIR/dist"

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
# Run CPack to produce the .deb
# ---------------------------------------------------------------------------
echo "==> Packaging .deb…"
mkdir -p "$DIST_ROOT"

(cd "$BUILD_DIR" && cpack -G DEB --config CPackConfig.cmake \
    -B "$DIST_ROOT" 2>&1)

echo ""
echo "Done:"
ls -lh "$DIST_ROOT"/*.deb
