# SYS.SY — Smaky 6 System File Analysis

Reverse-engineered from `private/decoded/1 System 1H complet avec appli inconnue.img`
and extracted to `/tmp/SYS.SY` via `../smaky6-tools/smaky6_samos.py`.

Analysis date: 2026-05 (Phase 1J / Phase 1K / Phase 1S of emulator project).

Runtime update: 2026-05-07 (Phase 1Q / Phase 1R) — full boot to CLI directory listing confirmed.

Error-code update: 2026-05-07.

---

## Error Code Decode (ER.SY)

The extracted image [private/extracted/1 Systeme_1HComplet](private/extracted/1%20Systeme_1HComplet) includes
`ER.SY`, which stores the human-readable error table.

### Encoding format

Each message entry starts with one byte `0x80 | code`, followed by a zero-terminated
French text string.

### Complete error code table (from manual p.24 + ER.SY French strings)

| Code (dec) | Code (oct) | English message (manual)  | French string (ER.SY)          |
|------------|------------|---------------------------|--------------------------------|
|  1         | 001        | Write protect file        | fichier protégé écriture       |
|  2         | 002        | Read protect file         | fichier protégé lecture        |
|  4         | 004        | Permanent file            | fichier permanent              |
|  5         | 005        | Line too long             | ligne trop longue              |
|  6         | 006        | End of file               | fin de fichier                 |
|  7         | 007        | File end overflow         | dépassement fin de fichier     |
| 10         | 012        | File in use for writing   | fichier ouvert en écriture     |
| 11         | 013        | File already exist        | fichier déjà existant          |
| 12         | 014        | File does not exist       | **fichier inexistant**         |
| 13         | 015        | Illegal filename          | nom de fichier illégal         |
| 14         | 016        | Illegal reservation       | réservation illégale           |
| 16         | 020        | Cannot load file          | chargement impossible          |
| 17         | 021        | Out of file               | plus de fichier                |
| 20         | 024        | File in use for reading   | fichier ouvert en lecture      |
| 21         | 025        | Unknown device            | périphérique inconnu           |
| 22         | 026        | Channel error             | erreur de canal                |
| 23         | 027        | File(s) in use            | fichier(s) en cours            |
| 24         | 030        | All channels in use       | tous les canaux occupés        |
| 25         | 031        | Directory full            | répertoire plein               |
| 26         | 032        | Disk full                 | disque plein                   |
| 30         | 036        | Device timeout            | timeout périphérique           |
| 31         | 037        | Write protect tab set     | languette de protection        |
| 32         | 040        | Write error               | **erreur d'écriture**          |
| 33         | 041        | Read error                | **erreur de lecture**          |
| 34         | 042        | No starting address       | pas d'adresse de départ        |
| 35         | 043        | Bad load                  | **chargement erroné**          |
| 36         | 044        | Buffer full               | tampon plein                   |
| 110        | 156        | Illegal order             | ordre illégal                  |
| 114        | 162        | System error              | erreur système                 |
| 115        | 163        | Map error                 | erreur de map                  |

**Note**: The CLI displays errors in **octal** (`ERROR 033` = decimal 27 = `0x1B`).
This tripped us up early — `0x4554=0x1B` at ERROR 033 is the octal display value 33₈ = 27₁₀.

### Confirmed runtime mapping

- `ERROR 033` (octal) = decimal 27 = `0x1B` → **not in the table above** — this is not a standard ER.SY code; it is the octal display of `0x1B` (value stored at `0x4554`). Resolved as an emulator FDC bug, not an OS error.
- `ERROR 035` (octal) = decimal 29 = `0x1D` → **Bad load** (`chargement erroné`) — seen when `selected_drive` was corrupted by step-command bytes; fixed in Phase 1Q.
- `ERROR 012` (octal) = decimal 10 → **File in use for writing** — or **File does not exist** (code 12 decimal = 014 octal); context-dependent.

This confirms that `ERROR 035` = *Bad load* is a real I/O failure class,
consistent with the post-relocation floppy read path analysis and the drive-selection bug fix.

### Drive naming reminder (Smaky conventions)

- `DX0` = first floppy drive (boot drive)
- `DX1` = second floppy drive

This mapping is important when interpreting monitor/CLI commands such as `DX0:/S`
and `DX1:/D`, and when correlating emulator drive-select bits with boot behavior.

---

## Overview

`SYS.SY` is the primary system file on every bootable Smaky 6 floppy disk. It is
located in the on-disk directory as a "SY"-type entry (special loader type). The
Phantom ROM bootloader reads it from sectors 3–38 of track 0 and distributes its
content into two separate RAM regions.

| Property          | Value                                              |
|-------------------|----------------------------------------------------|
| File size         | 8960 bytes (0x2300)                                |
| Sectors occupied  | 35 sectors × 256 bytes = 8960 bytes                |
| Disk start        | Track 0, sector 3 (after 3 directory sectors)      |
| Directory `load`  | 0x60C0 — sector/track encoding for SY-type files,  |
|                   | **NOT** a RAM load address                         |
| Directory `entry` | 0x5700 — likely the OS-loader stub entry, not the  |
|                   | OS binary load address (see below)                 |

