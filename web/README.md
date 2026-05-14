# Smemu6 — Web Build

Browser-playable build of the Smaky 6 emulator via Emscripten/WebAssembly.

## Prerequisites

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh   # add to ~/.bashrc or run before each session
```

## Building

From the project root (after sourcing `emsdk_env.sh`):

If your packaged `emcc` freezes the system cache under `/usr/share/emscripten/cache`
and the build fails with a `PermissionError` or `FROZEN_CACHE` complaint, create a
repo-local config and cache first:

```sh
EM_CONFIG="$PWD/.emscripten-local" emcc --generate-config "$PWD/.emscripten-local"

# Then edit .emscripten-local and add / change:
#   CACHE = '/absolute/path/to/your/repo/.emscripten-cache'
#   FROZEN_CACHE = False

mkdir -p .emscripten-cache
```

After that, run the web configure / build through the same local config:

```sh
# Configure (first time or after CMakeLists.txt changes)
EM_CONFIG="$PWD/.emscripten-local" cmake --preset web

# Build
EM_CONFIG="$PWD/.emscripten-local" cmake --build build-web

# Output files: build-web/smemu6.js  build-web/smemu6.wasm
# Plus:         build-web/smemu6.data  (preloaded ROMs and floppies)
# Copy web/index.html into build-web/ to serve the emulator:
cp web/index.html build-web/
```

Alternatively, without presets:

```sh
EM_CONFIG="$PWD/.emscripten-local" emcmake cmake -S . -B build-web -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
EM_CONFIG="$PWD/.emscripten-local" cmake --build build-web
```

## Serving locally

Browsers enforce `SharedArrayBuffer` policies and require a local HTTP server
(not `file://` URLs):

```sh
# Python 3 (simplest)
cd build-web
python3 -m http.server 8080
# Open http://localhost:8080/
```

Or use any static file server that sets:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

The repository also includes a helper script:

```sh
tools/serve_web.sh
```

It serves `build-web/` on `http://127.0.0.1:8080/` with the required COOP/COEP
headers.

## Using the emulator

1. Open `http://localhost:8080/` in a browser.
2. Wait for the ROMs and disk images to load (progress shown on screen).
3. The emulator starts automatically and boots from the preloaded floppy image
   (if `floppies/` was available at build time).
4. Use **Load DX0 / Load DX1** buttons to load a `.dsk` image from your
   local machine into the virtual filesystem, then click **Reset** to reboot.

## Controls

Same as the native build — see [docs/EMULATOR_GUIDE_EN.md](../docs/EMULATOR_GUIDE_EN.md).

| Host key                 | Smaky 6 function         |
| ------------------------ | ------------------------ |
| Pause / F11              | BREAK (NMI -> monitor)   |
| Shift+Pause / Shift+F11  | SHIFT+BREAK (hard reset) |
| Ctrl+Q                   | Quit (reloads page)      |

## Preloaded files

At build time, `--preload-file` bundles:

| Source path | Virtual FS path            |
| ----------- | -------------------------- |
| `roms/`     | `/roms/`                   |
| `floppies/` | `/floppies/` (if present)  |

## Notes

- The launcher dialog is automatically skipped in the web build (`-no-launcher`
  is passed via `Module.arguments`).  Use the HTML controls instead.
- Sound uses SDL2's push-mode (`SDL_QueueAudio`) — no `SharedArrayBuffer`
  required for audio alone.
- The file picker in the launcher code is replaced by a JS `<input type="file">`
  element defined in `web/index.html`; no native dialog thread is needed.
