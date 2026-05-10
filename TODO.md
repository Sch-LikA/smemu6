# Smemu6 — TODO / Known Gaps

Items are grouped by subsystem.  Entries marked **[confirmed]** have been verified
against disassembly or hardware documentation.

---

## UI / Display

### ~~Startup launcher window (SDL configuration dialog)~~ ✅ Done

Before the emulator window opens, show a small SDL launcher that lets users
configure common options without touching the CLI.  Implemented in
`src/launcher.c` / `src/launcher.h`.  `main.c` calls `launcher_run()` after
SDL_Init; it fills a `LauncherConfig` struct and returns; `main()` applies the
struct to the existing config fields and continues with the normal startup path.
Skip with `-no-launcher` or when `SDL_VIDEODRIVER=dummy` (headless).

#### Window layout (top to bottom)

**Header**
- Single centred line: *"Smemu6 -- Smart Emulator of the Smart Keyboard"*

**Storage section**

| Label | Control | Emulator option |
|-------|---------|-----------------|
| `DX0:` | Drop-down: **Floppy** / **Harddisk** | `-floppy` / `-harddisk` |
| *(DX0 path)* | File-picker button → opens OS file dialog filtered to `*.dsk *.DSK` | path argument |
| `DX1:` | Static label: **Floppy** (no second harddisk) | `-floppy2` |
| *(DX1 path)* | File-picker button → opens OS file dialog filtered to `*.dsk *.DSK` | path argument |
| Autoboot | Toggle: **On** / **Off** — injects Enter after 3 s to auto-select floppy boot | `-autoboot` | **On** |

File paths are shown truncated (last 46 chars with `…` prefix) on a sub-row
directly below each disk row (Browse + Clear buttons).  A small `×` button clears the selection.

**Screen section**

| Label | Control | Emulator option | Default |
|-------|---------|-----------------|---------|
| Scaling | Drop-down: **1×** / **2×** / **3×** / **4×** | `-scale N` | **1×** |
| Phosphor colour | Drop-down: **Green** / **White** | *(new `-phosphor white` option)* | Green |
| Scanlines | Drop-down: **Off** / **On** | `-scanlines` | On |
| Disable screen blanking | Toggle: **On** / **Off** | `-no-display-off` | On |

> **Note:** `-phosphor white` is implemented. The launcher **Phosphor colour** drop-down is wired directly.

> **Note:** `-scanlines` is implemented and tested. The launcher control is wired directly.

**Sound section**

| Label | Control | Emulator option | Default |
|-------|---------|-----------------|---------|
| Beeper | Toggle: **On** / **Off** | `-no-beeper` when Off | On |
| Drive sounds | Static label: **Off** *(coming soon)* | `-drive-sound` (stub) | Off |

**Button row (bottom)**

- **Help** — opens a second SDL window (or overlaid panel) showing:
  - Two-paragraph summary of SAMOS usage (LIST, COPY, INIT, TAB shortcut)
  - Keyboard mapping table (host key → Smaky 6 key, same content as
    `docs/EMULATOR_GUIDE_EN.md` §5 Keyboard Controls)
- **Start** — closes the launcher window and starts the emulator with the
  chosen settings.  If a required ROM (`roms/samos_sys17.rom`) is missing,
  show an SDL_ShowSimpleMessageBox warning before starting.

#### Implementation notes

- Rendered with SDL2 primitives only — no `SDL_ttf`.  Text uses the authentic
  Smaky 6 chargen ROM embedded as `src/chargen_rom.h` (128 chars × 16 rows, LSB-first;
  rows 0–9 carry glyph data including descenders, rows 10–15 are zero).
  `FONT_ROWS 10` is used as the render loop bound so descenders (`p`, `q`, `g`, `y`, `j`)
  are shown; `FONT_H 8` is used for layout/centering.
- File picker: async `tinyfiledialogs` call running in a detached `SDL_CreateThread`
  so the event loop is never blocked.  `PickCtx` state machine (`PICK_IDLE` /
  `PICK_RUNNING` / `PICK_DONE`) is polled from the main loop.
  `SDL_HINT_VIDEO_X11_NET_WM_PING=0` prevents the WM from marking the window
  as unresponsive while the picker thread starts up.
- The launcher must work in headless mode (`SDL_VIDEODRIVER=dummy`): detect the
  dummy driver and skip the launcher, reading config from CLI only.
- All launcher settings are additive: CLI options passed alongside the binary
  pre-populate the launcher controls so the user can review and adjust them.
  After clicking Start the launcher values are authoritative for all settings
  (disk paths, type, scale, phosphor, scanlines, autoboot, beeper).