---

## Binary Layout

```
Offset 0x0000–0x07FF  (2048 bytes)  SYSMON monitor
Offset 0x0800–0x22FF  (7168 bytes)  SAMOS OS proper
```

### Part 1 — SYSMON (0x0000–0x07FF)

SYSMON is the Smaky 6 monitor / debugger. It has the same RST-table structure as
the Phantom ROM but with different targets.

After the Phantom ROM is bank-switched out (via `OUT(0x01),A=0`), the CPU
can write to RAM at 0x0000–0x07FF. The OS-loader stub LDIRs the SYSMON section
of SYS.SY to RAM 0x0000, installing it in place of the now-hidden Phantom ROM.

**SYSMON RST table** (from SYS.SY bytes 0x0000–0x003F, confirmed from raw binary):

| RST    | Raw bytes (6–7)              | Decoded                                          | Via pointer  |
|--------|------------------------------|--------------------------------------------------|--------------|
| RST 00 | `F3 31 30 45 C3 05 01`       | `DI; LD SP,0x4530; JP 0x0105`                   | —            |
| RST 08 | `E5 2A 62 45 E3 C9`          | `PUSH HL; LD HL,(0x4562); EX (SP),HL; RET`      | `(0x4562)`   |
| RST 10 | `E5 2A 64 45 E3 C9`          | `PUSH HL; LD HL,(0x4564); EX (SP),HL; RET`      | `(0x4564)`   |
| RST 18 | `E5 2A 4C 45 E3 C9`          | `PUSH HL; LD HL,(0x454C); EX (SP),HL; RET`      | `(0x454C)`   |
| RST 20 | `E5 2A 5C 45 E3 C9`          | `PUSH HL; LD HL,(0x455C); EX (SP),HL; RET`      | **`(0x455C)`** — **SAMOS syscall dispatcher** |
| RST 28 | `E5 2A 68 45 E3 C9`          | `PUSH HL; LD HL,(0x4568); EX (SP),HL; RET`      | `(0x4568)`   |
| RST 30 | `E5 2A 6A 45 E3 C9`          | `PUSH HL; LD HL,(0x456A); EX (SP),HL; RET`      | `(0x456A)`   |
| RST 38 | `E5 2A 66 45 E3 C9`          | `PUSH HL; LD HL,(0x4566); EX (SP),HL; RET`      | `(0x4566)`   |

**RST 20h = SAMOS syscall:** `(0x455C)` is initialized to `0x012D` (the syscall
dispatcher) by SYSMON startup at `0x00D3`: `LD HL,0x012D; LD (0x455C),HL`.
The dispatcher reads an **inline byte** immediately following the `E7` opcode from
the caller's return address on the stack, uses it as a word-table index into the
default table at `0x0F30`, and jumps to the handler. The inline byte is the syscall
code. See `docs/CLI_SY_analysis.md` for a full annotated disassembly.

SYSMON vectors at 0x0000–0x003F are all indirect calls through RAM pointers in the
0x4530–0x456F workspace. This makes SYSMON dynamically reconfigurable.

**SYSMON I/O ports used** (from SYS.SY bytes 0x0000–0x01FF):

| Offset | Addr  | Instruction       | Notes                                           |
|--------|-------|-------------------|-------------------------------------------------|
| 0x0048 | 0x0048| `OUT (0x01),A`    | Phantom ROM bank-switch                         |
| 0x00E3 | 0x00E3| `OUT (0x00),A`    | **Video mode init** (value 0x01 = alpha-only enable) |
| 0x02BC | 0x02BC| `OUT (0x00),A`    | **Video mode set** (syscall tail — all mode changes) |
| 0x0158 | 0x0158| `OUT (0x19),A`    | Floppy control                                  |

#### SYSMON 1-H monitor strings and command decoder

The extracted `SYS.SY` used by the current boot floppy is a **1-H** monitor build,
not the earlier `1-0` variant.

**Verified embedded strings in the SYSMON / early OS image:**

| SYS.SY offset | String | Notes |
|---------------|--------|-------|
| 0x07EC | `MON 1-H` | Monitor version string inside the 0x0000–0x07FF SYSMON portion |
| 0x07F4 | `HEXA` | Numeric-display mode label |
| 0x0C1A | `LOAD` | Loader/serial input path string |
| 0x0D72 | `OCTAL` | Alternate numeric-display mode label |
| 0x0ACC | `SZ-H-VNC  A   B   C   D   E    DE     HL   IX ^SP IY ^(SP) I PC` | Register dump header |
| 0x1094 | `SAMOS 1-H` | Matching OS-family banner |

**Strings not present in this image:**
- `MONITEUR 1-0`
- `adresse de début`

This strongly suggests the current floppy belongs to a later `1-H` system set and
that the older French monitor prompts come from a different system revision.

**Verified monitor entry/dispatch points** (from relocated disassembly `roms/syssy_ram_0000_22ff.asm`):

| RAM addr | Role |
|----------|------|
| 0x0941 | Main monitor command-loop setup |
| 0x0976 | Compare first command byte with `'O'` |
| 0x097B | Compare first command byte with `'S'` |
| 0x0991 | Compare with `'='` |
| 0x0996 | Compare with `'P'` |
| 0x099B | Compare with `'M'` |

