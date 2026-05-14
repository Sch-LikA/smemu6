#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/build-web"
PORT="${1:-8080}"

if [ ! -d "$BUILD_DIR" ]; then
    printf 'serve_web.sh: missing build directory: %s\n' "$BUILD_DIR" >&2
    printf 'Build the web preset first, for example:\n' >&2
    printf '  cd %s && EM_CONFIG="$PWD/.emscripten-local" cmake --preset web && EM_CONFIG="$PWD/.emscripten-local" cmake --build build-web\n' "$ROOT_DIR" >&2
    exit 1
fi

for required_file in \
    index.html \
    smemu6.js \
    favicon.ico \
    smaky6-128.png \
    smaky6-256.png \
    smaky6-logo-header.png
do
    if [ ! -f "$BUILD_DIR/$required_file" ]; then
        printf 'serve_web.sh: missing required web asset: %s\n' "$BUILD_DIR/$required_file" >&2
        printf 'Rebuild the web target so CMake can stage the shell assets into build-web/.\n' >&2
        printf '  cd %s && EM_CONFIG="$PWD/.emscripten-local" cmake --build build-web\n' "$ROOT_DIR" >&2
        exit 1
    fi
done

cd "$BUILD_DIR"
printf 'Serving %s at http://127.0.0.1:%s/\n' "$BUILD_DIR" "$PORT"
python3 - "$PORT" <<'PY'
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
import sys

port = int(sys.argv[1])

class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()
PY