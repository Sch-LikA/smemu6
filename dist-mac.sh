#!/usr/bin/env bash
# dist-mac.sh — Build and package a macOS .dmg of smemu6
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2024-2026 Marcel Prisi
#
# Usage:
#   ./dist-mac.sh [--skip-build]
#
#   --skip-build   Skip CMake configure+build (reuse build/ output)
#
# Output:  dist/smemu6-macos-<arch>-<version>.dmg
#
# Prerequisites:
#   brew install cmake sdl2
#   Xcode command-line tools (xcode-select --install)
#   create-dmg (optional, for prettier .dmg):  brew install create-dmg

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ARCH="$(uname -m)"   # arm64 or x86_64
BUILD_DIR="$SCRIPT_DIR/build"
DIST_ROOT="$SCRIPT_DIR/dist"

VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo "dev")"
APP_NAME="smemu6"
APP_BUNDLE="$BUILD_DIR/${APP_NAME}.app"
DMG_NAME="${APP_NAME}-macos-${ARCH}-${VERSION}.dmg"

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
    cmake --preset default -DCMAKE_INSTALL_PREFIX=/

    echo "==> Building…"
    cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.logicalcpu 2>/dev/null || nproc)"
fi

if [[ ! -d "$APP_BUNDLE" ]]; then
    echo "ERROR: $APP_BUNDLE not found. Run without --skip-build first."
    exit 1
fi

# ---------------------------------------------------------------------------
# Install ROMs into the bundle (Contents/Resources/roms/)
# ---------------------------------------------------------------------------
echo "==> Installing ROMs into bundle…"
cmake --install "$BUILD_DIR" --prefix "$BUILD_DIR" --component smemu6

# ---------------------------------------------------------------------------
# Run macdeployqt replacement: macOS SDK's dylibbundler / install_name_tool
# We use macdeployqt-equivalent via dylibbundler to bundle SDL2 dylib.
# Prefer create-dmg if available, fall back to hdiutil.
# ---------------------------------------------------------------------------
echo "==> Bundling SDL2 dylib into ${APP_BUNDLE}…"
FRAMEWORKS="$APP_BUNDLE/Contents/Frameworks"
mkdir -p "$FRAMEWORKS"

# Find SDL2 dylib (Homebrew typical paths)
SDL2_DYLIB="$(find /opt/homebrew /usr/local -name 'libSDL2*.dylib' 2>/dev/null | grep -v devel | head -1 || true)"
if [[ -z "$SDL2_DYLIB" ]]; then
    echo "ERROR: libSDL2 dylib not found. Install it with: brew install sdl2"
    exit 1
fi
echo "    SDL2 → $SDL2_DYLIB"

# Copy SDL2 into the bundle
cp "$SDL2_DYLIB" "$FRAMEWORKS/"
DYLIB_BASENAME="$(basename "$SDL2_DYLIB")"

# Rewrite the embedded rpath in the executable to point inside the bundle
install_name_tool \
    -change "$SDL2_DYLIB" "@executable_path/../Frameworks/$DYLIB_BASENAME" \
    "$APP_BUNDLE/Contents/MacOS/$APP_NAME"

# Give the copied dylib a self-referential id
install_name_tool -id "@executable_path/../Frameworks/$DYLIB_BASENAME" \
    "$FRAMEWORKS/$DYLIB_BASENAME"

# ---------------------------------------------------------------------------
# Ad-hoc code signing
# ---------------------------------------------------------------------------
# A proper Apple-notarized signature requires a paid Apple Developer account.
# Ad-hoc signing (-) removes the "damaged app" Gatekeeper error for most
# users.  Without notarization, macOS will still warn on first launch; users
# can clear the quarantine flag with:  xattr -cr smemu6.app
echo "==> Ad-hoc signing bundle…"
codesign --force --deep --sign - "$APP_BUNDLE"

# ---------------------------------------------------------------------------
# Create .dmg
# ---------------------------------------------------------------------------
mkdir -p "$DIST_ROOT"
DMG_PATH="$DIST_ROOT/$DMG_NAME"

if command -v create-dmg &>/dev/null; then
    echo "==> Creating .dmg with create-dmg…"
    create-dmg \
        --volname "Smaky 6 Emulator" \
        --volicon "$SCRIPT_DIR/gfx/smaky6.icns" \
        --window-pos 200 120 \
        --window-size 600 400 \
        --icon-size 128 \
        --icon "${APP_NAME}.app" 160 160 \
        --hide-extension "${APP_NAME}.app" \
        --app-drop-link 440 160 \
        "$DMG_PATH" \
        "$APP_BUNDLE"
else
    echo "==> create-dmg not found; creating plain .dmg with hdiutil…"
    STAGING="$(mktemp -d)"
    cp -R "$APP_BUNDLE" "$STAGING/"
    hdiutil create -volname "Smaky 6 Emulator" \
        -srcfolder "$STAGING" \
        -ov -format UDZO \
        "$DMG_PATH"
    rm -rf "$STAGING"
fi

echo ""
echo "Done: $DMG_PATH"
ls -lh "$DMG_PATH"