**Command behavior we can support from data flow:**
- `'O'` toggles the numeric display base at `0x0BBA` by flipping bit 6 of workspace byte `0x454A`;
  the monitor then prints either `OCTAL` (`0x0D72`) or `HEXA` (`0x07F4`).
- Separate monitor documentation states that **`0x5600`** and **`0x4100`** are common starting
  addresses for monitor work. `0x4100` is the screen-visible choice because it lies in the alpha
  display RAM range (`0x4000–0x44FF`). This is consistent with the current `1-H` image, whose
  `M` path contains a literal `LD DE,0x4100` at `0x0BAA`.
- `'S'` jumps to `0x0A49`, loads `A=E`, then executes `CPIR` with `BC=0x0000`.
  On success it stores the found address back to workspace `0x4544` and returns to the update path.
  Strong evidence: **search memory for a byte value**.
- `'P'` jumps to `0x0C90`, a formatted print/dump path that uses helper `0x0C71` and emits output
  through the common `RST 20` character path.
- `'M'` jumps to `0x0B9A`, rearranges parsed arguments, and performs `LDIR`.
  Strong evidence: **move/copy a memory block**.
- `LOAD` is associated with the routine at `0x0C28`, which consumes a byte stream via helpers
  `0x0BDA`/`0x0BD3` and echoes bytes through port `0x03`, consistent with a serial or paper-tape
  loader entry exposed by the monitor.

The long register-label string at `0x0ACC` matches the register display path around
`0x0AAD–0x0B44`, so SYSMON clearly retains a monitor/debugger role in this `1-H` image.

### Part 2 — SAMOS OS (0x0800–0x22FF)

The OS section loads at **RAM 0x0800** (not 0x5700). This was confirmed by
analysing the JP targets in the first 128 bytes of the OS section: all absolute
addresses land either in SYSMON range (0x0000–0x07FF) or OS range (0x0800–0x22FF),
never in the gap between 0x2300 and 0x5700.

**OS jump table** (at RAM 0x0800, SYS.SY file offset 0x0800):

| RAM addr | JP target | Classification |
|----------|-----------|----------------|
| 0x0800   | 0x0941    | OS internal    |
| 0x0803   | 0x094D    | OS internal    |
| 0x0806   | 0x0A5F    | OS internal    |
| 0x0809   | 0x011D    | SYSMON call    |
| 0x080C   | 0x0941    | OS internal    |
| 0x0818   | 0x0BDA    | OS internal    |
| 0x081B   | 0x0095    | SYSMON call    |
| 0x081E   | 0x0965    | OS internal    |
| 0x0821   | 0x0BD3    | OS internal    |

The OS makes direct CALL/JP into SYSMON (0x011D, 0x0095, etc.), so SYSMON must
be present in RAM 0x0000–0x07FF before any OS code executes.

---

## I/O Ports Found in SYS.SY

### SYSMON section (0x0000–0x07FF)

| Port  | Dir | Address       | Description                                              |
|-------|-----|---------------|----------------------------------------------------------|
| 0x00  | W   | 0x00E3        | **Video mode control** — init write (alpha-only, value 0x01) |
| 0x00  | W   | 0x02BC        | **Video mode control** — all runtime mode-switch writes  |
| 0x01  | W   | 0x0048        | **Dual function:** `data=0x00` → Phantom ROM bank-switch (disable 2 KB ROM, make 0x0000–0x07FF writable RAM). `data≠0x00` → **ISR ACK** — resets the emulator's `cla_seen` flag so Stage 2 can fire in the next ISR frame. The SAMOS 50 Hz ISR writes a non-zero value here at the start of every interrupt. |
| 0x19  | W   | 0x0158        | Floppy control register                                  |

### OS section (0x0800–0x22FF)

| Port  | Dir | RAM addr | Description                                                           |
|-------|-----|----------|-----------------------------------------------------------------------|
| 0x00  | R   | 0x0960   | **Keyboard CLA** — reads current latched key code (bit 7=0 → key present) |
| 0x00  | R   | 0x0983   | **Keyboard CLA** — second read in ISR debounce path (feeds circular buffer) |
| 0x01  | R   | 0x097E   | **Keyboard status** — bit 2 = FOUND (re-checked after debounce wait). Per §10.4 CLAVIER: "not necessary" since bit 7 of CLA already encodes FOUND. |
| 0x00  | W   | 0x09D8   | **Video mode control** — restore saved mode from workspace `(0x4549)` |
| 0x00  | W   | 0x11D6   | **Video mode control** — restore shadow `(0x457F)` after ISR          |
| 0x00  | W   | 0x192B   | **Video mode control** — hard reset / init, writes alpha (0x01)       |
| 0x00  | W   | 0x1FD9   | **Video mode control** — display off, writes 0x00 (`XOR A`)           |

