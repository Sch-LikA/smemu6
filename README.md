# Smemu6 — Smart Emulator of the Smart Keyboard

*The Smart Machine deserves a smart emulator.*

Emulator for the **Smaky 6**, a 1978 Swiss Z80-based personal computer
developed at EPFL (Lausanne) by Jean-Daniel Nicoud and commercialized by
Epsitec (~450 units, 1979–1983).

The machine is a Z80 @ 2.5 MHz with a 512×240 green-phosphor display, a
Micropolis hard-sectored 5.25" floppy drive, and an optional WD1000/WD1001/WD1002-compatible
Winchester hard disk.  The Phantom ROM (2 KB) bootstraps the SAMOS operating
system from floppy.

## A machine worth remembering

The Smaky 6 was a genuinely groundbreaking design for its era: a fully integrated
personal workstation with a custom operating system, a coherent human-interface
philosophy, and hardware capabilities that compared favourably with systems costing
far more.  Jean-Daniel Nicoud's team at EPFL created it years before "personal
computing" became mainstream in Europe, and the ~450 units that were built had an
outsized influence on Swiss computer science education and research.

Today very few working Smaky 6 machines survive.  **smemu6** is a work of love — an
attempt to preserve and document this little-known but important piece of computing
history so that the machine's software, design decisions, and spirit remain
accessible long after the last physical unit stops working.  If you care about
vintage computing, digital preservation, or simply great engineering from an
unlikely corner of the world, this project is for you.

Hardware reference: [docs/dev/HARDWARE.md](docs/dev/HARDWARE.md)

