# CLI.SY — Smaky 6 Command-Line Interface Analysis

Reverse-engineered from `private/extracted/1 Systeme_1HComplet/CLI.SY`.

Analysis date: 2026-05-07 (Phase 1S, following full-boot confirmation in Phase 1Q/1R).

---

## Overview

`CLI.SY` is the interactive command-line shell for the Smaky 6. It is loaded by
SAMOS OS after boot and provides the user-facing `>` prompt with a set of built-in
commands.

| Property         | Value                                          |
|------------------|------------------------------------------------|
| File size        | 6687 bytes (0x1A1F)                            |
| RAM load base    | `0x5600`                                       |
| RAM end          | ~`0x6F1E` (load base + file size − 1)          |
| Confirmed by     | All 40 command-handler addresses in file range |
| File location    | `private/extracted/1 Systeme_1HComplet/CLI.SY`|

---

## Command Table

### Format

The command table lives at file offset `0x0A9F` (RAM `0x609F`). Each entry has the
structure:

```
NAME_BYTE ...  ; one or more ASCII bytes, all < 0x80
0xC3           ; JP opcode (high bit set → terminates name scan)
lo hi          ; 16-bit handler address (little-endian)
flag_byte      ; 0x00 = no argument required, 0x01 = requires argument
```

The name scanner reads bytes until one ≥ `0x80` is found. `0xC3` (= Z80 `JP nn`)
serves a dual purpose: it terminates the name field and is also the first byte of
the actual jump instruction. The `flag_byte` immediately follows and is used by the
dispatcher to validate that an argument was supplied.

### Complete command list (40 entries)

| # | Name     | Handler (RAM) | Flag | Notes                   |
|---|----------|---------------|------|-------------------------|
| 0 | CDIR     | `0x621B`      | 0    | Change/list directory   |
| 1 | HELP     | `0x623F`      | 0    |                         |
| 2 | LINE     | `0x62F0`      | 1    |                         |
| 3 | SET      | `0x6325`      | 1    |                         |
| 4 | **MODE** | **`0x62BF`**  | **0**| Video mode switch       |
| 5 | MSG      | `0x639D`      | 0    |                         |
| 6 | STP      | `0x63AC`      | 0    |                         |
| 7 | SDAY     | `0x6405`      | 0    | Set day                 |
| 8 | STIME    | `0x63F1`      | 0    | Set time                |
| 9 | SDATE    | `0x63DC`      | 0    | Set date                |
|10 | TYPE     | `0x6C5A`      | 1    | Print file              |
|11 | APPEND   | `0x6BA8`      | 0    |                         |
|12 | ENTER    | `0x6493`      | 0    |                         |
|13 | DELETE   | `0x64F2`      | 1    |                         |
|14 | COMPRESS | `0x6578`      | 0    |                         |
|15 | CLEAR    | `0x658D`      | 0    |                         |
|16 | INIT     | `0x65A2`      | 1    |                         |
|17 | LIST     | `0x6676`      | 1    | Directory listing       |
|18 | PRINT    | `0x693B`      | 1    |                         |
|19 | COPY     | `0x6D4B`      | 1    |                         |
|20 | MON      | `0x6D3A`      | 0    | Enter monitor           |
|21 | LOAD     | `0x6A70`      | 0    |                         |
|22 | DX0:/S   | `0x61ED`      | 0    | Drive DX0 source        |
|23 | DX1:/D   | `0x6202`      | 0    | Drive DX1 destination   |
| … | (17 more entries) | | |                         |

### CLI special-key behaviour (confirmed by manual p.13–14)

| Key                   | CLI action                                                           |
|-----------------------|----------------------------------------------------------------------|
| TAB                   | Inserts literal string `DX1:` into the command line                 |
| ESC (UNDO key)        | **Current emulator behaviour:** the emulator now emits top-left key code `0x06` as its working mapping. The older `0x04` cancel-path assumption remains under re-audit against live CLI behaviour. |
| KILL (function key)   | Aborts current peripheral I/O transfer; sends EOF to SAMOS          |
| SHIFT-BREAK           | Hard reset → reboot from DX0:                                       |
| FUNCTION-SHIFT-BREAK  | Hard reset → reboot from DX1: (emulator: not yet implemented)       |