- Window size: 460 × 464 logical pixels at 1:1; no scaling needed.

#### New options to add to main.c / README / guides

| New flag | Purpose | Default |
|----------|---------|---------|
| ~~`-phosphor white`~~ | ~~Use white-phosphor palette instead of green~~ | ✅ Done |
| ~~`-scanlines`~~ | ~~Draw alternating dim lines over the framebuffer (CRT effect)~~ | ✅ Done |
| ~~`-no-launcher`~~ | ~~Skip the launcher and go straight to the emulator~~ | ✅ Done |

---

### ~~Period-correct green-phosphor look~~ ✅ Done

The SDL renderer now draws lit pixels as `#00E700` (P31 green phosphor) on a near-black
dark green background `#000800` (`VIDEO_COLOR_LIT` / `VIDEO_COLOR_BG` in `video.h`).

### ~~Correct pixel aspect ratio~~ ✅ Done

The real CRT had non-square pixels (~2–4× taller than wide).  The machine pixel buffer
(512×240) is rendered 1:1 into the SDL logical window; the physical window is enlarged by
the `-scale` factor (default 2×), giving 1024×520 at the default setting.  The `VIDEO_ASPECT_H`
constant is kept equal to `VIDEO_PX_H` so no stretching is applied inside the logical window
— integer scaling via the SDL window size is the preferred approach.

### ~~Integer scaling option~~ ✅ Done

`-scale N` (N = 1..8, default 2) multiplies the physical window by N.  At default 2×,
the window is 1024 × 520 (240+20 status bar × 2).  SDL logical size stays fixed at
`VIDEO_WIN_W × VIDEO_WIN_H` so the rendering code is scale-independent.

### ~~Live floppy track/sector visualisation~~ ✅ Done

The status bar (14 logical px `VIDEO_LED_H`, scales with `-scale`) shows per drive slot (DX0 / DX1).
Each slot auto-detects whether it holds a floppy or a harddisk image:

- **Floppy** (amber LED): `T:nn S:nn` — current track and sector
- **Harddisk** (orange-red LED): `C:n H:n S:nn` — last cylinder, head and sector accessed
- **Empty**: LED off, no info text

The label (`DX0:` / `DX1:`) is rendered in green for floppies and cyan for harddisks.
---

## Keyboard

### ~~Auto-repeat (SAMOS ≥ 1.3)~~ ✅ Done  [confirmed]

SAMOS ISR Stage 4 (`0x01DF–0x0206`) implements hardware-accurate key auto-repeat.

The two RAM locations controlling it:

| Address  | Octal    | Role |
|----------|----------|------|
| `0x4558` | `042530` | Initial-delay countdown. Set to `0x23` (35 frames = **700 ms**) on first keypress; decremented each frame; when it hits zero, reloaded to `3` (3 frames = **60 ms**) for the fast-repeat rate. |
| `0x4577` | `042567` | Repeat key code register. Stores the last key written to the circular buffer; re-injected each time the countdown fires. |

**Current status:** Physical keyboard uses a FIFO→circular-buffer path that bypasses
the SAMOS ISR entirely (because ISR Stage 2 is permanently blocked by the `0x4582=0x80`
sentinel once SAMOS is running).  SDL key-repeat events are filtered out
(`if (ev->repeat) return`), so **holding a key produces exactly one character**.

**To implement:** In `keyboard_frame_tick()`, after draining one key from the FIFO into
the circular buffer, also set `m->bus[0x4558] = 0x23` and `m->bus[0x4577] = code`.
The SAMOS ISR will then handle the actual repeat injection autonomously on subsequent
frames, using the same timing as the real hardware.

✅ Done — `keyboard_frame_tick()` sets `m->bus[0x4558] = 0x23` and `m->bus[0x4577] = code`
after each drain to the SAMOS circular buffer.  `keyboard_event()` zeros `0x4558` on any
regular KEYUP while SAMOS is loaded, stopping the countdown when the key is released.
`repeat_scan` (SDL_Scancode) tracked in `struct kbd`.

See [docs/dev/keyboard_analysis.md](docs/dev/keyboard_analysis.md) for the full Stage 4 disassembly.

### Special / function keys  [codes confirmed from doc p.213]

The Smaky 6 keyboard has two categories of extra keys beyond the ASCII set.

**Category 1 — 7 "touches de fonction" (bitmask, read via GETFON)**

These 7 keys are NOT sent through the key FIFO.  When no regular key is pressed
(FOUND=0), the CLA port returns a 7-bit bitmask where each bit represents one
function key held down.  The SAMOS `?GETFON` / `GETFON` system calls read this
bitmask.  Codes confirmed from doc section 10.4, page 213 (octal):