**Try it in your browser:** [sch-lika.github.io/smemu6](https://sch-lika.github.io/smemu6/)

---

## Table of Contents

1. [Building](#1-building)
2. [Required Files](#2-required-files)
3. [Quick Start](#3-quick-start)
4. [Command-Line Options](#4-command-line-options)
    - [Storage](#41-storage)
    - [Boot Control](#42-boot-control)
    - [Display](#43-display)
    - [Audio](#44-audio)
    - [Timeouts and Scripting](#45-timeouts-and-scripting)
    - [Debug and Tracing](#46-debug-and-tracing)
    - [Miscellaneous](#47-miscellaneous)
5. [Keyboard Controls](#5-keyboard-controls)
6. [Runtime Signals](#6-runtime-signals)
7. [Boot Sequence](#7-boot-sequence)
8. [Disk Image Formats](#8-disk-image-formats)
9. [ROM Extraction](#9-rom-extraction)
10. [Documentation](#10-documentation)

---

## 1. Building

**Dependencies:** CMake ≥ 3.16, SDL2 development libraries, git (used by
CMake FetchContent to pull the Z80/Zeta CPU library).

```bash
# Ubuntu / Debian
sudo apt install cmake libsdl2-dev git

# macOS (Homebrew)
brew install cmake sdl2
```

```bash
git clone https://github.com/Sch-LikA/smemu6
cd smemu6
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The binary is `build/smemu6`.  Run all commands from the repository root
or from inside `build/` (paths below use `build/` as the working directory).

### Web / Emscripten build

A browser-playable WebAssembly build is supported via Emscripten.
See [web/README.md](web/README.md) for full instructions.  Quick summary:

```bash
# Install and activate emsdk (one-time)
source /path/to/emsdk/emsdk_env.sh

# Configure and build (after creating .emscripten-local as described in web/README.md)
EM_CONFIG="$PWD/.emscripten-local" cmake --preset web
EM_CONFIG="$PWD/.emscripten-local" cmake --build build-web

# Serve locally with the required COOP/COEP headers
tools/serve_web.sh
# Open http://127.0.0.1:8080/
```

### Experimental SDCC scaffold

An experimental standalone SDCC proof-of-execution scaffold lives under
`examples/sdcc/hello_alpha`. It is intentionally outside CMake and CI for now.

- expected tools: `sdcc`, `sdasz80`, `python3`
- current target shape: ordinary `.SM`, `flags=1`, `load=entry=0x6000`
- current runtime scope: confirmed direct alpha-RAM output, no libc startup,
    and a verified return-to-CLI path for returning `main`
- the example now also carries a tiny reusable target header,
  `examples/sdcc/hello_alpha/smaky6.h`, for first-target alpha-RAM output
    with a simple row/column helper over the 64-column alpha screen
    plus a minimal row-clear helper for claiming dedicated screen space

```bash
tools/build_smaky6_sdcc_example.sh
tools/run_smaky6_sdcc_example.sh build/smemu6

# Or target another one-file example directory under examples/sdcc/
tools/build_smaky6_sdcc_example.sh hello_alpha
tools/run_smaky6_sdcc_example.sh build/smemu6 hello_alpha
```

For reverse-engineering work on the archived symbol-table files, a small
standalone C dumper now lives at `tools/dump_smaky6_st_symbols.c`.
The shared parser API is in `tools/smaky6_st_symbols.h` /
`tools/smaky6_st_symbols.c`, and `tools/export_smaky6_st_symbols.c` can emit a
JSON dump or a generated C header from `FLO.ST` / `SM6.ST`.

---

## 2. Required Files

- `roms/samos_sys17.rom`: 2 KB, required. Phantom bootstrap ROM (TMS2716 `SYS17`).
- `roms/chargen.rom`: 2 KB, optional. Character generator PROM; a synthetic fallback is built in.
- `floppies/*.dsk`: optional. Floppy disk images; see §8.
- `harddisks/SM6WIN0.DSK`: optional 16 MB Winchester drive 0 image.
- `harddisks/SM6WIN1.DSK`: optional 16 MB Winchester drive 1 image.

Without `samos_sys17.rom` the CPU will execute random bytes and the emulator
will report a warning but continue.

---

## 3. Quick Start

```bash
cd build

# Boot SAMOS from floppy (machine boots DX0 automatically)
./smemu6 -floppy "../floppies/Sys1-H.dsk"

# Boot SAMOS from a metadata-preserving DX0 hostdir export
python3 ../tools/extract_samos_image.py ../floppies/Sys2-2.dsk ../tmp/Sys2-2-hostdir
./smemu6 -floppy-hostdir ../tmp/Sys2-2-hostdir

# Boot from Winchester (DX0) with floppy accessible as DX1
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK \
            -floppy2 "../floppies/Sys1-H.dsk"

# Headless run with SDL dummy drivers (e.g. in CI)
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy \
    ./smemu6 -floppy "../floppies/Sys1-H.dsk" \
                -inject-str "LIST\n" -timeout 20

# Scale the window up 3× and add CRT scanline effect
./smemu6 -floppy "../floppies/Sys1-H.dsk" -scale 3 -scanlines
```

---

## 4. Command-Line Options

### 4.1 Storage

#### `-floppy <path>`

Mount a floppy disk image on **DX0:** (the primary floppy drive).  Floppy
controller writes are supported. File-backed floppy images are writable in
place.

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk
```

#### `-floppy-hostdir <path>`

Mount a host-directory-backed floppy on **DX0:** using an in-memory overlay.
For bootable DX0 media, the host tree must preserve SAMOS metadata and sector
order; `tools/extract_samos_image.py` can export a suitable tree from a known
good `.dsk` image such as `floppies/Sys2-2.dsk`.

```bash
python3 tools/extract_samos_image.py floppies/Sys2-2.dsk tmp/Sys2-2-hostdir
./smemu6 -floppy-hostdir tmp/Sys2-2-hostdir
```

#### `-floppy2 <path>`

Mount a floppy disk image on **DX1:** (the secondary floppy drive).

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -floppy2 ../floppies/Burotic.dsk
```

#### `-harddisk <path>`

Mount a flat binary hard-disk image as **Winchester drive 0** (SM6WIN0).
The WD1000/WD1001/WD1002-compatible controller is emulated at ports `0x20–0x27`.
Geometry: 6 heads, 32 sectors/track, 256 bytes/sector, up to 255 cylinders.

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 ../floppies/Sys1-H.dsk
```

> **Note:** On a Winchester-equipped Smaky 6 the hard disk is DX0 and the
> floppy (if fitted) is DX1.  Use `-harddisk` with `-floppy2`, not `-floppy`.

#### `-harddisk2 <path>`

Mount a flat binary hard-disk image as **Winchester drive 1** (SM6WIN1).

> **Note:** The WD1000/WD1001/WD1002 hardware supports two drives (SDH register bit 3), and the emulator
> correctly images both.  However, standard SAMOS does **not** expose a separate CLI device
> name for drive 1 — the OS has only a single Winchester dispatch path (RST 20 → `0x0339`)
> governed by a binary floppy/Winchester flag at RAM address `0x4502`.  Drive 1 is accessible
> only if the SAMOS Winchester driver explicitly sets SDH bit 3, which is not observed in the
> standard `SYS.SY` 1-H image.  Mount it for archival or custom-software use, but do not
> expect it to appear as a second named device in the SAMOS CLI.

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -harddisk ../harddisks/SM6WIN0.DSK -harddisk2 ../harddisks/SM6WIN1.DSK
```

---

### 4.2 Boot Control

#### `-break-to-monitor`

Inject a **SHIFT+BREAK** combination shortly after startup to enter the SAMOS
monitor instead of booting from floppy.  Useful for ROM-level debugging.

#### `-inject-str <string>`

Inject a sequence of key codes once the SAMOS **CLI prompt** (`>`) is detected
on screen.  The string is converted to uppercase Smaky key codes:

- `\n`: Enter (CR, `0x0D`).
- `\f`: Wait for the next CLI prompt before injecting the following bytes.
- `a`–`z`: Uppercased to `A`–`Z`.
- `A`–`Z`, `0`–`9`, `space`: passed as-is.
- Other characters: skipped.

```bash
# Run the LIST command automatically after boot
./smemu6 -floppy ../floppies/Sys1-H.dsk -inject-str "LIST\n"

# Run one command, wait for the next prompt, then run another
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 ../floppies/Sys1-H.dsk \
    -inject-str "LIST DX1:\n\fLIST DX1:\n"
```

#### `-inject-delay <frames>`

Wait this many 50 Hz frames after the CLI prompt becomes visible before
triggering `-inject-str` or `-inject-keycode`. Useful when the OS reaches the
prompt before the screen content has fully stabilized.

#### `-inject-at-prompt <n>`

Fire the configured injection on the `n`th visible CLI prompt transition
instead of the first one. Default: **1**.

#### `-inject-at-frame <n>`

Fire `-inject-str` or `-inject-keycode` at absolute frame `n` instead of
waiting for a CLI prompt. Useful for low-level boot or timing probes.

---

### 4.3 Display

#### `-scale <n>`

Integer pixel-doubling factor for the SDL window.  Range: 1–8.  Default: **1**.

The logical resolution is 512 × 508 (512 wide; 480 px machine area with 2:1
vertical stretch + 14 px disk-activity bar + 14 px function-key bar).  The
physical window is `n × 512` by `n × 508`.

| Scale | Window size | Typical use          |
|-------|-------------|----------------------|
| 1     | 512 × 508   | Default / CI         |
| 2     | 1024 × 1016 | Comfortable on 1080p |
| 3     | 1536 × 1524 | HiDPI / 1440p        |
| 4     | 2048 × 2032 | 4K screens           |

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -scale 2
```

#### `-scanlines`

Draw a CRT-style scanline overlay: every other output row is darkened,
simulating the dark gaps between phosphor scan lines on a real monitor.

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -scale 2 -scanlines
```

#### `-vmode <mode>`

Force the display to start in a specific video mode, overriding what the ROM
writes to port `0x00`.  One of:

- `alpha`: text only, alpha character plane.
- `graphic`: bitmap only, 60-row × 512-px graphic plane.
- `super`: superimposed alpha + graphic.

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -vmode alpha
```

#### `-gfxbits <order>`

Set the bit order for the graphic (bitmap) plane.  One of:

- `msb`: bit 7 = leftmost pixel. Default and standard Smaky 6 hardware setting.
- `lsb`: bit 0 = leftmost pixel. Use for compatibility checks on unusual images.

#### `-no-display-off`

Suppress display-blank writes.  Normally, writing `0x00` to port `0x00`
(bit 0 = 0) blanks the screen.  With this flag those writes are ignored and
the screen stays visible at all times.  Useful when software briefly blanks
the display during a mode switch and you want to keep the window live.

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -no-display-off
```

#### `-verbose-video`

Log display on/off and video mode changes to `stderr`.  Off by default.
Useful when debugging boot sequences or investigating unexpected screen
blanking.

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -verbose-video
```

---

### 4.4 Audio

#### `-no-beeper`

Disable the emulated buzzer.  By default the beeper is on and generates
sample-accurate square-wave tones through SDL audio.  Use this flag when
running headless or when audio is unavailable.

```bash
SDL_AUDIODRIVER=dummy ./smemu6 -floppy ../floppies/Sys1-H.dsk -no-beeper
```

#### `-drive-sound`

Enable floppy drive sound effects: motor whir, head-step clicks, and
sector-hole ticks.  Off by default.

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -drive-sound
```

---

### 4.5 Timeouts and Scripting

#### `-timeout <seconds>`

Kill the emulator after this many wall-clock seconds.  `0` = run forever.

Default policy (when this option is omitted):

- With `-trace`: 45 seconds.
- Without `-trace`: run forever.

Useful in CI pipelines combined with `-inject-str`.

```bash
./smemu6 -floppy ../floppies/Sys1-H.dsk -inject-str "LIST\n" -timeout 30
```

For readable CLI/screen captures, pair `-scrdump` with `-no-display-off`.
SAMOS often blanks the display between updates, so `-scrdump` alone can miss
the visible prompt.

---

### 4.6 Debug and Tracing

All trace output goes to **stderr**.

#### `-trace`

Log Z80 PC milestones to stderr as the machine boots: ROM entry, MOVROM
(Phantom ROM bank-switch), SAMOS handoff, CLI prompt detection.  Also sets
the default timeout to 45 s (see `-timeout`).

#### `-traceflow`

Log a dense stream of Z80 PC values during the focused post-handoff control
flow (low-RAM range).  Very verbose — use only when chasing a specific boot
hang.

#### `-trace08`

Log every IN/OUT access on **port 0x08** (E405/08 RTC serial interface):
clock edges, data bits, and decoded register values.

#### `-trace11`

Log every read of **port 0x11** (unknown device; always returns `0x00`).

#### `-tracecd`

Log every read of **port 0xCD** (mapped as `0x0D` after 6-bit mask; Winchester
DMA / status register; always returns `0x00`).

#### `-trace19`

Log every write to **port 0x19** (floppy motor-on / NMI-arm / drive-select
control register).  Shows the raw value and the decoded drive-select and
motor/NMI bits.

#### `-tracefdc`

Log focused floppy events: sector-ID bytes, checksum mismatches, and INIR
data-stream boundaries.  Less noisy than `-trace19`; useful for diagnosing
sector-read errors.

#### `-tracekbd`

Log every keyboard CLA read (port `0x00` IN) and status read (port `0x01` IN),
including the key code returned and the FOUND flip-flop state.
For a focused Linux/X11 regression check of overlapping printable typing, run
`tools/check_keyboard_asd_trace.sh`; it injects overlapping `a s d` keydown/keyup
events and verifies that the CLI receives all three visible insertions in order.

#### `-tracesnd`

Log every write to **port 0x03** (buzzer bit-bang).  Each line shows the
T-state timestamp and the new bit value, allowing exact frequency measurement.

#### `-trace-win`

Log every Winchester hard-disk controller command (RESTORE, SEEK, READ, WRITE) to
stderr.  Each line shows the drive number, cylinder, head, sector and LBA,
allowing diagnosis of disk access patterns and CHS mapping issues.

#### `-scrdump`

After each frame, dump any changed alpha-plane rows to stderr as ASCII text.
Useful for capturing screen output in headless / CI runs without a screen.

#### `-dump-ram <path>`

At emulator exit, write the full 64 KB address space to a binary file.
Also triggered at any time by **Ctrl+D** in the terminal or the `SIGUSR1`
signal (see §6), which writes a timestamped dump without stopping the emulator.

---

### 4.7 Miscellaneous

#### `-help`

Print the short option summary to stderr and exit.

---

## 5. Keyboard Controls

The host keyboard maps to audited Smaky 6 matrix positions.  Ordinary keys are
resolved through the S471 table in `src/keyboard.c`, then exposed through the
strict CLA / `SYS.SY` path.  This is now a host-scancode-position model, not a
host text-input passthrough.

- `A`..`Z`, `0`..`9`, `Space`: resolved by host position through the audited S471 table.
- `Backspace`: Smaky BS (`0x08`).
- `Tab`: Smaky TAB (`0x09`).
- `Enter / Return`: Smaky CR (`0x0D`).
- `F1`..`F7`: Smaky function-key bits CURSOR / COPY / KILL / PROGRA / SHOW / SEARCH / CHANGE.
- `F9`: DEFINE (`0x1F`) via the current strict matrix map.
- `F11` / `Pause`: BREAK, top-right key; fires NMI and drops into SAMOS monitor.
- `Shift+F11` / `Shift+Pause`: SHIFT+BREAK, hard reset and reboot from DX0:.
- `Escape`: ESC / UNDO, top-left key, current working code `0x06`.
- `End`: current audited ordinary-key position 30 (`0x04` normal, `0x05` shifted).
- `Ctrl+D` in the terminal: dump 64 KB RAM to file, same as `SIGUSR1`.

Older convenience aliases such as `F8`, `Insert`, `Home`, `Left Alt`, `Left Ctrl`,
`Left Windows / Super`, `AltGr`, and `Delete` should no longer be treated as
current documented bindings unless they are reintroduced in code.

Hardware note: the full S471 keyboard ROM dump is now available.  The emulator
matches the confirmed special-key outputs it exposes directly, including
`Escape -> 0x06`, `Backspace -> 0x08`, `Tab -> 0x09`, `Return -> 0x0D`, and
`Space -> 0x20`.  Ordinary printable keys now follow the current audited host
scancode position map into the S471 normal / Shift / Caps layers.  This is a
strict CLA-centric baseline, but it does not yet expose every original Smaky
physical position or a separate accented-text compatibility path.

Runtime note: overlapping printable key presses are queued and promoted one by
one, so fast typing now reaches the CLI through the visible input path instead
of stopping after the first printable key.

Function-key note: `F1`..`F7` are program keys, not CLI text-entry keys. They do
not echo characters at the prompt; they are consumed by software that reads the
Smaky function-key state.

**Function-key status bar:** The bottom strip of the emulator window shows
7 clickable buttons — one per Smaky function key (CURSOR, COPY, KILL,
PROGRA, SHOW, SEARCH, CHANGE).  Left-click and hold to activate a function
key; release to deactivate.  **Right-click** toggles a persistent **latch**:
the button stays active (rendered in yellow) until right-clicked again,
enabling single-handed modifier+key combinations.

**Disk / RESET status bar:** The middle strip shows floppy and Winchester
drive activity LEDs.  At the right end are two buttons:

- **BREAK** — left-click fires an NMI (same as `F11` / `Pause`)
- **RESET** — requires **two clicks**: first click arms the button (it blinks
  orange for 3 seconds); second click confirms the hard reset.  Clicking
  anywhere else cancels the armed state.

---

## 6. Runtime Signals

- `SIGINT`: clean shutdown, same as closing the window.
- `SIGTERM`: clean shutdown.
- `SIGUSR1`: dump 64 KB RAM to `smaky6_ram_NNNN_pcXXXX.bin` without stopping.

The emulator prints its PID at startup:

```text
[main] PID 12345 — send SIGUSR1 to dump RAM
```

Example:

```bash
kill -USR1 12345
```

---

## 7. Boot Sequence

```text
Power-on / RESET
    │
    ▼
Phantom ROM (roms/samos_sys17.rom, 2 KB at 0x0000–0x07FF)
    │
    ├─ Display "ROM de chargement rev 1-7"
    ├─ Wait for boot key:
    │     Enter / Space        → boot from DX0: (floppy)
    │     SHIFT+BREAK          → boot from DX0: (hard reset path)
    │     BREAK                → PDP-11 paper-tape loader (USART)
    │     FUNCTION+BREAK       → RAM test
    │
    ▼
Phantom ROM loads SYS.SY from DX0: via NMI-driven floppy streaming
    │   (sectors read through port 0x1B, NMI on sector holes at 80 Hz)
    │
    ▼
OUT (01h), A=00h  →  MOVROM: Phantom ROM disabled, RAM at 0x0000
LDIR: copy SYSMON from SYS.SY to 0x0000–0x07FF
    │
    ▼
JP 0x0105  →  SAMOS OS init
    │
    ▼
Load CLI.SY  →  "SAMOS rev 2-8 / DX0: / >" prompt
```

**Automatic boot:** The FOUND latch on the keyboard controller powers up
asserted on real hardware.  The emulator replicates this: the Phantom ROM
boot menu receives an Enter key on the very first CLA read and immediately
proceeds to boot from DX0.  No flag is needed for a normal floppy or
Winchester boot.

**With `-harddisk`:** Mount the Winchester image as DX0 and the floppy
(if any) as DX1 using `-floppy2`.  The Phantom ROM boots from DX0 (the
Winchester) automatically.

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 ../floppies/Sys1-H.dsk
```

---

## 8. Disk Image Formats

### Floppy images (`.dsk`)

Raw flat binary: track 0 sector 0 first, 256 bytes per sector, 16 sectors per
track.

- `163,840 bytes`: 40 tracks × 16 × 256, standard single-sided 5.25".
- `315,392 bytes`: 77 tracks × 16 × 256, extended 77-track drives.

Track count is **auto-detected** from image size.

### Hard-disk images (`.DSK`)

Raw flat binary indexed by LBA.  Sector size: 256 bytes.

```text
LBA = cylinder × 192 + head × 32 + sector_within_track
```

The supplied `harddisks/SM6WIN0.DSK` and `SM6WIN1.DSK` images are 16 MB each.
Only the first ~1054 sectors (~270 KB) contain non-zero data in the known
disk images.

---

## 9. ROM Extraction

If you have the original PDF documentation with hex listings:

```bash
pdftotext -layout Smaky6-doc.pdf /tmp/smaky6.txt

# Extract Phantom ROM (SYS17, 2 KB)
python3 ../smaky6-tools/smaky6_rom_extract.py /tmp/smaky6.txt \
    --start 0 --end 7777 \
    --output roms/samos_sys17.rom \
    --report sysmon_report.txt
```

The character generator ROM (`roms/chargen.rom`) is a standard 2716 EPROM
image (2048 bytes, 16 bytes per glyph, 128 glyphs).  If the file is missing,
the emulator falls back to a built-in synthetic table that covers the
printable ASCII range.

---

## 10. Documentation

Additional guides are available in the [`docs/`](docs/) folder:

- [Emulator Guide](docs/EMULATOR_GUIDE_EN.md): English, full reference for the emulator CLI, keyboard, and disk formats.
- [Guide de l'émulateur](docs/EMULATOR_GUIDE_FR.md): French reference guide.
- [Smaky 6 User Guide](docs/SMAKY6_USER_GUIDE_EN.md): English version of the original Smaky 6 user manual.
- [Guide utilisateur Smaky 6](docs/SMAKY6_USER_GUIDE_FR.md): French version of the original Smaky 6 user manual.
- [Smaky 6 Funny Guide](docs/SMAKY6_FUNNY_GUIDE_EN.md): English light-hearted introduction.
- [Guide amusant Smaky 6](docs/SMAKY6_FUNNY_GUIDE_FR.md): French light-hearted introduction.
- [Smaky 6 for Kids](docs/SMAKY6_KIDS_EN.md): English simplified guide for younger users.
- [Smaky 6 pour les enfants](docs/SMAKY6_KIDS_FR.md): French simplified guide for younger users.

---

## License

Emulator source code copyright © 2024–2026 Marcel Prisi —
released under the **GNU General Public License v3** (see [LICENSE](LICENSE)).

### Acknowledgements

The Smaky 6 was designed by **Jean-Daniel Nicoud** and his team at
[EPFL](https://www.epfl.ch) (Lausanne, Switzerland) and commercialized by
**Epsitec SA**.  The ROM content has been made freely available by Epsitec SA for anyone to use.

### Third-party libraries

- [redcode/Z80](https://github.com/redcode/Z80): LGPL v3.
- [redcode/Zeta](https://github.com/redcode/Zeta): LGPL v3.
- [SDL2](https://www.libsdl.org): zlib.
- [tinyfiledialogs](https://sourceforge.net/projects/tinyfiledialogs/): zlib.
