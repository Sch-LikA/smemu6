# Smaky 6 Emulator

Emulator for the **Smaky 6**, a 1978 Swiss Z80-based personal computer
developed at EPFL (Lausanne) by Jean-Daniel Nicoud and commercialized by
Epsitec (~450 units, 1979–1983).

The machine is a Z80 @ 2.5 MHz with a 512×240 green-phosphor display, a
Micropolis hard-sectored 5.25" floppy drive, and an optional WD1010-compatible
Winchester hard disk.  The Phantom ROM (2 KB) bootstraps the SAMOS operating
system from floppy.

Hardware reference: [docs/dev/HARDWARE.md](docs/dev/HARDWARE.md)

---

## Table of Contents

1. [Building](#1-building)
2. [Required Files](#2-required-files)
3. [Quick Start](#3-quick-start)
4. [Command-Line Options](#4-command-line-options)
   - [Storage](#41-storage)
   - [Boot Automation](#42-boot-automation)
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
git clone https://github.com/your-username/smaky6emu
cd smaky6emu
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The binary is `build/smaky6emu`.  Run all commands from the repository root
or from inside `build/` (paths below use `build/` as the working directory).

---

## 2. Required Files

| Path                      | Size  | Required | Description |
|---------------------------|-------|----------|-------------|
| `roms/samos_sys17.rom`    | 2 KB  | **Yes**  | Phantom bootstrap ROM (TMS2716 "SYS17") |
| `roms/chargen.rom`        | 2 KB  | No       | Character generator PROM; a synthetic fallback is built in |
| `floppies/*.dsk`          | varies| No       | Floppy disk images (see §8) |
| `harddisks/SM6WIN0.DSK`   | 16 MB | No       | Winchester drive 0 image |
| `harddisks/SM6WIN1.DSK`   | 16 MB | No       | Winchester drive 1 image |

Without `samos_sys17.rom` the CPU will execute random bytes and the emulator
will report a warning but continue.

---

## 3. Quick Start

```bash
cd build

# Boot SAMOS from floppy, auto-select floppy boot after 3 s
./smaky6emu -disk "../floppies/1 Systeme_1HComplet.dsk" -autoboot

# Boot with a Winchester hard disk attached
./smaky6emu -disk "../floppies/1 Systeme_1HComplet.dsk" \
            -harddisk ../harddisks/SM6WIN0.DSK

# Headless run with SDL dummy drivers (e.g. in CI)
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy \
    ./smaky6emu -disk "../floppies/1 Systeme_1HComplet.dsk" \
                -autoboot -inject-str "LIST\n" -timeout 20

# Scale the window up 3× (useful on HiDPI screens)
./smaky6emu -disk "../floppies/1 Systeme_1HComplet.dsk" -scale 3
```

---

## 4. Command-Line Options

### 4.1 Storage

#### `-disk <path>`
Mount a floppy disk image on **DX0:** (the primary floppy drive).  The image
is opened read-only; no writes are flushed back to the file.

```bash
./smaky6emu -disk ../floppies/sys.dsk
```

#### `-disk2 <path>`
Mount a floppy disk image on **DX1:** (the secondary floppy drive).

```bash
./smaky6emu -disk sys.dsk -disk2 data.dsk
```

#### `-harddisk <path>`
Mount a flat binary hard-disk image as **Winchester drive 0** (SM6WIN0).
The WD1010-compatible controller is emulated at ports `0x20–0x27`.
Geometry: 6 heads, 32 sectors/track, 256 bytes/sector, up to 255 cylinders.

```bash
./smaky6emu -disk sys.dsk -harddisk ../harddisks/SM6WIN0.DSK
```

#### `-harddisk2 <path>`
Mount a flat binary hard-disk image as **Winchester drive 1** (SM6WIN1).

```bash
./smaky6emu -disk sys.dsk -harddisk SM6WIN0.DSK -harddisk2 SM6WIN1.DSK
```

---

### 4.2 Boot Automation

#### `-autoboot`
After approximately 3 seconds (150 frames at 50 Hz), inject an **Enter** key
(`0x00`) to automatically select the floppy-boot option from the Phantom ROM
boot menu.  Combine with `-autoboot2` to choose a different boot target, or
with `-inject-str` to type a command once SAMOS is running.

```bash
./smaky6emu -disk sys.dsk -autoboot
```

#### `-autoboot2 <code>`
Override the key code injected by `-autoboot` for the **second stage** of the
boot selection.  Accepts decimal or `0xHH` hex.  Default: `0x20` (Space).

The Phantom ROM boot menu accepts:
- `0x00` / Enter — boot from DX0:
- `0x20` / Space — boot from DX0: (same as Enter in most ROM versions)

```bash
./smaky6emu -disk sys.dsk -autoboot -autoboot2 0x00
```

#### `-autoboot3 <code>`
Inject an optional **third stage** key code after the second stage.  Disabled
by default.  Use when a three-step boot sequence is required.

#### `-break-to-monitor`
Inject a **SHIFT+BREAK** combination shortly after startup to enter the SAMOS
monitor instead of booting from floppy.  Useful for ROM-level debugging.

#### `-inject-str <string>`
Inject a sequence of key codes once the SAMOS **CLI prompt** (`>`) is detected
on screen.  The string is converted to uppercase Smaky key codes:

| Escape sequence | Result              |
|-----------------|---------------------|
| `\n`            | Enter (CR, `0x0D`)  |
| `a`–`z`         | Uppercased to `A`–`Z` |
| `A`–`Z`, `0`–`9`, space | Passed as-is |
| Other characters | Skipped            |

```bash
# Run the LIST command automatically after boot
./smaky6emu -disk sys.dsk -autoboot -inject-str "LIST\n"
```

#### `-inject-via-fifo`
Route `-inject-str` bytes through the **keyboard FIFO** (same path as physical
SDL key events) instead of writing directly to the SAMOS circular buffer.
Slower but exercises the full keyboard emulation path; useful for testing.

#### `-inject-delay <frames>`
Accepted for backward compatibility; no longer has any effect.

---

### 4.3 Display

#### `-scale <n>`
Integer pixel-doubling factor for the SDL window.  Range: 1–8.  Default: **2**.

The logical resolution is always 512×252 (512×240 machine pixels + 12 px status
bar); the physical window is `n × 512` by `n × 252`.

| Scale | Window size   | Typical use          |
|-------|---------------|----------------------|
| 1     | 512 × 252     | Compact / CI         |
| 2     | 1024 × 504    | Default (1080p screens) |
| 3     | 1536 × 756    | HiDPI / 1440p        |
| 4     | 2048 × 1008   | 4K screens           |

```bash
./smaky6emu -disk sys.dsk -scale 3
```

#### `-vmode <mode>`
Force the display to start in a specific video mode, overriding what the ROM
writes to port `0x00`.  One of:

| Value     | Mode                                      |
|-----------|-------------------------------------------|
| `alpha`   | Text only (alpha character plane)         |
| `graphic` | Bitmap only (60-row × 512-px graphic plane) |
| `super`   | Superimposed (alpha + graphic)            |

```bash
./smaky6emu -disk sys.dsk -vmode alpha
```

#### `-gfxbits <order>`
Set the bit order for the graphic (bitmap) plane.  One of:

| Value | Meaning                         | When to use                            |
|-------|---------------------------------|----------------------------------------|
| `lsb` | Bit 0 = leftmost pixel (default) | Standard Smaky 6 hardware              |
| `msb` | Bit 7 = leftmost pixel          | Some third-party disk images           |

#### `-no-display-off`
Suppress display-blank writes.  Normally, writing `0x00` to port `0x00`
(bit 0 = 0) blanks the screen.  With this flag those writes are ignored and
the screen stays visible at all times.  Useful when software briefly blanks
the display during a mode switch and you want to keep the window live.

```bash
./smaky6emu -disk sys.dsk -no-display-off
```

---

### 4.4 Audio

#### `-no-beeper`
Disable the emulated buzzer.  By default the beeper is on and generates
sample-accurate square-wave tones through SDL audio.  Use this flag when
running headless or when audio is unavailable.

```bash
SDL_AUDIODRIVER=dummy ./smaky6emu -disk sys.dsk -no-beeper
```

#### `-drive-sound`
Enable floppy drive sound effects: motor whir, head-step clicks, and
sector-hole ticks.  Off by default.

```bash
./smaky6emu -disk sys.dsk -drive-sound
```

---

### 4.5 Timeouts and Scripting

#### `-timeout <seconds>`
Kill the emulator after this many wall-clock seconds.  `0` = run forever.

Default policy (when this option is omitted):
- With `-trace`: 45 seconds.
- Without `-trace`: run forever.

Useful in CI pipelines combined with `-autoboot` and `-inject-str`.

```bash
./smaky6emu -disk sys.dsk -autoboot -inject-str "LIST\n" -timeout 30
```

#### `-autoboot-timeout <seconds>`
A separate timeout that applies only in `-autoboot` mode.  `0` = disabled.
When specified, overrides the general `-timeout` policy for autoboot runs.

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

#### `-tracesnd`
Log every write to **port 0x03** (buzzer bit-bang).  Each line shows the
T-state timestamp and the new bit value, allowing exact frequency measurement.

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

The host keyboard maps to the Smaky 6 keyboard.  Alphanumeric keys and most
punctuation are passed through.  The Smaky 6 keyboard is uppercase-only on the
physical hardware, so lowercase letters are automatically uppercased.

| Host key                    | Smaky 6 function                                          |
|-----------------------------|-----------------------------------------------------------|
| **A–Z**, **0–9**, space      | Direct character (uppercased)                            |
| **Backspace**               | Smaky BS (`0x08`)                                        |
| **Enter / Return**          | Smaky CR (`0x0D`)                                        |
| **F11** / **Pause**         | **BREAK** — fires NMI, drops into SAMOS monitor           |
| **Shift+F11** / **Shift+Pause** | **SHIFT+BREAK** — hard reset (reboots from DX0:)     |
| **F1–F7**                   | Smaky function keys (CHANGE, SEARCH, SHOW, COPY, CURSOR, PROGRA, KILL) |
| **Ctrl+D** (terminal)       | Dump 64 KB RAM to file (same as SIGUSR1)                 |

Accented Swiss-French characters (é, è, à, ü, ö, …) are accepted from the
host as UTF-8 `SDL_TEXTINPUT` events and translated to the Smaky 6 chargen
code table.

---

## 6. Runtime Signals

| Signal     | Effect                                                                 |
|------------|------------------------------------------------------------------------|
| `SIGINT`   | Clean shutdown (same as closing the window)                            |
| `SIGTERM`  | Clean shutdown                                                         |
| `SIGUSR1`  | Dump 64 KB RAM to `smaky6_ram_NNNN_pcXXXX.bin` without stopping        |

The emulator prints its PID at startup:
```
[main] PID 12345 — send SIGUSR1 to dump RAM
```

Example:
```bash
kill -USR1 12345
```

---

## 7. Boot Sequence

```
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

**With `-autoboot`**: the emulator injects Enter (~3 s after power-on) to skip
the boot-key wait.  The timeout is long enough to cover the floppy seek and
load; `-autoboot-timeout` can cap the total run time.

**With `-harddisk`**: the Winchester drive is available after SAMOS has loaded.
The Phantom ROM does not boot directly from Winchester; a floppy with
`SYS.SY` is still required for the initial boot.

---

## 8. Disk Image Formats

### Floppy images (`.dsk`)

Raw flat binary: track 0 sector 0 first, 256 bytes per sector, 16 sectors per
track.

| Image size   | Geometry              | Notes                        |
|--------------|-----------------------|------------------------------|
| 163,840 bytes | 40 tracks × 16 × 256 | Standard single-sided 5.25"  |
| 315,392 bytes | 77 tracks × 16 × 256 | Extended (77-track drives)   |

Track count is **auto-detected** from image size.

### Hard-disk images (`.DSK`)

Raw flat binary indexed by LBA.  Sector size: 256 bytes.

```
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
python3 tools/smaky6_rom_extract.py /tmp/smaky6.txt \
    --start 0 --end 7777 \
    --output roms/samos_sys17.rom \
    --report sysmon_report.txt
```

The character generator ROM (`roms/chargen.rom`) is a standard 2716 EPROM
image (2048 bytes, 16 bytes per glyph, 128 glyphs).  If the file is missing,
the emulator falls back to a built-in synthetic table that covers the
printable ASCII range.

---

## License

Hardware design by Jean-Daniel Nicoud / EPFL / Epsitec.  Emulator source TBD.