| Key     | Octal  | Hex    | Bit |
|---------|--------|--------|-----|
| CHANGE  | `001`  | `0x01` | 0   |
| SEARCH  | `002`  | `0x02` | 1   |
| SHOW    | `004`  | `0x04` | 2   |
| COPY    | `010`  | `0x08` | 3   |
| CURSOR  | `020`  | `0x10` | 4   |
| PROGRA  | `040`  | `0x20` | 5   |
| KILL    | `100`  | `0x40` | 6   |

To implement: add a `uint8_t fonct_bits` field to `struct kbd`; set/clear the
appropriate bit on SDL key down/up; return `fonct_bits` (with bit 7 clear = FOUND=0)
from `keyboard_read_cla()` when no regular key is pending.  The existing `0x80`
no-key sentinel already occupies the FOUND=1 / no-key case — the FOUND=0 path just
needs to return `fonct_bits` instead of `0x80`.

✅ Done — `fonct_bits` field added to `struct kbd`; FONCT[] table in `keyboard_event()`
sets/clears bits on KEYDOWN/KEYUP; `keyboard_read_cla()` returns `0x80 | fonct_bits`
when no regular key is pending.

SDL mapping:

| Key     | SDL scancode                          | Host key                  |
|---------|---------------------------------------|---------------------------|
| CHANGE  | `SDL_SCANCODE_RCTRL`                  | Right Ctrl                |
| SEARCH  | `SDL_SCANCODE_APPLICATION`            | Menu / App key            |
| SHOW    | `SDL_SCANCODE_F10`                    | F10 (Fn not SDL-visible)  |
| COPY    | `SDL_SCANCODE_LALT`                   | Left Alt                  |
| CURSOR  | `SDL_SCANCODE_LCTRL`                  | Left Ctrl                 |
| PROGRA  | `SDL_SCANCODE_RALT`                   | AltGr (= Right Alt)       |
| KILL    | `SDL_SCANCODE_LGUI`                   | Left Windows / Super      |

**Category 2 — regular FIFO keys with special codes** ✅ Done

These keys send a code through the normal FIFO path (same as letters/digits).
Codes confirmed from doc section 10.4, page 213 (octal):

| Key      | Octal  | Hex    | Glyph | Host key  | Note                        |
|----------|--------|--------|-------|-----------|--------------------------|
| MACRO    | `036`  | `0x1E` | `«`   | F8        | French opening guillemet    |
| DEF(INE) | `037`  | `0x1F` | `»`   | F9        | French closing guillemet    |

Implemented in `KEY_TABLE[]` in `keyboard.c`:
```c
{ SDL_SCANCODE_F8,  0x1E },   /* MACRO  → «  */
{ SDL_SCANCODE_F9,  0x1F },   /* DEFINE → »  */
```

**REP / FNCT** shows code `0` in the doc — this is the hardware repeat / function
modifier key.  It generates no independent code; skip for now.

### Swiss-French accented characters and "Mise en page" codes  [confirmed from doc p.214/p.215]

**"Mise en page" (formatting) control codes** — doc section 10.4, page 215:

These codes are in the standard ASCII C0 range and behave as formatting / color-select
controls for the display and printer.  `0x0E` and `0x0F` are dual-purpose: they act
as color-switch codes in a formatting context, but the chargen ROM also maps `0x0F`
to the ü glyph when used as a display character (see table below).

| Oct | Hex    | Name  | Meaning                          |
|-----|--------|-------|----------------------------------|
| 000 | `0x00` | NUL   | null (also listed under "Special") |
| 007 | `0x07` | BEL   | bell — triggers the speaker (also "Special") |
| 011 | `0x09` | TAB   | horizontal tab                   |
| 012 | `0x0A` | LF    | line feed                        |
| 013 | `0x0B` | VT    | vertical tab                     |
| 014 | `0x0C` | FF    | form feed                        |
| 015 | `0x0D` | CR    | carriage return                  |
| 016 | `0x0E` | Red   | switch to red ink/color          |
| 017 | `0x0F` | Black | switch to black ink / also ü glyph |
| 033 | `0x1B` | ESC   | escape (printer/serial control prefix — also "Special"; chargen *displays* this as ä) |

**"Communication Requests"** — serial/data-link control codes (doc p.215).
Left column listed in hex, right column in octal.
Glyph shapes read directly from `roms/chargen.rom` (stride 16 bytes/char, 8 rows used):

