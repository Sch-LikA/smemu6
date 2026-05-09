# Smaky 6 Emulator — TODO / Known Gaps

Items are grouped by subsystem.  Entries marked **[confirmed]** have been verified
against disassembly or hardware documentation.

---

## UI / Display

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

The status bar (20 logical px, scales with `-scale`) shows per-drive:
- Amber LED (bright = active transfer, dim = mounted idle, off = no image)
- Drive label glyph (`DX0:` / `DX1:`, from chargen ROM, rendered as 1px-per-bit)
- `T:nn S:nn` — current track and sector from `m->fdc.track[d]` / `m->fdc.phased_sector[d]`

Each chargen glyph bit is rendered as a 2×2 logical-pixel filled rect so glyphs look
crisp at any `-scale` value without needing a separate font.
---

## Keyboard

### Auto-repeat (SAMOS ≥ 1.3)  [confirmed]

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

Alternatively: track the held key and countdown in `struct kbd` and inject repeats
directly from `keyboard_frame_tick()`, bypassing the ISR.  The first approach is
cleaner because it lets SAMOS control the rate.

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

Suggested SDL mapping (F-keys not otherwise used):

| Key     | SDL scancode        |
|---------|---------------------|
| CHANGE  | `SDL_SCANCODE_F1`   |
| SEARCH  | `SDL_SCANCODE_F2`   |
| SHOW    | `SDL_SCANCODE_F3`   |
| COPY    | `SDL_SCANCODE_F4`   |
| CURSOR  | `SDL_SCANCODE_F5`   |
| PROGRA  | `SDL_SCANCODE_F6`   |
| KILL    | `SDL_SCANCODE_F7`   |

**Category 2 — regular FIFO keys with special codes**

These keys send a code through the normal FIFO path (same as letters/digits).
Codes confirmed from doc section 10.4, page 213 (octal):

| Key      | Octal  | Hex    | Glyph | Note                        |
|----------|--------|--------|-------|-----------------------------|
| MACRO    | `036`  | `0x1E` | `«`   | French opening guillemet    |
| DEF(INE) | `037`  | `0x1F` | `»`   | French closing guillemet    |

To implement: add these two entries to `KEY_TABLE[]` in `keyboard.c`:
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
| 010 | `0x08` | BS   | backspace — already mapped (`SDL_SCANCODE_BACKSPACE`) |
| 177 | `0x7F` | DEL  | delete-forward — not yet mapped; also the filled-block glyph in chargen |

To implement: add `{ SDL_SCANCODE_DELETE, 0x7F }` to `KEY_TABLE[]`.

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

The emulator supports two floppy drives. Mount via `-disk <img>` (DX0) and
`-disk2 <img>` (DX1). Drive selection from port `0x19` motor-on+NMI-arm writes
(bits 2+3 both set) sets `fdc.selected_drive`; stepping and sector reads respect
the selected drive. The status bar shows `DX0:` and `DX1:` labels.

---

## ~~Winchester / Hard Disk~~ ✅ Done

**WD1010-compatible controller emulated in `src/winchester.c` / `src/winchester.h`.**

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
| `0x27` | W   | Command — `0x1n`=RESTORE, `0x2n`=READ SECTOR, `0x3n`=WRITE(stub)     |
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