The `TAB → DX1:` shortcut is because the most common cross-drive operation starts
with `DX1:` as the file source or destination.  Users can then append a filename
directly, e.g. `TAB MYFILE.SM` → `DX1: MYFILE.SM`.

### CLI Peripheral device names

I/O peripherals are referenced with a `$` prefix in XFER, APPEND, PRINT, TYPE, COPY:

| Name   | Direction | Hardware                                  |
|--------|-----------|-------------------------------------------|
| `$PR`  | Input     | Paper reader — USART 4 (20 mA current loop) |
| `$PP`  | Output    | Paper punch — USART 4                     |
| `$PI`  | Input     | Parallel interface input                  |
| `$PO`  | Output    | Parallel interface output                 |
| `$MI`  | Input     | Modem in — USART 6                        |
| `$MO`  | Output    | Modem out — USART 6                       |
| `$LP`  | Output    | Line printer (requires overlay LP.SY)     |
| `$KEY` | Input     | Keyboard                                  |
| `$DIS` | Output    | Display                                   |

### I/O instructions in CLI.SY (only 3)

| RAM address | File offset | Instruction    | Purpose                     |
|-------------|-------------|----------------|-----------------------------|
| `0x612C`    | `0x0B2C`    | `OUT (0x64),A` | Unknown (drive-related?)    |
| `0x61CB`    | `0x0BCB`    | `OUT (C),H`    | Unknown                     |
| `0x6F57`    | `0x1957`    | `OUT (0x19),A` | Unknown                     |

**No video-mode writes exist within CLI.SY itself.** All video mode switching is
done through SAMOS OS syscalls (see below).

---

## MODE Command Analysis

### Syntax

```
MODE          — alpha mode (same as MODE A)
MODE A        — alpha-only display
MODE G        — graphics-only display
MODE G P      — graphics, small-points variant
MODE 2        — both alpha and graphics layers active
MODE 2 P      — both layers, small-points variant
```

### Disassembly (handler at RAM `0x62BF`, file `0x0CBF`)

```asm
62bf  CALL 0x5DA4       ; parse argument / skip whitespace
62c2  JR C, 62cb        ; no argument → default to alpha
62c4  CALL 0x5B9D       ; read command character into A
62c7  CP 0x41           ; 'A'?
62c9  JR NZ, 62cf       ; → not A
62cb  RST 20h / 0x11    ; syscall: set alpha mode
62cd  JR 62e3           ; → return to command loop

62cf  CP 0x47           ; 'G'?
62d1  JR NZ, 62da       ; → not G
62d3  CALL 0x62e6       ; check for 'P' sub-arg → carry=1 if 'P'
62d6  RST 20h / 0x12    ; syscall: set graphics mode (carry=P)
62d8  JR 62e3

62da  CP 0x32           ; '2'?
62dc  JR NZ, 62e3       ; → unknown arg, ignore
62de  CALL 0x62e6       ; check for 'P' sub-arg → carry=1 if 'P'
62e1  RST 20h / 0x13    ; syscall: set both modes (carry=P)
62e3  JP 0x56A2         ; return to main command loop

; Subroutine 0x62e6: check for 'P' sub-argument
62e6  INC DE
62e7  CALL 0x5B9D       ; try to read next char
62ea  CP 0x50           ; 'P'?
62ec  SCF               ; carry = 1 if match
62ed  RET Z             ;   → return with carry set
62ee  OR A              ; carry = 0 if no match
62ef  RET
```

---

## SAMOS Syscall Mechanism (RST 20h)

### Dispatch trampoline at 0x0020

The `RST 20h` instruction (opcode `E7`) always jumps to address `0x0020` in SYSMON.
The code there is:

```asm
0020  PUSH HL
0021  LD HL,(0x455C)    ; load dispatcher address (initially 0x012D)
0024  EX (SP),HL        ; replace old HL on stack with dispatcher address
0025  RET               ; jump to dispatcher, with return addr of RST+1 on stack
```

This is the classic Z80 computed-call trampoline. `(0x455C)` is initialized to
`0x012D` during SYSMON startup (`LD HL,0x012D; LD (0x455C),HL` at `0x00D3`).

### Syscall dispatcher at 0x012D

