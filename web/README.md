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
2. Wait for the launcher to finish loading the bundled floppy library.
3. Pick a built-in disk from the DX0 / DX1 dropdowns, or use **Use own…** to
  stage your own `.dsk` image before boot.
4. Click **Start** to launch the emulator with the selected disks.
5. After the emulator has started, use **Insert…** on the front panel to load a
  new `.dsk` image into the virtual filesystem, then click **Reset** to reboot.
6. On phones and tablets, tap **Keyboard** on the front panel to ask the browser
  to open the device's on-screen keyboard.

## Controls

Same as the native build — see [docs/EMULATOR_GUIDE_EN.md](../docs/EMULATOR_GUIDE_EN.md).

| Host key                 | Smaky 6 function         |
| ------------------------ | ------------------------ |
| Pause / F11              | BREAK (NMI -> monitor)   |
| Shift+Pause / Shift+F11  | SHIFT+BREAK (hard reset) |
| Ctrl+Q                   | Quit (reloads page)      |

## Preloaded files

At build time, `--preload-file` bundles:

- `roms/` -> `/roms/`
- `floppies/` -> `/floppies/` (bundled library)

## Notes

- The web shell is now responsive: the launcher, canvas, and front-panel
  controls scale down to narrow screens, the canvas preserves the full
  `512 × 508` machine window instead of cropping the lower bars, and touch
  targets are enlarged for phones and tablets.
- On touch devices, the running machine view now exposes a dedicated
  **Keyboard** button that focuses a hidden text field so mobile browsers can
  show the system on-screen keyboard.
- The launcher preloads the bundled floppy library from `/floppies/` and lets
  you choose DX0 / DX1 boot disks before starting the emulator.
- The launcher dialog is automatically skipped in the web build (`-no-launcher`
  is passed via `Module.arguments`).  Use the HTML controls instead.
- Sound uses SDL2's push-mode (`SDL_QueueAudio`) — no `SharedArrayBuffer`
  required for audio alone.
- The file picker in the launcher code is replaced by a JS `<input type="file">`
  element defined in `web/index.html`; no native dialog thread is needed.