| Oct | Hex    | Name | Standard meaning       | Chargen glyph (from ROM)          |
|-----|--------|------|------------------------|-----------------------------------|
| 001 | `0x01` | SOH  | Start of heading       | empty rectangle (box outline)     |
| 002 | `0x02` | STX  | Start of text          | box with small mark above it      |
| 003 | `0x03` | ETX  | End of text            | decorative cross / diamond        |
| 004 | `0x04` | EOT  | End of transmission    | `<` left-pointing chevron         |
| 005 | `0x05` | ENQ  | Enquiry                | `>` right-pointing chevron        |
| 006 | `0x06` | ACK  | Acknowledge            | letter `A` shape                  |
| 025 | `0x15` | NAK  | Negative acknowledge   | ê                                 |
| 026 | `0x16` | SYN  | Synchronous idle       | ï                                 |
| 027 | `0x17` | ETB  | End transmission block | î                                 |
| 030 | `0x18` | CAN  | Cancel                 | ô                                 |
| 031 | `0x19` | EM   | End of medium          | ù                                 |
| 032 | `0x1A` | SUB  | Substitute             | û                                 |

Also confirmed from ROM:

| Oct  | Hex    | Chargen glyph                                              |
|------|--------|------------------------------------------------------------|
| 0136 | `0x5E` | `^` circumflex accent (top 3 rows of A, no bar/legs)       |
| 0177 | `0x7F` | solid filled block ▓ (7 rows of `0x7E`)                    |



| Oct | Hex    | Name | Chargen glyph (same byte, display context) |
|-----|--------|------|--------------------------------------------|
| 034 | `0x1C` | FS   | ö                                          |
| 035 | `0x1D` | GS   | ç                                          |
| 036 | `0x1E` | RS   | « (= MACRO key code)                       |
| 037 | `0x1F` | US   | » (= DEFINE key code)                      |

**"DC" paper-tape reader/punch controls** — doc p.215, codes in **octal**:

| Oct | Hex    | Name | Meaning              | Chargen glyph |
|-----|--------|------|----------------------|---------------|
| 021 | `0x11` | DC1  | Reader on            | â             |
| 022 | `0x12` | DC2  | Aux on               | é             |
| 023 | `0x13` | DC3  | Reader off           | è             |
| 024 | `0x14` | DC4  | Aux off              | ë             |

**Summary of dual-use principle:** the entire range `0x0F–0x1F` (and a few others such
as `0x00`, `0x07`, `0x08`, `0x09–0x0D`) carries two meanings depending on context:
as a **control/format code** when interpreted by the OS, printer driver, or serial
handler; and as a **Swiss-French display glyph** when rendered by the chargen ROM.
The emulator must honour both: pass the raw byte to the chargen for display, and
let the OS/SAMOS handle the control semantics.

**"Correction" codes** — doc section 10.4, page 215:

| Oct | Hex    | Name | Meaning                                              |
|-----|--------|------|------------------------------------------------------|
| 010 | `0x08` | BS   | backspace — mapped to `SDL_SCANCODE_BACKSPACE` ✅    |
| 177 | `0x7F` | DEL  | delete-forward — mapped to `SDL_SCANCODE_DELETE` ✅; also the filled-block glyph in chargen |

Note on `0x1B` dual use: when written to the display it renders the ä glyph (Prom
2716 chargen mapping); when sent to the printer or serial port it acts as an escape
sequence prefix.  This is why `SDL_SCANCODE_ESCAPE` must **not** be mapped to `0x1B`
— the keyboard has no ESC key; the UNDO key generates NMI instead.

**Chargen glyph block `0x0F–0x1F`** — the keyboard EPROM (Prom 2716) maps this range
to Swiss-French glyphs instead of the standard ASCII C0 control codes.  The full
mapping (octal → hex → character), confirmed from doc section 10.4, page 214:

| Oct | Hex    | Char | | Oct | Hex    | Char |
|-----|--------|------|-|-----|--------|------|
| 017 | `0x0F` | ü    | | 030 | `0x18` | ô    |
| 020 | `0x10` | à    | | 031 | `0x19` | ù    |
| 021 | `0x11` | â    | | 032 | `0x1A` | û    |
| 022 | `0x12` | é    | | 033 | `0x1B` | ä ← (not ESC!) |
| 023 | `0x13` | è    | | 034 | `0x1C` | ö    |
| 024 | `0x14` | ë    | | 035 | `0x1D` | ç    |
| 025 | `0x15` | ê    | | 036 | `0x1E` | «    |
| 026 | `0x16` | ï    | | 037 | `0x1F` | »    |
| 027 | `0x17` | î    | | | | |

Additional remaps outside the standard ASCII printable range (same page):