On entry, the stack contains the address of the inline byte (the byte immediately
following the `E7` opcode in the caller's code). The dispatcher:

1. Reads the inline byte (syscall code) from the caller's return address on the stack.
2. Increments the stacked return address past the inline byte so the caller resumes
   at the instruction after the inline byte.
3. Multiplies the code by 2 (`SLA A`) to get a word-table offset.
4. If carry from `SLA` (code ≥ `0x80`): uses alternate table from `(0x455A)`.
5. Otherwise: uses default table at `0x0F30`.
6. Dispatches to the handler address read from the table.

```asm
012d  DI
012e  LD (0x4554),A      ; save A (scratch)
0131  EX (SP),HL         ; HL ↔ inline_byte_addr (stack top)
0132  LD A,(HL)          ; A = syscall code
0133  INC HL             ; advance past inline byte → true return address
0134  EX (SP),HL         ; restore true return addr to stack top
0135  PUSH HL            ; push original HL (caller's HL)
0136  PUSH AF            ; push syscall code in A
0137  SLA A              ; A = code×2 (table word index)
0139  LD HL,(0x455a)     ; HL = alternate table base (or 0 if not set)
013c  JR C, 0141         ; if carry (code ≥ 0x80), use alternate table
013e  LD HL,0x0f30       ; else use default table
0141  ADD A,L            ; HL += offset
0142  LD L,A
0143  LD A,(HL)          ; fetch handler lo
0144  INC HL
0145  LD H,(HL)          ; fetch handler hi
0146  LD L,A             ; HL = handler address
0147  POP AF             ; restore syscall code
0148  LD A,(0x4554)      ; restore original A
014b  EX (SP),HL         ; HL (handler addr) → stack top; HL = caller's original HL
014c  EI
014d  RET                ; jump to handler
```

### Syscall table (first 0x20 entries, at 0x0F30)

| Code | Handler  | Used by              |
|------|----------|----------------------|
| 0x00 | `0x0424` |                      |
| 0x05 | `0x051B` |                      |
| 0x06 | `0x0485` | SYSMON startup       |
| 0x10 | `0x02FE` |                      |
**PROM contradiction note (2026-05-13):** external S471 PROM decoding suggests
the top-left physical ESC/UNDO position emits `0x06` on the normal layer, not
`0x04`. The emulator has now been switched to that `0x06` working mapping, but the
older CLI audit still needs to be reconciled against live runtime before this area
can be treated as settled hardware behaviour.

| 0x11 | `0x02DE` | **MODE A** (alpha)   |
| 0x12 | `0x02E6` | **MODE G** (graphics)|
| 0x13 | `0x02EE` | **MODE 2** (both)    |
| 0x1C | `0x025E` | SYSMON startup       |
| 0x1D | `0x0528` |                      |
| …    | …        |                      |

---

## Video Mode Switching — Hardware Analysis

### Shadow register and hardware port

`(0x457F)` is a RAM shadow of the video mode hardware register. All mode changes:
1. Modify `(0x457F)` in RAM.
2. Write the new value to **I/O port `0x00`** via `OUT (0x00),A`.

Initialization (SYSMON startup, around `0x00E1`):
```asm
00e1  LD A,0x01
00e3  OUT (0x00),A        ; set hardware to alpha-only mode
00e5  LD (0x457F),A       ; update RAM shadow
```

### Mode syscall disassembly

All three mode syscalls share a common tail at `0x02B9`:
```asm
02b9  LD (0x457F),A       ; update shadow
02bc  OUT (0x00),A        ; **write to video mode port 0x00**
02be  POP AF
02bf  RET
```

**Syscall 0x11 — MODE A (alpha-only):**
```asm
02de  PUSH AF
02df  LD A,(0x457F)
02e2  AND 0xF3            ; clear bits 2 and 3  (11110011 mask)
02e4  JR 0x02B9           ; → write and return
```

**Syscall 0x12 — MODE G (graphics-only, optional 'P'):**
```asm
02e6  PUSH AF
02e7  LD A,(0x457F)
02ea  SET 3,A             ; set bit 3
02ec  JR 0x02F4           ; → shared suffix
```

**Syscall 0x13 — MODE 2 (both layers, optional 'P'):**
```asm
02ee  PUSH AF
02ef  LD A,(0x457F)
02f2  RES 3,A             ; clear bit 3
; fall through to shared suffix
```

**Shared suffix at 0x02F4 (used by 0x12 and 0x13):**
```asm
02f4  SET 2,A             ; set bit 2
02f6  SET 1,A             ; set bit 1 (tentative 'P' flag)
02f8  JR C, 0x02B9        ; carry (= 'P' arg) → keep bit 1 → write
02fa  RES 1,A             ; no 'P' → clear bit 1
02fc  JR 0x02B9           ; → write and return
```

### Port 0x00 bit encoding

| Bit | Mask | Meaning                                    |
|-----|------|--------------------------------------------|
|  0  | 0x01 | Display enable (always 1 during operation) |
|  1  | 0x02 | Small-points mode ('P' flag)               |
|  2  | 0x04 | Graphics layer active                      |
|  3  | 0x08 | Graphics-only (suppress alpha layer)       |

### Resulting port values per MODE command

| Command   | Port 0x00 value | Description                          |
|-----------|-----------------|--------------------------------------|
| `MODE A`  | `0x01`          | Alpha-only (graphics off)            |
| `MODE G`  | `0x0D`          | Graphics-only (`0x01\|0x04\|0x08`)   |
| `MODE G P`| `0x0F`          | Graphics-only + small points         |
| `MODE 2`  | `0x05`          | Both layers (`0x01\|0x04`)           |
| `MODE 2 P`| `0x07`          | Both layers + small points           |

### Correction: OUT(0x06) false positives

An earlier naive byte scan of `SYS.SY` reported `OUT (0x06),A` at RAM addresses
`0x0613` and `0x065B`. These were **false positives**: the byte `0xD3` (the `OUT`
opcode) appeared as the low byte of jump targets in `JP 0x06D3` and `CALL 0x06D3`
instructions, not as standalone `OUT` opcodes. Port `0x06` is **not** the video
mode register.

---

## Key Workspace Addresses (CLI.SY context)

| Address  | Role                                              |
|----------|---------------------------------------------------|
| `0x455C` | RST 20h dispatcher vector (init to `0x012D`)      |
| `0x455A` | RST 20h alternate syscall table pointer           |
| `0x4554` | Scratch register used by syscall dispatcher       |
| `0x457F` | Shadow of I/O port 0x00 (video mode register)     |
| `0x5600` | CLI.SY load base                                  |
| `0x609F` | CLI.SY command table (file offset `0x0A9F`)       |
| `0x62BF` | CLI.SY MODE command handler                       |
| `0x56A2` | CLI.SY main command loop return point             |
| `0x0F30` | SAMOS default syscall dispatch table              |
| `0x012D` | SAMOS syscall dispatcher entry point              |

---

## Emulator Implications

1. **Port 0x00 is implemented** (`src/machine.c` `case 0x00:` write handler).
   The handler decodes the four bits and calls `video_set_mode()` accordingly.
   Writing 0x00 (display off) is currently a no-op (no blank mode in `VideoMode`).
   Port 0x00 writes are **video-only**.  Physical keyboard input bypasses CLA entirely
   (see `docs/dev/keyboard_analysis.md`): `keyboard_event()` pushes to a software FIFO,
   and `keyboard_frame_tick()` writes one key per frame into the SAMOS circular buffer at
   the current write pointer, stopping only when the `0x80` guard sentinel is hit (buffer
   full).  CLA is only used by the autoboot/inject path (`machine_inject_key()`).
   SAMOS syscall `0x0D` (the blocking read the CLI uses) polls the circular buffer write
   pointer at `0x457C`; when it equals the base `0x4596` the buffer is empty.

2. **`(0x457F)` is the shadow.** If the emulator needs to query the current mode
   at runtime (e.g., for scanline rendering decisions), reading RAM address `0x457F`
   is the cheapest way to retrieve the current mode byte.

3. **No display-mode writes in CLI.SY itself.** CLI.SY always delegates to SAMOS
   OS syscalls for mode switching. The only I/O in CLI.SY (`OUT (0x64)`, `OUT (C)`,
   `OUT (0x19)`) is unrelated to video.