> **Note:** port `0x00` READS (IN A,(0x00) / `keyboard_read_cla`) are the
> keyboard CLA path and have nothing to do with video.
> Port 0x00 writes are unrelated to keyboard; they are video-mode control only.
>
> **Post-OS keyboard path:** After SAMOS loads, the ISR Stage 2 path that writes
> to the circular buffer is **permanently blocked** by `(0x4582) == 0x80` (set at OS
> init and never changed).  Physical keypresses therefore bypass the ISR entirely:
> `keyboard_event()` pushes codes to a FIFO, and `keyboard_frame_tick()` drains the
> entire FIFO each frame into the circular buffer at the current write pointer
> (`(0x457C)`), stopping only when the `0x80` guard sentinel is hit.
>
> `keyboard_frame_tick()` also sets the `samos_loaded` flag once it detects that
> SAMOS has written its 50 Hz ISR vector to `(0x4566)` (`== 0x003E`).  This happens
> at `0x00CD` in SYS.SY, **after** the SAMOS boot-menu keyboard wait at `0x00B5`.
> Before `samos_loaded` is set, the `iff1=0` branch of `keyboard_read_cla()` is
> allowed to pop the FIFO (for physical keys in monitor mode); after it is set,
> `iff1=0` means "inside the SAMOS ISR" and the FIFO must not be touched there.
>
> The CLA port read path is used only during the pre-OS boot phase: Phantom ROM
> `kbd_wait` at `0x00FD` (device-selection prompt) and SAMOS init `kbd_wait` at
> `0x00B5` (boot-menu / second keypress), both of which poll `IN A,(0x00)` with
> interrupts disabled.  After SAMOS is running, the autoboot injection path
> (`machine_inject_key()`) also uses CLA fields.
> See `docs/dev/keyboard_analysis.md` for the full pipeline description.
| 0x03  | W   | 0x5B0F          | Buzzer/beep                                      |
| 0x08  | R/W | 0x5D58–0x5DCA  | **3-wire synchronous serial (E405/08 RTC)** (see detail below) |
| 0x0B  | W   | 0x5ADB–0x5B37  | **Bit-serial shift clock** (see detail below)     |
| 0x18  | W   | 0x7010–0x703F  | Floppy-related byte stream (bit7 ready; `IN F,(C)` poll) |
| 0x19  | R/W | multiple        | Floppy control (same as ROM)                     |
| 0x1A  | W   | 0x70A2          | Floppy CONT                                      |
| 0x1C  | W   | 0x63C0, 0x6583, 0x6594 | Unknown — disk DMA or acknowledge         |
| 0x2B  | W   | 0x6B8E, 0x6DAB  | Winchester reset/select                          |

#### Port 0x08 — 3-wire synchronous serial interface (E405/08 RTC)

This is a **proprietary Epsitec protocol**, not SPI (SPI was standardized by
Motorola in the mid-1980s; this machine predates it).  Confirmed target:
**E405/08 RTC chip** (IC5 on extension board, “Horloge absolue”).

Data bit: **bit 0** is the bidirectional I/O line (MISO on IN, data-out via
bit0 on OUT).  Bits 1–2 are direction/CS control held high during a
transaction.  Bit 3 is CLK.  The comment `SET 2,A; SET 1,A` sets the
CS/direction lines; the data bit is placed at bit0 via `RR C; RLA`.

Command phase: 4 bits, **LSB first** (C=0x0F for read, C=0x07 for write).
Data phase: 7 BCD bytes, **LSB first** per byte.
Z80 receive: `RRA; RR (HL)` → first received bit lands at bit0 of `(HL)`.

Protocol (disassembled from OS ~0x5D53–0x5D9A):

```
PUSH DE, PUSH BC, PUSH AF
EX DE,HL
XOR A
OUT (0x08),A          ; reset (clock/data = 0)

LD B, 4               ; transmit 4 bits
LD C, 0x0F            ; C = mask for data nibble
transmit_loop:
  SET 2, A            ; bit 2 = MOSI (data out)
  CB CF               ; SET 1, A  [clock bit setup]
  RRA                 ; shift data bit from C into A via carry
  CB 19               ; RR C      [advance source bit]
  RLA                 ; shift into position
  OUT (0x08), A       ; clock low, data valid
  EX (SP),HL          ; delay (~19 cycles)
  EX (SP),HL
  CB DF               ; SET 3, A  ; bit 3 = CLK high
  OUT (0x08), A       ; clock high (latch data)
  EX (SP),HL          ; delay
  EX (SP),HL
  CB 9F               ; RES 3, A  ; clock low
  DJNZ transmit_loop

LD C, 7               ; 7 bytes of receive
LD B, 8               ; 8 bits per byte
receive_loop:
  OUT (0x08), A
  EX (SP),HL
  EX (SP),HL
  PUSH AF
  IN A, (0x08)         ; read MISO on bit 0
  RRA
  CB 1E                ; RR (HL)  ; shift bit into memory
  POP AF
  CB DF                ; SET 3,A  ; clock high
  OUT (0x08), A
  EX (SP),HL
  EX (SP),HL
  CB 9F                ; RES 3,A  ; clock low
  DJNZ receive_loop
  INC HL
  DEC C
  JR NZ receive_loop
XOR A
OUT (0x08), A          ; deselect
```

**Bit assignments for port 0x08 (confirmed from R. Forster schematic, Oct 1979):**