| Oct | Hex    | Char | Note                        |
|-----|--------|------|-----------------------------|
| 043 | `0x23` | `#`  | same as ASCII               |
| 133 | `0x5B` | `[`  | same as ASCII               |
| 134 | `0x5C` | `\`  | same as ASCII               |
| 135 | `0x5D` | `]`  | same as ASCII               |
| 136 | `0x5E` | `^`  | circumflex accent (confirmed from ROM)    |
| 137 | `0x5F` | `_`  | underscore (shown as `–`)   |
| 140 | `0x60` | `` ` `` | backtick                |
| 173 | `0x7B` | `{`  | same as ASCII               |
| 174 | `0x7C` | `\|` | same as ASCII               |
| 175 | `0x7D` | `}`  | same as ASCII               |
| 176 | `0x7E` | `~`  | same as ASCII               |
| 177 | `0x7F` | ▓   | solid filled block (confirmed from ROM)  |

**Note on character generator variants (doc p.214):** The table on that page compares
four ROM variants: Prom 2716, Versatec, Motorola 6571, and 74S262.  The Versatec
mapping is very close to the Prom 2716 (only minor differences in the upper range).
The Motorola 6571 and 74S262 are substantially different — do not use them as a
reference for the emulator.  The `roms/chargen.rom` image should correspond to the
Prom 2716 layout documented above.

**SDL mapping for accented keys:** On a standard PC keyboard, the host OS delivers
these as UTF-8 `SDL_TEXTINPUT` events rather than scancodes.  The cleanest approach
is to add a `SDL_TEXTINPUT` handler in `keyboard_event()` alongside the existing
`SDL_KEYDOWN` handler: scan the Unicode codepoint against a lookup table and push
the corresponding Smaky code to the FIFO.  This avoids layout-specific scancode
assumptions and works for any OS input method.

---

## Display / Video

### ~~Lowercase character support~~ ✓ DONE

The chargen ROM already has lowercase glyphs (0x61–0x7A).  The fix was on the
keyboard input side: replaced printable-character entries in `KEY_TABLE[]` with
an `SDL_TEXTINPUT` handler (`keyboard_text_event()`).  The host OS applies shift /
Caps Lock so 'a' arrives by default and 'A' with Shift.  The raw byte is pushed to
the FIFO unchanged, and the chargen renders it correctly.

### ~~Display-off mode~~ ✓ DONE

Writing `0x00` to port `0x00` now blanks the machine area (black pixels).
Port writes with bit 0 = 1 re-enable the display.  The status bar remains
visible in both states.  Implemented via `vid.display_on` flag.

### ~~CRT phosphor colour option~~ ✅ Done

Add a `-phosphor <colour>` CLI option (and a matching control in the launcher)
to select the screen palette:

| Value   | Lit colour | Background  | Real-world CRT |
|---------|-----------|-------------|----------------|
| `green` | `#00E700` | `#000800`   | **Default** — P31 green phosphor (current) |
| `white` | `#E8E8E8` | `#080808`   | White phosphor — some Smaky 6 units shipped with this |

**Implementation:**
1. ~~Add two `uint32_t` constants to `video.h` for the white palette
   (`VIDEO_COLOR_LIT_WHITE`, `VIDEO_COLOR_BG_WHITE`), keeping the existing
   green constants as-is.~~ ✅
2. ~~Add a `phosphor` field (enum or int) to `struct vid` in `machine_internal.h`.~~ ✅ (`PhosphorColour` enum)
3. ~~In `video_render_frame()`, select `LIT`/`BG` based on `m->vid.phosphor`.~~ ✅
4. ~~Add `machine_set_phosphor(m, phosphor)` in `machine.c` / `machine.h`.~~ ✅
5. ~~Parse `-phosphor green|white` in `main.c`; call `machine_set_phosphor()`.~~ ✅
6. Wire to the **Phosphor colour** drop-down in the launcher (already stubbed
   in the launcher TODO above).

---

## Floppy

### Write support

The floppy controller is **read-only**.  `floppy_read_data()` streams sector bytes
from the `.dsk` image; `floppy_write_cont()` only handles head-seek stepping.
There is no path that writes sector data back to the image file — port `0x1B` writes
(which the OS uses to send data bytes to the controller) are silently ignored.

**Preferred approach — copy-on-write RAM buffer (non-destructive mode):**

1. At image load time, `malloc` a shadow buffer equal to the full image size and
   `fread` the entire `.dsk` into it.  All `floppy_read_data()` calls serve bytes
   from the shadow buffer instead of directly from the file.
2. Add a write-phase state machine in `floppy_write_cont()` / a new
   `floppy_write_data()` handler (port `0x1B` OUT) that collects 256 bytes +
   checksum and patches the correct offset **in the shadow buffer only** —
   the on-disk image is never touched during the session.
