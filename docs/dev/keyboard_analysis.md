# Smaky 6 Keyboard Hardware & SAMOS ISR Analysis

## Hardware Model

The Smaky 6 keyboard uses a **dedicated encoder EPROM** that interfaces with the Z80 via two I/O ports:

| Port | Direction | Description |
|------|-----------|-------------|
| 0x00 | IN (read) | **CLA** — Current Latched Key code (7-bit, bit 7 = 0 means key present) |
| 0x01 | IN (read) | **Status** — bit 2 = FOUND (key physically held) |

### Emulator keyboard flags (autoboot/inject path only)

Physical keyboard input bypasses all CLA machinery (see "Emulator Implementation" section below).
The following flags are used **only** by `machine_inject_key()` and the autoboot stages:

- **`found`** — "new key event" latch.  Set by `machine_inject_key()`, cleared by `keyboard_read_cla()` on the first call that returns a key code.  Acts like a one-shot notification.
- **`physically_held`** — injected key-down state.  Set by `machine_inject_key()`, cleared when `key_hold_frames` countdown reaches 0 and no further injection.
- **`key_hold_frames`** — countdown (5 frames) so Stage 2 sees the key as still held.
- **`cla_seen`** — ISR-cycle flag.  Set by every CLA read.  **Cleared by the ISR ACK** (`OUT (0x01), data≠0`) at the start of each 50 Hz frame, so each ISR frame starts with `cla_seen=0`.

Port 0x01 bit 2 (FOUND) reflects `physically_held || key_hold_frames > 0`, not `found`.

CLA read **does** consume the `found` latch, but `physically_held && cla_seen` enables Stage 2
to see the injected key for its circular-buffer write.  This is only relevant during boot (before
SAMOS sets the 0x4582 sentinel and permanently blocks Stage 2).

---

## SAMOS ISR Keyboard Pipeline

The SAMOS interrupt service routine fires via RST 38h at 50 Hz (every ~20 ms).  The full keyboard section occupies addresses `0x014E–0x0197`.

### Stage 1 — Direct CLA read (0x015B–0x016D)

```
015B  LD HL, 0x4580     ; HL → "last-read" register
015E  LD (HL), 0x00     ; clear it
0160  IN A, (0x00)      ; *** CLA READ #1 ***  (does NOT clear FOUND)
0162  BIT 7, A
0164  JR NZ, 016E       ; bit7=1 → no key → go to no-key path
0166  LD (0x457E), A    ; store key code at 0x457E
0169  XOR A
016A  LD (0x4558), A    ; (0x4558) = 0
016D  RET               ; early return — key captured in 0x457E
```

Result when key is pressed: key code stored at `0x457E`, `(0x4558)=0`, return.  
**FOUND is still 1** (hardware latch untouched by the CLA read).

### Stage 2 — No-key path / debounce (0x016E–0x0197)

Only reached when Stage 1's CLA read returned 0x80 (no active key press):

```
016E  AND 0x7F          ; mask bit7 (A = 0)
0170  LD (HL), A        ; (0x4580) = 0
0171  INC HL            ; HL = 0x4581
0172  LD (HL), 0x00
0174  INC HL            ; HL = 0x4582
0175  LD A, (HL)        ; A = (0x4582)  [inter-frame keycode register]
0176  CP 0x80
0178  RET Z             ; if sentinel 0x80: no prior key pending, return
0179  DEC HL            ; HL = 0x4581
017A  LD B, 0           ; B=0 → 256 iterations
017C  DJNZ 017C         ; busy-wait debounce (~2560 T-states)
017E  IN A, (0x01)      ; *** READ STATUS PORT ***
0180  AND 0x04          ; test FOUND bit
0182  RET Z             ; FOUND=0 after debounce → key released, discard
0183  IN A, (0x00)      ; *** CLA READ #2 *** — reads key into buffer path
0185  AND 0x7F
0187  LD B, A           ; B = key code
…    ; → continues into circular buffer management at 0x0188–0x0197
```

### Stage 3 — Circular buffer management (0x019B–0x01DF)