| Bit | Direction | Function                                |
|-----|-----------|-----------------------------------------|
| 0   | Bidir     | Serial data (MISO on IN, data on OUT)   |
| 1   | Output    | CS / direction control (high during tx) |
| 2   | Output    | CS / direction control (high during tx) |
| 3   | Output    | CLK                                     |

**Target confirmed**: E405/08 RTC (IC5, extension board).

Register layout (7 bytes, BCD, confirmed empirically from SAMOS display output):

| Byte | Content  | Range  | SAMOS field            |
|------|----------|--------|------------------------|
| 0    | hours    | 00–23  | time **hh**            |
| 1    | minutes  | 00–59  | time **mm**            |
| 2    | day      | 01–31  | date **DD**            |
| 3    | month    | 01–12  | date **MM**            |
| 4    | year     | 00–99  | date **YY**            |
| 5    | weekday  | 1–7    | day name (1=Mon…7=Sun) |
| 6    | seconds  | 00–59  | time **ss**            |

#### Port 0x0B — Bit-serial shift clock

Protocol (disassembled from OS ~0x5ADA–0x5B10):

```
wait_loop:
  CALL 0x0BD3         ; = RST 20h (E7), then bytes 0x14, 0x47, 0x83, 0x5F, 0x78 = LD B,A
  OUT (0x0B), A       ; strobe: output one bit
  LD A, B
  CP 0x00
  JR Z, wait_loop     ; loop until count reaches 0
  DEC A
  JR NZ, wait_loop
```

Used to transfer multi-byte values bit-serially. The subroutine at 0x0BD3 wraps
each byte transmission; 8–12 consecutive calls per multi-byte message.
Target peripheral: unknown serial device.

#### Port 0x18 — Floppy-related byte stream

Protocol (disassembled from OS ~0x7008–0x7040):

```
LD A, 0x28
LD B, 0x28            ; 40 bytes to transfer
LD C, 0x18            ; port number in C
byte_ready:
  IN F,(C)            ; test bit 7 of port 0x18 (sign flag)
  JP P, byte_ready    ; loop while bit7 = 0 (not ready)
  OUT (0x18), A       ; send data byte
  DJNZ byte_ready     ; next byte
```

Identical ready-polling pattern to the Phantom ROM's `IN F,(C)` with C=0x1A.
Almost certainly a second floppy data channel or a Winchester DMA port.

---

## Boot Flow After Phantom ROM

### Normal boot path

```
Phantom ROM (0x0000–0x07FF, ROM)
  │
  ├─ boot_main (0x003B): keyboard probe → drive-control byte stored at (0x4500)
  ├─ floppy_seek_sys (0x016E): OUT(0x19) seek; poll bit6 until settled → NC
  ├─ boot_menu_setup (0x0090):
  │    ├─ LDIR 12 bytes from ROM[0x0137] → RAM 0x57C0  (the handoff stub)
  │    └─ call block_copy (RST 20h / 0x01EE)
  └─ block_copy (0x01EE):
       ├─ setup_sector (0x021D): OUT(0x19) motor-on; poll bit5 until track-0 found
       ├─ Patch (0x450B) ← 0x0210  (motor-stop stub)
       ├─ Patch (0x450F) ← 0x025A  (floppy_stream_read)
       ├─ OUT(0x19, ctrl+0x0C) → motor on, sector-INT enable
       ├─ EI; JR $-2  ← spin; each sector-hole asserts maskable INT
       │    INT → Z80 IM 0 → bus value 0xCF → RST 08h (0x0007)
       │    RST 08h → indirect via (0x450F) → floppy_stream_read (0x025A)
       │    floppy_stream_read reads one sector (sync+ID+256 bytes+checksum)
       │    Loads: directory (track 0, holes 0–2) then SYS.SY (holes 3–37)
       │           then CLI.SY and other files as needed
       └─ When all sectors done: (0x450F) ← 0x0300 (motor-off + return)
            next INT → 0x0300 → OUT(0x19,0); RET → block_copy returns

floppy_boot (0x00FA):
  └─ Scan directory for "SYS     SY"; push entry address (IX+19/20); RET
       → jumps into SYS.SY entry point in RAM

SYS.SY entry (in RAM ~0x5700 area):
  └─ RST 30h → JP (0x57C0) → 12-byte handoff stub at RAM 0x57C0:
       ├─ DI
       ├─ XOR A; OUT(0x01),A  → bank-switch Phantom ROM OUT
       │    (RAM 0x0000–0x07FF is now writable)
       ├─ LD DE,0x0000; LDIR  → copy SYS.SY[0x0000–0x07FF] to RAM 0x0000
       │    (installs SYSMON, permanently replacing the hidden Phantom ROM)
       └─ JP 0x0000  → SYSMON now running from RAM

SYSMON (RAM 0x0000–0x07FF) + SAMOS OS (RAM 0x0800–0x22FF):
  ├─ SYSMON RST 00 at 0x0000 → DI; LD SP,0x4530; JP 0x0105
  └─ OS jump table at 0x0800 → dispatch to 0x0941, 0x094D, ...
```

### NMI / user BREAK path (NOT part of normal boot)