3. Track a `dirty` flag in the FDC state (`fdc.dirty`); set it on the first write.
4. On emulator exit, if `dirty` is set, present a save prompt via SDL message box
   (`SDL_ShowMessageBox`) or a CLI `y/N` question so the user can choose to
   flush the shadow buffer to disk (overwrite the original image), discard
   changes, or save to a new file (keeping the original intact).

An optional `-write-through` flag could bypass the buffer and write directly to
the image file (the simpler `r+b` / `fseek` / `fwrite` path), for users who
prefer permanent writes.

### ~~Second floppy drive (DX1)~~ ✅ Done

The emulator supports two floppy drives. Mount via `-floppy <img>` (DX0) and
`-floppy2 <img>` (DX1). Drive selection uses port `0x19` (IC7 LS475, Plan F5):
DRISEL1 (bit 5 = `0x20`) selects DX0, DRISEL2 (bit 6 = `0x40`) selects DX1;
the latch updates whenever MOTORON (bit 3) is asserted. Stepping and sector reads
respect `fdc.selected_drive`. The status bar shows `DX0:` and `DX1:` labels.

---

## ~~Winchester / Hard Disk~~ ✅ Done

**WD1000/WD1001/WD1002-compatible controller emulated in `src/winchester.c` / `src/winchester.h`.**

Port map (ports `0x20–0x27`, `0x2B`; 6-bit mask `port & 0x3F`):

| Port   | R/W | Description                                                           |
|--------|-----|-----------------------------------------------------------------------|
| `0x20` | R   | Data register (IN) — read next sector byte                            |
| `0x20` | W   | Data register (OUT) — write sector byte (stub, discarded)             |
| `0x21` | R   | Error register — `0x00` = no error                                    |
| `0x21` | W   | Write pre-compensation (ignored)                                      |
| `0x23` | W   | Sector number register — bits[4:0], 0-based                           |
| `0x24` | W   | Cylinder low byte                                                     |
| `0x25` | W   | Cylinder high byte                                                    |
| `0x26` | W   | SDH — bits[2:0]=head (0–5), bit[3]=drive select (0/1)                 |
| `0x27` | R   | Status — `0xFF`=no image, `0x50`=RDY+SC, `0x58`=RDY+SC+DRQ           |
| `0x27` | W   | Command — `0x1n`=RESTORE, `0x2n`=READ SECTOR, `0x3n`=WRITE(stub), `0x7n`=SEEK |
| `0x2B` | W   | Unknown — no-op                                                       |

Geometry (confirmed from Phantom ROM disassembly at `0x0370–0x0398`):
- **6** heads/cylinder, **32** sectors/track, **256** bytes/sector
- CHS→LBA: `cyl×192 + head×32 + sec`; `DE` in Phantom ROM = LBA

Disk images mounted via CLI: `-harddisk <img>` (drive 0), `-harddisk2 <img>` (drive 1).

Status without image → `0xFF` (BSY forever → *Disque inactif* message on boot).
WRITE SECTOR command is a stub (bytes counted but discarded).

---

## USART / Serial

`usart.c` is a bare stub.  The 8251 devices on ports `0x04/0x05` (permanent I/O)
and `0x06/0x07` (cassette) return fixed ready-bits only.  No actual byte transfer.

The paper-tape bootstrap path (ROM `0x046D–0x04C1`) is therefore also non-functional
from the USART side, though it is unreachable under normal boot anyway.

---

## ~~RTC (Real-Time Clock)~~ ✅ Done

**Hardware confirmed from extension board schematic (R. Forster, Oct 1979).**

Chip **E405/08** (IC5), port **0x08** R/W.  Proprietary 3-wire synchronous
bit-bang serial protocol (not SPI — predates the standard).  Bit3=CK,
bits1–2=CS/direction control (held high during transaction), bit0=bidir data.
Command phase: 4 bits LSB-first (0x0F=read, 0x07=write).  Data phase: 7 BCD
bytes LSB-first per byte.

Register layout confirmed empirically from SAMOS display output:

| Byte | Content  | Range  | SAMOS field          |
|------|----------|--------|----------------------|
| 0    | hours    | 00–23  | time **hh**          |
| 1    | minutes  | 00–59  | time **mm**          |
| 2    | day      | 01–31  | date **DD**          |
| 3    | month    | 01–12  | date **MM**          |
| 4    | year     | 00–99  | date **YY**          |
| 5    | weekday  | 1–7    | day name (1=Mon…7=Sun) |
| 6    | seconds  | 00–59  | time **ss**          |

