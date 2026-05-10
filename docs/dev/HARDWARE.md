# Smaky 6 — Hardware Reference for FPGA Recreation

*Based on nine original Epsitec schematics and documents (J. Zahn November 1978,
R. Forster October 1979) and reverse-engineering of the SAMOS 2-8 ROM images.*

---

## Table of Contents

1. [Overview](#1-overview)
2. [CPU Subsystem](#2-cpu-subsystem)
3. [Memory Subsystem](#3-memory-subsystem)
4. [Display Subsystem](#4-display-subsystem)
5. [Keyboard Interface](#5-keyboard-interface)
6. [I/O Port Map](#6-io-port-map)
7. [Floppy Controller](#7-floppy-controller)
8. [Winchester Controller](#8-winchester-controller)
9. [Serial Interfaces (USART)](#9-serial-interfaces-usart)
10. [Parallel Interface](#10-parallel-interface)
11. [Sound / Buzzer](#11-sound--buzzer)
12. [Interrupt Architecture](#12-interrupt-architecture)
13. [Boot Sequence (Phantom ROM)](#13-boot-sequence-phantom-rom)
14. [Signal Glossary](#14-signal-glossary)

---

## 1. Overview

The **Smaky 6** is a Swiss Z80-based personal computer designed at EPFL (Lausanne)
by Jean-Daniel Nicoud and commercialized by Epsitec (~450 units, 1979–1983).

The machine is organized on a **backplane with plug-in boards**:

| Board        | Schematic document             | Content                                       |
|--------------|-------------------------------|-----------------------------------------------|
| PROCESSEUR   | doc-227 (J. Zahn, Nov 1978)   | Z80, clock, 8251 USARTs, NMI, bus control     |
| MÉMOIRE      | doc-228 (J. Zahn, Nov 1978)   | DRAM banks, ROM sockets, address decode       |
| AFFICHAGE    | doc-229 (J. Zahn, Nov 1978)   | Scan counters, pixel clock, DMA HOLD logic    |
| CARACTÈRES   | doc-230 (J. Zahn, Nov 1978)   | Char-gen, serializers, line buffers, parallel |
| Ext. board   | doc-230-memext (R. Forster, Oct 1979) | 32K DRAM + Phantom ROM + RTC + SIRING ||
| CLAVIER      | doc-231 (J. Zahn, Nov 1978)   | Keyboard scan matrix, encoder (S471), counters |
| Floppy ctrl  | doc-189-191 (3 sheets)        | Micro-floppy serialisation / decode / IRQ     |
| Par. I/O     | doc-219-225 (J. Zahn, 7 pp)   | Parallel interface description + port map    |

Hardware versions:
- **32 KB model** — 2 × 8 × 4116 DRAM on mainboard; fixed SYSMON + SAMOS ROMs at 0x0000–0x1FFF.
- **48 KB model** — as above plus one extra DRAM bank.
- **64 KB Phantom model** — mainboard 32 KB DRAM + **extension board** (32 KB DRAM
  + 2 KB Phantom ROM + RTC + SIRING connector); single 2 KB Phantom bootstrap ROM at
  0x0000–0x07FF that bank-switches itself out after loading the OS from floppy.
  *This is the variant documented and emulated here.*

---

## 2. CPU Subsystem

### 2.1 Processor

| Parameter        | Value                                                  |
|------------------|--------------------------------------------------------|
| CPU              | Zilog Z80 (NMOS, 40-pin DIP)                           |
| Clock            | **12.0576 MHz** crystal → ÷5 divider → **2.4115 MHz** |
|                  | Rounded in documentation to **2.5 MHz**                |
| T-states / frame | 50,000 (2.5 MHz ÷ 50 Hz)                              |
| Interrupt mode   | **IM 0** (never changed by Phantom ROM or SAMOS)       |
| Reset            | Power-on + RESET signal from front panel               |

The 12.0576 MHz master oscillator is also the pixel clock for the video
subsystem (see §4).  The CPU clock is derived by a divide-by-5 prescaler on the
PROCESSEUR board; confirmed by schematic annotation.

### 2.2 Bus control signals

| Signal       | Pin | Direction | Description                                              |
|--------------|-----|-----------|----------------------------------------------------------|
| `HOLDAL`     | 31  | →CPU      | BUSREQ from display board (AFFICHAGE) — steals bus cycles for DMA |
| `HOLDBL`     | 33  | →CPU      | Second BUSREQ source (reserved / Winchester)             |
| `HOLDA`      | 30  | ←CPU      | BUSACK — display board grants bus when asserted          |
| `NMILOW`     | 17  | →CPU      | NMI (active-low), driven by BREAK key and floppy sector sensor |
| `INTREDYLOW` | 21  | →CPU      | INT from display frame timer (50 Hz RST 38h)             |
| `INTRECLOW`  | 23  | →CPU      | INT from USART (data-ready; not used by SAMOS 2-8)       |
| `RESETL`     | 26  | →CPU      | Power-on / front-panel RESET                             |
| `PETRIL`     | 36  | →CPU      | WAIT (not used in standard configuration)                |
| `MREQL`      | 19  | ←CPU      | Memory request                                           |
| `IORL`       | 20  | ←CPU      | I/O request                                              |
| `WRL`        | 22  | ←CPU      | Write strobe                                             |
| `M1L`        | 27  | ←CPU      | M1 (opcode fetch / INT acknowledge)                      |
| `RFSH`       | 28  | ←CPU      | DRAM refresh (fed to RAS logic on MÉMOIRE board)         |

The display board asserts `HOLDAL` during every horizontal blanking period to
fetch one byte from video RAM, keeping the screen populated without CPU
involvement.  This causes up to ~8 stolen T-states per scan line.  The emulator
does not model stolen cycles (effect on timing is negligible at 2.5 MHz).

---

## 3. Memory Subsystem

### 3.1 Address map (64 KB Phantom model)

Addresses confirmed in **octal** on the MÉMOIRE schematic:
`040000` = 0x4000, `046000` = 0x4600, `100000` = 0x8000.

| Address range      | Octal range          | Size   | Description                                       |
|--------------------|----------------------|--------|---------------------------------------------------|
| `0x0000–0x07FF`    | 000000–003777        | 2 KB   | **Phantom ROM** (TMS2716 EPROM, "SYS17")          |
|                    |                      |        | Becomes writable RAM after `OUT (01h), A=00h`     |
| `0x0800–0x3FFF`    | 004000–037777        | ~14 KB | Lower RAM (SAMOS OS loaded here from floppy)      |
| `0x4000–0x44FF`    | 040000–042377        | 1280 B | **Alpha (text) framebuffer** — 20 rows × 64 cols  |
| `0x4500–0x45FF`    | 042400–042777        | 256 B  | SAMOS OS workspace (CLI buffer, current filename) |
| `0x4600–0x54FF`    | 043000–052377        | 3840 B | **Graphic framebuffer** — 60 rows × 64 bytes      |
| `0x5500–0x77FF`    | 052400–073777        | ~8 KB  | SYS.SY code (loaded by Phantom from floppy)       |
| `0x7800–0xFFFF`    | 076000–177777        | ~32 KB | Upper RAM                                         |

### 3.2 DRAM (MÉMOIRE board)

| Component         | Part number | Count | Capacity           |
|-------------------|-------------|-------|--------------------|
| DRAM chips        | Intel 4116  | 16    | 16K × 1-bit each   |
| Bank 1 (E20–E27)  | 8 × 4116    | —     | 16 KB              |
| Bank 2 (F20–F27)  | 8 × 4116    | —     | 16 KB              |
| **Total**         | —           | —     | **32 KB** (initial revision) |

The 4116 requires **three supply voltages**: +12 V, +5 V, −5 V.  Address
multiplexing (RAS/CAS) is handled by LS 158 multiplexers (F19, E19) on the
MÉMOIRE board.  The Z80 `RFSH` signal is fed to the RAS logic for the mandatory
refresh cycles.

Upper RAM (0x8000–0xFFFF) is provided by the memory extension board (§3.5).

### 3.3 ROM (MÉMOIRE board)

The right-hand section of the MÉMOIRE board carries four EPROM sockets
**C21–C24**, accepting either **2708** (1 KB) or **2716 / TMS2716** (2 KB) chips.
A jumper selects whether the ROM block maps to **0x4000 or 0x6000**.
On the 64 KB Phantom variant the only populated socket is the single
2 KB Phantom bootstrap at **0x0000**.

ROM chip-select is generated by an **LS 138** (D20) decoding A11–A13.

### 3.4 Address decode (MÉMOIRE board)

Two cascaded **LS 139** demultiplexers (E17, top; E17, bottom) decode A14–A15
into four 16 KB quadrant-selects:

| A15 A14 | Region       | Select signal |
|---------|--------------|---------------|
| 0  0    | 0000–3FFF    | lower RAM / ROM |
| 0  1    | 4000–7FFF    | video + OS    |
| 1  0    | 8000–BFFF    | upper RAM     |
| 1  1    | C000–FFFF    | upper RAM     |

The `MOVROM` signal (latched by D18/D19 flip-flop on receipt of
`OUT (01h), A=00h`) disables the Phantom ROM chip-select, exposing the
underlying RAM at 0x0000–0x07FF.

### 3.5 Memory Extension Board (64 KB Phantom model)

*Confirmed from Epsitec schematic: "Extension mémoire 32K RAM dynamique + 2K EPROM /
Horloge absolue + SIRING 7910", designed by Ronald Forster, Epsitec, October 1979.*

In the 64 KB Phantom model the mainboard ROM sockets (C21–C24) are **unpopulated**.
All ROM and the upper 32 KB of RAM live on this plug-in extension board.

| Component       | Part      | Count | Function                                         |
|-----------------|-----------|-------|--------------------------------------------------|
| IC1             | TMS2716   | 1     | **Phantom ROM** — 2 KB EPROM at 0x0000–0x07FF    |
| IC16–IC31       | 4116 DRAM | 16    | **Upper 32 KB RAM** — 0x8000–0xFFFF              |
| IC5             | E405/08   | 1     | **RTC** (Horloge absolue, chip: **E405**, port: **0x08**) — serial, 32.768 kHz |
| IC2, IC10       | LS158     | 2     | Row/column address MUX for DRAM                  |
| IC3             | 1/2 LS139 | 1     | RAS / bank decoder                               |
| IC8             | S287      | 1     | Extension bus interface + address decode         |
| IC11            | 81LS95    | 1     | Octal tri-state data bus driver                  |
| IC14, IC15      | C175      | 2     | Quad D latch — DRAM data line buffers            |
| —               | 32 kHz XTAL | 1  | Crystal for RTC                                  |
| —               | 1.5 V cell  | 1  | Battery backup for RTC                           |

**Phantom ROM (IC1)**: The TMS2716 EPROM is on this board, not the mainboard.
Its chip-select (`sel0`) is driven by IC8 (S287) and is disabled when the
Z80 executes `OUT (01h), A=00h` (MOVROM), exposing the underlying RAM.

**DRAM (IC16–IC31)**: 16×4116 = 32 KB at 0x8000–0xFFFF.
RAS/CAS multiplexing by IC2/IC10 (LS158 MUX), bank select by IC3 (LS139).
Refresh driven by the Z80 `RFSH` / `REFRESH` signal from the mainboard bus.

**RTC — E405/08 (IC5)**: 3-wire synchronous serial interface.

The E405 is a **custom RTC ASIC manufactured by Micro Electronic Marin (MEM)**,
a Swiss company based in Le Locle/Marin-Epagnier.  It was one of the earliest
real-time clock chips produced for personal computers.
*(Information courtesy M. Pierre-Yves Rochat.)*

The slash notation `E405/08` is an Epsitec schematic convention used by
R. Forster: the part before the slash is the **chip type** (E405) and the
number after the slash is the **I/O port address** (0x08).  Port 0x08 is
therefore directly encoded in the component designator.

**Important distinction:** J. Zahn's schematics also use E-prefixed identifiers
(e.g. E17 = LS 139, E9 = 2101 SRAM, E11 = 2101 SRAM) but these are **board
grid coordinate references** — the letter denotes the row and the number
denotes the column on the backplane layout.  They do **not** encode any I/O
port address.  Only R. Forster's extension board uses the `ChipType/PortAddr`
slash notation.  **E405/08 (IC5) is the only such chip across all nine
schematics.**

Serial interface (3-wire synchronous bit-bang, proprietary — predates SPI):
- **CK** — serial clock (toggled by Z80 I/O write)
- **I/O** — bidirectional data line (MOSI on write, MISO on read)
- **CS** — implicit chip-select (bits 1–2 held high during a transaction)

Protocol: 4-bit command phase (LSB-first), then 7 BCD data bytes (LSB-first per
byte).  Command 0b1111 (0x0F) = read; 0b0111 (0x07) = write.

Register layout (7 bytes, BCD, confirmed empirically from SAMOS display):

| Byte | Content  | Range  | SAMOS field         |
|------|----------|--------|---------------------|
| 0    | hours    | 00–23  | time **hh**         |
| 1    | minutes  | 00–59  | time **mm**         |
| 2    | day      | 01–31  | date **DD**         |
| 3    | month    | 01–12  | date **MM**         |
| 4    | year     | 00–99  | date **YY**         |
| 5    | weekday  | 1–7    | day name (1=Mon…7=Sun) |
| 6    | seconds  | 00–59  | time **ss**         |

**SIRING / 7910**: The board title suffix "SIRING 7910" is the Epsitec
internal board designation. "7910" is the date code (October 1979).
The meaning of "SIRING" is not confirmed from available sources.

**Bus connectors**: The board uses ADPER (B22) and ADMEN (B21/B15/B14)
for address bus connections, plus REFRESH (A21), WRITE (A16/A30), NODA (B20),
and RESET (A20).

---

## 4. Display Subsystem

### 4.1 Architecture

The display is driven by two independent hardware planes that share the same
Z80 address space.  A dedicated scan-counter board (AFFICHAGE, sheet 11-1)
generates composite video without CPU intervention using **DMA HOLD cycles**.

| Plane    | Address range   | Content                          |
|----------|-----------------|----------------------------------|
| Alpha    | 0x4000–0x44FF   | Character codes (ASCII, 7-bit)   |
| Graphic  | 0x4600–0x54FF   | Bitmap pixels (1 bpp)            |

Display modes are set by `OUT (00h), A`:

| `A` value | Mode constant | Description                       |
|-----------|---------------|-----------------------------------|
| `0x01`    | ALPHA         | Text only (alpha plane)           |
| `0x0D`    | GRAPHIC       | Bitmap only (suppress alpha)      |
| `0x0F`    | GRAPHIC+P     | Bitmap + small-points mode        |
| `0x05`    | SUPER         | Superimposed (alpha + graphic)    |
| `0x07`    | SUPER+P       | Superimposed + small-points       |
| `0x00`    | OFF           | Display blanked                   |

Port 0x00 bit encoding:
- bit 0 = ENALPHA/display enable
- bit 1 = small-points ('P') mode
- bit 2 = ENGRA (graphics layer active)
- bit 3 = graphics-only (suppress alpha)

### 4.2 Alpha (text) plane

| Parameter        | Value                                               |
|------------------|-----------------------------------------------------|
| Columns          | 64                                                  |
| Rows             | 20                                                  |
| Cell size        | 8 × 8 pixels (displayed as 8 px wide × 8 px tall)  |
| Character ROM    | **74S262** + **2716 EPROM** (2 KB, 128 chars × 16 bytes) |
| Character set    | 7-bit ASCII; no lowercase on original hardware      |
| Bit order        | Bit 0 = leftmost pixel (LSB-first)                  |
| Total buffer     | 1280 bytes at 0x4000                                |

The character generator is a two-stage pipeline: a **74S262** parallel-load
shift register serializes the 8 pixel bits from the 2716, then a second
serializer (also on the CARACTÈRES board) feeds the analog CRT.

**GROS signal**: a hardware double-width character mode exists (GROS = "big"
in French) but is not used by SAMOS 2-8 and is not emulated.

### 4.3 Graphic (bitmap) plane

| Parameter        | Value                                                          |
|------------------|----------------------------------------------------------------|
| Logical rows     | **60** (stored rows in RAM)                                    |
| Bytes per row    | 64                                                             |
| Total bytes      | 3840 (60 × 64) at 0x4600–0x54FF                               |
| Pixel encoding   | 1 bit per pixel; **bit 0 = leftmost pixel** (LSB-first)        |
| Vertical stretch | Each stored row is displayed as **4 identical scan lines**     |
| Display size     | 512 × 240 pixels (60 × 4 = 240 display lines, 64 × 8 = 512 px wide) |
| Pixel aspect     | ~1:1.6 (CRT was ~4:3; 512 px wide ÷ 240 px tall × 4/3 = 1.6×) |

**Why 60 rows?**  The AFFICHAGE scan counter counts 240 active lines.  The DMA
address counter increments once every 4 lines, giving 60 unique row addresses.
The same 64-byte row is read four consecutive display times.

**Superimpose**: when both planes are active (SUPER mode), the graphic pixel
takes priority over the alpha character where the graphic bit = 1; the alpha
character shows through where the graphic bit = 0.

### 4.4 Timing (AFFICHAGE board)

| Parameter           | Value                                    |
|---------------------|------------------------------------------|
| Pixel clock         | **12.0576 MHz** (master crystal)         |
| Pixels per line     | 512 active + ~88 blanking = 600 total    |
| Line rate           | 12.0576 MHz ÷ 600 ≈ **20.1 kHz**        |
| Active lines        | 240                                      |
| Frame rate          | **50 Hz** (PAL-region CRT sync)          |
| Lines per frame     | ~401 (240 active + ~161 V-blank)         |
| Phosphor            | **P31 green** (peak emission ~530 nm)    |

The scan counter board generates the `HOLDAL` signal once per pixel clock
period during H-blank to steal one bus cycle and fetch the next character/bitmap
byte.  Two **Intel 2101** static-RAM line buffers are used as pipeline latches
so the CRT DAC stream is continuous even when the CPU holds the bus.

### 4.5 SDL2 rendering (emulator mapping)

```
Physical pixel buffer : 512 × 240 (1 bpp → ARGB8888)
Lit pixel colour      : #00E700  (P31 green phosphor)
Background colour     : #000800  (dark phosphor glow)
Aspect-corrected view : 512 × 384 (1.6× vertical — accurate hardware AR)
Emulator render size  : 512 × 480 (2× vertical — integer scaling for crisp display)
Status bar            : +12 px below → total SDL window 512 × 492 (×2 = 1024 × 984)
Note: the emulator uses 2× (480 lines) rather than the exact 1.6× (384 lines) to
keep all integer scale factors clean. The smaky6_samos.py image export uses 1.6×.
```

---

## 5. Keyboard Interface

### 5.1 Overview

The Smaky 6 keyboard is an **encoder-per-key** matrix with a dedicated EPROM
translating scan position to a **7-bit ASCII-compatible code**.  The result is
latched by a flip-flop (the "FOUND" flip-flop) and read by the CPU via I/O.

### 5.2 Port protocol

| Port | Direction | Name | Function                                        |
|------|-----------|------|-------------------------------------------------|
| 0x00 | Read      | CLA  | bits[6:0] = key code; bit 7 = NOT-FOUND (1 if no key) |
| 0x00 | Write     | MODE | Display mode register (see §4.1 — same address!) |
| 0x01 | Read      | ST   | bit 3 = always 1; bit 2 = FOUND (key available) |

Reading port 0x00 **clears** the FOUND flip-flop.

### 5.3 Special keys

| Key               | Code / action                                          |
|-------------------|--------------------------------------------------------|
| BREAK (ESC)       | Generates **NMI** — fires NMILOW pin 17 of Z80         |
| SHIFT+BREAK       | Boot from DX0: (captured before NMI in Phantom ROM)    |
| FUNCTION+SHIFT+BREAK | Boot from DX1:                                    |
| FUNCTION+BREAK    | Memory POST test                                       |
| FUNCTION keys F1–F7 | 7-bit codes with bit 7 set (0x80–0x86 range)         |
| KILL              | Abort peripheral transfer (mapped to function key)     |
| TAB               | Inserts `DX1:` at command prompt                       |

### 5.4 Key layout

The keyboard has **57 alphanumeric / punctuation keys** plus **7 function keys**
in a **QWERTZ Swiss** layout.  Uppercase only on alphanumeric characters.

---

## 6. I/O Port Map

All I/O is decoded with a **6-bit address mask** (`port & 0x3F`); ports
0x00–0x3F alias to 0x40–0x7F, 0x80–0xBF, 0xC0–0xFF.

| Port (masked) | RD / WR | Name        | Description                                              |
|---------------|---------|-------------|----------------------------------------------------------|
| `0x00`        | R       | CLA         | Keyboard character latch; reading clears FOUND flip-flop |
| `0x00`        | W       | MODE        | Display mode register (see §4.1)                         |
| `0x01`        | R       | ST          | Keyboard status: bit 2 = FOUND, bit 3 = 1               |
| `0x01`        | W       | MOVROM / ACK| `=0x00`: bank-switch Phantom ROM out; else ISR ACK       |
| `0x02`        | R/W     | PAR         | Parallel port data (bidirectional)                       |
| `0x03`        | R       | SPAR        | Parallel port status                                     |
| `0x03`        | W       | BEEP        | **Sound bit-bang**: bit 0 toggles the buzzer             |
| `0x04`        | R/W     | USART0-DATA | 8251 USART "permanent I/O" (C15) — data register         |
| `0x05`        | R/W     | USART0-CMD  | 8251 USART "permanent I/O" — status / command            |
| `0x06`        | R/W     | USART1-DATA | 8251 USART "cassette" (C13) — data register              |
| `0x07`        | R/W     | USART1-CMD  | 8251 USART "cassette" — status / command                 |
| `0x08`        | R/W     | RTC         | **E405/08 RTC** 3-wire synchronous serial (Horloge absolue): bit3=CK, bit2/1=CS, bit0=data (MISO on IN) |
| `0x11`        | R       | (unknown)   | Unknown device; returns 0x00 (stub)                      |
| `0x19`        | R/W     | FDC-CTRL    | Floppy control / sector index (see §7)                   |
| `0x1A`        | R/W     | FDC-CONT    | Floppy continuation / step-pulse register                |
| `0x1B`        | R       | FDC-DATA    | Floppy streaming data byte                               |
| `0x20`        | R       | WIN-DATA-R  | Winchester data register (IN) — read next sector byte    |
| `0x20`        | W       | WIN-DATA-W  | Winchester data register (OUT) — write sector byte (stub)|
| `0x21`        | R       | WIN-ERR     | Winchester error register — `0x00` = no error            |
| `0x21`        | W       | WIN-PRECOMP | Write pre-compensation cylinder (ignored)                |
| `0x23`        | W       | WIN-SEC     | Sector number: bits[4:0], 0-based (0–31)                 |
| `0x24`        | W       | WIN-CYL-LO  | Cylinder low byte                                        |
| `0x25`        | W       | WIN-CYL-HI  | Cylinder high byte (Phantom always 0 → max 255 cyls)     |
| `0x26`        | W       | WIN-SDH     | SDH: bits[2:0]=head (0–5), bit[3]=drive select (0/1)     |
| `0x27`        | R       | WIN-STATUS  | `0xFF`=no image, `0x50`=RDY+SC, `0x58`=RDY+SC+DRQ       |
| `0x27`        | W       | WIN-CMD     | `0x1n`=RESTORE, `0x2n`=READ SECTOR, `0x3n`=WRITE(stub)  |
| `0x2B`        | W       | WIN-?       | Unknown register — no-op                                 |
| `0x0D` (=0xCD)| R       | WIN-DMA     | Winchester DMA / status (returns 0x00)                   |

---

## 7. Floppy Controller

### 7.1 Drive hardware

| Parameter       | Value                                                 |
|-----------------|-------------------------------------------------------|
| Drive type      | **Micropolis** 5.25" hard-sectored, single-sided      |
| Tracks          | 40 (standard) or 77 (extended); auto-detected from image size |
| Sectors         | **16 hard sectors** per track (physical index holes)  |
| Bytes / sector  | **256**                                               |
| Capacity        | 40-track: 163,840 bytes; 77-track: 315,392 bytes      |
| Speed           | **300 RPM**                                           |
| Sector rate     | 300 RPM × 16 sectors = **80 sector-pulses / second**  |
| Drive names     | DX0: (lower, drive A), DX1: (upper, drive B)          |

### 7.2 Port protocol (bit-banged discrete logic)

**Port 0x1A — CONT (write, Plan F4 step/motor control):**

| Bit | Name        | Function                                             |
|-----|-------------|------------------------------------------------------|
| 0   | MOTOR       | 1 = spindle motor on                                 |
| 1   | HEAD_LOAD   | 1 = head loaded (pressed against disk)               |
| 2   | STEP_PULSE  | 0→1 rising edge = one track step                     |
| 3   | DIRECTION   | 1 = step toward track 0 (inward); 0 = away           |

> **Note:** bit 4 of port 0x1A writes is **not** drive select.  Drive selection
> is controlled solely via port 0x19 DRISEL1 (bit 5) / DRISEL2 (bit 6) — see below.

**Port 0x1A — CONT (read):**

| Bit | Name      | Function                           |
|-----|-----------|------------------------------------|
| 7   | BYTE_READY| 1 = next streaming byte available  |

**Port 0x19 — CTRL (write / Phantom ROM mode, IC7 LS475 Plan F5):**

| Bit | Name     | Function                                                          |
|-----|----------|-------------------------------------------------------------------|
| 1   | WRTMOD   | Write mode                                                        |
| 2   | INTON    | Interrupt / NMI arm                                               |
| 3   | MOTORON  | Spindle motor on                                                  |
| 4   | STPDIRIN | Step direction (1 = toward track 0)                               |
| 5   | DRISEL1  | Drive select: **1 = DX0** (drive A)                               |
| 6   | DRISEL2  | Drive select: **1 = DX1** (drive B)                               |
| 7   | DRISEL3  | Third drive select (not used on standard Smaky 6)                 |

Drive selection takes effect whenever any DRISEL bit (bits 5/6) is written,
regardless of MOTORON.  The Phantom ROM always writes DRISEL together with MOTORON
(`0x2C` / `0x4C`), but SAMOS probes drive presence by writing DRISEL *without*
MOTORON (e.g. `0x40` = DX1 probe, no motor) before reading status.

Common written values: `0x2C` = DX0 arm (DRISEL1\|MOTORON\|INTON), `0x4C` = DX1 arm
(DRISEL2\|MOTORON\|INTON), `0x40` = DX1 presence probe (DRISEL2 only, no motor),
`0x00` = motor off / NMI disarm.

**Port 0x19 — CTRL (read):**

| Bits  | Function                                          |
|-------|---------------------------------------------------|
| [3:0] | Current sector index (0–15, hard-sector counter)  |
| [6]   | SEEK_BUSY (1 = head still settling after step)    |

**Port 0x1B — DATA (read):**

Streaming byte from current sector.  Protocol per sector read:
```
byte 0      : sync / gap marker
byte 1      : sector ID (track × 16 + sector)
bytes 2–257 : 256 data bytes
byte 258    : checksum (sum of data bytes mod 256)
```

### 7.3 Interrupt scheme

The Micropolis sector-hole sensor asserts **NMI** once per sector (80 times/second
at 300 RPM).  During floppy loading the CPU polls port 0x19 and 0x1A to find the
right sector, then reads 256 bytes via port 0x1B.  The Phantom ROM INT vector
`(0x450F)` → `0x025A` is the `floppy_stream_read` routine.

When the floppy NMI is armed (`OUT (19h)` with bits 2+3 set), the INT
acknowledge cycle returns **RST 08h (0xCF)** instead of the normal
**RST 38h (0xFF)** (see §12).

---

## 8. Winchester Controller

### 8.1 Drive hardware

| Parameter       | Value                                                      |
|-----------------|-------------------------------------------------------------|
| Controller      | WD1000/WD1001/WD1002-compatible (confirmed from Phantom ROM disassembly; chip identification courtesy of M. Pierre-Yves Rochat) |
| Sectors/track   | **32** (sector index masked with `0x1F` in ROM at 0x0380)   |
| Heads/cylinder  | **6** (`C=6` divisor in CHS decomposition loop at 0x0380)   |
| Bytes/sector    | **256** (INIR with `B=0` → 256 iterations)                  |
| Max cylinders   | **255** (Phantom always writes `0` to CYL_HI port 0x25)     |
| Drive capacity  | 255 × 6 × 32 × 256 ≈ **12 MB** per drive                   |
| Drive names     | SM6WIN0 (drive 0), SM6WIN1 (drive 1)                        |
| Image format    | Flat binary; `harddisks/SM6WIN0.DSK` / `SM6WIN1.DSK`        |

### 8.2 Port protocol (WD1000/WD1001/WD1002-compatible register set)

All Winchester registers are at ports `0x20–0x27` and `0x2B`
(decoded with the 6-bit mask `port & 0x3F`).

| Port   | R/W | Register        | Description                                                           |
|--------|-----|-----------------|-----------------------------------------------------------------------|
| `0x20` | R   | Data (IN)       | Read next byte from sector buffer (256 bytes, use INIR)               |
| `0x20` | W   | Data (OUT)      | Write next byte during WRITE SECTOR (stub — discarded)                |
| `0x21` | R   | Error           | Error bits after last command: `0x00` = no error                      |
| `0x21` | W   | Write precomp   | Write pre-compensation cylinder (ignored)                             |
| `0x23` | W   | Sector number   | Sector within track: bits[4:0], 0-based (0–31)                        |
| `0x24` | W   | Cylinder low    | Low byte of cylinder address                                          |
| `0x25` | W   | Cylinder high   | High byte of cylinder (Phantom always writes 0 → max 255 cylinders)   |
| `0x26` | W   | SDH             | bits[2:0] = head (0–5), bit[3] = drive select (0 or 1)                |
| `0x27` | R   | Status          | `0xFF` = no image (BSY), `0x50` = RDY+SC (idle), `0x58` = RDY+SC+DRQ |
| `0x27` | W   | Command         | `0x1n` = RESTORE, `0x2n` = READ SECTOR, `0x3n` = WRITE SECTOR (stub) |
| `0x2B` | W   | (unknown)       | Purpose not confirmed from schematics; treated as no-op               |

### 8.3 CHS → LBA mapping

Confirmed from Phantom ROM disassembly at `0x0370–0x0398`:

```
sector   = DE & 0x1F                   (5-bit field, 0-based)
track    = DE >> 5                     (packed 16-bit address DE, bits[15:5])
head     = track mod 6
cylinder = track div 6

LBA      = cylinder × 192 + head × 32 + sector
         = cylinder × HEADS_PER_CYL × SECTORS_PER_TRK
           + head × SECTORS_PER_TRK
           + sector
```

The 16-bit register pair `DE` in the Phantom ROM equals the LBA exactly.
Disk images are flat arrays of 256-byte sectors indexed by LBA.

### 8.4 Status register values

| Value  | Meaning                                                                     |
|--------|-----------------------------------------------------------------------------|
| `0xFF` | No image mounted — BSY set; `winchester_init` loop times out → *Disque inactif* |
| `0x50` | RDY (bit 6) + SC (bit 4) — drive ready, idle                                |
| `0x58` | RDY + SC + DRQ (bit 3) — data available for read (or write buffer ready)    |

### 8.5 Command set

| Command | Code   | Action                                                            |
|---------|--------|-------------------------------------------------------------------|
| RESTORE | `0x1n` | Seek to cylinder 0; reset state; return to IDLE                   |
| READ    | `0x2n` | CHS→LBA, load 256 bytes from image into buffer, set DRQ           |
| WRITE   | `0x3n` | Enter WRITING phase; accept 256 bytes via port 0x20 (stub — bytes discarded) |

Commands execute **instantaneously** (no BSY delay) — the emulator is
synchronous and the Phantom ROM polls port 0x27 in a tight loop.

### 8.6 Bus arbitration note

The `HOLDBL` line (Z80 pin 33, §2.2) is labeled "Winchester" on the
schematic.  The real hardware likely uses a DMA engine to burst sector
data without CPU involvement.  The emulator bypasses DMA entirely: after
a READ command sets DRQ, the CPU reads port 0x20 directly via `INIR`.

---

## 9. Serial Interfaces (USART)

Two **Intel 8251** USART chips are on the PROCESSEUR board (doc-227, J. Zahn):

| Instance   | Schematic ref | Schematic label       | I/O ports     | Connected to                   |
|------------|--------------|----------------------|---------------|--------------------------------|
| USART 0    | C15          | `adc 4 (permanent I/O)` | 0x04 / 0x05 | Permanent I/O — paper tape reader / modem / RS-232 |
| USART 1    | C13          | `adc 8 (cassette)`   | 0x06 / 0x07   | Cassette tape interface |

The `adc N` label is J. Zahn's notation for "adresse de canal N"
(channel address N), encoding the base I/O port of the device.

Each 8251 uses the standard data/status/command register pair.  Port `+0` is
data; port `+1` is status (read) / command (write).

Status register bits (8251 standard):
- bit 0 = RXRDY (receive data ready)
- bit 1 = TXRDY (transmit register empty)
- bit 2 = TXEMPTY (transmit shift register empty)

Baud rate is set by external jumpers (S7/M1/M5 baud-rate straps visible on the
PROCESSEUR schematic connecting to a header).

---

## 10. Parallel Interface

| Port  | Direction | Name  | Description                                           |
|-------|-----------|-------|-------------------------------------------------------|
| 0x02  | R/W       | PAR   | 8-bit bidirectional data                              |
| 0x03  | R         | SPAR  | Status: bit 0 = RDYP (rx ready), bit 1 = FULP (tx full), bits 6–7 = S6/S7 |

The parallel port is used for printer output (`LP.SY`) and external peripherals.
The CARACTÈRES board hosts the parallel interface logic alongside the character
generator (confirmed on schematic sheet 11-3 by the `INTERFACE PARALLÈLE` label).

---

## 11. Sound / Buzzer

The Smaky 6 has a single **bit-banged buzzer** (piezo / small loudspeaker):

| Port | Bit | Function                            |
|------|-----|-------------------------------------|
| 0x03 | 0   | Buzzer level (0 or 1)               |

The SAMOS `RST 38h` interrupt handler (50 Hz) toggles this bit at the desired
frequency to produce square-wave beeps.  Typical frequencies: 440–4000 Hz.
No dedicated sound chip or timer exists; all tone generation is done by
timed `OUT (03h)` loops in software.

**FPGA note:** Implement as a 1-bit register on port 0x03 write, connected to a
PWM or 1-bit DAC output.  An external RC low-pass filter (≈3.3 kΩ + 10 nF) is
sufficient to drive a small speaker.

---

## 12. Interrupt Architecture

The Z80 operates in **Interrupt Mode 0** throughout (never altered by any
firmware).  In IM 0 the interrupting device places a **full opcode** on the data
bus during the INT acknowledge M1 cycle.

### 11.1 Two interrupt sources share the single INT line

| Source                  | Opcode on bus | Vector | Handler                              |
|-------------------------|---------------|--------|--------------------------------------|
| Display frame timer (50 Hz) | `0xFF` (RST 38h) | 0x0038 | SAMOS frame ISR: keyboard, beeper, floppy ticks |
| Floppy sector-hole NMI  | `0xCF` (RST 08h) | 0x0008 | Floppy stream-read: `LD HL,(0x450F); EX (SP),HL; RET` → 0x025A |

The arbitration rule is simple: if `fdc.nmi_armed` is set (port 0x19 written
with bits 2+3 = 1), return RST 08h; otherwise return RST 38h.

### 11.2 NMI (BREAK key)

The BREAK key drives **NMILOW** (Z80 pin 17) directly.  The NMI vector is at
0x0066 and enters the monitor / reboot menu.

```
SHIFT + BREAK     → reboot from DX0:
FUNCTION + BREAK  → memory POST
BREAK alone       → drop to SAMOS monitor
```

### 11.3 50 Hz frame interrupt

Generated by the AFFICHAGE board's vertical-sync counter.  In SAMOS 2-8:

1. **Stage 1** (RST 38h entry): reads CLA (port 0x00) to capture key.
2. **Stage 2** (one frame later): processes the captured key code, beeper toggle,
   floppy tick, and screen refresh.
3. ISR ACK: `OUT (01h), A=08h` — resets stage flag and re-enables the next frame.

---

## 13. Boot Sequence (Phantom ROM)

```
POWER-ON / RESET
    │
    ▼
Z80 fetches from 0x0000 (Phantom ROM, 2 KB SYS17)
    │
    ├─ Display "ROM de chargement rev 1-7"
    ├─ Wait for boot key:
    │     SHIFT+BREAK          → boot DX0:
    │     FUNCTION+SHIFT+BREAK → boot DX1:
    │     BREAK                → PDP-11 paper-tape loader via USART
    │     FUNCTION+BREAK       → RAM test
    │
    ▼
Phantom ROM reads floppy (NMI-driven, RST 08h):
    ├─ Seek to track 0
    ├─ Load SYS.SY into RAM at 0x5500–0x77FF
    │     (sector-by-sector via port 0x1B streaming)
    │
    ▼
OUT (01h), A=00h  →  MOVROM signal: Phantom ROM chip-select disabled
                      0x0000–0x07FF becomes writable RAM
    │
    ├─ LDIR: copy SYSMON from SYS.SY to 0x0000–0x07FF
    │
    ▼
JP 0x0105  →  SAMOS OS init (EI, install ISR at 0x0038, etc.)
    │
    ▼
Load CLI.SY  →  "SAMOS rev 2-8 / DX0: / >" prompt
```

---

## 14. Signal Glossary

| Signal       | Description                                                           |
|--------------|-----------------------------------------------------------------------|
| `MOVROM`     | "Move ROM out" — disables Phantom ROM, reveals RAM at 0x0000          |
| `HOLDAL/BL`  | Bus request from display / Winchester DMA                             |
| `HOLDA`      | Bus acknowledge from Z80                                              |
| `NMILOW`     | NMI active-low (BREAK key or schematic label `NMILOW`)               |
| `ENALPHA`    | Enable alpha (text) plane output to CRT mixer                         |
| `ENGRA`      | Enable graphic (bitmap) plane output to CRT mixer                     |
| `GROS`       | Double-width character mode (hardware exists; not used by SAMOS 2-8)  |
| `RAS` / `CAS`| DRAM row/column address strobes (4116 chips)                          |
| `WRIOW`      | Write-enable to I/O latches                                           |
| `DELAYSEL`   | Delay-line chip-select used in 4116 RAS timing circuit                |
| `HMBLOW`     | H-blank low — triggers HOLD cycle on every horizontal retrace         |
| `SELWIR`     | Gated write-enable: MREQ + WR + address-selected                     |
| `PETRIL`     | Z80 WAIT input (not driven in standard configuration)                 |
| `INTREDYLOW` | INT from display 50 Hz counter (active-low)                          |
| `INTRECLOW`  | INT from 8251 USART data-ready (active-low)                           |

---

*Document compiled from nine Epsitec schematic documents: six board schematics
by J. Zahn (November 1978: doc-227 CPU, doc-228 MÉMOIRE, doc-229 AFFICHAGE,
doc-230 CARACTÈRES, doc-231 CLAVIER), one extension board schematic by Ronald
Forster (doc-230-memext, October 1979), one micro-floppy controller schematic
(doc-189-191, 3 sheets), and two interface documentation documents (doc-211-212
keyboard text, doc-219-225 parallel interface 7 pp.); the SAMOS 2-8 / Phantom
ROM disassembly; and the Smemu6 emulator source.*