```
nmi_handler (0x0066)  — triggered by BREAK key only:
  ├─ OR A; CALL 0x0210 → stop motor
  ├─ CALL kbd_wait (0x00FD) → wait for keypress
  ├─ JP Z, 0x046D → if key=0 (Enter): PDP-11 paper-tape loader via USART
  └─ key≠0: LDIR 0xB300 bytes from ROM[0x04C2] → RAM 0x5500; JP 0x5500
       The stub at 0x5500 is an INFINITE DIAGNOSTIC POST LOOP:
         set SP; beep; clear screen; draw logo; RAM test × 4 banks; JR 0x5500
       It does NOT load SYS.SY and NEVER returns.
```

---

## SAMOS Init: RST Dispatch, kbd_wait Autoboot, and Runtime Code Patching

### RST Dispatch Table (SYSMON 0x0000–0x003D)

All eight RST instructions use an identical 6-byte trampoline:
```
PUSH HL
LD HL,(0x45xx)   ; load a function pointer from OS workspace
EX (SP),HL       ; swap: target on stack, saved HL back in register
RET              ; jump to target — caller's return address is on the stack below
```
This makes every RST a **1-byte indirect syscall** whose destination is a live RAM word.
Swapping RST targets at runtime changes which function fires for that syscall number.

| RST   | Workspace ptr | Initial target | Purpose                      |
|-------|--------------|----------------|------------------------------|
| RST 08h | `(0x4562)` | –              | Floppy stream / sector INT   |
| RST 10h | `(0x4564)` | –              | (reserved)                   |
| RST 18h | `(0x454C)` | –              | Screen / display call        |
| RST 20h | `(0x455C)` | `0x012D`       | General OS call (installed at 0x00D6) |
| RST 28h | `(0x4568)` | –              | Floppy / block-copy call     |
| RST 30h | `(0x456A)` | –              | Jump via (0x57C0) handoff    |
| RST 38h | `(0x4566)` | `0x003E`       | **50 Hz ISR** (installed at 0x00D0) |

### SAMOS Init Sequence (0x0095–0x0104)

This subroutine is called from the main SYSMON entry at `JP 0x0105 → CALL 0x0095`
with interrupts still **disabled** (DI from the 12-byte handoff stub).

```asm
; ── Workspace zero-fill ────────────────────────────────────────────
0095  LD HL,0x454A
0098  LD B,0xB0         ; 176 bytes
009A  XOR A
009B  LD (HL),A         ; zero 0x454A..0x45F9
009C  INC HL
009D  DJNZ 0x009B

; ── Sentinel installation ──────────────────────────────────────────
009F  LD A,0x80
00A1  LD (0x458A),A     ; ISR control byte = 0x80 (blocks Stage-2 circ-buf write)
00A4  LD (0x4595),A     ; circular-buf guard sentinel
00A7  LD (0x45B6),A     ; end-of-circ-buf guard sentinel
00AA  LD (0x45FF),A     ; workspace tail guard

; ── Circular-buffer write pointer ─────────────────────────────────
00AD  LD HL,0x4596
00B0  LD (0x457C),HL    ; circ-buf write ptr ← start of buffer
                        ; B=0 after the fill loop above

; ── Boot-menu keyboard wait ────────────────────────────────────────
;   DJNZ acts as a ~1 ms inter-poll delay (256 iterations at 2.5 MHz).
;   IN A,(0x00) reads the CLA (keyboard data) port.
;   Bit 7 encodes FOUND: 0 = key present, 1 = no key.
;   JR NZ loops while bit7=1 (no key); exits when bit7=0 (key).
00B3  DJNZ 0x00B3       ; inner delay: spin B times (B=0 → 256 iters)
00B5  IN A,(0x00)       ; read CLA
00B7  AND 0x80          ; test FOUND bit
00B9  JR NZ,0x00B3      ; no key → delay and retry

; ── Hardware-init calls (USART/port setup) ─────────────────────────
;   CALL 0x056A: OTIR — outputs 6 bytes from 0x0564 to port C=5 then C=7.
;   These configure the USART (8251 or equivalent).
00BB  LD C,5
00BD  CALL 0x056A
00C0  LD C,7
00C2  CALL 0x056A

; ── Runtime code patching: install RET stubs ──────────────────────
;   0x45B9 and 0x45BC are workspace slots used as pluggable ISR stage hooks.
;   Writing 0xC9 (RET opcode) turns them into safe no-ops that can later be
;   replaced with real handler code (e.g. CALL xxx; RET) by device drivers.
;   This is SAMOS's standard "configurable handler" pattern — the same
;   technique used for the RST dispatch table, but at byte granularity.
00C5  LD A,0xC9          ; A = RET opcode
00C7  LD (0x45B9),A      ; patch workspace slot 1 → RET stub
00CA  LD (0x45BC),A      ; patch workspace slot 2 → RET stub

; ── ISR vector installation ────────────────────────────────────────
;   RST 38h dispatches via (0x4566); writing 0x003E there installs the 50 Hz ISR.
;   This is the trigger the emulator uses to detect "SAMOS is now running"
;   (keyboard_frame_tick checks bus[0x4566..7] == 0x003E).
00CD  LD HL,0x003E
00D0  LD (0x4566),HL     ; *** ISR vector installed — samos_loaded fires here ***

; ── RST 20h target + display-on ───────────────────────────────────
00D3  LD HL,0x012D
00D6  LD (0x455C),HL     ; RST 20h → 0x012D
00D9  LD A,0x44
00DB  RST 20h            ; (display init call)
00DC  LD A,0x0E
00DE  ...
00E1  LD A,0x01
00E3  OUT (0x00),A       ; video mode = alpha ON
00E5  LD (0x457F),A      ; update video shadow register

; ── RAM bank test ─────────────────────────────────────────────────
00E8  LD DE,0x4000
00EB  XOR A              ; HL = 0
00EC  LD L,A
00ED  LD H,A
00EE  LD B,4             ; test 4 offsets: 0x00, 0x10, 0x20, 0x30
00F0  ADD HL,DE          ; HL += 0x4000 → 0x4000, 0x4010, 0x4020, 0x4030
00F1  LD C,(HL)          ; save original byte
00F2  LD (HL),A          ; write 0
00F3  CP (HL)            ; read back
00F4  LD (HL),C          ; restore
00F5  JR NZ,0x00FB       ; mismatch → stop (first bad bank)
00F7  ADD A,0x10         ; next offset
00F9  DJNZ 0x00F0
00FB  PUSH HL
00FC  LD H,0x00
00FE  LD L,A             ; HL = highest-passing offset (0x00, 0x10, 0x20, or 0x30)
00FF  RST 20h            ; report result / set stack base
0104  RET
```

