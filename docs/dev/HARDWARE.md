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
15. [FPGA Implementation Notes](#15-fpga-implementation-notes)

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
| Pixel encoding   | **Nibble-interleaved**: high nibble → even scan line, low nibble → odd scan line |
| Bit order        | Bit 3 of nibble = leftmost pixel (**MSB-first** within nibble) |
| Native size      | 256 × 120 px (64 bytes × 4 px/nibble = 256 wide; 60 pairs × 2 lines = 120 tall) |
| Display size     | 512 × 480 (2× horizontal, 4× vertical stretch) |
| Pixel aspect     | ~1:1 on output (square pixels at 2× stretch match original CRT ~4:3 frame) |

**Why 60 byte-pairs?**  The AFFICHAGE scan counter counts 240 active lines.  The DMA
address counter increments once every 2 lines (not 4), giving 120 unique line addresses
but only 60 unique RAM addresses (each address feeds both an even and an odd line via
the nibble split). Each 64-byte row is read twice (high nibble, then low nibble).

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
Disk-activity bar     : +14 px below → 512 × 494
Function-key bar      : +14 px below → total SDL window 512 × 508 (×2 = 1024 × 1016)
Note: the emulator uses 2× (480 lines) rather than the exact 1.6× (384 lines) to
keep all integer scale factors clean. The smaky6_samos.py image export uses 1.6×.
```

**Phosphor persistence simulation:** The emulator models P31 phosphor remanence
with a per-frame exponential decay applied to `phosphor_buf[]` (a `float` array
parallel to the pixel buffer).  Each frame, live pixels write `1.0f` into the
buffer; dead pixels decay by `phosphor_decay` (default `0.70f`).  The decayed
float is multiplied into the green channel before rendering, so pixels linger
beyond the frame in which they were drawn — matching the slow fade of a real P31
phosphor screen.  Configurable via `-phosphor-decay <0.0..0.99>` or disabled
entirely with `-no-phosphor`.

### 5.0 Hardware block diagram

Source: doc-231 CLAVIER (J. Zahn, Nov 1978).

```mermaid
flowchart LR

subgraph HOST["Host (Z80 system)"]
  Z80["Z80 CPU (~2.5 MHz)\nI/O reads:\n- IN A,(00h)=CLA\n- IN A,(01h)=STATUS"]
  P00["Port 00h (IN)\nCLA keycode (7-bit)\nbit7=0 => key present\nRead side-effect:\nSTROBE_L clears latches\nand restarts scan"]
  P01["Port 01h (IN)\nSTATUS\nbit2 = FOUND\nOUT (ACK) no latch effect"]
  BUS["LS257 x2 (A4,A6)\nBus drivers / mux\n(OE tied to J1 option:\n\"keyboard on bus\")"]
  FUL["FULCLA latch (4013)\nSET on key detect\nRESET by STROBE_L\nNot directly readable"]
  FND["FOUND latch (4013)\nSET on key detect\nRESET by STROBE_L\nReadable: STATUS bit2"]
  DEC["LS138 (A3)\nI/O decode (INTERLOW)"]
end

subgraph KBD["Keyboard board"]
  OSC["4093 (B8)\n300 kHz oscillator / debounce"]
  CNT["4024 (A5)\nDivider / scan sequencing"]
  ARR["ARRIVE ~2.3 kHz\n(divided timing)\nTriggers scan cycle"]
  MUX["4051 x2 (B4,B6)\nColumn select mux"]
  MAT["Keyboard matrix"]
  ENC["S471 EPROM (B5)\nKeyboard encoder\nOutputs keycode[6:0]\nDetects keypress"]
  MODS["Modifiers\nSHIFT/CTRL/CAPSLOCK"]
  FN["7 function keys (direct)\nCURSOR COPY KILL\nPROGRA SHOW SEARCH CHANGE\nNote: read when FOUND=0"]
end

OSC --> CNT
CNT --> ARR
ARR --> MUX
MUX --> MAT
MAT --> ENC
MUX -.-> ENC

MODS --> ENC
FN --> ENC

ENC -->|keycode[6:0]| BUS
ENC -->|SET| FND
ENC -->|SET| FUL

DEC --> BUS
BUS --> P00
BUS --> P01

P00 -->|IN A,(00h)| Z80
P01 -->|IN A,(01h)| Z80

P00 -.->|STROBE_L resets| FND
P00 -.->|STROBE_L resets| FUL
P00 -.->|restart scan| MUX

FND -.->|FOUND controls mux select\n(e.g. LS257 pin1)| BUS
```

```mermaid
flowchart LR
    subgraph Z80IF["Z80 Interface (CLAVIER board)"]
        direction TB
        FULCLA["FULCLA latch\n(event: key detected — not directly readable)"]
        DLATCH["Data latch\n(port 0x00 = CLA)"]
    end

    subgraph KBDSCAN["Keyboard Scanner"]
        direction TB
        FKEYS["7 Function keys\n(direct wired to bits 6–0)\n— read when FOUND=0 —"]
        MATRIX["57-key matrix\n(QWERTZ Swiss)"]
        ROM["Encoder ROM\n(S471 EPROM)\n7-bit key codes"]
        FOUND(["FOUND\nflip-flop"])
        MUX{{"Data mux\nFOUND=1 → ROM output\nFOUND=0 → F-key bits\nbit 7 = NOT-FOUND"}}
    end

    SPECKEYS["BREAK\nSHIFT\nCAPS LOCK"]

    %% Regular key path
    MATRIX -->|scan position| ROM
    ROM -->|bits 6–0| MUX

    %% Function key path (bypasses ROM)
    FKEYS -->|"bits 6–0 direct\n(max 7 simultaneous)"| MUX

    %% FOUND controls the mux and bit 7
    FOUND -->|"select + bit 7 = ~FOUND"| MUX

    %% Data from mux to Z80
    MUX -->|DATA bus| DLATCH

    %% ARRIVE: key pressed → sets FOUND + FULCLA
    FOUND -->|"ARRIVE (inverted)"| FULCLA

    %% STROBE: IN A,(0x00) → clears FOUND + FULCLA
    DLATCH -->|"STROBE\n(generated by IN A,0x00)"| FOUND

    %% Special keys → Z80 directly
    SPECKEYS -->|"RESET(LOW)\nNMI(LOW)"| Z80IF
```

**Key behaviours from the schematic (doc-231):**

**Scan-freeze architecture:** The keyboard continuously scans until a key is detected.
When a pressed key is found the scanner **stops immediately** (`FOUND=1`, `FULCLA=1`,
scan counter frozen).  The CPU reads a perfectly stable, debounced keycode.
This is why no key FIFO is needed: hardware guarantees exactly one key visible at a time.

```
Normal:    scan → scan → scan → scan → …
Key held:  scan → DETECT → FREEZE → HOLD stable keycode → wait for IN A,(00h)
Release:   STROBE clears FOUND+FULCLA → scan restarts → next key (or FOUND=0)
```

- `IN A,(0x00)` (CLA read) generates **STROBE** which simultaneously latches the value
  AND clears FOUND + FULCLA AND **resumes the scan counter**.
- As long as a regular key is physically held the scanner reasserts FOUND within
  **≤200 µs** (one full scan cycle at 300 kHz), giving hardware auto-repeat base rate.
- **N-key rollover is not possible** (`NKRO=NO`): scan freezes on the first detected key;
  simultaneous presses are not detectable.
- Function keys **bypass the encoder ROM** — their state is always on DATA[6:0] when
  FOUND=0, regardless of which combinations are held (up to all 7 simultaneously).
- BREAK generates **NMI(LOW)** (non-maskable interrupt → drops into SYSMON).
  SHIFT+BREAK generates **RESET(LOW)** instead.

**FOUND vs FULCLA distinction:**
- **FOUND** is *level-sensitive*: reflects whether a physical key is currently pressed.
  It is re-asserted within 200 µs if the key is still held when the scan restarts.
- **FULCLA** is *event-sensitive* (latched): set once when the key is first detected,
  cleared by STROBE.  Reading port 0x01 bit 2 reflects FOUND, not FULCLA.
- Optional mode: the hardware supports a `BLOCKING` configuration where the scan
  stays frozen until the key is physically released (prevents repeated re-trigger).

### 5.1 Overview

The Smaky 6 keyboard is an **encoder-per-key** matrix with a dedicated EPROM
translating scan position to a **7-bit ASCII-compatible code**.  The result is
latched by a flip-flop (the "FOUND" flip-flop) and read by the CPU via I/O.

### 5.2 Port protocol

| Port | Direction | Name | Function                                        |
|------|-----------|------|-------------------------------------------------|
| 0x00 | Read      | CLA  | bits[6:0] = key code; bit 7 = NOT-FOUND (1 if no key) |
| 0x00 | Write     | MODE | Display mode register (see §4.1 — same address!) |
| 0x01 | Read      | ST   | bit 3 = always 1; bit 2 = **FOUND** (level: key held) |

Reading port 0x00 **clears FOUND and FULCLA** and **resumes the scan counter**.

> **Official doc note:** *"le registre de status du clavier n'est en fait pas
> nécessaire"* — bit 7 of the CLA byte encodes FOUND state directly, so the
> separate status port read is redundant in practice.

### 5.3 Special keys

| Key               | Code / action                                          |
|-------------------|--------------------------------------------------------|
| BREAK (ESC)       | Generates **NMI** — fires NMILOW pin 17 of Z80         |
| SHIFT+BREAK       | Boot from DX0: (captured before NMI in Phantom ROM)    |
| FUNCTION+SHIFT+BREAK | Boot from DX1:                                    |
| FUNCTION+BREAK    | Memory POST test                                       |
| FUNCTION keys F1–F7 | 7-bit bitmask returned by CLA when FOUND=0 (see §5.5) |
| KILL              | Bit 6 of the function-key bitmask                      |
| TAB               | Code `0x09`; SAMOS CLI inserts `DX1:` at prompt        |
| MACRO             | Code `0x1E` (`«`); replays recorded keystroke sequence  |
| DEFINE            | Code `0x1F` (`»`); starts keystroke recording           |
| BACKSPACE         | Code `0x08`; erases previous character                 |
| DELETE            | Code `0x7F` (▓ block glyph in chargen); deletes current |

### 5.4 Key layout

The keyboard has **57 alphanumeric / punctuation keys** plus **7 function keys**
in a **QWERTZ Swiss** layout.  Uppercase only on alphanumeric characters.

### 5.5 Function key (FOUND=0) return value

When no regular key is pressed (FOUND=0) **and no regular key has been frozen in the
hardware latch**, the CLA port returns a **7-bit bitmask** — one bit per function key
held simultaneously.  Bit 7 is always 1 in this case (matching the NOT-FOUND encoding).
Function key bits are **level-sensitive**: the value reflects the physical state at the
moment `IN A,(0x00)` is executed; they do not latch.

| Key     | Bit | Hex    |
|---------|-----|--------|
| CHANGE  | 0   | `0x01` |
| SEARCH  | 1   | `0x02` |
| SHOW    | 2   | `0x04` |
| COPY    | 3   | `0x08` |
| CURSOR  | 4   | `0x10` |
| PROGRA  | 5   | `0x20` |
| KILL    | 6   | `0x40` |

**Important:** on real hardware, pressing a function key alone produces **no character**
in the SAMOS CLI.  The bitmask is consumed by the `GETFON` / `?GETFON` syscalls only.
In the emulator, `keyboard_read_cla()` returns `0x80 | fonct_bits` when FOUND=0.
The SAMOS ISR Stage 2 (`AND 0x7F; LD (0x4580), A` at `0x016E–0x0170`) stores
`fonct_bits` to the GETFON register naturally every ISR frame, matching hardware.

### 5.6 Swiss-French accent codes

Codes `0x0F–0x1D` in the Smaky 6 chargen map to the 15 Swiss-French accented
characters.  They are dual-use: the chargen ROM renders the glyph, and the SAMOS
printer / serial drivers interpret them as formatting or accent modifiers.

| Hex    | Char (lower/upper) | | Hex    | Char (lower/upper) |
|--------|--------------------|-|--------|--------------------|
| `0x0F` | ü / Ü             | | `0x18` | ô / Ô             |
| `0x10` | à / À             | | `0x19` | ù / Ù             |
| `0x11` | â / Â             | | `0x1A` | û / Û             |
| `0x12` | é / É             | | `0x1B` | ä / Ä             |
| `0x13` | è / È             | | `0x1C` | ö / Ö             |
| `0x14` | ë / Ë             | | `0x1D` | ç / Ç             |
| `0x15` | ê / Ê             | | | |
| `0x16` | ï / Ï             | | | |
| `0x17` | î / Î             | | | |

The emulator receives accented characters as 2-byte UTF-8 via `SDL_TEXTINPUT` events.
`keyboard_text_event()` decodes the UTF-8 codepoint and looks it up in `ACCENT_TABLE[]`
(defined in `src/keyboard.c`) to obtain the Smaky chargen code.

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

1. **Stage 1** (RST 38h entry, 0x015B–0x016D): reads CLA (port 0x00).  If bit7=0 (regular key), stores to 0x457E (syscall 0x0E) and returns early.  If bit7=1, stores `CLA & 0x7F` to 0x4580 (GETFON register) and falls into Stage 2.
2. **Stage 2** (0x016E–0x0183): reads `0x4582` (`INC HL; INC HL; LD A,(HL); CP 0x80; RET Z`).  A direct audit of `SYS.SY` on 2026-05-13 confirmed that the init code writes `0x80` to `0x458A` at `0x00A1`, not to `0x4582`.  The old claim that Stage 2 is "permanently blocked by 0x4582=0x80" is therefore not supported by the binary and should be treated as withdrawn pending re-audit.
3. **Beeper toggle, floppy tick, screen refresh** follow.
4. **ISR ACK**: `OUT (01h), A=08h` — acknowledges the 50 Hz tick.

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

## 15. FPGA Implementation Notes

This section collects actionable information for a clean-room FPGA re-implementation
of the Smaky 6.  Each subsection cross-references the relevant schematic section and
points to the exact emulator behaviour where the hardware was confirmed empirically.

### 15.1 Clock Management

All timing in the machine derives from a single **12.0576 MHz** master crystal.
A typical FPGA board provides a 50 MHz or 100 MHz oscillator; use the board's
PLL/MMCM to synthesize the required frequencies:

| Domain         | Frequency       | Derived by                    | Notes                                              |
|----------------|-----------------|-------------------------------|----------------------------------------------------|
| Pixel clock    | **12.0576 MHz** | PLL output                    | Master oscillator; drives AFFICHAGE scan counter   |
| CPU clock      | **2.4115 MHz**  | Pixel clock ÷ 5               | Implement as a clock-enable (CE) on the master domain, not a true divided clock |
| RTC oscillator | **32.768 kHz**  | Separate CMOS crystal module  | Asynchronous to CPU; 2-FF synchronizer required at I/O domain crossing |

**Recommended practice**: run all synchronous logic on the 12.0576 MHz master domain.
Use a 5-cycle CE counter to gate the Z80 softcore's clock enable.  Never feed a true
divided clock to the softcore — multi-cycle glitches appear on bus signals at
clock-domain boundaries.

Approximation for common boards:

| Board oscillator | PLL ratio to get ~12.0576 MHz | Actual output   | Error   |
|------------------|-------------------------------|-----------------|---------|
| 50 MHz           | × 12 ÷ 50 (or × 6 ÷ 25)     | 12.000 MHz      | −0.05%  |
| 100 MHz          | × 6 ÷ 50                     | 12.000 MHz      | −0.05%  |
| 48 MHz (USB)     | × 4 ÷ 16 (= exact)           | 12.000 MHz      | −0.05%  |
| 27 MHz (HDMI)    | × 4 ÷ 9 + fine-tune          | ~12.000 MHz     | <0.1%   |

A 0.05% error from 12.000 vs 12.0576 MHz is inaudible and invisible at the frame
rate (50.02 Hz vs 50.24 Hz); the Smaky 6 is not time-locked to a broadcast signal.

### 15.2 CPU Subsystem

**Z80 implementation choices:**

| Option               | Notes                                                                    |
|----------------------|--------------------------------------------------------------------------|
| TV80 (Verilog)       | Cycle-accurate; exposes full bus; IM 0 verified; recommended             |
| T80 (VHDL)           | Well-tested; used in MiSTer cores; also exposes `IntE` / data-bus input  |
| Real Z80A / Z80B     | 5 V NMOS; needs 3.3 V level shifters; no softcore overhead               |
| ZXNEXT Z80N          | Custom Z80 extension; avoid unless specifically targeting ZX Next boards |

**Bus arbitration — HOLD cycles (§2.2):**

The AFFICHAGE board asserts `HOLDAL` (`BUSREQ`) once per scan line during the 88
pixel-clock H-blank window to fetch one display byte via DMA.  Implement as a
bus-arbiter FSM with three states:

```
IDLE → REQUESTING (assert BUSREQ) → ACTIVE (bus granted, drive addr, latch byte) → IDLE
```

Cycle budget: 88 pixel clocks = ~7.3 µs.  After BUSACK the Z80 releases the bus in
the next M-cycle boundary (≤ 5 pixel clocks = 1 T-state).  Latch the display byte and
release `BUSREQ` well within the 88-clock window.

**INT and WAIT:**
- `PETRIL` (WAIT): not driven in standard configuration — tie to VCC (inactive).
- `INTREDYLOW` (INT): generated by V-sync counter (line 240) at 50 Hz.
- `INTRECLOW` (USART data-ready INT): not used by SAMOS 2-8; pull high (inactive).

### 15.3 Memory Subsystem

**FPGA BRAM allocation (64 KB total):**

| Region              | Address range       | Size    | BRAM type          | Notes                                                  |
|---------------------|---------------------|---------|--------------------|--------------------------------------------------------|
| Phantom ROM         | 0x0000–0x07FF       | 2 KB    | Initialized ROM    | Pre-load `samos_sys17.rom`; CS disabled by MOVROM latch |
| Lower RAM           | 0x0800–0x3FFF       | ~14 KB  | Single-port BRAM   | —                                                      |
| Alpha framebuffer   | 0x4000–0x44FF       | 1280 B  | **Dual-port BRAM** | Port A = CPU R/W; Port B = display DMA read            |
| OS workspace        | 0x4500–0x45FF       | 256 B   | Single-port BRAM   | Not accessed by display DMA                            |
| Graphic framebuffer | 0x4600–0x54FF       | 3840 B  | **Dual-port BRAM** | Port A = CPU R/W; Port B = display DMA read            |
| SYS.SY area         | 0x5500–0x77FF       | ~8 KB   | Single-port BRAM   | Loaded from floppy at boot                             |
| Upper RAM           | 0x8000–0xFFFF       | 32 KB   | BRAM or ext. SRAM  | Extension board; 16× 4116 = 32 KB on original hardware |
| Character gen ROM   | (separate block)    | 2 KB    | Initialized ROM    | 128 chars × 16 bytes; loaded from `chargen.rom`        |

Total: 64 KB data RAM + 2 KB chargen ROM = 66 KB = 528 Kbit of BRAM.

**MOVROM latch:**
A 1-bit D register, synchronously cleared by system RESET and set by the decode
`(IORQ & WR & addr[5:0] == 0x01 & data == 0x00)`.  While clear, address range
0x0000–0x07FF maps to the Phantom ROM block; while set, it maps to the lower
RAM block at the same addresses.

**4116 DRAM refresh**: irrelevant for FPGA BRAM.  Ignore `RFSH` / `REFRESH` entirely.

### 15.4 Display Subsystem

#### 15.4.1 Scan counter — horizontal and vertical timing

```
Pixel counter : 0–599 (modulo 600)
  Active pixels : 0–511  (512 px)
  H-blank       : 512–599 (88 px = ~7.3 µs @ 12.0576 MHz)
  HOLDAL asserted: pixel 512–599 (one bus-cycle steal during H-blank)

Line counter : 0–400 (modulo ~401)
  Active lines  : 0–239  (240 lines)
  V-blank       : 240–400 (~161 lines)
  INT (50 Hz)   : fired at line 240 pixel 0

Frame rate: 12.0576 MHz ÷ 600 ÷ 401 ≈ 50.14 Hz
```

The signal `HMBLOW` (H-blank low) is the internal name for the H-blank trigger
that fires `HOLDAL`.

#### 15.4.2 Video output options

**Composite video (original — 20.1 kHz non-standard):**
- H-sync: active-low pulse, ~57 pixel clocks wide (4.7 µs).
- V-sync: active-low, ~3 lines (PAL-style; exact width from AFFICHAGE schematic).
- Video level: 0 = black (0.3 V into 75 Ω), 1 = white (1.0 V).
- R-2R ladder DAC: use 3 levels (sync, black, white) with a 2-resistor ladder.
- Most modern LCD monitors will not sync to 20.1 kHz — composite output only works
  with a CRT or with an external scan converter (GBS-8220 or similar).

**VGA output (31.5 kHz / 60 Hz — recommended for FPGA prototyping):**
- Use a line-doubling scan converter implemented in FPGA BRAM.
- Pixel clock for VGA 640×480@60: 25.175 MHz; derive from PLL alongside 12.0576 MHz.
- Map the 512-pixel Smaky line to VGA 640 columns by centering (64 blank px each side).
- Each Smaky scan line is output twice to convert 240 lines / 50 Hz → 480 lines / 60 Hz.
- VSYNC polarity: negative (active-low) for 640×480@60; HSYNC: negative.

**HDMI output (best quality):**
- Use a DVI/HDMI TMDS IP core (e.g. `hdmi` from projf.io or digilent's DVI core).
- Target 720×576@50 Hz or 640×480@60 Hz for standard EDID compatibility.
- Scale 512×240 native to target resolution with integer or bilinear scaling.

#### 15.4.3 Character generator pipeline (CARACTÈRES board)

```
Pixel col ÷ 8 → char column index
Line ÷ 12     → char row index            (12 scan lines per char row)
Line mod 12   → scan line within char cell (0–7 for glyph, 8–11 for spacing)

Chargen address = char_code × 16 + scan_within_cell

Data byte → 8-bit shift register (load every 8 pixel clocks)
  Shift out LSB first: bit 0 = leftmost pixel
  (74S262 confirmed: bit 0 serialized first — LSB-first)
```

Note: VIDEO_CHAR_H is 12 in the emulator (20 rows × 12 lines = 240 active lines),
not 8 as the physical ROM has glyph rows.  The ROM rows 8–11 are always 0x00
(blank spacing), so rows 0–7 hold the actual glyph and rows 8–11 add the
inter-row gap.

#### 15.4.4 Graphic plane pipeline

```
Scan pair index = active_line ÷ 2        (0–119; 120 unique RAM lines)
BRAM address = (scan_pair ÷ 2) × 64 + col  (60 unique byte-pair rows × 64 cols)

Even line (active_line even):  pixel_nibble = data_byte[7:4]  (high nibble)
Odd  line (active_line odd):   pixel_nibble = data_byte[3:0]  (low nibble)

Nibble serialization (MSB-first):
  bit 3 → pixel 0 (leftmost)
  bit 2 → pixel 1
  bit 1 → pixel 2
  bit 0 → pixel 3 (rightmost)

Each nibble produces 4 native pixels; each native pixel = 2 master pixel clocks wide
→ 4 nibbles × 4 pixels × 2 clocks = 32 master clocks per byte column (matches
  64 bytes × 32 clocks = 2048 clocks / 512 px × 2 = confirmed consistent).
```

Superimpose mode (ENALPHA + ENGRA both set):
```
video_out = graphic_px ? LIT : alpha_px
```
Priority: graphic pixel 1 always wins over alpha character pixel.

#### 15.4.5 DMA line-buffer ping-pong

The AFFICHAGE board uses two Intel 2101 (256×4 bit) SRAMs as ping-pong line
buffers — one fills from DMA while the other serializes to the CRT pipeline:

```
Frame N, line L:
  Buffer A → serialize to CRT output (reading)
  Buffer B → DMA fill for line L+1 during H-blank (writing)

H-blank transition:
  Swap A↔B roles
  B (just filled) becomes the new serialize-source
  A (just emptied) becomes the new DMA-fill target
```

On FPGA: two 64-byte dual-port BRAM blocks with alternating port-role selection
driven by the H-blank flag.  Write-port width: 8 bits.  Read-port width: 1 bit
(pixel serial) or 8 bits (byte for the shift register).

### 15.5 Interrupt Architecture

The Z80 runs in **IM 0** throughout (see §12).  During the INT acknowledge
M-cycle (`M1` + `IORQ` both asserted), an interrupt controller must place an
opcode on the data bus:

| Condition          | Opcode on bus | RST vector | Handler purpose                 |
|--------------------|---------------|------------|---------------------------------|
| `fdc_nmi_armed = 0`| `0xFF`        | 0x0038     | 50 Hz SAMOS frame ISR           |
| `fdc_nmi_armed = 1`| `0xCF`        | 0x0008     | Floppy sector-hole stream-read  |

**Bus driver requirement**: during the INT ack cycle, the data bus must be driven
by the interrupt logic (not the memory).  In a real-Z80 design: a 74HCT245
transceiver with `OE# = !(M1 & IORQ)` drives the appropriate opcode byte.
In a softcore (TV80): the `int_vector` port is sampled by the core on the ack
cycle; drive it with the appropriate opcode based on `fdc_nmi_armed`.

**NMI from BREAK key**: edge-triggered, no data-bus cycle required.  The Z80
vectorises to 0x0066 automatically.  On FPGA: 2-FF synchronizer on the BREAK
GPIO, then a rising-edge detector to generate a 1-cycle pulse on `NMI#`.

**50 Hz INT**: 1-cycle pulse generated when the scan line counter reaches 240
(V-sync entry).  The interrupt is re-enabled by the SAMOS ISR via
`OUT (01h), A=08h` (ISR ACK, §12.1.3); model this as a level-sensitive INT that
is cleared by the ack write.

### 15.6 Floppy Controller

The Micropolis hard-sectored drive is discontinued.  Implement using SD card,
SPI flash, or USB mass storage as the backing medium.

**Hard-sector NMI timing:**

```
Sector period   = 60 s ÷ (300 RPM × 16 sectors) = 12.5 ms
NMI rate        = 80 Hz
Sector index    = free-running modulo-16 counter driven by 12.5 ms timer
Master clocks per sector = 12_057_600 ÷ 80 = 150_720 cycles
```

Use a 17-bit counter (0–150719) clocked at 12.0576 MHz.  On carry: toggle NMI
and increment the sector index register (port 0x19 bits[3:0]).

**Sector byte-stream state machine:**

```
State IDLE:
  Waiting for Z80 to read port 0x1B
  Byte-ready flag (port 0x1A bit 7) = 1 (always asserted)

On first IN(1Bh) after sector NMI:
  Emit byte 0 = 0x00 (sync)        → byte_pos = 0
  Emit byte 1 = sector_id          → byte_pos = 1
  Emit bytes 2–257 = sector data   → byte_pos 2–257
  Emit byte 258 = checksum (sum of data bytes mod 256)
  Then return to IDLE
```

**Seek-busy (port 0x19 bit 6):**
Assert bit 6 for ~40 sector-clock counts (≈2 ms at 80 Hz) after each step pulse
rising edge on port 0x1A bit 2.  The SAMOS floppy driver polls bit 6 until clear
before starting the sector read.

**Drive select (port 0x19 bits 5/6):**
Bits 5 and 6 select DX0 or DX1 respectively.  Route to a 2-way mux feeding the
storage backend (e.g. two SD card slots, or two image files via an SPI flash
directory).

### 15.7 Winchester Controller

The WD1000/WD1001/WD1002 register set (§8) maps cleanly to a small FSM.

**DMA implementation (replacing the CPU INIR loop):**

```
1. CPU writes READ command to port 0x27.
2. Controller asserts HOLDBL (BUSREQ to Z80).
3. After BUSACK: DMA engine issues 256 write cycles on the bus,
   writing sector bytes to the target RAM address.
4. Deassert BUSREQ; set DRQ bit in status register.
5. CPU reads status 0x50 (RDY + SC, DRQ clear) — transfer complete.
```

Target RAM address: the Phantom ROM uses the stack pointer `SP` as the destination
for disk reads (confirmed from disassembly at 0x0370–0x0398).  The DMA controller
must capture `SP` from the Z80 registers before asserting BUSREQ, or the FPGA
implementation may skip true DMA and let the CPU read via `INIR` (simpler, as in
the emulator).

**Storage backend:** A standard SPI SD card at CMD17/CMD18 sector-read.  Each SD
sector is 512 bytes; use the first 256 bytes of each SD sector to match the Smaky
sector size, or pack two Smaky sectors per SD sector for efficiency.

```
SD sector number = CHS_to_LBA(cylinder, head, sector)
LBA = cylinder × 192 + head × 32 + sector
```

### 15.8 RTC (E405 replacement)

**Option A — Soft RTC (all in FPGA, no external chip):**

Synthesize 32.768 kHz from the master clock:
```
CE period = 12_057_600 ÷ 32_768 ≈ 368 master cycles per RTC tick
```
Drive a 7-byte BCD register file with the E405 register layout (§3.5 table).
Implement the 3-wire serial decode (4-bit command + 7-byte read/write) as a
simple FSM on port 0x08.  On FPGA reset, load default values (e.g. 00:00:00
01/01/00 Mon).

**Option B — External DS3231 with bridge (recommended for accurate timekeeping):**

Wire a DS3231 SOP-8 module to two FPGA GPIO pins (SDA, SCL) plus a 3.3 V supply.
Implement an I²C master in the FPGA and a protocol bridge FSM that translates
E405 3-wire serial reads/writes to DS3231 I²C transactions.

E405 → DS3231 register mapping:

| E405 byte | E405 content | DS3231 register | DS3231 address |
|-----------|--------------|-----------------|----------------|
| 0         | hours (BCD)  | Hours           | 0x02           |
| 1         | minutes (BCD)| Minutes         | 0x01           |
| 2         | day (BCD)    | Date            | 0x04           |
| 3         | month (BCD)  | Month           | 0x05           |
| 4         | year (BCD)   | Year            | 0x06           |
| 5         | weekday (1–7)| Day             | 0x03           |
| 6         | seconds (BCD)| Seconds         | 0x00           |

Both chips use BCD encoding.  DS3231 weekday is 1–7 with the same 1=Monday
convention as the Smaky 6 (confirm against local calendar at integration time).

### 15.9 Keyboard Interface

**PS/2 keyboard (recommended):**

Implement a PS/2 receiver FSM clocked on PS/2 CLK falling edge (open-collector,
pull up to 3.3 V through 4.7 kΩ).

Translation table: map PS/2 scan code set 2 extended codes to Smaky 7-bit codes.
The `KEY_TABLE[]` array in `src/keyboard.c` provides the mapping; synthesize into
an 8-bit × 256 LUT stored in BRAM or LUT RAM.

Key FPGA signals:

| Signal   | Direction | Description                                           |
|----------|-----------|-------------------------------------------------------|
| `FOUND`  | output    | 1-bit D register: set on valid key strobe             |
| `CLA`    | output    | 7-bit key code register; cleared to 0 on `IN (00h)`  |
| `NMI#`   | output    | Pulse from BREAK key (also F11/Pause scan code)       |
| `SHIFT`  | internal  | Latched shift state at BREAK key press time           |

**FOUND flip-flop**: set by the PS/2 receiver on a valid make-code strobe.
Cleared on the first `IN A,(00h)` (CLA read, port 0x00).

**SHIFT + BREAK boot convention:**
At the moment BREAK fires, the PS/2 receiver must latch the current SHIFT state.
The CLA byte returned on the subsequent `IN (00h)` during Phantom ROM boot is:
- `0x00` (NUL/Enter) → boot DX0:
- `0x60` (SHIFT modifier bit set) → boot DX1:
(See Phantom ROM analysis; the exact encoding is inferred from firmware behaviour.)

### 15.10 Parallel Interface

Ports 0x02/0x03 (CARACTÈRES board):

| Port | R/W | Signal | FPGA implementation                           |
|------|-----|--------|-----------------------------------------------|
| 0x02 | R/W | PAR    | 8-bit bidirectional I/O register; direction controlled by SPAR FULP flag |
| 0x03 | R   | SPAR   | 2-bit status: bit0=RDYP (ext. READY), bit1=FULP (output full) |
| 0x03 | W   | BEEP   | 1-bit buzzer register (bit 0)                 |

Centronics printer interface: connect PAR to data lines D0–D7, assert a
STROBE pulse on write, sample BUSY (→ FULP bit 1) from printer.

### 15.11 Sound Output

The buzzer bit (port 0x03 bit 0) is a 1-bit output.  Two FPGA output options:

**Direct 1-bit output (simplest):**
```
FPGA GPIO → 33 Ω series resistor → 8 Ω speaker (+ capacitor for DC blocking)
```
Audible but low fidelity.  Safe for a small piezo buzzer.

**RC-filtered output:**
```
FPGA GPIO → 3.3 kΩ → node → 10 nF to GND → speaker / line out
```
Corner frequency ≈ 4.8 kHz, sufficiently above the 440–4000 Hz beep range.
The RC filter also eliminates FPGA switching noise.

**PWM/Σ∆ modulator (best quality):**
Oversample the 1-bit stream at the master clock rate into a 1-bit Σ∆ DAC.
Filter output with a simple RC and feed a DAC or audio codec.

### 15.12 Serial Interfaces (USART)

Two Intel 8251 instances (§9) at 0x04–0x07.  Replace with open-source UART cores:

- Any 8-N-1 UART core is compatible.  The 8251 status register (RXRDY at bit 0,
  TXRDY at bit 1, TXEMPTY at bit 2) must be replicated precisely — SAMOS tests
  these bits with bitwise AND masks before reading/writing.
- Baud rate: strapped on original hardware.  On FPGA, use a configurable clock
  divider or synthesize exact baud rates from the 12.0576 MHz master.

| Instance | Ports     | Physical interface | Typical baud |
|----------|-----------|--------------------|-------------|
| USART 0  | 0x04/0x05 | RS-232 (DB9) or USB-UART | 1200–9600 |
| USART 1  | 0x06/0x07 | Cassette audio (600 baud FSK) or second RS-232 | 600–4800 |

For cassette emulation (USART 1): a Kansas City Standard (KCS) FSK modulator/
demodulator can be added as an FPGA block to read/write `.wav` files on SD card.

### 15.13 Power Budget

An all-FPGA implementation eliminates the ±12 V / −5 V rails required by the
4116 DRAM chips.  Expected power rails:

| Rail     | Min current | Usage                                               |
|----------|-------------|-----------------------------------------------------|
| +3.3 V   | 300 mA      | FPGA I/O banks, SRAM, UART transceivers, LED drivers |
| Core voltage | 200 mA  | Device-specific (1.0–1.2 V for iCE40/ECP5/Artix-7) |
| +5 V     | 50 mA       | Only if using a real Z80A chip; use level shifters  |

A standard USB 5 V (500 mA) supply is sufficient for an iCE40 or ECP5 based
design.  Artix-7 boards typically have on-board regulators accepting 5–12 V.

### 15.14 Recommended FPGA Targets

| Board (approx. cost) | FPGA                  | BRAM budget | Open toolchain | Notes |
|----------------------|-----------------------|-------------|----------------|-------|
| iCE40 HX8K Breakout (~$25) | Lattice iCE40 HX8K | 32 × 4 Kbit | IceStorm + nextpnr | Tight on BRAM; needs careful packing; no PLL for exact 12.0576 MHz |
| iCE40 UP5K Breakout (~$10) | Lattice iCE40 UP5K | 1 Mbit SPRAM | IceStorm + nextpnr | Single-port SPRAM only; dual-port video RAM requires time-multiplexing |
| Colorlight 5A-75B (~$15) | Lattice ECP5-25F | 56 × 18 Kbit | nextpnr-ecp5/Yosys | Comfortable; HDMI output possible; open toolchain |
| Colorlight i5 / i9 (~$25) | Lattice ECP5-25/45F | same | nextpnr-ecp5/Yosys | Adds DDR3 for frame buffer if needed |
| Digilent Arty A7-35T (~$130) | Xilinx Artix-7 35T | 1.8 Mbit | Vivado webpack (free) | Most comfortable; PLL can hit 12.0576 MHz exactly; easy prototyping |
| **Tang Nano 2K** (~$5) | Gowin GW1NZ-2 | 72 Kbit BSRAM, no PSRAM | Gowin EDA / apicula | **Insufficient** — BRAM exhausted by video RAM alone; no RAM for CPU space; LUT count too low; see §15.14 note |
| **Tang Nano 4K** (~$8) | Gowin GW1NSR-4C | 180 Kbit BSRAM + 64 Mbit PSRAM | Gowin EDA (free) / apicula+nextpnr | Tight but feasible with careful optimisation; see §15.14.1 |
| **Tang Nano 9K** (~$10) | Gowin GW1NR-9C | 468 Kbit BSRAM + 64 Mbit PSRAM | Gowin EDA (free) / apicula+nextpnr (experimental) | Recommended Gowin target; see §15.14.2 |
| **Altera DE1** (educational) | Altera Cyclone II EP2C20 | 239 Kbit M4K | Quartus II 13.0sp1 (free) | See §15.14.3 |
| **MiSTer FPGA** (DE10-Nano) | Intel Cyclone V SX 5CSEBA6 | 5.5 Mbit M10K | Quartus Prime Lite (free) | See §15.14.4 |
| **1ChipMSX** (Kay Nishi / MSX Assoc., 2006) | Altera Cyclone EP1C12 | 208 Kbit M4K + 32 MB SDRAM | Quartus II 11.0sp1 (legacy) | See §15.14.5 |

**Minimum viable target**: the ECP5-based Colorlight boards (used in LED-wall
controllers) are widely available and have sufficient BRAM, PLLs, and I/O for
a complete Smaky 6 implementation without external SRAM.  Within the Gowin Tang
Nano family, the **Tang Nano 9K** is the recommended minimum; the Tang Nano 4K
is a borderline fit requiring careful resource optimisation (see §15.14.1); the
Tang Nano 2K is definitively insufficient (see note below).

**Tang Nano 2K — why it is insufficient:**
The GW1NZ-2 provides only 2,304 LUTs, 72 Kbit BSRAM, and no on-chip PSRAM:

| Resource        | GW1NZ-2 available | Smaky 6 minimum | Verdict |
|-----------------|-------------------|-----------------|---------|
| LUTs            | 2,304             | ~4,000–4,500    | Short by ~2×  |
| Block SRAM      | 72 Kbit (4 blocks)| ~74 Kbit for video RAM + chargen + Phantom ROM alone | Already over budget |
| On-chip RAM     | none              | 64 KB CPU space | No solution |
| External SRAM   | none on-board     | —               | No solution |
| HDMI / VGA      | none on-board     | Required        | No output path |
| SD card         | none on-board     | Required        | No storage path |

The BSRAM alone is the hard blocker: the dual-port alpha framebuffer (1280 B),
graphic framebuffer (3840 B), chargen ROM (2 KB), and Phantom ROM (2 KB) together
require ~74 Kbit, leaving nothing for line buffers or FIFOs.  LUT count and the
absence of any RAM for the CPU address space make the gap unbridgeable.

#### 15.14.1 Sipeed Tang Nano 4K

The Tang Nano 4K (GW1NSR-4C) sits between the 2K and 9K: it has the same 64 Mbit
on-chip PSRAM as the 9K but only 4,608 LUTs and 180 Kbit BSRAM.  A complete Smaky 6
implementation is feasible but requires careful resource management.

| Feature         | Specification                                                        |
|-----------------|----------------------------------------------------------------------|
| FPGA            | Gowin GW1NSR-4C — 4,608 LUTs, 3,456 flip-flops, 1 PLL               |
| Block SRAM      | 10 × 18 Kbit BSRAM = **180 Kbit** (~22 KB)                          |
| On-chip PSRAM   | **64 Mbit (8 MB)** — same die as GW1NR-9C; same PSRAM controller     |
| Video output    | **HDMI** (micro-HDMI connector) — native TMDS pins                   |
| Keyboard        | No PS/2 — add via PMOD/GPIO header                                   |
| Mass storage    | **TF (microSD)** slot — on-board                                     |
| Audio           | No DAC — buzzer via GPIO → RC filter                                 |
| Programming     | USB-C (on-board BL702 JTAG bridge)                                   |
| Toolchain       | Gowin EDA Education Edition (free) / `apicula` + `nextpnr-himbaechel` (GW1N-4 experimental) |

**BSRAM constraint analysis** (10 × 18 Kbit = 180 Kbit = ~22 KB):

| Block               | Size    | BSRAM blocks | Port type needed |
|---------------------|---------|-------------|-----------------|
| Alpha framebuffer   | 1280 B  | 1           | Dual-port (CPU + DMA) |
| Graphic framebuffer | 3840 B  | 3           | Dual-port (CPU + DMA) |
| Character gen ROM   | 2048 B  | 1           | Single-port (read-only) |
| Phantom ROM         | 2048 B  | 1           | Single-port (read-only) |
| Ping-pong line buffers (×2) | 128 B each | 1 (shared) | Dual-port |
| **Subtotal (required)** | ~9.4 KB | **7 blocks** | — |
| USART FIFOs, misc   | ~256 B  | 1           | Single-port |
| **Total required**  | ~9.7 KB | **8 blocks** | — |
| **Remaining**       | 1.4 KB  | **2 blocks** | Available for extra buffering |

At 8 of 10 blocks consumed, the BSRAM budget is tight but workable.  No BSRAM
can be used for the CPU address space — it must go entirely into PSRAM.

**LUT constraint analysis** (4,608 LUTs available):

| Subsystem                    | Estimated LUT cost | Notes |
|------------------------------|--------------------|-------|
| TV80 / T80 Z80 softcore      | ~1,500–2,000       | Depends on pipeline depth and register implementation |
| Video scan FSM + serializers | ~400–600           | Scan counter, two shift registers, MUX, line-buffer arbiter |
| Floppy controller FSM        | ~200–300           | Sector timer, byte-stream state machine, step/direction logic |
| Winchester controller FSM    | ~150–200           | Register file + simple FSM |
| RTC (soft option A)          | ~100–150           | 7-byte BCD register file + serial decode |
| Keyboard PS/2 receiver       | ~100–150           | Shift register + translation ROM address decode |
| USART × 2 (simple 8-N-1)     | ~200–300           | One small UART core each |
| PSRAM controller             | ~300–500           | Gowin IP core (uses LUT-based glue) |
| Bus arbiter + glue           | ~150–200           | MOVROM latch, address decode, INT controller |
| **Total estimated**          | **~3,100–4,200**   | Within 4,608 LUT budget at low-to-mid estimate |

The design **fits** in the LUT budget at optimistic estimates; it becomes risky at
pessimistic estimates.  Practical mitigation strategies:

1. **Use the T80 over TV80**: the T80 (VHDL) typically synthesises ~10–15% more
   efficiently than TV80 (Verilog) on Gowin targets due to better FF packing.
2. **Share the character generator BSRAM** between the chargen read port and the
   Phantom ROM read port by time-multiplexing (they are never active simultaneously:
   the Phantom ROM is only accessed during the 2.5 MHz CPU cycle; the chargen is
   only read during the 12.0576 MHz pixel clock cycle).  This saves 1 BSRAM block.
3. **Infer LUT RAM** for the keyboard translation table (256 × 7 bit = 1792 bits —
   fits in ~28 LUTs as distributed RAM on GW1N), freeing a BSRAM block.
4. **Use the Gowin PSRAM IP core** rather than a custom controller: the IP core is
   optimised for the GW1NSR-4C and verified against the PSRAM timing spec.
5. **Fold the Winchester controller registers** into the PSRAM address space rather
   than implementing them as separate flip-flop registers, saving ~50 LUTs.

**Single PLL limitation**: the GW1NSR-4C has only one PLL (vs two on the GW1NR-9C).
Use the PLL to generate 12.0576 MHz; derive the HDMI pixel clock (25.175 MHz)
from the same PLL using a second output channel.  Both can be generated
simultaneously if the PLL VCO frequency is set to ~300–600 MHz and the two output
dividers are chosen accordingly (e.g. VCO = 361.728 MHz → ÷30 = 12.0576 MHz,
÷14 = 25.837 MHz; or VCO = 453.6 MHz → ÷37.6 ≈ requires fractional divider).
The Gowin EDA PLL configuration wizard will find exact integer ratios.

**Verdict**: the Tang Nano 4K is a viable but constrained target.  Expect to spend
significant time on LUT optimisation to fit the Z80 softcore alongside the video
subsystem.  The Tang Nano 9K (§15.14.2) is preferred if cost is not the primary
constraint — it costs only ~$2 more and removes all resource pressure.

#### 15.14.2 Sipeed Tang Nano 9K

The Tang Nano 9K is an ultra-low-cost (~$10) Gowin FPGA board that ships with
an HDMI connector, SD card slot, and on-chip PSRAM — making it one of the most
capable boards per dollar for a retro-computer core:

| Feature         | Specification                                                        |
|-----------------|----------------------------------------------------------------------|
| FPGA            | Gowin GW1NR-9C — 8064 LUTs, 6480 flip-flops, 2 PLLs                 |
| Block SRAM      | 26 × 18 Kbit BSRAM = **468 Kbit** (~58 KB) — all video RAM + chargen fit in BRAM with room to spare |
| On-chip PSRAM   | **64 Mbit (8 MB)** pseudo-SRAM — use for full 64 KB CPU address space; no external SRAM needed |
| Video output    | **HDMI** (on-board micro-HDMI connector) — native TMDS pins; use an open HDMI TMDS core |
| Keyboard        | No PS/2 connector — add via PMOD or use 4-pin GPIO header with 4.7 kΩ pull-ups |
| Mass storage    | **TF (microSD) card** slot — on-board; use for floppy / Winchester images |
| Audio           | No on-board DAC — buzzer output via GPIO → RC filter → speaker (§15.11) |
| Programming     | **USB-C** (on-board BL702 USB-JTAG bridge) — no separate programmer needed |
| Toolchain       | **Gowin EDA** (Education Edition, free license from Gowin website) **or** experimental open-source: `apicula` + `nextpnr-himbaechel` (GW1N-9 support in progress as of 2025) |

**PSRAM note**: the GW1NR-9C integrates a 64 Mbit HyperRAM-compatible PSRAM
die in the same package as the FPGA fabric.  This PSRAM is single-port and has
a burst-access latency of ~80 ns (6 cycles at 12 MHz).  For the CPU address
space (0x0000–0xFFFF = 64 KB), PSRAM is ample but introduces wait states:

- Assert `PETRIL` (Z80 WAIT) for 2–3 extra clock cycles on every MREQ cycle
  targeting PSRAM (i.e. all addresses except dual-port BRAM video regions).
- BRAM regions (video RAM 0x4000–0x54FF, chargen ROM) are accessed at full
  speed — no wait states needed there.
- The SAMOS OS runs a 2.4 MHz Z80; 3 extra wait states = 4 T-states total =
  ~1.66 µs per access.  PSRAM latency at 12 MHz is ~250 ns (3 cycles), well
  within this budget.

**BRAM allocation on GW1NR-9C** (26 × 18 Kbit = 468 Kbit):

| Block               | Size    | BRAM blocks used |
|---------------------|---------|-----------------|
| Alpha framebuffer   | 1280 B  | 1 × 18 Kbit      |
| Graphic framebuffer | 3840 B  | 3 × 18 Kbit      |
| Character gen ROM   | 2048 B  | 1 × 18 Kbit      |
| Phantom ROM         | 2048 B  | 1 × 18 Kbit      |
| **Total**           | ~9.2 KB | **7 blocks** (19 free for FIFO, line buffers, CPU pipeline) |

Remaining 19 BSRAM blocks (342 Kbit) are available for the ping-pong line
buffers (§15.4.5), USART FIFOs, and any additional buffering.

**HDMI output**: the GW1NR-9C has dedicated TMDS output pairs.  Use the
`Tang_Nano_9K_HDMI` example from Sipeed's GitHub as a starting point; it
provides a proven 720×480p or 640×480p HDMI output module with a 25 MHz pixel
clock (easily co-generated by the PLL alongside 12.0576 MHz).

**PLL configuration**: the GW1NR-9C has two PLLs.  Use PLL0 to generate:
- 12.0576 MHz (Smaky master / pixel clock) from the 27 MHz on-board oscillator
  using ratio 12/27 → 12.000 MHz (0.05% error; acceptable — see §15.1).
- 25.175 MHz (VGA/HDMI pixel clock) from the same PLL using a second output.

**Toolchain**: Gowin EDA Education Edition is free but requires online
registration.  The open-source `apicula` project (reverse-engineered bitstream)
+ `nextpnr-himbaechel` backend adds GW1N-9 support; synthesis via Yosys.
As of mid-2025 basic BRAM and PLL primitives are supported but the PSRAM
controller requires the Gowin IP core library (proprietary).  A hybrid flow
(Yosys synthesis → Gowin place-and-route) is also possible.

#### 15.14.3 Altera DE1

The Terasic DE1 is a classic university FPGA board, still widely available second-hand:

| Feature         | Specification                                                       |
|-----------------|---------------------------------------------------------------------|
| FPGA            | Altera Cyclone II EP2C20F484C7 — 20,060 LEs, 52 M4K blocks         |
| Embedded RAM    | 52 × 4 Kbit M4K = **239 Kbit** (~29 KB) — sufficient for video RAM and chargen; main 64 KB RAM must use on-board SRAM |
| External SRAM   | **512 KB** (2× IS61LV25616AL, 10 ns) — single-port; use for main CPU address space |
| Video output    | **VGA** (4-bit R/G/B DAC per channel via resistor ladder) — native; perfect for scan-doubled 640×480@60 Hz |
| Keyboard        | **PS/2** connector — native; direct connection to keyboard FSM (§15.9) |
| Mass storage    | **SD card** slot — use for floppy and Winchester disk images         |
| Audio output    | 24-bit Wolfson WM8731 codec — can drive the buzzer output via I²S   |
| Programming     | USB Blaster (on-board) — programming and JTAG debug                 |
| Toolchain       | Altera Quartus II 13.0sp1 (last version supporting Cyclone II; free Web Edition) |

**Memory map implementation note**: the on-board SRAM (512 KB) is ample for the
entire 64 KB address space.  Allocate:
- SRAM bank 0 (lower 256 KB): CPU address space 0x0000–0xFFFF (mapped to SRAM A[15:0]).
- Retain M4K BRAM for: alpha framebuffer (dual-port), graphic framebuffer (dual-port),
  character generator ROM.

The 512 KB SRAM is single-port; the CPU and display DMA cannot access it
simultaneously.  Use the HOLD cycle arbitration (§15.2) to multiplex access:
during H-blank the DMA holds the bus, reads from SRAM into a BRAM line buffer,
then releases.  The video serializer drains from the BRAM line buffer independently.

**PLL**: Cyclone II PLLs can generate 12.0576 MHz from the 50 MHz on-board
oscillator using integer ratios (50 × 12 ÷ 50 = 12.000 MHz; error < 0.05%).

#### 15.14.4 MiSTer FPGA (DE10-Nano)

MiSTer is the dominant open-source retro-computing FPGA platform.  A Smaky 6
core would integrate naturally into the MiSTer ecosystem:

| Feature         | Specification                                                       |
|-----------------|---------------------------------------------------------------------|
| FPGA            | Intel Cyclone V SX 5CSEBA6U23I7 — 41,500 ALMs, hard ARM Cortex-A9 HPS |
| Embedded RAM    | 312 M10K blocks × 10 Kbit = **5.5 Mbit** — all 66 KB fit entirely in BRAM; dual-port trivially available |
| External RAM    | Optional 128 MB DDR3 add-on board (MiSTer SDRAM module) — not required for Smaky 6 |
| Video output    | **HDMI** (native FPGA pins via ADV7513 on I/O board) — MiSTer framework handles scaler and EDID |
| Keyboard        | **USB keyboard** via HPS USB hub — MiSTer framework handles PS/2 emulation and key remapping |
| Mass storage    | **SD card** via HPS — MiSTer framework provides OSD file selector for disk images |
| Audio           | Analog audio via I/O board 3.5 mm jacks — MiSTer framework provides volume control |
| Toolchain       | Intel Quartus Prime Lite (free) + MiSTer build scripts              |

**MiSTer framework advantages** for a Smaky 6 core:
- **OSD (On-Screen Display)**: built-in overlay for loading floppy / Winchester images from SD card, selecting ROM files, and setting options — replaces the command-line launcher entirely.
- **TV80 precedent**: the TV80 Z80 softcore is already used in proven MiSTer cores (ZX Spectrum, CPC, MSX, Colecovision); the integration path is well-documented.
- **Scan doubler / scaler**: MiSTer's `video_mixer` module handles 15 kHz → HDMI upscaling, so the Smaky 6's 20.1 kHz composite timing can be fed directly and displayed on any HDMI monitor.
- **Save states**: the MiSTer framework optionally supports RAM snapshot / restore, useful for debugging the Phantom ROM boot sequence.

**BRAM allocation on Cyclone V**: the 5.5 Mbit M10K budget is 83× the 66 KB needed.
All memory regions — including all dual-port video RAM — fit without compromise.
The character generator ROM, Phantom ROM, and all RAM can coexist in BRAM with
no external SRAM required, making the DE10-Nano the most comfortable single-board
target for a Smaky 6 core.

**Recommended first target** if developing a MiSTer core: fork an existing Z80
MiSTer core (e.g. `MiSTer-devel/ZX48K` or `MiSTer-devel/MSX`) and replace the
Z80 peripheral wiring with the Smaky 6 I/O map (§6).  The TV80 softcore, clock
generation, and MiSTer top-level wiring can be reused verbatim.

#### 15.14.5 1ChipMSX (Kay Nishi / MSX Association, 2006)

The 1ChipMSX is a commercial board that implemented the complete MSX-2+ computer
on a single Altera Cyclone FPGA chip plus SDRAM:

| Feature         | Specification                                                       |
|-----------------|---------------------------------------------------------------------|
| FPGA            | Altera Cyclone EP1C12Q240C8 — 12,060 LEs, 52 M4K blocks            |
| Embedded RAM    | 52 × 4 Kbit M4K = **208 Kbit** (~26 KB) — insufficient for full 64 KB; see note below |
| External SDRAM  | **32 MB** (2× 16 MB SDRAM) — use for CPU address space 0x0000–0xFFFF |
| Video output    | **VGA** (analog RGB) + composite — native on board                  |
| Keyboard        | **PS/2** connector — native                                         |
| Mass storage    | **SD card** slot — use for floppy / Winchester images                |
| Audio           | **PSG** (AY-3-8910) + DAC on board — buzzer output can use DAC      |
| Toolchain       | Altera Quartus II 11.0sp1 (last version supporting Cyclone I; free Web Edition) — no open-source toolchain available |

**BRAM constraint**: the EP1C12's 208 Kbit M4K blocks provide only ~26 KB of
embedded BRAM.  This is insufficient to hold the full 64 KB address space in BRAM.

Recommended allocation:
- **Dual-port BRAM** (M4K): alpha framebuffer (1280 B), graphic framebuffer (3840 B),
  character generator ROM (2 KB) — total ~7 KB; fits in ~14 M4K blocks.
- **SDRAM**: entire 64 KB CPU address space — access via an SDRAM controller
  (e.g. `sdram_controller` from the 1ChipMSX source or a standard open-source core).
- **SDRAM latency**: SDRAM has variable read latency (CAS latency 2–3 cycles).
  Insert WAIT states on the Z80 bus (`PETRIL`) for SDRAM accesses — this is safe
  since `PETRIL` is otherwise unused (§2.2 / §15.2).

**1ChipMSX open-source**: the original firmware and FPGA source are available at
`https://github.com/gnogni/1chipmsx` (unofficial mirror) under a custom license.
The SDRAM controller and VGA scan-doubler from that project can be reused directly
in a Smaky 6 port.  The existing Z80 (T80) softcore instance is a drop-in replacement
target for the Smaky 6 CPU wiring.

**Toolchain note**: Altera Quartus II Web Edition 11.0sp1 is the last version
supporting Cyclone I.  It is available for free download from Intel's legacy archive
but requires a license file (free registration).  No open-source toolchain
(IceStorm/nextpnr) supports Cyclone I.

---

## 15.15 Recommended Open-Source IP Cores

This section lists vetted open-source IP cores that directly replace or replicate
the original Smaky 6 chips.  All cores listed have been used in production retro-
computer FPGA projects; none require a commercial licence.

### 15.15.1 Z80 CPU

| Core       | Language | Repository / source                         | Notes |
|------------|----------|---------------------------------------------|-------|
| **TV80**   | Verilog  | `https://github.com/hutch2/tv80`            | Cycle-accurate; full IM 0/1/2; used in MiSTer ZX Spectrum, CPC, MSX, Colecovision cores; exposes `int_vector` input for IM 0 opcode injection |
| **T80**    | VHDL     | `https://github.com/sorgelig/T80` (MiSTer fork) | Derived from opencores T80; typically ~10–15% smaller than TV80; used in 1ChipMSX and MiSTer MSX core; also exposes full bus |
| **Z80 opencores** | VHDL | `https://opencores.org/projects/t80` | Original T80; less actively maintained than the MiSTer fork |

**Recommendation**: use **TV80** for Verilog-based designs (Colorlight, iCE40,
Artix-7); use the **MiSTer T80 fork** for VHDL designs (1ChipMSX, DE1) or when
LUT budget is tight.

For IM 0 INT acknowledge (§15.5): TV80 exposes a `cpu_di` data-bus input that is
sampled during the INT ack M-cycle.  Drive it with `8'hFF` (RST 38h) or `8'hCF`
(RST 08h) based on the `fdc_nmi_armed` flag, gated by `(m1_n == 0 && iorq_n == 0)`.

### 15.15.2 Intel 8251 USART

No cycle-exact open 8251 clone is in wide use.  The recommended approach is a
simple 8-N-1 UART core with an 8251-compatible register interface wrapper:

| Core                  | Language    | Repository / source                                     | Notes |
|-----------------------|-------------|---------------------------------------------------------|-------|
| **simple_uart**       | Verilog     | `https://github.com/ben-marshall/uart`                  | Minimal, configurable baud rate; add 8251 register wrapper (see below) |
| **uart** (UART16550)  | Verilog     | `https://opencores.org/projects/uart16550`              | Full 16550; register-compatible superset of 8251; overkill but proven |
| **ACIA 6850**         | VHDL        | `https://github.com/hoglet67/ACIA`                      | 6850-style; shares RXRDY/TXRDY paradigm with 8251; easy to adapt |
| **Minimig UART**      | Verilog     | Inside `https://github.com/MiSTer-devel/Minimig-AGA_MiSTer` | Simple two-register UART, already adapted for retro-computer use |

**8251 register wrapper** (minimal, for ports 0x04–0x07):

The SAMOS firmware only reads three status bits (§9):
- bit 0 = RXRDY (receive data ready)
- bit 1 = TXRDY (transmit register empty)
- bit 2 = TXEMPTY (transmit shift register empty)

A 2-register interface (data port + status/command port) wrapping any standard
UART is sufficient.  The mode-byte and command-byte write protocol of the real
8251 can be implemented as a 2-state FSM (first write = mode, second = command)
or simply ignored — SAMOS 2-8 does not rely on the mode initialisation sequence
after the Phantom ROM sets up the USART.

### 15.15.3 PS/2 Keyboard Receiver

| Core                | Language | Repository / source                                   | Notes |
|---------------------|----------|-------------------------------------------------------|-------|
| **ps2_keyboard**    | Verilog  | `https://github.com/alangarf/ps2_keyboard_controller` | Clean scan-code receiver; outputs make/break + scan code; MIT licence |
| **ps2**             | VHDL     | `https://opencores.org/projects/ps2`                  | Keyboard + mouse; well-tested; used in various opencores retro projects |
| **MiSTer ps2**      | Verilog  | Inside MiSTer framework (`sys/ps2.v`)                 | Handles extended codes and key repeat; already integrated with USB→PS/2 bridge on DE10-Nano |

**Key translation ROM**: implement as a 256×8 Verilog parameter array (inferred
as LUT RAM on iCE40/ECP5 or as BRAM on Gowin/Xilinx/Altera).  Source the mapping
from `src/keyboard.c` `KEY_TABLE[]` plus SDL_TEXTINPUT logic; map PS/2 set-2
make codes to Smaky 7-bit ASCII codes.

### 15.15.4 I²C Master (for DS3231 RTC bridge, §15.8 Option B)

| Core               | Language | Repository / source                                   | Notes |
|--------------------|----------|-------------------------------------------------------|-------|
| **i2c_master**     | Verilog  | `https://github.com/alexforencich/verilog-i2c`        | Clean, parameterised; MIT licence; widely used in FPGA designs |
| **i2c_master** (opencores) | VHDL | `https://opencores.org/projects/i2c`             | Wishbone interface; slightly heavier; original reference design |
| **tiny_i2c**       | Verilog  | `https://github.com/emard/ulx3s-misc/tree/master/examples/i2c` | Minimal (~50 LUTs); sufficient for a single DS3231 transaction |

The DS3231 → E405 protocol bridge (§15.8) requires only single-byte random-read
and random-write I²C transactions at 100 kHz (standard mode); any of the above
cores is adequate.

### 15.15.5 SPI Master / SD Card Controller

| Core                   | Language | Repository / source                                     | Notes |
|------------------------|----------|---------------------------------------------------------|-------|
| **sd_card** (GHDL)     | VHDL     | `https://github.com/emard/vhdl-sd-card`                 | Full SD/SDHC read support; proven on ECP5 and iCE40 |
| **sdspi**              | Verilog  | `https://github.com/ZipCPU/sdspi`                       | SPI-mode SD; MIT; used in ZipCPU projects; configurable sector size |
| **sd_controller**      | Verilog  | `https://github.com/mczerski/SD-card-controller-FPGA`   | Simple CMD17/CMD18 read; minimal; easy to integrate |
| **Gowin PSRAM IP**     | Gowin IP | Gowin EDA IP Catalog (`PSRAM_Memory_Interface_HS_V2`)   | Required for Tang Nano 4K/9K on-chip PSRAM; proprietary but free with Gowin EDA licence |

**Sector mapping for Smaky 6**: map Smaky LBA (§8.3) directly to the SD block
number (both are 512-byte-sector-indexed — Smaky sectors are 256 bytes; pack two
per SD sector, or use the first 256 bytes of each 512-byte SD sector).

### 15.15.6 HDMI / DVI Output

| Core                  | Language      | Repository / source                                   | Notes |
|-----------------------|---------------|-------------------------------------------------------|-------|
| **hdmi** (Project F)  | SystemVerilog | `https://github.com/projf/fpga-display-controller`    | Clean TMDS encoder; tested on Arty A7, ECP5, iCE40 UP5K; MIT licence |
| **HDMI** (sylefeb)    | Verilog       | `https://github.com/sylefeb/Silice` (examples/hdmi)   | Used in Silice retro-computer demos; simple and portable |
| **hdmi_tx**           | Verilog       | `https://github.com/hdl-util/hdmi`                    | Full-featured; audio channel support; useful if buzzer audio over HDMI is desired |
| **DVI** (MiSTer)      | Verilog       | Inside MiSTer framework (`sys/hdmi.sv`)               | Handles all EDID and HDMI handshake for DE10-Nano; not portable to other boards |
| **Gowin TMDS**        | Gowin IP      | Gowin EDA IP Catalog (`HDMI_TX`)                      | Required for Tang Nano 4K/9K micro-HDMI output; uses dedicated TMDS I/O |

**For the Smaky 6**: the HDMI core needs to accept a 1-bit pixel input (LIT/BG),
a 512-pixel-wide active area, and a 50 Hz / 240-line frame rate.  Feed it through
a scan-doubler (one line buffer BRAM, read back at 2×) to reach standard
640×480@60 Hz timing before the TMDS encoder.

### 15.15.7 SDRAM Controller (for DE1, 1ChipMSX)

| Core                  | Language | Repository / source                                   | Notes |
|-----------------------|----------|-------------------------------------------------------|-------|
| **sdram** (pipelined) | Verilog  | `https://github.com/hdl-util/sdram-controller`        | Supports 16-bit SDRAM; pipelined; easy to adapt to 8-bit data bus |
| **SDRAM** (MiSTer)    | Verilog  | `https://github.com/MiSTer-devel/Template_MiSTer/blob/master/sys/sdram.sv` | Standard MiSTer SDRAM module; 32-bit bus internally, 8/16-bit exposed; used by all MiSTer cores |
| **1ChipMSX SDRAM**    | VHDL     | `https://github.com/gnogni/1chipmsx` (`src/sdram/`)  | Already validated against EP1C12 + 32 MB SDRAM; directly reusable for 1ChipMSX port |
| **SDRAM** (Hamsterworks) | VHDL  | `http://hamsterworks.co.nz/mediawiki/index.php/SDRAM_Memory_Module` | Tutorial-grade; very readable; good starting point for custom implementations |

**Wait-state integration**: after issuing a CPU read to SDRAM, assert `PETRIL`
(Z80 WAIT) for 2–3 clock cycles (§15.3 / §15.14.3).  The SDRAM controller's
`busy` or `ack` output drives the WAIT deassert logic directly.

### 15.15.8 Video Scan Doubler / Scaler

| Core                  | Language | Repository / source                                   | Notes |
|-----------------------|----------|-------------------------------------------------------|-------|
| **scandoubler**       | Verilog  | MiSTer framework (`sys/scandoubler.v`)                | 1-bit greyscale (adaptable); line-buffer ping-pong; output at 31.5 kHz |
| **video_mixer**       | Verilog  | MiSTer framework (`sys/video_mixer.sv`)               | Full pipeline: scandoubler + OSD overlay + HDMI output; DE10-Nano specific |
| **scanline_doubler**  | Verilog  | `https://github.com/hdl-util/scan-doubler`            | Simple 2-line BRAM buffer; parameterisable width; portable |
| **HDMI scaler** (GBS-8200 FPGA) | Verilog | `https://github.com/ramapcsx2/gbs-control` | Full retro-scaler; overkill for direct FPGA use but documents the algorithm |

For the Smaky 6's 512×240@50 Hz native resolution, a simple line-doubler
(buffer each line in BRAM, play back twice) producing 512×480@50 Hz is
sufficient.  Padding to 640 columns adds 64 blank pixels left + right for
standard 640×480 VGA/HDMI timing.

### 15.15.9 Summary Table

| Original chip / function  | Recommended open core(s)                          | Section  |
|---------------------------|---------------------------------------------------|----------|
| Z80 CPU                   | TV80 (Verilog) / T80 MiSTer fork (VHDL)           | §15.15.1 |
| Intel 8251 USART ×2       | simple_uart + 8251 register wrapper               | §15.15.2 |
| PS/2 keyboard receiver    | ps2_keyboard (Verilog) / opencores ps2 (VHDL)     | §15.15.3 |
| E405 RTC (3-wire serial)  | Custom FSM (§15.8 Option A) **or** i2c_master + DS3231 bridge | §15.15.4 |
| SD card / storage backend | sdspi / sd_card (VHDL) / Gowin PSRAM IP           | §15.15.5 |
| HDMI / DVI output         | hdmi (Project F) / Gowin TMDS IP / MiSTer hdmi.sv | §15.15.6 |
| SDRAM controller          | sdram (hdl-util) / MiSTer sdram.sv / 1ChipMSX SDRAM | §15.15.7 |
| Scan doubler              | scandoubler (MiSTer) / scanline_doubler (hdl-util) | §15.15.8 |
| 74S262 shift register     | 8-bit PISO shift register — trivial FPGA primitive; no external core needed | — |
| Intel 2101 line buffer    | 64-byte dual-port BRAM primitive — inferred by synthesiser from `reg [7:0] buf[0:63]` | — |
| WD1000/1002 Winchester    | No open clone; implement as a simple FSM (§15.7) — the register set is minimal | — |
| Micropolis FDC            | No open clone; implement as a custom sector-timer + byte-stream FSM (§15.6) | — |

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
