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
9. [Sound](#9-sound)
10. [Automation and scripting](#10-automation-and-scripting)
11. [Debugging and tracing](#11-debugging-and-tracing)
12. [RAM dumps](#12-ram-dumps)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. Requirements

- `CMake`: version 3.16 or newer, for the build system.
- `SDL2`: version 2.0 or newer, for the window, keyboard, and display.
- `C compiler`: C11-capable compiler such as `gcc` or `clang`.
- `git`: any recent version, used by FetchContent for the Z80 core.

Optional (for PDF generation only):

- `pandoc`: Markdown → PDF conversion.
- `lualatex`: PDF engine used by `pandoc`.

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

- `roms/samos_sys17.rom`: 2 KB, required. Phantom bootloader ROM (SYS17 TMS2716).
- `roms/chargen.rom`: 2 KB, optional. Character generator PROM.

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
./smemu6 -floppy <disk.dsk>
```

**Boot with two floppy drives:**

```bash
./smemu6 -floppy <disk.dsk> -floppy2 <disk2.dsk>
```

**Boot from Winchester (DX0) with a floppy accessible as DX1:**

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 <disk2.dsk>
```

**Boot from Winchester (DX0) with a host-directory-backed development floppy on DX1:**

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2-hostdir floppies/DX1
```

**Boot from a metadata-preserving host-directory export on DX0:**

```bash
python3 ../tools/extract_samos_image.py ../floppies/Sys2-2.dsk ../tmp/Sys2-2-hostdir
./smemu6 -floppy-hostdir ../tmp/Sys2-2-hostdir
```

`../tools/extract_samos_image.py` keeps that legacy interface but now acts as a
wrapper around the unified `../smaky6-tools/smaky6_samos.py extract-all ...
--metadata --clear` workflow.

> On Winchester-equipped Smaky 6 machines the hard disk **is** DX0.
> The floppy drive (if installed) occupies the DX1 slot.
> Combining `-floppy` (DX0) with `-harddisk` is not a real hardware
> configuration; only `-harddisk` + `-floppy2` matches real hardware.
>
> Hard-disk images are currently mounted with an in-memory write overlay.
> Guest-side changes are visible to the running session, but the backing
> `.DSK` file remains unchanged and the overlay is lost on remount or exit.

## 5. Command-Line Reference

### Floppy drives

- `-floppy <img>`: mount a floppy image on drive **DX0:**.
- `-floppy-hostdir <dir>`: build a writable in-memory DX0 overlay from a host directory at startup; native builds only.
- `-floppy2 <img>`: mount a floppy image on drive **DX1:**.
- `-floppy2-hostdir <dir>`: build a writable in-memory DX1 overlay from a host directory at startup; native builds only.
- `-dump-vfd-manifest <file>`: dump the planned host-directory virtual floppy layout as JSON; requires `-floppy-hostdir` or `-floppy2-hostdir`; use `-` for stdout.

Current host-directory floppy limitations:

- Native desktop builds only; the web build does not support this feature.
- The first slice rebuilds at mount time and on explicit refresh (`Ctrl+R` or
  `SIGUSR2`); it does not watch the host directory automatically.
- Guest-side writes land in the in-memory overlay only. They are visible to the
  running emulator session but are discarded on refresh/remount or emulator
  exit; the host directory stays untouched.
- Bootable DX0 hostdir media must preserve SAMOS metadata and ordering.
  `tools/extract_samos_image.py` exports a suitable tree from an existing
  floppy image such as `Sys2-2.dsk` by writing sidecars with `flags`, `load`,
  `entry`, dates, and `start_sector`. That script now delegates to the unified
  `smaky6_samos.py extract-all ... --metadata --clear` implementation.
- Host file names must currently use `NAME.TT` with a 1-8 character base name,
  `_` allowed, and a known 2-character Smaky type.
- Host trees may also contain nested `NAME.DR/` directories. Entries inside a
  `.DR` container are encoded with sector numbers relative to the container
  start, matching the SAMOS on-disk format.
- Optional sidecars `NAME.TT.meta.json` are supported for regular files and
  `NAME.DR.meta.json` for directory containers. They may provide `type`
  (validation only), `flags`, `load`, `entry`, `date_month`, `date_year`, and
  `start_sector`.
- In current native builds, guest commands such as `LIST DX1:`,
  `LIST DX1:BOX`, `TYPE DX1:TEXTFILE.BS`, and `TYPE DX1:BOX:INNER.BS` work
  against the virtual DX1 media. One CLI rule is now pinned down: directory
  names also omit `.DR`, including `CDIR` arguments. Do not use probes such as
  `CDIR DX1:BOX.DR`; real floppy media rejects that form too (`CDIR DX1:M.DR`
  on `Burotic.dsk` reports `fichier existant`).
- `CDIR` is a directory-creation command, not a change-directory oracle.
  On writable floppy images, `CDIR DX1:NEWDIR` creates `NEWDIR.DR`. The current
  hostdir-backed DX1 path now offers the same behavior through a writable
  in-memory overlay, while still leaving the host files unchanged.

For native desktop builds, `Ctrl+R` refreshes any mounted host-directory
virtual floppy in place. Headless or scripted runs can do the same with
`SIGUSR2`.

For inspection and testing, `-dump-vfd-manifest <file>` emits the exact file
layout the emulator plans to build for the selected hostdir-backed floppy,
including sectors, size, sidecar-
derived metadata, and both absolute plus encoded sector values for nested `.DR`
entries.

Example sidecar:

```json
{
  "type": "SM",
  "flags": 4660,
  "load": "0x5600",
  "entry": "0x5678",
  "date_month": 12,
  "date_year": 82
}
```

### Hard disk (Winchester)

- `-harddisk <img>`: mount a Winchester image on hard-disk drive 0 (SM6WIN0).
- `-harddisk2 <img>`: mount a Winchester image on hard-disk drive 1 (SM6WIN1).

**Hardware note:** On a Winchester-equipped Smaky 6 the hard disk occupies the
DX0 slot and the floppy (if present) occupies DX1. The valid combinations are:

- Floppy-only (DX0): `-floppy <img>`.
- Two floppies (DX0 + DX1): `-floppy <img> -floppy2 <img>`.
- Winchester (DX0) + floppy (DX1): `-harddisk <img> -floppy2 <img>`.
- Winchester only: `-harddisk <img>`.

### Boot control

- `-break-to-monitor`: inject SHIFT+BREAK to enter the SYSMON monitor at startup.
- `-no-launcher`: skip the startup configuration dialog and boot directly with the supplied media/options.

### String injection

- `-inject-str <s>`: inject a string once the SAMOS `>` prompt is detected. Use `\n` for Enter and `\f` to wait for the next CLI prompt before continuing.
- `-inject-keycode <hex>`: inject one raw keyboard code through the low-level CLA path once the CLI prompt is stable. Post-boot injected held keys now use the emulator's current best hardware model: the first CLA read returns a bit-7-set regular code, which can produce visible CLI text.
- `-inject-at-prompt <n>`: fire the configured injection on the `n`th visible CLI prompt transition instead of the first one. Default: `1`.
- `-inject-at-frame <n>`: fire `-inject-keycode` or `-inject-str` at absolute frame `n` instead of waiting for a CLI prompt.
- `-inject-delay <f>`: wait `f` frames after the CLI prompt appears before firing `-inject-str` or `-inject-keycode`.
- `-inject-hold-frames <f>`: hold `-inject-keycode` active for `f` ISR frames before releasing it. Default: `1`.

`-inject-str` is still the normal way to type commands automatically. `-inject-keycode` now follows the low-level CLA model closely enough to reproduce the visible post-boot `A` path in audit runs, but it remains a reverse-engineering / low-level testing tool rather than the normal command-entry mechanism.

**Example — run `LIST` automatically:**

```bash
./smemu6 -floppy <disk.dsk> -no-launcher -inject-str "LIST\n"
```

**Example — run two commands on successive prompts:**

```bash
./smemu6 -harddisk ../harddisks/SM6WIN0.DSK -floppy2 <disk.dsk> \
  -no-launcher -inject-str "LIST DX1:\n\fLIST DX1:\n"
```

### Display

- `-vmode <m>`: force video mode: `alpha` (text only), `graphic` (graphics only), or `super` (text + graphics).
- `-gfxbits <b>`: nibble bit order: `msb` (default, hardware-correct) or `lsb`.
- `-scale <n>`: integer window scale 1–8; default `1` gives a `512 × 508` window.
- `-scanlines`: draw a CRT-style scanline overlay that darkens every other output row.
- `-phosphor <c>`: screen phosphor colour, either `green` (default, P31 `#00E700`) or `white` (`#E8E8E8`).
- `-phosphor-decay <f>`: per-frame persistence decay `0.0`–0.99; default `0.70` approximates P31 remanence between display-off frames.
- `-no-phosphor`: disable phosphor persistence.
- `-no-display-off`: ignore display-blank writes to port `0x00`; the screen stays visible at all times.
- `-verbose-video`: log display on/off and video mode changes to stderr.

### Timing and timeouts

- `-timeout <s>`: global wall-clock timeout in seconds. `0` disables it; the default is 45 s when `-trace` is active.

### Sound

- `-no-beeper`: silence the machine's 1-bit buzzer. The beeper is on by default.
- `-drive-sound`: enable synthesized floppy drive sounds: motor whir, head-step clicks, and sector-hole ticks. Off by default.

---

## 6. Keyboard Mapping

The Smaky 6 has a QWERTZ Swiss keyboard layout with special function keys.
The emulator now resolves ordinary keys from host scancode positions through the
audited S471 table, then feeds them through the strict CLA / `SYS.SY` path.

### Special keys

- `F1`: **CURSOR** function key.
- `F2`: **COPY** function key.
- `F3`: **KILL** function key.
- `F4`: **PROGRA** function key.
- `F5`: **SHOW** function key.
- `F6`: **SEARCH** function key.
- `F7`: **CHANGE** function key.
- Web build note: browsers may reserve host `F1`..`F7`; use the dedicated
  front-panel CURSOR / COPY / KILL / PROGRA / SHOW / SEARCH / CHANGE buttons in
  the web UI when those host function keys are intercepted.
- `Arrow keys`: host aliases for the documented `CURSOR+r/d/f/c` combinations (`Up/Left/Right/Down`).
- `F9`: **DEFINE** (`0x1F`) via the current strict matrix map.
- `End`: current audited ordinary-key position 30 (`0x04` normal, `0x05` shifted).
- `F11` or `Pause`: **BREAK** (top-right key), triggers NMI and drops into SYSMON monitor.
- `Shift+F11` or `Shift+Pause`: **SHIFT+BREAK**, hard reset and reboot from DX0:.
- `Escape`: **ESC / UNDO** (top-left key), current working mapping emits `0x06`.

Older convenience aliases such as `F8`, `Insert`, `Home`, `Left Alt`, `Left Ctrl`,
`Left Windows / Super`, `AltGr`, and `Delete` are not part of the current documented
strict keyboard baseline and should not be relied on unless they are reintroduced
explicitly in code.

### Standard keys

Ordinary keys currently supported by the strict matrix path include the audited
host positions for `A`..`Z`, `0`..`9`, `Space`, `Backspace`, `Tab`, `Return`,
brackets, backslash, comma, period, and minus.  `Shift` selects the S471 Shift
layer, and `Caps Lock` selects the audited caps-like layer.

Overlapping printable key presses are queued and promoted one by one, so normal
fast typing now reaches the CLI in order instead of stopping after the first
printable key.

The Smaky 6 keyboard uses **QWERTZ** layout (Swiss German) — if your PC
keyboard is QWERTY or AZERTY, some punctuation positions differ because the
mapping is now position-based rather than text-input-based.

Hardware note: the full S471 keyboard ROM dump is now available.  The emulator
matches the confirmed special-key outputs it exposes directly, including
`Escape -> 0x06`, `Backspace -> 0x08`, `Tab -> 0x09`, `Return -> 0x0D`, and
`Space -> 0x20`.  This is now a strict CLA-centric baseline for non-text keys,
while printable host text still uses `SDL_TEXTINPUT` compatibility handling.
That compatibility path now includes a one-shot fallback for composed accented
characters, so host layouts that emit UTF-8 text such as `ü`, `ö`, `ä`, `é`,
`è`, `ê`, and `ç` can reach the CLI again even when SDL does not expose a fresh
claimable text scancode for the composed event.

**Function-key status bar:** The bottom strip of the emulator window shows
7 clickable buttons — one per Smaky function key (CURSOR, COPY, KILL,
PROGRA, SHOW, SEARCH, CHANGE).

- **Left-click and hold** to activate a function key; release to deactivate.
- **Right-click** toggles a persistent **latch**: the button stays active
  (rendered in yellow) until right-clicked again, enabling single-handed
  modifier+key combinations.

These are modifier-only keys: no character appears on screen when they are
pressed, matching real hardware behaviour. Programs that care about Smaky
function keys read this state directly; the CLI does not treat them as typed
text.

**Disk / RESET status bar:** The middle strip shows floppy and Winchester
drive activity LEDs.  At the right end:

- **BREAK** button — left-click fires an NMI (same as `F11` / `Pause`).
- **RESET** button — requires **two clicks**: first click arms the button
  (blinks orange for 3 s); second click confirms the hard reset.  Clicking
  anywhere else cancels.

---

## 7. Display Options

The Smaky 6 has two independent display layers:

- **Alpha layer** — 64 × 20 character text display
- **Graphic layer** — monochrome bitmap (>30 000 pixels)

The `-vmode` flag controls which layers are rendered:

- `alpha`: text layer only.
- `graphic`: graphics layer only.
- `super`: both layers overlaid, normal operation.

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

- `163 840 bytes`: 40 tracks × 16 × 256 B, standard single-sided 5.25" image.
- `315 392 bytes`: 77 tracks × 16 × 256 B, extended 77-track-drive image.

Track count is auto-detected from the image size.

Place image files anywhere and pass the path to `-floppy` / `-floppy2`.
The `floppies/` directory in the repository is the conventional location.

### Extracting files from a floppy

Use the tools from the companion project `../smaky6-tools/`:

```bash
python3 ../smaky6-tools/smaky6_samos.py list ../floppies/Sys1-H.dsk
python3 ../smaky6-tools/smaky6_samos.py extract-all ../floppies/Sys1-H.dsk private/extracted/ --metadata --clear
```

### Subdirectories (`.DR` files)

SAMOS supports nested directories stored as files with the `.DR` extension.
On disk, the container file is `NAME.DR`, but in CLI commands the path
component omits the `.DR` suffix. Use forms such as `LIST NAME`,
`TYPE NAME:INNER.BS`, or `LIST DX1:NAME` to access a subdirectory. `CDIR`
alone lists the current directory; `CLEAR` returns to the root.

Inside the emulator, subdirectories work transparently — the floppy image
contains all sectors and no special handling is required. When listing a
disk image with `smaky6_samos.py`, sub-entries are shown indented with `>`:

```text
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
./smemu6 -floppy <disk.dsk> -drive-sound
```

**Example — mute everything:**

```bash
./smemu6 -floppy <disk.dsk> -no-beeper
```

---

## 10. Automation and Scripting

The emulator can be driven non-interactively by combining `-inject-str` and `-timeout`.

**Run a command and capture screen output:**

```bash
./smemu6 \
  -floppy <disk.dsk> \
    -no-display-off \
    -inject-str "LIST\n" \
    -timeout 20 \
    -scrdump 2>screen.txt
```

`-scrdump` is most useful together with `-no-display-off`; many SAMOS boot and
CLI paths blank the display between updates, so using both options keeps the
prompt visible and makes the dumped rows readable.

**Run with tracing for debugging:**

```bash
./smemu6 \
  -floppy <disk.dsk> \
    -trace \
    -timeout 15 2>trace.log
```

**Dump RAM at the end of a run:**

```bash
./smemu6 \
  -floppy <disk.dsk> \
    -inject-str "BASIC\n" \
    -timeout 30 \
    -dump-ram basic_init.bin
```

---

## 11. Debugging and Tracing

These flags are intended for emulator development and reverse-engineering.
They produce output on **stderr**.

- `-trace`: Z80 program counter at key boot milestones.
- `-traceflow`: control flow in low RAM after OS handoff.
- `-tracekbd`: every keyboard status-port read and CLA write.
- `-tracesnd`: every write to port `0x03` (buzzer).
- `-trace08`: all `IN`/`OUT` traffic on port `0x08`.
- `-trace11`: all reads from port `0x11`.
- `-trace19`: all writes to port `0x19` (floppy control).
- `-tracecd`: all reads from port `0xCD` (Winchester DMA/status register).
- `-trace-win`: every Winchester controller command (RESTORE, SEEK, READ, WRITE) with CHS and LBA.
- `-tracefdc`: focused floppy ID/checksum stream events.
- `-scrdump`: changed screen rows printed to stderr each frame.

**Tip:** Combine with shell redirection to capture traces without mixing
them with emulator output:

```bash
./smemu6 -floppy <disk.dsk> -tracekbd 2>kbd.log
```

For a focused Linux/X11 regression check of the printable-key path, run
`tools/check_keyboard_asd_trace.sh` from the repository root. It launches the
emulator, waits until the CLI prompt is visible, injects overlapping `a s d`
keydown/keyup events, and verifies that the CLI receives all three visible
insertions in order. The script also releases any injected host keys during
cleanup so it does not leave the desktop input state latched.

---

## 12. RAM Dumps

**Automatic dump on exit:**

```bash
./smemu6 -floppy <disk.dsk> -timeout 10 -dump-ram snapshot.bin
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

### Black screen / no video after boot

Try forcing a video mode:

```bash
./smemu6 -floppy <disk.dsk> -vmode alpha
```

### Graphics appear inverted or garbled

The graphic plane uses nibble-interleaved encoding with MSB-left by default
(hardware-verified). If images still look wrong, you can force the bit order:

```bash
./smemu6 -floppy <disk.dsk> -gfxbits lsb
```

### Emulator exits immediately with error 043

The floppy image may be unreadable or in the wrong format.
Check the file size: a valid image is 163 840 bytes (40-track) or 315 392 bytes (77-track).

```bash
wc -c myimage.dsk
```

### Keyboard input not reaching the emulator

Make sure the SDL window has focus (click on it). The emulator only
processes keyboard events when its window is focused.

### `-inject-str` never fires

Check that the SAMOS `>` prompt is actually visible when you expect injection to
happen. The emulator only injects when `machine_cli_prompt_visible()` detects
the prompt in video RAM.

### `-scrdump` output is sparse or the prompt never becomes readable

Use `-no-display-off -scrdump` together. `-scrdump` alone only reports changed
rows, and normal SAMOS display-blanking can hide the prompt between frames.

### How to generate / update the PDF manuals

```bash
./tools/generate_pdfs.sh
```