### kbd_wait Autoboot Behaviour

**Confirmed on real hardware**: the Smaky 6 boots to the CLI with no keypress.

The hardware keyboard controller continuously asserts FOUND=1 with code `0x00`
("null / Enter") whenever no physical key is held — this is the controller's
**idle resting state**, not a latched event.  Both kbd_waits therefore exit
immediately:

| Wait site | Who calls it | Returns A= | Effect |
|-----------|-------------|-----------|--------|
| Phantom ROM `0x00FD` | `boot_main` | `0x00` | Select DX0 floppy boot (default) |
| SAMOS init `0x00B5`  | `0x0095`    | `0x00` | Proceed with default USART/boot config |

Any key pressed during either wait overrides the default (e.g. pressing a
non-null key at `0x00FD` selects double-sided / Winchester path).

**Emulator note**: `keyboard_read_cla()` returns `0x00` (FOUND asserted,
code=0x00) when `iff1=0` and no physical key is in the FIFO and
`samos_loaded=0`.  This mirrors the hardware idle state and makes both waits
exit without a keypress.  Once `samos_loaded=1` the `iff1=0` guard stops
applying this idle-return (since `iff1=0` then means "inside the ISR", not
"Phantom ROM polling").

---

## Video Mode (Phase 1S — confirmed)

**I/O port `0x00` is the Smaky 6 video mode control register.** This was
confirmed by tracing the `MODE` CLI command through SAMOS OS syscalls 0x11/0x12/0x13
back to `OUT (0x00),A` at RAM `0x02BC`.

### Shadow register

`(0x457F)` is a RAM shadow of port 0x00. Every write to the hardware is
immediately mirrored there. All mode-state queries should read `(0x457F)`.

### SYSMON initialization

```asm
00e1  LD A,0x01
00e3  OUT (0x00),A        ; alpha-only, display enabled
00e5  LD (0x457F),A       ; update shadow
```

### Port 0x00 bit encoding

| Bit | Mask | Meaning                                    |
|-----|------|--------------------------------------------|
|  0  | 0x01 | Display enable (always 1 during operation) |
|  1  | 0x02 | Small-points mode ('P' flag)               |
|  2  | 0x04 | Graphics layer active                      |
|  3  | 0x08 | Graphics-only (suppress alpha layer)       |

### MODE command → syscall → port value

| CLI command | SAMOS syscall | Handler  | Port 0x00 value |
|-------------|---------------|----------|------------------|
| `MODE A`    | `RST 20h/0x11`| `0x02DE` | `0x01` — alpha only |
| `MODE G`    | `RST 20h/0x12`| `0x02E6` | `0x0D` — graphics only (0x01\|0x04\|0x08) |
| `MODE G P`  | `RST 20h/0x12`| `0x02E6` | `0x0F` — graphics + small points |
| `MODE 2`    | `RST 20h/0x13`| `0x02EE` | `0x05` — both layers (0x01\|0x04) |
| `MODE 2 P`  | `RST 20h/0x13`| `0x02EE` | `0x07` — both + small points |

The carry flag into the syscall encodes the 'P' sub-argument (set by CLI.SY
if the user typed `G P` or `2 P`).

### Common syscall tail (all three mode syscalls)

```asm
02b9  LD (0x457F),A       ; update shadow
02bc  OUT (0x00),A        ; write to video mode hardware
02be  POP AF
02bf  RET
```

### Correction: OUT(0x06) false positives

An earlier naive byte scan of SYS.SY reported `OUT (0x06),A` at RAM
`0x0613` and `0x065B`. These were **false positives**: the byte `0xD3` (the `OUT`
opcode) appeared as the low byte of the targets in `JP 0x06D3` and
`CALL 0x06D3` instructions. Port 0x06 is **not** a video mode register.

