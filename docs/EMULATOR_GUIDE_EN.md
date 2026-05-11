# Smemu6 — User Guide

This guide covers how to build, run, and use the **Smaky 6 emulator**.
For documentation about the Smaky 6 computer itself (commands, OS, hardware),
see `docs/SMAKY6_USER_GUIDE_EN.md`.

---

## Table of Contents

1. [Requirements](#1-requirements)
2. [Building](#2-building)
3. [ROM files](#3-rom-files)
4. [Quick start](#4-quick-start)
5. [Command-line reference](#5-command-line-reference)
6. [Keyboard mapping](#6-keyboard-mapping)
7. [Display options](#7-display-options)
8. [Floppy images](#8-floppy-images)
9. [Automation and scripting](#9-automation-and-scripting)
10. [Debugging and tracing](#10-debugging-and-tracing)
11. [RAM dumps](#11-ram-dumps)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. Requirements

| Dependency | Minimum version | Purpose |
|------------|----------------|---------|
| CMake | 3.16 | Build system |
| SDL2 | 2.0 | Window, keyboard, display |
| C compiler | C11 (gcc / clang) | Compilation |
| git | any | FetchContent for Z80 core |

Optional (for PDF generation only):

| Dependency | Purpose |
|------------|---------|
| pandoc | Markdown → PDF conversion |
| lualatex | PDF engine used by pandoc |

---

## 2. Building

```bash
git clone https://github.com/Sch-LikA/smemu6
cd smemu6
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

For a release build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The resulting binary is `build/smemu6`.

---

## 3. ROM Files

Place ROM files in `roms/` (relative to the repository root).
The build system copies them automatically into `build/roms/`.

| File | Size | Required | Description |
|------|------|----------|-------------|
| `roms/samos_sys17.rom` | 2 KB | **Yes** | Phantom bootloader ROM (SYS17 TMS2716) |
| `roms/chargen.rom` | 2 KB | Optional | Character generator PROM |

> **Note:** If `chargen.rom` is absent, the emulator uses a built-in synthetic
> character table. Text will be legible but may differ slightly from the
> original hardware.
>
> If `samos_sys17.rom` is missing, the emulator prints a warning and the CPU
> executes undefined memory — nothing useful will happen.

---

## 4. Quick Start

**Boot into the machine monitor (no floppy):**

```bash
cd build
./smemu6
```

**Boot from a floppy image (starts automatically):**

```bash
./smemu6 -floppy ../floppies/sys.img
```

**Boot with two floppy drives:**

```bash
./smemu6 -floppy ../floppies/sys.img -floppy2 ../floppies/data.img
```

**Boot from Winchester (DX0) with a floppy accessible as DX1:**

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 ../floppies/data.img
```

> On Winchester-equipped Smaky 6 machines the hard disk **is** DX0.
> The floppy drive (if installed) occupies the DX1 slot.
> Combining `-floppy` (DX0) with `-harddisk` is not a real hardware
> configuration; only `-harddisk` + `-floppy2` matches real hardware.

**Boot and wait at the SAMOS `>` prompt (for `-inject-str`):**

```bash
./smemu6 -floppy ../floppies/sys.img -autoboot
```

---

## 5. Command-Line Reference

### Floppy drives

| Option | Description |
|--------|-------------|
| `-floppy <img>` | Mount a floppy image on drive **DX0:** |
| `-floppy2 <img>` | Mount a floppy image on drive **DX1:** |

### Hard disk (Winchester)

| Option | Description |
|--------|-------------|
| `-harddisk <img>` | Mount a Winchester image on hard-disk drive 0 (SM6WIN0) |
| `-harddisk2 <img>` | Mount a Winchester image on hard-disk drive 1 (SM6WIN1) |

**Hardware note:** On a Winchester-equipped Smaky 6 the hard disk occupies the
DX0 slot and the floppy (if present) occupies DX1. The valid combinations are:

| Configuration | Options |
|---------------|---------|
| Floppy-only (DX0) | `-floppy <img>` |
| Two floppies (DX0 + DX1) | `-floppy <img> -floppy2 <img>` |
| Winchester (DX0) + floppy (DX1) | `-harddisk <img> -floppy2 <img>` |
| Winchester only | `-harddisk <img>` |

### Boot control

| Option | Description |
|--------|-------------|
| `-autoboot` | Inject Enter once SAMOS loads, to reach the `>` prompt (needed for `-inject-str`). The machine boots from DX0 automatically; `-autoboot` is mainly needed to wait for SAMOS and fire string injection. Use `-autoboot2` to boot from a non-default drive. |
| `-break-to-monitor` | Inject SHIFT+BREAK to enter the SYSMON monitor at startup |
| `-autoboot2 <n>` | Boot-selection key code (decimal or `0xHH`): `0x00`=Enter/DX0, `0x40`=DX1, `0x60`=Winchester (default `0x20` = Space → DX0) |
| `-autoboot3 <n>` | Optional third key code (default: disabled) |
| `-autoboot-timeout <s>` | Wall-clock timeout in autoboot mode (`0` = off) |

### String injection

| Option | Description |
|--------|-------------|
| `-inject-str <s>` | Inject a string once the SAMOS `>` prompt is detected. Use `\n` for Enter. |
| `-inject-delay <f>` | Accepted for backward compatibility; no longer has any effect. |
| `-inject-via-fifo` | Route `-inject-str` through the hardware keyboard FIFO instead of the fast path |

**Example — run `LIST` automatically:**

```bash
./smemu6 -floppy ../floppies/sys.img -autoboot -inject-str "LIST\n"
```

### Display

| Option | Description |
|--------|-------------|
| `-vmode <m>` | Force video mode: `alpha` (text only), `graphic` (graphics only), `super` (text + graphics) |
| `-gfxbits <b>` | Nibble bit order: `msb` (default, hardware-correct) or `lsb` |
| `-scale <n>` | Integer window scale 1–8 (default `1` → 512 × 508 pixels) |
| `-scanlines` | Draw CRT-style scanline overlay (darkens every other output row) |
| `-phosphor <c>` | Screen phosphor colour: `green` (default, P31 `#00E700`) or `white` (`#E8E8E8`) |
| `-phosphor-decay <f>` | Per-frame persistence decay `0.0`–0.99 (default `0.70` ≈ P31); simulates phosphor remanence between display-off frames |
| `-no-phosphor` | Disable phosphor persistence (instant pixel decay) |
| `-no-display-off` | Ignore display-blank writes to port `0x00`; screen stays visible at all times |

### Timing and timeouts

| Option | Description |
|--------|-------------|
| `-timeout <s>` | Global wall-clock timeout in seconds (`0` = off; default 45 s when `-trace` is active) |

### Sound

| Option | Default | Description |
|--------|---------|-------------|
| `-no-beeper` | — | Silence the machine's 1-bit buzzer (beeper is **on** by default) |
| `-drive-sound` | — | Enable synthesized floppy drive sounds: motor whir, head-step clicks, and sector-hole ticks (off by default) |

---

## 6. Keyboard Mapping

The Smaky 6 has a QWERTZ Swiss keyboard layout with special function keys.
The emulator maps them to standard PC keys as follows.

### Special keys

| PC key | Smaky 6 function |
|--------|-----------------|
| `Left Ctrl` | **CURSOR** function key |
| `Left Alt` | **COPY** function key |
| `Left Windows` / `Super` | **KILL** function key |
| `AltGr` (Right Alt) | **PROGRA** function key |
| `F10` | **SHOW** function key |
| `Menu` / `App` | **SEARCH** function key |
| `Right Ctrl` | **CHANGE** function key |
| `F8` | **MACRO** — replay recorded keystroke sequence (`«` code 0x1E) |
| `F9` | **DEFINE** — record a keystroke sequence (`»` code 0x1F) |
| `F11` or `Pause` | **BREAK** (top-right key) — triggers NMI → drops into SYSMON monitor |
| `Shift+F11` or `Shift+Pause` | **SHIFT+BREAK** — hard reset (reboots from DX0:) |
| `Escape` | **ESC / UNDO** (top-left key) — cancels / clears current CLI line |
| `Tab` | Inserts `DX1:` in the CLI command line |

### Standard keys

All printable ASCII characters are passed through directly.
The Smaky 6 keyboard uses **QWERTZ** layout (Swiss German) — if your PC
keyboard is QWERTY or AZERTY, some punctuation keys may differ.

---

## 7. Display Options

The Smaky 6 has two independent display layers:

- **Alpha layer** — 64 × 20 character text display
- **Graphic layer** — monochrome bitmap (>30 000 pixels)

The `-vmode` flag controls which layers are rendered:

| Value | What is shown |
|-------|--------------|
| `alpha` | Text layer only |
| `graphic` | Graphics layer only |
| `super` | Both layers overlaid (normal operation) |

The `-scale` flag sets the integer zoom level. The default is 1 (512 × 508);
scale 2 gives a 1024 × 1016 window, comfortable on most monitors.  Add
`-scanlines` for a CRT-style scanline overlay at any scale.

Phosphor persistence (P31 green display) is simulated by default with a
per-frame decay factor of 0.70.  Use `-phosphor-decay <0.0..0.99>` to
adjust the persistence strength, or `-no-phosphor` to disable it entirely.

---

## 8. Floppy Images

### Supported format

The emulator reads **Micropolis raw sector images** in two sizes:

| Image size    | Geometry                  | Notes                        |
|---------------|---------------------------|------------------------------|
| 163 840 bytes | 40 tracks × 16 × 256 B    | Standard single-sided 5.25" |
| 315 392 bytes | 77 tracks × 16 × 256 B    | Extended (77-track drives)   |

Track count is auto-detected from the image size.

Place image files anywhere and pass the path to `-floppy` / `-floppy2`.
The `floppies/` directory in the repository is the conventional location.

### Extracting files from a floppy

Use the tools from the companion project `../smaky6-tools/`:

```bash
python3 ../smaky6-tools/smaky6_samos.py disk ../floppies/sys.dsk list
python3 ../smaky6-tools/smaky6_samos.py disk ../floppies/sys.dsk extract-all private/extracted/
```

### Subdirectories (`.DR` files)

SAMOS supports nested directories stored as files with the `.DR` extension.
The CLI command `CDIR NAME.DR` enters a subdirectory; `CDIR` alone lists
the current directory; `CLEAR` returns to the root.

Inside the emulator, subdirectories work transparently — the floppy image
contains all sectors and no special handling is required.  When listing a
disk image with `smaky6_samos.py`, sub-entries are shown indented with `> `:

```
17  U          DR  Directory   509  609 …
    > EDISK    SM  SMILE prog  512  525 …
    > TDISK    SM  SMILE prog  525  531 …
```

---

## 9. Sound

The emulator reproduces two categories of sound through SDL2 audio output.

### Buzzer (beeper)

The Smaky 6 has a simple 1-bit buzzer driven by port `0x03`. Software
produces tones by toggling the port in a tight loop; each write flips the
speaker state. The emulator models this accurately with a unipolar square
wave at 44 100 Hz.

The buzzer is **enabled by default**. Pass `-no-beeper` to silence it.

### Floppy drive sounds

The emulator can synthesize the acoustic character of the Micropolis 5.25"
hard-sectored drive:

- **Motor whir** — bandpass-filtered noise (300–1 500 Hz) that fades in when
  the spindle starts and fades out ~0.8 s after the last seek activity.
- **Head-step click** — a sharp crack (600–3 000 Hz bandpass noise with
  exponential decay) produced on every track seek step.
- **Sector-hole tick** — a soft noise burst fired once per sector hole as the
  disk rotates (16 ticks per revolution at ~300 RPM).

Drive sounds are **disabled by default** (synthesized sounds are a
work-in-progress; real samples will be added later). Enable with
`-drive-sound`.

**Example — boot with beeper and drive sounds:**

```bash
./smemu6 -floppy sys.img -autoboot -drive-sound
```

**Example — mute everything:**

```bash
./smemu6 -floppy sys.img -autoboot -no-beeper
```

---

## 10. Automation and Scripting

The emulator can be driven non-interactively by combining `-autoboot`,
`-inject-str`, and `-timeout`.

**Run a command and capture screen output:**

```bash
./smemu6 \
    -floppy ../floppies/sys.img \
    -autoboot \
    -inject-str "LIST\n" \
    -timeout 20 \
    -scrdump 2>screen.txt
```

**Run with tracing for debugging:**

```bash
./smemu6 \
    -floppy ../floppies/sys.img \
    -autoboot \
    -trace \
    -timeout 15 2>trace.log
```

**Dump RAM at the end of a run:**

```bash
./smemu6 \
    -floppy ../floppies/sys.img \
    -autoboot \
    -inject-str "BASIC\n" \
    -timeout 30 \
    -dump-ram basic_init.bin
```

---

## 11. Debugging and Tracing

These flags are intended for emulator development and reverse-engineering.
They produce output on **stderr**.

| Flag | What it traces |
|------|---------------|
| `-trace` | Z80 program counter at key boot milestones |
| `-traceflow` | Control flow in low RAM after OS handoff |
| `-tracekbd` | Every keyboard status port read and CLA write |
| `-tracesnd` | Every write to port `0x03` (buzzer) |
| `-trace08` | All `IN`/`OUT` traffic on port `0x08` |
| `-trace11` | All reads from port `0x11` |
| `-trace19` | All writes to port `0x19` (floppy control) |
| `-tracecd` | All reads from port `0xCD` (Winchester DMA/status register) |
| `-trace-win` | Every Winchester controller command (RESTORE, SEEK, READ, WRITE) with CHS and LBA |
| `-tracefdc` | Focused floppy ID/checksum stream events |
| `-scrdump` | Changed screen rows printed to stderr each frame |

**Tip:** Combine with shell redirection to capture traces without mixing
them with emulator output:

```bash
./smemu6 -floppy sys.img -autoboot -tracekbd 2>kbd.log
```

---

## 12. RAM Dumps

**Automatic dump on exit:**

```bash
./smemu6 -floppy sys.img -autoboot -timeout 10 -dump-ram snapshot.bin
```

**Interactive dump during a running session:**

Press `Ctrl+D` in the terminal, or send `SIGUSR1` to the process:

```bash
kill -SIGUSR1 $(pgrep smemu6)
```

This writes a file named `smaky6_ram_NNNN_pcXXXX.bin` in the current
directory, where `NNNN` is a sequence number and `XXXX` is the Z80 PC value
at the time of the dump.

The 64 KB dump can be inspected with any hex editor or disassembler:

```bash
xxd snapshot.bin | less
objdump -b binary -m z80 -D snapshot.bin | less
```

---

## 13. Troubleshooting

**Black screen / no video after boot**

Try forcing a video mode:

```bash
./smemu6 -floppy sys.img -autoboot -vmode alpha
```

**Graphics appear inverted or garbled**

The graphic plane uses nibble-interleaved encoding with MSB-left by default
(hardware-verified). If images still look wrong, you can force the bit order:

```bash
./smemu6 -floppy sys.img -autoboot -gfxbits lsb
```

**Emulator exits immediately with error 043**

The floppy image may be unreadable or in the wrong format.
Check the file size: a valid image is 163 840 bytes (40-track) or 315 392 bytes (77-track).

```bash
wc -c myimage.dsk
```

**Keyboard input not reaching the emulator**

Make sure the SDL window has focus (click on it). The emulator only
processes keyboard events when its window is focused.

**Autoboot does not reach the `>` prompt**

Some floppy images require more time to load. Try increasing the autoboot
timeout:

```bash
./smemu6 -floppy sys.img -autoboot -autoboot-timeout 60
```

**How to generate / update the PDF manuals**

```bash
./tools/generate_pdfs.sh
```