Implemented in `src/rtc.c` / `src/rtc.h`:
- Seeded from host `localtime()` on start
- Frame tick advances seconds every 50 frames (50 Hz)
- Full read/write protocol emulated (command nibble decode, falling-edge MISO
  preload, LSB-first accumulation on write)

---

## ~~Sound / Beeper~~ ✅ Done

Port `0x03` bit 0 drives the buzzer.  Each write calls `sound_set_bit()` which
maps the current T-state frame position (`m->snd.frame_base + m->cpu.cycles`)
to a sample index and fills a per-frame `int16_t[882]` buffer with the previous
level.  `sound_end_frame()` (called by `machine_run_frame`) flushes the frame
buffer to SDL via `SDL_QueueAudio` (push mode — no callback thread race).

This gives sample-accurate buzzer reproduction: a software loop toggling port
0x03 at 2500 Hz produces a 2500 Hz square wave in the audio output.

---

## MAME / FPGA

- **Phase 2**: MAME driver integration (not started)
- **Phase 3**: MiSTer FPGA RTL implementation (not started)

---

## ~~Web / Emscripten~~ ✅ Done

Browser-playable build of the emulator via Emscripten/WebAssembly.
See [web/README.md](web/README.md) for build and serving instructions.

### What works
- **SDL2** — Emscripten built-in port (`-s USE_SDL=2`); no source changes needed.
- **Z80 / Zeta** — pure C, no platform deps; compiles as-is.
- **`tinyfiledialogs` guarded** — `#ifdef __EMSCRIPTEN__` skips the native dialog;
  replaced by JS `<input type="file">` + `FileReader` callback into the virtual FS.
  `EMSCRIPTEN_KEEPALIVE` C functions `smemu6_pick_done()` / `smemu6_pick_cancel()`
  receive the result from JS.  No SDL thread needed on the web.
- **Main loop** — extracted into `main_loop_iter()` with a `MainLoopCtx` struct;
  `emscripten_set_main_loop(main_loop_iter, 0, 1)` replaces `while (running)`.
- **Launcher skipped** — `-no-launcher` is forced on Emscripten; the HTML shell
  provides equivalent controls.
- **ROM preloading** — `--preload-file roms@/roms` (and optionally `floppies`)
  embedded into `smemu6.data` at build time.
- **CMake integration** — `cmake/Emscripten.cmake` toolchain + `CMakePresets.json`
  "web" preset.  Build with `cmake --preset web && cmake --build build-web`.
- **HTML shell** — `web/shell.html` custom Emscripten shell with green-phosphor
  styling, DX0/DX1 file-load buttons, reset button, fullscreen, and stderr log.

### Remaining work
- **SDL launcher in browser** — `launcher_run()` is a blocking event loop and
  cannot run as-is under Emscripten.  To enable it: refactor into
  `launcher_init()` + `launcher_frame()` (called from `emscripten_set_main_loop`);
  when the user clicks Start, cancel the launcher loop, apply the `LauncherConfig`,
  and start the main emulator loop.  `tinyfiledialogs` would be replaced by an
  HTML `<input type="file">` picker (already guarded).  Remove the
  `no_launcher = 1` override in `main.c` for Emscripten once done.
- **Hot-mount** — loading a disk image after the emulator has started currently
  requires a page reload; a `machine_hot_mount()` C export and JS wiring would
  allow live disk swapping without reset.
- **COOP/COEP headers** — hosting requires `Cross-Origin-Opener-Policy: same-origin`
  + `Cross-Origin-Embedder-Policy: require-corp` for `SharedArrayBuffer` (not
  needed for sound or the CPU loop; only needed if SDL threads are ever used).
- **Floppy write support** — see Floppy section above; same gap applies to web.

---

## Distribution / CI

### macOS binary via GitHub Actions

Cross-compiling macOS binaries from Linux requires osxcross + Apple's SDK (needs
a free Apple Developer account and ~10 GB Xcode download) and is impractical.
The recommended approach is a **GitHub Actions** workflow on a `macos-latest`
runner, which builds natively in ~2 minutes at no cost:

```yaml
# .github/workflows/build-macos.yml
jobs:
  macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
      - run: brew install sdl2
      - run: cmake --preset default && cmake --build build -j$(sysctl -n hw.ncpu)
      - uses: actions/upload-artifact@v4
        with:
          name: smemu6-macos
          path: build/smemu6
```

**Code signing / Gatekeeper note:** Without an Apple Developer certificate
(free tier for local use, **$99/year** for distribution), macOS will block the
app on first launch with *"smemu6 can't be opened because it is from an
unidentified developer"*.  Users can bypass it via
*System Settings → Privacy & Security → Open Anyway* or:
```bash
xattr -d com.apple.quarantine smemu6
```
For a seamless install experience, **notarization** (uploading to Apple's
servers for malware scanning, then stapling a ticket to the binary) is required;
this needs the paid Developer account.