Called after Stage 1 (with key detected) or reached via fall-through after Stage 2.

- Circular buffer base: `0x458A+`, read pointer stored at `0x457C` (init: `0x4596`).
- Keys are written to the buffer with bit 7 SET as a "slot filled" marker.
- The 0x80 sentinel marks empty slots.
- On a new key press, `(0x4558)` is set to `0x23` (35) and the key code is saved to `(0x4577)` for auto-repeat.

### Stage 4 — Auto-repeat (0x01DF–0x0206)

Introduced in SAMOS **version 1.3** (confirmed from release notes). Runs at the end of every ISR frame.

The two key RAM locations are:

| Address | Octal   | Name              | Purpose |
|---------|---------|-------------------|---------|
| `0x4558`| `042530`| Repeat countdown  | Set to `0x23` (35 frames = **700 ms**) on first keypress; decremented each ISR frame; when it hits 0 it is reloaded to `3` (3 frames = **60 ms**) for the fast-repeat rate |
| `0x4577`| `042567`| Repeat key code   | Stores the last key code that was written to the circular buffer; re-injected each time the countdown fires |

```
01DF  LD HL,(0x457C)    ; HL = write pointer
01E2  DEC HL            ; HL → last-written slot
01E3  BIT 7,(HL)        ; test "slot filled" marker
01E5  RET Z             ; buffer empty → no repeat
01E6  LD HL,0x4558      ; HL → countdown
01E9  LD A,(HL)
01EA  OR A
01EB  RET Z             ; countdown=0 → repeat disabled
01EC  DEC (HL)          ; --countdown
01ED  RET NZ            ; not expired yet → wait
01EE  LD (HL),3         ; reload fast rate (3 frames = 60 ms)
01F0  LD A,(0x4577)     ; A = repeat key code
01F4  LD (0x4577),A     ; refresh
01F7  LD B,A            ; B = key code
01F8  LD HL,(0x457C)    ; HL = write pointer
01FB  LD A,(HL)
01FC  CP 0x80           ; next slot free?
01FE  JR Z,0205         ; skip if full
0200  LD (HL),B         ; [ptr] = key code  ← repeat injection
0201  INC HL
0202  LD (0x457C),HL    ; advance pointer
0205  RET
```

**Summary:** Initial delay = 35 × 20 ms = **700 ms**. Fast rate = 3 × 20 ms = **60 ms** (≈ 16.7 chars/sec).  Setting `(0x4558) = 0` disables repeat for that key (used by Stage 1 path at `0x016A` when the key is consumed non-blockingly via syscall 0x0E).

### Syscall consumers

| Syscall | Address | Source buffer |
|---------|---------|---------------|
| 0x0D (blocking char read) | 0x04F6 | Circular buffer at `0x457C`/`0x458A+` |
| 0x0E (non-blocking check) | — | `0x457E` (Stage 1 direct store) |

**The CLI uses syscall 0x0D.**  Therefore keys must travel through Stage 2's CLA Read #2 → circular buffer path.

---

## Why Keys Reach the CLI (the two-stage pipeline)

The SAMOS ISR fires at 50 Hz.  Each frame:

1. **ISR ACK** (`OUT (0x01), 0x08` at `0x0048`): resets `cla_seen=0`.
2. **Stage 1** (CLA read #1 at `0x0160`): `have_key = found || (key_held && cla_seen)`.
   - `cla_seen=0` here, so `have_key = found` only.
   - If `found=1` (key was pressed this frame): returns key code, clears `found`, sets `cla_seen=1`. Stage 1 stores it to `0x457E` and returns early — circular buffer NOT touched.
   - If `found=0`: returns `0x80`, sets `cla_seen=1`. Falls into the no-key path (Stage 2).
3. **Stage 2** (no-key path, CLA read #2 at `0x0183`):
   - Busy-wait 256 iterations.
   - Reads port 0x01: returns `0x08 | (key_held ? 0x04 : 0x00)`. If `key_held=0` → `RET Z` → discard.
   - If `key_held=1`: CLA read #2. `have_key = found || (key_held && cla_seen) = 0 || (1 && 1) = 1`. Returns key code. **Written to the circular buffer** → syscall 0x0D → CLI sees it.

**Summary**: On frame N when key is first pressed, Stage 1 stores to `0x457E`. On frame N+1 (key still held, `found=0`), Stage 1 returns `0x80`, Stage 2 fires and writes to the circular buffer. The CLI's blocking read (syscall `0x0D`) then returns the character.

---

## Emulator Implementation (`src/keyboard.c`, `src/machine.c`)

> **Important architectural note:** ISR Stage 2 is **permanently blocked** after boot.
> At `0x0174–0x0178`, the code does `LD A,(0x4582); CP 0x80; RET Z`.  After the OS
> loads, `(0x4582)` is set to `0x80` (the sentinel) and never changed, so Stage 2
> always returns early.  This means **no physical keypress can reach the circular
> buffer via the hardware ISR path** while SAMOS is running.  The emulator works
> around this by writing directly to the circular buffer from the main loop.

### Two separate delivery paths

| Caller | Path | Destination |
|--------|------|-------------|
| Physical keyboard (`SDL_KEYDOWN`) | FIFO → `keyboard_frame_tick()` → direct write to SAMOS circular buffer | Syscall 0x0D (CLI blocking read) |
| Autoboot / inject (`machine_inject_key()`) | Sets CLA fields (`found`, `key_code`, `physically_held`, `key_hold_frames`) → ISR Stage 1 → `0x457E` | Syscall 0x0E (non-blocking) |

### Physical keyboard: FIFO-based delivery

`keyboard_event()` pushes one key code per `SDL_KEYDOWN` into a 64-slot software FIFO.
SDL key-repeat events (`ev->repeat != 0`) are **discarded** — Stage 4 auto-repeat is not yet
wired up for the FIFO path (see [TODO.md](../../TODO.md)).
The CLA fields (`found`, `key_code`, `physically_held`, `key_hold_frames`) are **not touched** by
`keyboard_event()`.  Touching them would cause ISR Stage 2 (if ever un-blocked) to re-inject
keys each frame, creating loops.

```c
void keyboard_event(struct Smaky6 *m, const SDL_KeyboardEvent *ev)
{
    if (ev->type == SDL_KEYUP) return;          /* FIFO-based: nothing to do on key-up */
    if (ev->type != SDL_KEYDOWN) return;
    if (ev->repeat) return;                     /* discard SDL auto-repeat */

    /* Look up key in table, push to FIFO only: */
    m->kbd.fifo[m->kbd.fifo_tail] = code;
    m->kbd.fifo_tail = (m->kbd.fifo_tail + 1) & 63;
}
```

`keyboard_frame_tick()` is called **once per 50 Hz frame, BEFORE `machine_run_frame()`**.
It drains one key from the FIFO into the SAMOS circular buffer on every frame that
the buffer has space (guard sentinel not reached):

```c
void keyboard_frame_tick(struct Smaky6 *m)
{
    if (!m->cpu.iff1) return;   /* monitor mode: leave FIFO for CLA path */
    if (m->kbd.fifo_head == m->kbd.fifo_tail) return;  /* FIFO empty */
    uint16_t wr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
    if (m->bus[wr] == 0x80u) return;  /* 0x80 = guard sentinel, buffer full */
    uint8_t code = m->kbd.fifo[m->kbd.fifo_head];
    m->kbd.fifo_head = (m->kbd.fifo_head + 1) & 63;
    m->bus[wr] = code & 0x7Fu;
    wr++;
    m->bus[0x457Cu] = (uint8_t)(wr & 0xFFu);
    m->bus[0x457Du] = (uint8_t)(wr >> 8);
}
```

**Why guard-sentinel check (not `wr == 0x4596`)?**

An earlier version checked `wr == 0x4596` (buffer at base = empty) and hardcoded the
write address to `0x4596`.  This silently dropped keys whenever the OS left the write
pointer advanced — for example, while an unconsumed character was still in the buffer,
or if SAMOS delayed resetting the pointer.  Keys typed at normal interactive speed
would be lost.  The correct check mirrors `machine_inject_to_circ_buf()`: write at
`[wr]` (wherever the pointer currently is) and advance, stopping only when the `0x80`
guard sentinel is hit (buffer region full, ~32 chars capacity).

### Autoboot / inject: CLA-based delivery

`machine_inject_key()` (used only by autoboot stages and `-inject-str`) sets:

```c
m->kbd.key_code        = code;
m->kbd.found           = 1;
m->kbd.physically_held = 1;
m->kbd.key_hold_frames = 5;   /* hold for 5 frames so Stage 2 fires */
```

These flow through `keyboard_read_cla()` → ISR Stage 1 → `0x457E`, or
through Stage 2 → circular buffer, depending on the boot phase.

### `keyboard_read_cla()` — used by autoboot path

```c
uint8_t keyboard_read_cla(struct Smaky6 *m)
{
    int key_held = m->kbd.physically_held || (m->kbd.key_hold_frames > 0);
    int have_key = m->kbd.found || (key_held && m->kbd.cla_seen);
    m->kbd.cla_seen = 1;
    if (have_key) {
        m->kbd.found = 0;
        return m->kbd.key_code & 0x7Fu;
    }
    return 0x80u;
}
```

### ISR ACK (machine.c port 0x01 write, data≠0)

```c
case 0x01:
    if (data == 0x00) memory_unprotect_rom(m, 0x0000, 0x0800);
    else m->kbd.cla_seen = 0;   /* ISR frame start: reset CLA-seen flag */
    break;
```

---

## SAMOS Circular Buffer Mechanics (confirmed by trace)

The CLI blocking read (syscall 0x0D, address `0x04D4`) loops calling the peek
routine at `0x04F6` until it returns NZ (key present), then calls consume at `0x04E4`.

**Write** (by `keyboard_frame_tick()` or `machine_inject_to_circ_buf()`):
```
[ptr] = key_code;   ptr++
```
After: `[0x4596] = key`, `ptr = 0x4597`.

**Peek** (`0x04F6`):
```
HL = (0x457C)         ; load write pointer
A  = [HL]             ; read [ptr]
CP 0x80               ; sentinel check
JR Z → dec ptr, store ; ptr==base + [ptr]==0x80 means empty
OR A                  ; set flags
SBC HL, DE            ; HL -= 0x4596 (base)
; returns NZ if ptr > base (data present), Z if ptr == base (empty)
```
Note: peek reads `[ptr]` (the slot *after* the last-written byte), not `[base]`.
After writing one key, `[ptr] = [0x4597]`, which is typically `0x00` from uninitialized
memory — not `0x80`, so the `CP 0x80` guard does not falsely signal empty.

**Consume** (`0x04E4`):
```
dec ptr; store       ; ptr = 0x4596
HL = DE = 0x4596
A = [0x4596]         ; read character
INC HL               ; HL = 0x4597
LDIR (BC = ptr-base = 0)  ; copies 0 bytes
```
After consume: `ptr = 0x4596`, `[0x4596]` retains the consumed character (LDIR did nothing).
`keyboard_frame_tick()` uses the **guard-sentinel check** (`[wr] != 0x80`), not `ptr == 0x4596`.

---

## Notes on `SDL_VIDEODRIVER=dummy`

When `SDL_VIDEODRIVER=dummy` is set (CI / automated test mode), SDL delivers **no real keyboard events**.  Only the autoboot key injection path (`machine_inject_key()`) works.  Interactive keyboard testing requires a real display:

```sh
DISPLAY=:0 ./build/smaky6emu -disk "floppies/1 Systeme_1HComplet.dsk" -autoboot
```

Autoboot stage3 keys are held for 5 frames so Stage 2 can see them (pre-OS boot phase only).

---

## Memory Map Summary

| Address | Purpose |
|---------|---------|
| `0x457E` | Last key code from Stage 1 (used by syscall 0x0E) |
| `0x4558` | Debounce / key-repeat countdown (0x23 = 35 frames) |
| `0x4580` | Stage 2 key code staging register |
| `0x4581` | Secondary staging byte |
| `0x4582` | Inter-frame key sentinel (0x80 = none pending) |
| `0x457C` | Circular buffer **write** pointer (init: 0x4596) |
| `0x458A+` | Circular buffer storage (slots marked with bit 7 when filled) |
| `0x45BF` | Timer countdown register (unrelated to keyboard) |

---

## Key Codes — Hardware Reference (doc section 10.4, pages 213–215)

### Category 1 — "Touches de fonction" (7 function keys, bitmask)

These 7 keys are **not** sent through the key FIFO.  When no regular key is pressed
(FOUND=0), the CLA port returns a 7-bit bitmask — one bit per function key held.
The SAMOS `?GETFON` / `GETFON` system calls read this bitmask.

Codes confirmed from doc section 10.4, page 213 (octal):

| Key     | Octal | Hex    | Bit | Suggested SDL scancode    |
|---------|-------|--------|-----|---------------------------|
| CHANGE  | `001` | `0x01` | 0   | `SDL_SCANCODE_F1`         |
| SEARCH  | `002` | `0x02` | 1   | `SDL_SCANCODE_F2`         |
| SHOW    | `004` | `0x04` | 2   | `SDL_SCANCODE_F3`         |
| COPY    | `010` | `0x08` | 3   | `SDL_SCANCODE_F4`         |
| CURSOR  | `020` | `0x10` | 4   | `SDL_SCANCODE_F5`         |
| PROGRA  | `040` | `0x20` | 5   | `SDL_SCANCODE_F6`         |
| KILL    | `100` | `0x40` | 6   | `SDL_SCANCODE_F7`         |

**Implemented** (git commits step1–step3): `uint8_t fonct_bits` added to `struct kbd`
in `src/machine_internal.h`; `keyboard_event()` sets/clears the bit on SDL key-down/up;
`keyboard_read_cla()` returns `fonct_bits` instead of `0x80` when no regular key is
pending and `iff1=1` (OS running, interrupts enabled). Guard: only returns `fonct_bits`
when `fonct_bits != 0`, so the no-key sentinel `0x80` is still returned when no
function key is held.

### Category 2 — Special keys in the normal FIFO (codes confirmed from doc p.213)

| Key      | Octal | Hex    | Chargen glyph | Suggested SDL scancode    |
|----------|-------|--------|---------------|---------------------------|
| MACRO    | `036` | `0x1E` | `«`           | `SDL_SCANCODE_F8`         |
| DEF(INE) | `037` | `0x1F` | `»`           | `SDL_SCANCODE_F9`         |

`REP / FNCT` (code `0` in the doc) is the hardware repeat/function modifier; it
generates no independent code and can be ignored for now.

**Implemented** (git step1): added to `KEY_TABLE[]` in `src/keyboard.c`:
```c
{ SDL_SCANCODE_F8,  0x1E },   /* MACRO  → «  */
{ SDL_SCANCODE_F9,  0x1F },   /* DEFINE → »  */
```

### Correction keys (doc p.215)

| Oct | Hex    | Name | SDL scancode               | Note                        |
|-----|--------|------|----------------------------|-----------------------------|
| 010 | `0x08` | BS   | `SDL_SCANCODE_BACKSPACE`   | already in `KEY_TABLE[]`    |
| 177 | `0x7F` | DEL  | `SDL_SCANCODE_DELETE`      | implemented (git step1)     |

Note: `0x7F` also renders as the solid-filled block glyph ▓ in the chargen ROM
(confirmed by direct ROM inspection — see [Chargen glyph reference](#chargen-glyph-reference) below).

---

## Character Encoding — Hardware Reference (doc section 10.4, pages 214–215)

The Smaky 6 uses a single-byte encoding in the range `0x00–0x7F`.  Codes `0x00–0x1F`
are **dual-purpose**: the OS, printer driver, and serial handler interpret them as
control/format codes; the chargen ROM renders them as Swiss-French display glyphs.
The emulator must pass the raw byte to the chargen for display **and** let SAMOS
handle the control semantics.

### "Mise en page" (formatting/control) codes — doc p.215

| Oct | Hex    | Name  | Control meaning                  |
|-----|--------|-------|----------------------------------|
| 000 | `0x00` | NUL   | null                             |
| 007 | `0x07` | BEL   | bell — triggers the speaker      |
| 011 | `0x09` | TAB   | horizontal tab                   |
| 012 | `0x0A` | LF    | line feed                        |
| 013 | `0x0B` | VT    | vertical tab                     |
| 014 | `0x0C` | FF    | form feed                        |
| 015 | `0x0D` | CR    | carriage return                  |
| 016 | `0x0E` | Red   | switch to red ink / color        |
| 017 | `0x0F` | Black | switch to black ink              |
| 033 | `0x1B` | ESC   | escape — printer/serial prefix (**not** a keyboard key; chargen renders it as `ä`) |

### DC codes (paper-tape reader/punch controls) — doc p.215

| Oct | Hex    | Name | Meaning     |
|-----|--------|------|-------------|
| 021 | `0x11` | DC1  | Reader on   |
| 022 | `0x12` | DC2  | Aux on      |
| 023 | `0x13` | DC3  | Reader off  |
| 024 | `0x14` | DC4  | Aux off     |

### Communication-request codes — doc p.215

Glyph shapes confirmed by direct inspection of `roms/chargen.rom` (stride 16 bytes/char, 8 rows × 8 cols):

| Oct | Hex    | Name | Standard meaning         | Chargen glyph (from ROM)       |
|-----|--------|------|--------------------------|--------------------------------|
| 001 | `0x01` | SOH  | Start of heading         | empty rectangle (box outline)  |
| 002 | `0x02` | STX  | Start of text            | box outline with dot above     |
| 003 | `0x03` | ETX  | End of text              | decorative cross / diamond     |
| 004 | `0x04` | EOT  | End of transmission      | `<` left-pointing chevron      |
| 005 | `0x05` | ENQ  | Enquiry                  | `>` right-pointing chevron     |
| 006 | `0x06` | ACK  | Acknowledge              | letter-`A` shape               |
| 025 | `0x15` | NAK  | Negative acknowledge     | ê                              |
| 026 | `0x16` | SYN  | Synchronous idle         | ï                              |
| 027 | `0x17` | ETB  | End of transmission block| î                              |
| 030 | `0x18` | CAN  | Cancel                   | ô                              |
| 031 | `0x19` | EM   | End of medium            | ù                              |
| 032 | `0x1A` | SUB  | Substitute               | û                              |

### File-separator codes — doc p.215

| Oct | Hex    | Name | Chargen glyph            |
|-----|--------|------|--------------------------|
| 034 | `0x1C` | FS   | ö                        |
| 035 | `0x1D` | GS   | ç                        |
| 036 | `0x1E` | RS   | « (= MACRO key code)     |
| 037 | `0x1F` | US   | » (= DEFINE key code)    |

---

## Chargen Glyph Reference

The chargen ROM (`roms/chargen.rom`, 2048 bytes) maps 128 7-bit codes to pixel glyphs,
16 bytes per character (rows 0–15; only the top 8–10 rows are typically non-zero).
This is the **Prom 2716** variant documented on page 214.  Do **not** use the
Motorola 6571 or 74S262 tables as a reference — they differ significantly.

#### Swiss-French glyph block `0x0F–0x1F` (confirmed from doc p.214)

| Oct | Hex    | Char | | Oct | Hex    | Char |
|-----|--------|------|-|-----|--------|------|
| 017 | `0x0F` | ü    | | 030 | `0x18` | ô    |
| 020 | `0x10` | à    | | 031 | `0x19` | ù    |
| 021 | `0x11` | â    | | 032 | `0x1A` | û    |
| 022 | `0x12` | é    | | 033 | `0x1B` | ä    |
| 023 | `0x13` | è    | | 034 | `0x1C` | ö    |
| 024 | `0x14` | ë    | | 035 | `0x1D` | ç    |
| 025 | `0x15` | ê    | | 036 | `0x1E` | «    |
| 026 | `0x16` | ï    | | 037 | `0x1F` | »    |
| 027 | `0x17` | î    | | | | |

#### Additional remaps outside standard ASCII printable range (confirmed from doc p.214)

| Oct  | Hex    | Char     | Note                                           |
|------|--------|----------|------------------------------------------------|
| 043  | `0x23` | `#`      | same as ASCII                                  |
| 133  | `0x5B` | `[`      | same as ASCII                                  |
| 134  | `0x5C` | `\`      | same as ASCII                                  |
| 135  | `0x5D` | `]`      | same as ASCII                                  |
| 136  | `0x5E` | `^`      | circumflex accent — only top 3 rows populated (confirmed from ROM) |
| 137  | `0x5F` | `_`      | underscore (shown as `–` in the doc)           |
| 140  | `0x60` | `` ` ``  | backtick                                       |
| 173  | `0x7B` | `{`      | same as ASCII                                  |
| 174  | `0x7C` | `\|`     | same as ASCII                                  |
| 175  | `0x7D` | `}`      | same as ASCII                                  |
| 176  | `0x7E` | `~`      | same as ASCII                                  |
| 177  | `0x7F` | ▓        | solid filled block — cursor glyph (confirmed from ROM) |

#### Selected control-code glyphs confirmed by ROM inspection

| Oct | Hex    | Chargen glyph shape (from ROM bitmap)        |
|-----|--------|----------------------------------------------|
| 000 | `0x00` | mostly blank with two isolated pixels        |
| 001 | `0x01` | empty rectangle (box outline)                |
| 002 | `0x02` | box outline with dot above                   |
| 003 | `0x03` | decorative cross / diamond                   |
| 004 | `0x04` | `<` left-pointing chevron                    |
| 005 | `0x05` | `>` right-pointing chevron                   |
| 006 | `0x06` | letter-`A` shape                             |
| 007 | `0x07` | angular/arrow shape (BEL)                    |
| 010 | `0x08` | crosshair with downward element (BS)         |
| 011 | `0x09` | `←` left-pointing arrow (TAB)               |
| 012 | `0x0A` | `↵` down-and-left arrow (LF)               |
| 013 | `0x0B` | `→` right-pointing arrow (VT)              |
| 014 | `0x0C` | circular / recycled pattern (FF)             |
| 015 | `0x0D` | two isolated pixels (nearly invisible) (CR)  |
| 016 | `0x0E` | snowflake / asterisk-like (Red)              |
| 017 | `0x0F` | two dots above + body = ü (Black)            |
| 036 | `0x1E` | `«` left-pointing double chevrons (MACRO)    |
| 037 | `0x1F` | `»` right-pointing double chevrons (DEFINE)  |

#### SDL mapping for accented characters

On a standard PC keyboard the host OS delivers accented characters as UTF-8
`SDL_TEXTINPUT` events rather than scancodes.  The recommended approach is to add a
`SDL_TEXTINPUT` handler in `keyboard_event()` alongside the existing `SDL_KEYDOWN`
handler: match the Unicode codepoint against a lookup table and push the corresponding
Smaky code into the FIFO.

Example mapping entries needed:

| Unicode | Smaky hex | Char |
|---------|-----------|------|
| U+00FC  | `0x0F`    | ü    |
| U+00E0  | `0x10`    | à    |
| U+00E2  | `0x11`    | â    |
| U+00E9  | `0x12`    | é    |
| U+00E8  | `0x13`    | è    |
| U+00EB  | `0x14`    | ë    |
| U+00EA  | `0x15`    | ê    |
| U+00EF  | `0x16`    | ï    |
| U+00EE  | `0x17`    | î    |
| U+00F4  | `0x18`    | ô    |
| U+00F9  | `0x19`    | ù    |
| U+00FB  | `0x1A`    | û    |
| U+00E4  | `0x1B`    | ä    |
| U+00F6  | `0x1C`    | ö    |
| U+00E7  | `0x1D`    | ç    |
| U+00AB  | `0x1E`    | «    |
| U+00BB  | `0x1F`    | »    |