See `docs/CLI_SY_analysis.md` for the full MODE handler disassembly and
RST 20h dispatcher annotated listing.

---

## Open Questions

1. What chip does port 0x08 communicate with? Candidates: RTC (MSM5832?),
   display controller init register, or external latch.
2. What does port 0x0B address? A second serial port? Keyboard controller?
3. What is port 0x1C? (3 uses in OS near 0x63C0, 0x6583, 0x6594 — possible
   disk DMA acknowledge or printer strobe.)
4. What is `entry=0x5700` in the SYS.SY directory entry? The OS-loader stub
   occupies 0x5500 and extends to ~0x583E. 0x5700 is mid-stub. This may be
   the jump target written into a vector table by the stub.
5. ~~Does SYSMON install itself at 0x0000 during the normal boot?~~ **Resolved:**
   SYSMON installs at 0x0000 during the normal boot path. The 12-byte handoff stub
   at RAM 0x57C0 (LDIR'd from ROM 0x0137 by boot_menu_setup) does `OUT(0x01),0`
   then `LDIR` from SYS.SY into RAM 0x0000 before `JP 0x0000`. This happens
   unconditionally on every normal boot, before any OS code executes.
6. ~~What does RST 20h dispatch to, and which vector pointer does it use?~~ **Resolved (Phase 1S):**
   RST 20h uses `(0x455C)` (initialized to `0x012D`, the syscall dispatcher). The
   dispatcher reads an inline byte from the caller's instruction stream (the byte
   immediately after the `E7` opcode), uses it as a word-table index into the table
   at `0x0F30`, and dispatches to the handler. See `docs/CLI_SY_analysis.md`.
7. ~~What I/O port controls the video display mode?~~ **Resolved (Phase 1S):**
   **Port `0x00`** — written by the common tail of SAMOS mode-switch syscalls
   (0x11/0x12/0x13) at `OUT (0x00),A` (RAM `0x02BC`). Bit encoding: bit 0 =
   display enable, bit 1 = small-points, bit 2 = graphics layer, bit 3 =
   graphics-only. RAM `(0x457F)` is the shadow. See `docs/CLI_SY_analysis.md`.

---

## Current Runtime Status (Phase 1Q / Phase 1R — 2026-05-07)

### Full boot confirmed

The emulator boots completely from `floppies/1 Systeme_1HComplet.dsk` to the CLI
directory listing. All files visible: SYS.SY, CLI.SY, ER.SY, FLO.ST, SMILE.SM,
CCOPY.SM, etc.

Minimal reproducible boot command:
```
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
  ./build/smemu6 -disk "floppies/1 Systeme_1HComplet.dsk" -autoboot -timeout 35
```

### Root cause of former ERROR 033 — two FDC bugs fixed in `src/floppy.c`

**Bug 1 — `floppy_read_data` byte_pos=1 (sector ID byte)**

The Micropolis sector header byte stream is: sync (0x00) → **track number** → 256
data bytes → checksum. The OS at PC `0x20C2` executes `CP (HL)` comparing the
byte_pos=1 header byte against the expected-track variable at `(0x2B8B)`. The old
emulator returned the sector index here instead of the physical track number, so
the comparison always failed on any track > 0 (CLI.SY on tracks 2–3), causing the
retry counter to exhaust and raising error `0x1B` (`erreur de lecture`).

Fix: `floppy_read_data` post-ROM path at byte_pos=1 now returns `m->fdc.track[0]`.

**Bug 2 — `floppy_write_cont` post-ROM stepping protocol**

The Phantom ROM step protocol (bit 2 edge-triggered on port 0x19 write) was being
applied to post-ROM code. SYS.SY sub `0x2187` writes port `0x1A` once per step;
the old code never moved the head. The step direction was also derived from the
write value's bit 4.  Per Plan F5, bit 4 of port 0x19 is STPDIRIN (step
direction), not drive select; the actual drive is selected via DRISEL1 (bit 5)
/ DRISEL2 (bit 6), latched when MOTORON (bit 3) is asserted.

Fix: post-ROM path steps once per `floppy_write_cont` call; head direction is
derived by comparing `m->fdc.track[0]` against the target stored in workspace
variable `(0x2B8B)` (single-sided) or `(0x2B8C)` (double-sided, bit 6 of
`(0x2B88)` selects which). Drive is determined from `fdc.selected_drive` (set
by port 0x19 DRISEL1/DRISEL2 bits), not from bit 4 of the port 0x1A write value
(bit 4 of port 0x1A is not a reliable drive-select signal).

### Key workspace addresses (SYS.SY FDC protocol)

| Address | Role |
|---------|------|
| `0x2B88` | Drive control base value; bit 6 selects track variable (`0x2B8B` or `0x2B8C`) |
| `0x2B8B` | Expected/current track for drive A, head 0 |
| `0x2B8C` | Expected/current track for drive A, head 1 (double-sided) |
| `0x2B92` | Target track for next seek |
| `0x2BA3` | Sector table base — 16 entries × 2 bytes, entry[hole] = RAM destination |
| `0x4554` | Current error code |