**Architecture:** `macos-latest` builds x86_64.  Add an `aarch64` build and
combine with `lipo -create` for a universal binary that runs natively on both
Intel and Apple Silicon Macs.

**Remaining work:**
- Create `.github/workflows/build-macos.yml`
- Optionally add a `dist-mac.sh` script that wraps the binary in a `.app`
  bundle (using `macdeployqt`-style steps or a hand-crafted `Info.plist`)
- Investigate free code-signing / notarization options (e.g. signing with a
  self-signed cert, or using GitHub's own signing infrastructure)

### Windows binary via GitHub Actions

Use a `windows-latest` runner with the MinGW-w64 toolchain already available
on GitHub-hosted runners.  SDL2 is downloaded from the SDL GitHub releases
and cached to avoid re-downloading on every run.

```yaml
# .github/workflows/build-windows.yml
jobs:
  windows:
    runs-on: windows-latest
    defaults:
      run:
        shell: msys2 {0}
    steps:
      - uses: actions/checkout@v4
      - uses: msys2/setup-msys2@v2
        with:
          msystem: MINGW64
          install: >-
            mingw-w64-x86_64-gcc
            mingw-w64-x86_64-cmake
            mingw-w64-x86_64-SDL2
            make
      - run: cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -B build-win .
      - run: cmake --build build-win -j4
      - uses: actions/upload-artifact@v4
        with:
          name: smemu6-windows
          path: |
            build-win/smemu6.exe
            /mingw64/bin/SDL2.dll
```

This avoids the cross-compile complexity entirely by building natively inside
MSYS2/MinGW64 on a Windows runner.  The `SDL2_DIR` preset variable is not
needed here since MSYS2's SDL2 package installs to the standard MINGW64 prefix.

**Remaining work:**
- Create `.github/workflows/build-windows.yml`
- Optionally combine with the macOS workflow into a single
  `.github/workflows/release.yml` that runs on tag push and attaches all
  platform zips as GitHub Release assets

### Linux AppImage via GitHub Actions (x86-64 + ARM)

AppImage bundles the binary and its dependencies (including SDL2) into a
single portable executable that runs on any modern Linux distro without
installation.  Build both `x86_64` and `aarch64` on GitHub-hosted runners
using [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) with its
SDL2 plugin.

```yaml
# .github/workflows/build-linux.yml
jobs:
  appimage:
    strategy:
      matrix:
        include:
          - runner: ubuntu-22.04
            arch: x86_64
          - runner: ubuntu-22.04-arm
            arch: aarch64
    runs-on: ${{ matrix.runner }}
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libsdl2-dev cmake make fuse libfuse2

      - name: Build
        run: |
          cmake --preset default -DCMAKE_INSTALL_PREFIX=/usr
          cmake --build build -j$(nproc)
          DESTDIR=AppDir cmake --install build

      - name: Download linuxdeploy
        run: |
          wget -q "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${{ matrix.arch }}.AppImage"
          chmod +x linuxdeploy-${{ matrix.arch }}.AppImage

      - name: Build AppImage
        run: |
          ./linuxdeploy-${{ matrix.arch }}.AppImage \
            --appdir AppDir \
            --executable build/smemu6 \
            --desktop-file smaky6.desktop \
            --icon-file web/smaky6-256.png \
            --output appimage
        env:
          ARCH: ${{ matrix.arch }}

      - uses: actions/upload-artifact@v4
        with:
          name: smemu6-linux-${{ matrix.arch }}
          path: "*.AppImage"
```

**Notes:**
- `ubuntu-22.04-arm` is a GitHub-hosted ARM64 runner (available on free tier
  for public repos as of 2025).
- A `CMakeLists.txt` install target (`install(TARGETS smemu6 ...)`) and a
  `smaky6.desktop` file are required for linuxdeploy to work correctly.
- `smaky6.desktop` already exists in the repo root (currently untracked —
  needs to be committed).
- linuxdeploy's `--library` flag can be used to bundle additional `.so` files
  if needed (e.g. `libSDL2-2.0.so.0`).

**Remaining work:**
- Commit `smaky6.desktop` (currently untracked)
- Add `install(TARGETS smemu6 DESTINATION bin)` +
  `install(DIRECTORY roms/ DESTINATION share/smemu6/roms)` to `CMakeLists.txt`
- Create `.github/workflows/build-linux.yml`
- Test AppImage on a clean Ubuntu VM and on a Raspberry Pi / ARM board

