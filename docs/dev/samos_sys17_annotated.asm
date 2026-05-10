; =============================================================================
; Smaky 6 — Phantom Bootloader ROM  (SYS17 TMS2716)
; =============================================================================
;
; Source file : roms/samos_sys17.rom   (2048 bytes, 0x0000–0x07FF)
; Origin      : Converted from "SYS17 TMS2716.HEX" (Intel HEX, known-good)
; Disassembler: z80dasm 1.1.6
; Annotation  : reverse-engineered from disassembly + disk image analysis
;
; Role
; ----
; This is the "Phantom" boot ROM present in later 64 KB Smaky 6 machines.
; It REPLACES ALL OTHER ROM CHIPS — the 64 KB machine has no SYSMON ROM and no
; SAMOS ROM.  This single 2 KB chip is the entire physical ROM complement.
;
; Runtime status (2026-05)
; ------------------------
; Full boot to CLI directory listing works in the emulator.
;   - The Phantom ROM path completes: SYS.SY loads, handoff at 0x57C0 executes.
;   - SYSMON and SAMOS relocate to RAM 0x0000–0x22FF.
;   - CLI.SY loads from tracks 2–3; the directory listing is displayed.
; Two FDC emulation bugs were fixed (src/floppy.c) to reach full boot:
;   1. floppy_read_data byte_pos=1: now returns physical track number (not sector
;      index).  The OS at PC 0x20C2 does CP (HL) comparing this byte against the
;      expected-track variable at (0x2B8B); returning sector index caused the
;      comparison to fail on any track > 0.
;   2. floppy_write_cont post-ROM path: one step per write to port 0x1A; head
;      direction derived from workspace variables (0x2B8B / 0x2B8C) rather than
;      a bit-encoded step value.  Drive is always 0 (floppy A).
;
; Earlier 32 KB / 48 KB models had:
;   - SYSMON ROM  (4 KB, 2×2708 or 1×2716) at 0x0000–0x0FFF
;   - SAMOS ROM   (4 KB, optional 2716)     at 0x1000–0x1FFF
; These are absent in the 64 KB machine.  Instead, SYSMON and SAMOS both live
; in SYS.SY on the boot floppy.  The Phantom ROM loads them from disk into RAM
; then bank-switches itself out — after which no ROM exists at any address.
;
; Boot sequence (detailed)
; -------------------------
;   1.  RST 00h → DI; LD SP,0x4600; JP boot_main (0x003B)
;
;   2.  OUT(0x19, 0) → probe floppy seek status
;       (This also serves as the first write that may set seek_busy)
;
;   3.  OUT(0x00, 0) → reset keyboard FOUND flip-flop
;       IN(0x00) spin-loop (kbd_wait 0x00FD): waits until bit7=0 (key present)
;       Key code 0x00 = "neutral / Enter" → selects single-sided floppy boot.
;       Key code 0x20 (SLA result) → selects double-sided / Winchester path.
;       The drive-control byte is stored in OS workspace at 0x4500.
;
;   4.  RST 18h → screen_clear: fill alpha plane 0x4000–0x44FF with spaces;
;       zero OS workspace 0x4600–0x54FF.
;
;   5.  RST 28h → copy "ROM de chargement rev 1-7" to alpha plane.
;       RST 38h → short beep.
;
;   6.  ld (0x4502), 0 → clear Winchester flag.
;       CALL floppy_seek_sys (0x016E):
;         a. OUT(0x19, ctrl) — send seek command.
;         b. IN(0x19): read status.
;            bit 6 semantics: 1 = drive still seeking (busy), 0 = head settled.
;         c. Regardless of initial bit6, issues step pulses (+18, +2 tracks)
;            then enters the seek-settle poll loop:
;              LOOP: delay; poll IN(0x19) bit6;
;                    if bit6=1 → loop again (still seeking);
;                    if bit6=0 → break (settled) → return NC (floppy OK).
;            Timeout (B = 0x9C = 156 outer iterations): return CY (error).
;
;         So: bit6 must transition 1→0 during the poll window for NC return.
;         CY return means no floppy / timeout (code then tries Winchester).
;
;   7a. floppy found (NC from floppy_seek_sys) → jr nc → 0x0090:
;         display "Disque souple" string at 0x4080;
;         copy 12-byte stub from ROM 0x0137 to RAM 0x57C0;
;         set (0x4501)=0xFF (double-sided flag);
;         LD BC,0; LD HL,4; LD DE,0x5800 → RST 20h (block_copy / 0x01EE).
;
;   7b. Winchester or floppy CY (timeout): set (0x4502)=0xFF (Winchester flag);
;       jr → 0x007E: CALL winchester_test (0x030C);
;       if Winchester found (NC) → jr nc → 0x0090 (same path as floppy NC!);
;       if Winchester absent (CY) → display "Disque inactif" → spin forever.
;
;   8.  block_copy (RST 20h / 0x01EE): checks (0x4502) Winchester flag:
;         If floppy: CALL setup_sector (0x021D) — seek head to track 0;
;           setup_sector polls IN(0x19) bit5:
;             bit 5 semantics: 1 = head stepping/busy, 0 = track-0 found.
;             Returns NC when bit5→0 (success), CY on timeout.
;         On success: patch RAM vectors:
;           (0x450B) ← 0x0210  (print_str return stub)
;           (0x450F) ← 0x025A  (floppy_stream_read)
;         OUT(0x19, ctrl+0x0C) — motor on, INT-enable bit.
;         EI; JR $-2 — enable 50Hz IRQ and spin.
;
;   9.  THE SPIN LOOP WAITS FOR A FLOPPY SECTOR INT (maskable INT, IM 0).
;       The Micropolis hard-sector floppy controller asserts the hardware INT
;       line on each sector hole (~80/s at 300 RPM × 16 sectors).  The ROM
;       never executes an IM instruction so Z80 stays in IM 0 (default after
;       reset).  In IM 0 the interrupting device drives the data bus during
;       M1 acknowledge:
;         Floppy controller places opcode 0xCF = RST 08h on the bus.
;       RST 08h (0x0008): PUSH HL; LD HL,(0x450F); EX (SP),HL; RET
;         → indirect call through workspace pointer (0x450F) = 0x025A.
;         → jumps to floppy_stream_read (0x025A) on every sector-hole INT.
;       NMI (0x0066) is a separate user BREAK path — NOT triggered by the floppy.
;
;  10.  Each floppy INT calls floppy_stream_read (0x025A) via RST 08h:
;         — polls IN(0x19) bits for sector match;
;         — reads 260 bytes (sync + sector-ID + 256 data + checksum) from
;           port 0x1B, stores into RAM at the running load pointer;
;         — returns to the spin loop (0x020E) to await the next sector INT.
;         When ALL sectors of the current block are transferred:
;           (0x450F) is patched to point to 0x0300 (block_copy completion handler).
;         Next INT → RST 08h → 0x0300 → motor off → exits block_copy → caller.
;       block_copy #1 (RST 20h at 0x00AD) loads the floppy directory (32 sectors)
;       into RAM 0x5800.  block_copy #2 (RST 20h at 0x00EB) loads SYS.SY content.
;
;  NMI handler at 0x0066 (*** NOT part of normal floppy boot — user BREAK path ***):
;         OR A; CALL 0x0210 (motor-off stub) → if NC: test keyboard;
;         CALL kbd_wait (0x00FD) → spin waiting for any keypress;
;         JP Z, 0x046D → if key code = 0 (Enter) → "Chargeur PDP11" PDP11 paper-tape loader path
;                        (reads binary via USART-1 in PDP-11 paper-tape format);
;         Otherwise (any non-zero key):
;           LD HL,0x04C2; LD DE,0x5500; LD BC,0xB300; LDIR → copy
;           self-test POST stub from ROM[0x04C2] to RAM 0x5500;
;           JP 0x5500 → *** run self-test POST diagnostic loop ***.
;
;  11.  The stub at RAM 0x5500 is an INFINITE DIAGNOSTIC POST LOOP.
;       It does NOT load SYS.SY from the floppy — SYS.SY is loaded by
;       block_copy via the sector-INT path (see step 10 above).
;       The self-test stub is only reached when the user presses a non-zero
;       key from the NMI handler (user BREAK path, not normal boot).
;       The stub (copied from ROM 0x04C2, executed at RAM 0x5500):
;         a. LD SP,0x4600; CALL 0x56F5 (beep); clear alpha plane; clear workspace.
;         b. OUT(0x01),A — bank-switch Phantom ROM OUT (RAM 0x0000-0x07FF accessible).
;         c. OUT(0x00),4 — write 4 to keyboard port (unknown purpose).
;         d. CALL 0x55C9 (draw_glyph_list) — draw boot logo on GRAPHIC plane (0x4600+).
;         e. RAM test loop × 4 banks (0x4000, 0x8000, 0xC000, 0x0000):
;              CALL 0x55AB (IX-table-driven pattern test AA/55/00/FF/01);
;              CALL 0x56B7 — display pass/fail result on screen;
;              CALL 0x56F5 — beep.
;         f. Short delay; JR 0x5500 → *** RESTART DIAGNOSTIC LOOP FOREVER ***
;            The stub NEVER returns. It is a permanent POST self-test.
;
;  NORMAL BOOT COMPLETION (from floppy_boot 0x0090, after block_copy #2 returns):
;       floppy_boot (0x00FA) pushes the SYS.SY entry address (IX+19/20) as the
;       return address, then RET → jumps to entry point in SYS.SY.
;       Entry code executes RST 30h → JP (0x57C0):
;         12-byte stub at RAM 0x57C0 (copied from ROM[0x0137] by floppy_boot):
;           DI; XOR A; OUT(01h),A → bank-switch Phantom ROM out;
;           LD DE,0x0000; LDIR → copy SYSMON section to RAM 0x0000;
;           JP 0x0000 → *** BOOT SYSMON (now in RAM, not ROM) ***.
;
; Summary of port 0x19 read bit semantics (confirmed by disassembly):
;   bit [3:0]  Current hard-sector index (0–15), advances each sector hole.
;   bit 4      0 = data byte available (byte-ready); 1 = not ready.
;              ROM polls: bit4,a; jr nz → wait while bit4=1.
;   bit 5      0 = step/seek complete (track-0 found); 1 = head still moving.
;              setup_sector: bit5,a; jr z → exits loop and goes to success
;              when bit5=0 (counterintuitive: jr z jumps to success path).
;   bit 6      0 = seek settled; 1 = still seeking.
;              floppy_seek_sys poll: bit6,a; jr nz → loops while bit6=1;
;              exits when bit6=0 → returns NC (success).
;
; Memory model (64 KB Phantom)
; ----------------------------
;   0x0000–0x07FF  This ROM (write-protected)
;   0x0800–0x3FFF  Lower RAM
;   0x4000–0x44FF  Alpha screen buffer (20×64 chars)
;   0x4500–0x45FF  OS workspace / variables (see table below)
;   0x4600–0x54FF  Graphic bitmap (60 rows × 64 bytes = 3840 B; each row displayed 4×)
;   0x4600         Stack pointer at cold reset
;   0x5500–0x583E  OS-loader stub (LDIR'd from ROM 0x04C2; ~830 bytes of ROM code
;                   then zeros to wrap BC=0xB300 ending around 0xFF00)
;   0x8100–0xFFFF  Upper RAM
;
; SYS.SY RAM layout (after Phantom ROM bank-switch; confirmed Phase 1K):
;   After OUT(0x01),A=0 clears the Phantom ROM bank:
;   0x0000–0x07FF  SYSMON monitor (loaded from SYS.SY bytes 0x0000–0x07FF)
;   0x0800–0x22FF  SAMOS OS proper (loaded from SYS.SY bytes 0x0800–0x22FF)
;   NOTE: The OS section uses JP targets 0x09xx–0x22xx and SYSMON-entry targets
;         0x00xx–0x07xx, confirming load base = 0x0000/0x0800 (not 0x5700).
;         The directory 'entry=0x5700' may refer to the loader stub, not the OS.
;
; OS workspace at 0x4500 (selected variables)
; -------------------------------------------
;   0x4500  floppy drive/side control byte sent to port 0x19
;   0x4501  double-sided flag (0=single, 0xFF=double)
;   0x4502  Winchester flag  (0=floppy, 0xFF=Winchester)
;   0x4503  current logical sector (low nibble = sector, high = track nibble)
;   0x4504  current track
;   0x4505  target sector (low)
;   0x4507  retry/error counter
;   0x4508  expected sector ID
;   0x4509  destination pointer (RAM address for sector data)
;   0x450B  return-address vector (patched by RST 08h dispatch)
;   0x450F  indirect call vector (RST 28h target)
;
; I/O port map (confirmed from ROM + SYS.SY disassembly; updated Phase 1K)
; -------------------------------------------------------------------------
;   0x00 R   Keyboard CLA — bit 7: 0=key present, 1=none; [6:0]=key code
;   0x00 W   Reset keyboard FOUND flip-flop (A=0 at cold-start, A=0 again in OS)
;   0x01 W   Phantom ROM bank-switch latch: OUT(0x01),A=0 switches the 2 KB
;              Phantom ROM out of address space, making RAM 0x0000–0x07FF
;              writable. Written in hard_reset (0x0139) and by the OS-loader
;              stub copied to 0x5500. ALSO written by SYSMON at addr 0x0048.
;   0x03 W   Sound bit-bang (beep; RST 38h interrupt handler and OS buzzer)
;   0x04 R   USART-1 paper data (PDP11 paper-tape loader path at 0x046D)
;   0x05 R   USART-1 status (bit 1 = RX ready; polled before reading 0x04)
;   0x08 R/W SPI-style bit-serial interface (confirmed from SYS.SY OS section):
;              Bit 0 read  = MISO  (data in from peripheral)
;              Bit 2 write = MOSI  (data out to peripheral)
;              Bit 3 write = CLK   (clock; toggled with EX (SP),HL delay between edges)
;              Protocol: XOR A → OUT(0x08) (reset); then transmit 4 bits (B=4),
;              then receive 8 bits (B=8) using IN A,(0x08)/RRA/RR (HL).
;              Used extensively in the SAMOS OS at ~0x5D58–0x5DCA.
;              Target peripheral unknown (possibly RTC, display controller,
;              or hardware configuration latch).
;              *** NOT a simple display-mode register — prior doc was wrong. ***
;   0x0B R/W Bit-serial clock/data bus (confirmed from SYS.SY OS section):
;              OUT(0x0B),A used as a shift-clock strobe; the value in A carries
;              one serialised bit per pulse. Used in a tight loop at ~0x5ADA–0x5B0F
;              to transmit multi-byte values serially. Target peripheral unknown
;              (possibly USART init, keyboard controller, or second serial device).
;   0x18 R/W Floppy-related byte-stream port (confirmed from SYS.SY OS section):
;              IN F,(C) with C=0x18 polls bit 7 (M flag) for ready;
;              OUT(0x18),A then sends one data byte; DJNZ repeats for block.
;              Pattern identical to ROM's 0x1B stream reads — likely a second
;              floppy data channel or Winchester DMA port.
;   0x19 R/W Floppy/Winchester control & sector-index register
;              bits [3:0] read  → current hard-sector index (0–15)
;              bit  4 read      → 0 = byte ready; 1 = not ready (stream polling)
;              bit  5 read      → 0 = head settled/track0; 1 = still stepping
;              bit  6 read      → 0 = seek settled; 1 = still seeking
;              write            → drive control byte (seek, step, motor, INT-en)
;   0x1A W   Floppy CONT (alternative / combined with 0x19 write path)
;   0x1B R   Floppy serial data stream (sync → sector ID → 256 bytes → cksum)
;   0x1C R/W Unknown OS port — 3 uses in OS at ~0x63C0, 0x6583, 0x6594;
;              context suggests disk or DMA acknowledge.
;   0x21 R/W Winchester WD-style command/status
;   0x23 W   Winchester sector register
;   0x24 W   Winchester sector-count register
;   0x25 W   Winchester LBA/cylinder low
;   0x26 W   Winchester cylinder high
;   0x27 R/W Winchester drive/head / status
;   0x2B W   Winchester reset/select (2 uses in OS, ~0x6B8E and 0x6DAB)
;
; PHASE 1K NOTE — Video mode port NOT found:
;   Exhaustive search of all OUT instructions in both samos_sys17.rom and SYS.SY
;   found no port that functions as a simple alpha/graphic plane select.
;   Port 0x08 was previously documented as "Display mode + 50 Hz interrupt"
;   (from prior reverse-engineering notes) but disassembly proves it is a
;   bit-serial SPI peripheral interface, not a display-mode register.
;   Port 0x0B is a serial shift-clock, not a display latch.
;   Video mode switching (VMODE_ALPHA ↔ VMODE_SUPER) may be:
;     a) Triggered by a write to an address in the graphic plane range (0x4600+)
;        rather than an I/O port;
;     b) Controlled by a dedicated latch not exercised by this ROM/OS binary;
;     c) Always on in hardware (both planes active simultaneously, with priority).
;
; RST vector table
; ----------------
;   RST 00h (0x0000)  Cold reset entry: DI; LD SP,0x4600; JP boot_main
;   RST 08h (0x0007)  Dispatch via (0x450F) — indirect jump through RAM vector
;   RST 10h (0x0010)  Floppy read-sector entry (JP 0x0143)
;   RST 18h (0x0018)  Screen clear / alpha plane fill (JP 0x011C)
;   RST 20h (0x0020)  Block copy (LDIR wrapper) (JP 0x01EE)
;   RST 28h (0x0028)  String copy HL→DE until NUL (JP 0x0167)
;   RST 30h (0x0030)  Winchester read entry (JP 0x057C0)
;   RST 38h (0x0038)  50 Hz interrupt handler / beep (JP 0x0106)
;
; String table (embedded ASCII, null-terminated)
; -----------------------------------------------
;   0x0399  "ROM de chargement rev 1-7"  (boot banner, shown on alpha plane)
;   0x03B3  "Disque souple"              ("floppy disk" — floppy boot path)
;   0x03C1  "Disque dur"  (if boot=Winchester, shown instead)  [inferred]
;   0x03CC  "simple face"               (single-sided)
;   0x03D8  "double faces"              (double-sided)
;   0x03E5  …further strings used in secondary boot/error messages…
;
; =============================================================================

	org	00000h

; =============================================================================
; RST 00h / Cold reset entry
; =============================================================================
	di			;0000  disable interrupts
	ld sp,04600h		;0001  stack → 0x4600 (bottom of OS workspace)
	jp boot_main		;0004  → 0x003B

; =============================================================================
; RST 08h  — Indirect call through RAM vector at (0x450F)
; =============================================================================
rst_08:				;0007
	ld h,c			;0007
	push hl			;0008
	ld hl,(0450fh)		;0009  load target address from RAM vector
	ex (sp),hl		;000c  swap with TOS so RET jumps there
	ret			;000d

; filler / data
	ld b,l			;000e
	ld b,c			;000f

; =============================================================================
; RST 10h  — Floppy read-sector
; =============================================================================
rst_10:				;0010
	jp floppy_read_sector	;0010  → 0x0143

; filler / data
	add hl,bc		;0013
	ld l,041h		;0014
	ld d,e			;0016
	ld b,e			;0017

; =============================================================================
; RST 18h  — Clear alpha plane + reset RAM workspace
; =============================================================================
rst_18:				;0018
	jp screen_clear		;0018  → 0x011C

; filler / data
	cpl			;001b
	ld h,h			;001c
	ld l,a			;001d
	ld (hl),l		;001e
	ld h,d			;001f

; =============================================================================
; RST 20h  — Block copy (LDIR wrapper: BC=count, HL=src, DE=dst)
; =============================================================================
rst_20:				;0020
	jp block_copy		;0020  → 0x01EE

; filler / data
	ld h,(hl)		;0023
	ld h,c			;0024
	ld h,e			;0025
	ld h,l			;0026
	ld (hl),e		;0027

; =============================================================================
; RST 28h  — String copy HL→DE until NUL
; =============================================================================
rst_28:				;0028
	jp str_copy		;0028  → 0x0167

; filler / data
	ld l,045h		;002b
	ld c,(hl)		;002d
	ld b,h			;002e
	ld c,c			;002f

; =============================================================================
; RST 30h  — Winchester (hard disk) read entry
; =============================================================================
rst_30:				;0030
	jp winchester_read	;0030  → 0x057C0  (in SYS.SY / upper RAM)

; NOTE: Z80 NMI (non-maskable interrupt) ALWAYS vectors to 0x0066.
; On the Smaky 6, NMI is triggered by the user BREAK key — NOT by the floppy.
; The Micropolis floppy fires the maskable INT line on each sector-hole
; crossing (~80×/s at 300 RPM × 16 sectors/track).  In IM 0 this causes
; RST 08h (0xCF) which dispatches via (0x450F) → floppy_stream_read.
; The NMI handler at 0x0066 is the BREAK / keyboard diagnostic path only.

; filler / data
	ld d,h			;0033
	ld c,h			;0034
	ld c,a			;0035
	ld b,c			;0036
	ld b,h			;0037

; =============================================================================
; RST 38h  — 50 Hz interrupt handler / beep tone generator
; =============================================================================
rst_38:				;0038
	jp beep			;0038  → 0x0106

; =============================================================================
; boot_main — main boot sequence
; =============================================================================
boot_main:			;003b
	xor a			;003b  A = 0
	out (000h),a		;003c  reset keyboard FOUND flip-flop
	call kbd_wait		;003e  → 0x00FD; wait for keypress; returns A=keycode
	; Drive/side selection from key code:
	;   key=0x00 (neutral/Enter) → Z=1 → jr z skips SLA → A stays 0x20?
	; Actually: LD A,0x20 loads A first, then if Z (key was 0) → skip SLA
	; → A=0x20. If Z clear (key nonzero): SLA A → A=0x40.
	;   A=0x20 → ld (0x4500),0x20 → single-sided floppy (drive A)
	;   A=0x40 → ld (0x4500),0x40 → double-sided / Winchester path
	ld a,020h		;0041
	jr z,$+4		;0043  if key=0: skip SLA, A=0x20 (single-sided floppy)
	sla a			;0045  if key≠0: SLA → A=0x40 (double-sided/Winchester)
	ld (04500h),a		;0047  store drive-control byte in OS workspace
	rst 18h			;004a  clear alpha plane (screen_clear)

	; Display "ROM de chargement rev 1-7" banner
	ld hl,str_banner	;004b  → 0x0399
	ld de,04000h		;004e  → alpha plane base
	rst 28h			;0051  copy string HL→DE

	rst 38h			;0052  play a short beep

	; Initialise disk flags
	xor a			;0053
	ld (04502h),a		;0054  clear Winchester flag (0=floppy)

	; Try to locate SYS.SY on floppy
	call floppy_seek_sys	;0057  → 0x016E; returns NC on success
	ld hl,str_souple	;005a  → 0x03B3 "Disque souple"
; NOTE: jr nc = jump if NO carry. floppy_seek_sys returns NC on success.
;       On CY (floppy timeout/absent): fall through to Winchester probe.
	jr nc,$+51		;005d  if floppy_seek_sys NC (floppy settled) → 0x0090

	; Floppy seek timed out — try Winchester
	ld a,0ffh		;005f
	ld (04502h),a		;0061  set Winchester flag (0xFF)
	jr $+26			;0064  → 0x007E (winchester_test)

; =============================================================================
; nmi_handler — Z80 NMI vector (0x0066)
; =============================================================================
; Called ONLY by the user BREAK key (hardware NMI from keyboard/button).
; NOT triggered by the floppy: sector-holes fire the maskable INT line,
; which in IM 0 causes RST 08h → indirect dispatch via (0x450F), not NMI.
;
; Flow:
;   1. OR A; CALL print_str (0x0210, NC) → out(0x19,0) stop motor, return.
;   2. CALL kbd_wait → spins until a key is pressed.
;   3. JP Z, 0x046D → if key code = 0 (Enter/neutral) → PDP11 paper-tape loader path.
;   4. Otherwise: LDIR 0xB300 bytes from 0x04C2 (ROM stub) → 0x5500 (RAM).
;      JP 0x5500 → execute the copied diagnostic POST stub.
;
; The stub at 0x5500 (source ROM 0x04C2) is an INFINITE DIAGNOSTIC POST LOOP:
; it clears the screen, runs a RAM test across 4 banks, then loops forever.
; It does NOT load SYS.SY; SYS.SY is loaded by block_copy before this path.
nmi_handler:			;0066
	or a			;0066  NC
	call print_str		;0067  → 0x0210; NC path: stop motor, return
	call kbd_wait		;006a  → 0x00FD; wait for keypress
	jp z,0046dh		;006d  if key=0 → PDP11 paper-tape loader path at 0x046D

	; Key pressed → copy 179-byte OS-loader stub to RAM and run it
	ld hl,004c2h		;0070  source: ROM stub at 0x04C2 (179 bytes)
	ld de,05500h		;0073  destination: RAM at 0x5500
	ld bc,0b300h		;0076  LDIR byte count (far more than 179; copies to end)
	ldir			;0079
	jp 05500h		;007b  *** jump to copied OS-loader stub ***

; =============================================================================
; boot_menu_setup — common entry after boot device is found (0x0090)
; =============================================================================
; Reached via:
;   floppy: JR NC (0x005D → 0x0090) when floppy_seek_sys returns NC
;   Winchester: JR NC (0x0084 → 0x0090) when winchester_test returns NC
;
; 0x0090: LD DE,0x4080; RST 28h → copy "Disque souple"/"Disque dur" to 0x4080
; 0x0094: LDIR: copy 12 bytes from ROM 0x0137 (hard_reset stub) to RAM 0x57C0
; 0x009F: LD (0x4501),0xFF → set double-sided flag
; 0x00A4: LD BC,0; LD HL,4; LD DE,0x5800 → RST 20h (block_copy / 0x01EE)
;         This is the call into block_copy that:
;           - seeks floppy to track 0 (setup_sector)
;           - patches RAM vectors: (0x450F)←0x025A (floppy_stream_read)
;           - enables motor + sector-INT: OUT(0x19, ctrl+0x0C)
;           - EI; JR $-2 ← SPIN HERE waiting for floppy sector INT
;
; 0x00AE: (code after RST 20h — never reached directly; entered from NMI path)
;   LD HL,0x5800; LD DE,0x044D ("SYS     SY"); LD B,0x20; LD C,0x00
;   RST 10h → floppy_read_sector: walk 32 directory entries looking for "SYS SY"
;   JR C → retry if not found
; This secondary path (0x00AE) appears to be dead code or reached via a
; different entry mechanism after the stub at 0x5500 runs.
;
; boot_menu_error — drive-absent dead loop (0x007E)
; =============================================================================
; Reached from: Winchester CY (drive absent), falling through 0x0064 \u2192 0x007E.
;
; 0x007E: CALL winchester_test (0x030C) — probe Winchester controller.
;   NC → JR NC (0x0084) → 0x0090 (boot_menu_setup, same path as floppy NC).
;   CY → LD HL, "Disque inactif"; display at 0x4100; DI; JR $-2 → dead loop.
;
; (detailed Winchester init sequence follows at 0x030C–0x0398)

; =============================================================================
; kbd_wait — spin until a key is pressed
; =============================================================================
; Entry:  —
; Exit:   A = keycode (7-bit); Z flag set if A=0
; Clobbers: A
; Note: "djnz $+0" at entry is a B-iteration delay before the first poll.
kbd_wait:			;00fd
	djnz $+0		;00fd  short delay (B times)
	in a,(000h)		;00ff  read keyboard CLA
	or a			;0101  test bit 7 (set = no key)
	jp m,kbd_wait		;0102  loop while no key (bit 7 = 1)
	ret			;0105  A = key code (0 if neutral/Enter)

; =============================================================================
; beep — RST 38h: bit-bang buzzer on port 0x03
; =============================================================================
; Also used as 50 Hz interrupt handler (the interrupt vector RST 38h).
; Produces a short tone by toggling port 0x03 with decreasing values.
beep:				;0106
	ld a,083h		;0106  initial tone value
	push af			;0108
	and 007h		;0109  extract 3-bit octave
	ld b,a			;010b  B = octave count
	ld c,020h		;010c  C = 32 cycles per half-period
	pop af			;010e
	push af			;010f
tone_loop:
	out (003h),a		;0110  write to buzzer port
	dec a			;0112
	jr nz,tone_loop		;0113
	dec c			;0115
	jr nz,tone_loop		;0116
	djnz tone_loop		;0118  repeat B octaves
	pop af			;011a
	ret			;011b

; =============================================================================
; screen_clear — RST 18h: fill alpha plane with spaces; zero OS workspace
; =============================================================================
screen_clear:			;011c
	ld de,04001h		;011c
	ld hl,04000h		;011f
	ld (hl),020h		;0122  write space (0x20) to first char
	ld bc,004ffh		;0124  1279 more bytes
	ldir			;0127  fill rest of alpha plane with spaces
	ld hl,04600h		;0129  OS workspace base
	ld de,04601h		;012c
	ld bc,00effh		;012f  0x0EFF bytes
	ld (hl),000h		;0132  zero first byte
	ldir			;0134  zero rest of workspace
	ret			;0136

; =============================================================================
; hard_reset — triggered by NMI or explicit call; re-initialises everything
; =============================================================================
hard_reset:			;0137
	di			;0137
	xor a			;0138
	out (001h),a		;0139  Phantom ROM bank-switch: OUT(0x01),0 maps RAM over ROM 0x0000-0x07FF
	ld de,00000h		;013b
	ldir			;013e  (BC already set by caller — clears a region)
	jp 00000h		;0140  cold restart

; =============================================================================
; floppy_read_sector — RST 10h
; =============================================================================
; Reads one 256-byte sector from the floppy into the RAM buffer.
; BC = sector address encoding; fills OS workspace sector variables.
floppy_read_sector:		;0143
	push bc			;0143
	push af			;0144
	push hl			;0145
	push de			;0146
	ld b,000h		;0147
	add hl,bc		;0149
	ld a,(de)		;014a  compare directory entry field
	cp (hl)			;014b
	jr nz,$+15		;014c
	inc de			;014e
	inc hl			;014f
	ld a,(de)		;0150
	or a			;0151
	jr nz,$-7		;0152  loop until field mismatch or NUL
	pop de			;0154
	pop ix			;0155  IX = matched directory entry pointer
	pop af			;0157
	pop bc			;0158
	or a			;0159  NC = found
	ret			;015a
	pop de			;015b
	pop hl			;015c
	pop af			;015d
	ld c,a			;015e
	ld b,000h		;015f
	add hl,bc		;0161
	pop bc			;0162
	djnz $-32		;0163  try next directory entry
	scf			;0165  CY = not found
	ret			;0166

; =============================================================================
; str_copy — RST 28h: copy NUL-terminated string from HL to DE
; =============================================================================
str_copy:			;0167
	ld a,(hl)		;0167
	or a			;0168
	ret z			;0169  stop at NUL
	ldi			;016a  copy byte HL→DE, advance both, dec BC
	jr $-5			;016c  loop

; =============================================================================
; floppy_seek_sys — seek floppy head, poll until settled, return NC/CY
; =============================================================================
; Port 0x19 bit 6 semantics:
;   1 = head still seeking (busy)   → ROM loops here while bit6=1
;   0 = head settled (seek done)    → ROM exits loop and returns NC
;
; Algorithm:
;   1. OUT(0x19, ctrl)      — send seek command (sets bit6=1 on real HW)
;   2. IN(0x19), bit 6,a
;      bit6=1 (still seeking): JR NZ → jump to step+2 pulse at 0x0185
;      bit6=0 (already settled): fall through to step+18 pulse at 0x0179
;      BOTH paths converge at 0x0185 (second step pulse) then 0x018E (loop).
;   3. Step pulse +18 (bit6=0 path) or +2 (bit6=1 path), then:
;      step pulse +2, load B=0x9C (156 outer iterations).
;   4. Outer+inner poll loop (0x018E–0x01A1):
;        outer: decrement B; if B=0 → return CY (timeout)
;        inner: IN(0x19); bit6,a; if bit6=1 → outer loop again
;               if bit6=0 → fall through → NC success
;   5. NC path (0x01A3): clear expected sector ID at (0x4508).
;      JP 0x0210 (print_str tail-call, NC → ret nc → return NC to caller).
;
; Exit: NC = floppy head settled; CY = timeout/error
floppy_seek_sys:		;016e
	ld a,(04500h)		;016e  load drive-control byte
	out (019h),a		;0171  seek command → floppy controller (sets bit6=1)
	in a,(019h)		;0173  read status
	bit 6,a			;0175  bit6: 1=seeking, 0=settled
	jr nz,$+14		;0177  if bit6=1 (seeking): skip step+18, go to step+2
	; bit6=0 path: first send step+18 then fall into step+2
	ld a,(04500h)		;0179
	add a,012h		;017c  step +18 tracks
	out (019h),a		;017e  send step command
	ld b,006h		;0180
	call delay		;0182  short delay
	; Common path (bit6=1 jumps here, bit6=0 falls here after step+18)
	ld a,(04500h)		;0185
	add a,002h		;0188  step +2 tracks
	out (019h),a		;018a  send step command
	ld b,09ch		;018c  B = 0x9C = 156 outer loop iterations
; Outer poll loop: delay one unit, then check inner; timeout when B=0
poll_outer:			;018e
	push bc			;018e
	ld b,001h		;018f
	call delay		;0191
	pop bc			;0194
	djnz poll_inner		;0195  B≠0 → check inner poll; B=0 → timeout below
	or a			;0197  (fall-through = timeout: NC before SCF)
	call print_str		;0198  → 0x0210 (NC → out(0x19,0) stop motor, ret nc)
	scf			;019b  CY = seek timed out
	ret			;019c
; Inner poll: read bit6; if still 1 → back to outer loop
poll_inner:			;019d
	in a,(019h)		;019d
	bit 6,a			;019f  bit6: 1=busy, 0=settled
	jr nz,poll_outer	;01a1  bit6=1 → outer loop (decrement B, delay)
	; bit6=0: seek complete!
	ld hl,04508h		;01a3
	ld (hl),000h		;01a6  clear expected sector ID
	or a			;01a8  clear carry = NC
	jp print_str		;01a9  tail-call: out(0x19,0); ret nc → NC to caller

; =============================================================================
; floppy_load_sector — load one 256-byte sector via port 0x1B stream
; =============================================================================
; Protocol: wait for bit 4 of port 0x19 to clear (byte ready), then
; read sync byte, sector ID, 256 data bytes, checksum from port 0x1B.
; Uses IN F,(C) [ED 70] to test sign flag on each byte for ready polling.
floppy_load_sector:		;01ac
	ld hl,04508h		;01ac  HL → expected sector ID byte
	ld a,(04501h)		;01af  double-sided flag
	or a			;01b2
	ld a,(04504h)		;01b3  current track
	jr nz,$+18		;01b6  if double-sided → different track encoding
	; Single-sided track/side encoding
	ld b,a			;01b8  B = track
	ld a,(04500h)		;01b9
	res 7,a			;01bc  clear side bit
	srl b			;01be  shift track LSB into carry
	jr nc,$+4		;01c0
	set 7,a			;01c2  set side bit if track LSB was 1
	ld (04500h),a		;01c4  store updated drive-control byte
	ld a,b			;01c7
	ld c,010h		;01c8  C = 16 (sectors per track)
	ld b,(hl)		;01ca  B = current expected sector
	ld (hl),a		;01cb  store new expected sector ID
	sub b			;01cc  compute sector offset
	ret z			;01cd  already at target sector
	jr nc,$+6		;01ce
	ld c,000h		;01d0
	neg			;01d2  absolute value of sector offset
	ld b,a			;01d4
	ld a,(04500h)		;01d5
	add a,00ah		;01d8  add motor-on + head-load bits
	add a,c			;01da  add direction bit
	out (019h),a		;01db  send to floppy control port
	out (01ah),a		;01dd  also to 0x1A (alternate control latch)
	push bc			;01df
	ld b,008h		;01e0
	call delay		;01e2  delay for step settle
	pop bc			;01e5
	djnz $-9		;01e6  repeat B times (step pulses)
	ld b,00fh		;01e8
	scf			;01ea
	jp delay		;01eb  final delay with CY set

; =============================================================================
; block_copy — RST 20h target: boot-media load entry
; =============================================================================
; Called from boot_main 0x00AD with BC=0, HL=4, DE=0x5800.
; Checks Winchester flag, calls setup_sector (seek to track 0),
; patches RAM vectors, enables interrupts, and spins waiting for
; floppy sector INT (maskable INT, RST 08h / 0xCF) on each sector hole.
; Each INT dispatches via (0x450F) → floppy_stream_read to read one sector.
;
; When all SYS.SY sectors have loaded, check_done patches (0x450F) → 0x0300
; (motor-off + return), which unwinds block_copy back to floppy_boot.
block_copy:			;01ee  RST 20h target
	ld a,(04502h)		;01ee  check Winchester flag
	or a			;01f1
	jp nz,winchester_path	;01f2  → 0x0339 if Winchester

	; Floppy path: seek head to track 0, patch vectors, spin
	call setup_sector	;01f5  → 0x021D; seek head + compute coords
	jr c,$+24		;01f8  CY (error) → 0x0210 (error display)

	; Patch RAM indirect vectors so RST 08h dispatches to floppy_stream_read
	ld hl,00210h		;01fa  return stub address (print_str / motor-stop)
	ld (0450bh),hl		;01fd  → RST 08h slot A
	ld hl,0025ah		;0200  floppy_stream_read entry
	ld (0450fh),hl		;0203  → RST 08h indirect vector

	ld a,(04500h)		;0206
	add a,00ch		;0209  add motor-on + sector-INT-enable bits
	out (019h),a		;020b  start floppy motor + enable sector-hole INT
	ei			;020d  enable 50 Hz IRQ (for beep during spin)
	jr $+0			;020e  *** spin: wait for floppy sector INT ***
	                        ;      INT → RST 08h (0x0007) → dispatch via (0x450F)
	                        ;      → floppy_stream_read (0x025A): reads one sector

; =============================================================================
; print_str — display a string on the alpha plane (called at 0x0210)
; =============================================================================
print_str:			;0210
	push af			;0210
	xor a			;0211
	out (019h),a		;0212  stop floppy motor
	pop af			;0214
	ret nc			;0215  return NC to caller
	; If CY set → error: swap return address with error-string pointer
	ex de,hl		;0216
	ld hl,00089h		;0217  error banner address in ROM
	ex (sp),hl		;021a  replace return address on stack
	ex de,hl		;021b
	ret			;021c  jump to error handler

; =============================================================================
; setup_sector — seek floppy head to track 0, encode sector address
; =============================================================================
; Port 0x19 bit 5 semantics:
;   1 = head still stepping (busy)   → loop continues while bit5=1
;   0 = head at track 0 (settled)   → JR Z exits to success at 0x024B
;     (Note: JR Z fires when Z=1, i.e., bit5=0 → zero flag set)
;
; Algorithm:
;   1. Encode logical address via addr_to_track (0x02F5), store results.
;   2. OUT(0x19, ctrl+0x0A) — motor on, head load.
;   3. Poll loop (BC = 0xD000 = 53248 iterations):
;        IN(0x19); bit5,a; JR Z → bit5=0 (settled) → success at 0x024B
;        DEC BC; if BC=0 → timeout → return CY.
;   4. Success (0x024B): clear retry counter, return NC.
;
; Exit: NC = track 0 found; CY = timeout
setup_sector:			;021d
	push hl			;021d
	ld h,b			;021e
	ld l,c			;021f
	call addr_to_track	;0220  → 0x02F5; encode logical address
	ld (04503h),hl		;0223  store sector address
	pop hl			;0226
	call addr_to_track	;0227
	ld (04509h),de		;022a  store destination RAM pointer
	ld (04505h),hl		;022e  store target sector
	ld bc,0d000h		;0231  timeout counter (~53248 iterations)
	ld a,(04500h)		;0234
	add a,00ah		;0237  motor-on + head-load bits
	out (019h),a		;0239  send to floppy (starts motor)
 poll_track0:
	in a,(019h)		;023b  read status
	bit 5,a			;023d  bit5: 1=stepping, 0=at track0
	jr z,$+12		;023f  bit5=0 → Z=1 → JR Z taken → 0x024B (success)
	dec bc			;0241  still stepping: count down timeout
	ld a,b			;0242
	or c			;0243
	jr nz,poll_track0	;0244  loop
	ld hl,00403h		;0246  timeout: error string pointer
	scf			;0249  CY = error
	ret			;024a

; =============================================================================
; delay — busy-wait loop (B * 0x90 * 256 cycles ≈ variable µs)
; =============================================================================
delay:				;024b  also entry at 0x024F / 0x0251
	xor a			;024b
	ld (04507h),a		;024c  clear retry counter
	ld b,090h		;024f
delay_inner:			;0251
	push bc			;0251
	ld b,000h		;0252
	djnz $+0		;0254  256-cycle inner loop
	pop bc			;0256
	djnz delay_inner	;0257
	ret			;0259

; =============================================================================
; floppy_stream_read — sector data stream reader (entered from 0x025A)
; =============================================================================
; Called after IRQ fires; polls port 0x19 bit 4 for byte-ready,
; reads sector-ID then 256 data bytes then checksum from port 0x1B.
floppy_stream_read:		;025a
	call floppy_load_sector	;025a  → 0x01AC; position head
	jp c,02c7h		;025d  error → retry
	ld a,(04500h)		;0260
	add a,00ch		;0263
	out (019h),a		;0265  motor on + INT enable
	in a,(019h)		;0267
	and 00fh		;0269  mask sector-index bits [3:0]
	ld hl,04503h		;026b  compare to expected sector
	cp (hl)			;026e
	jr nz,$+80		;026f  wrong sector → advance and retry

	; Correct sector found — stream in data
	ld hl,04508h		;0271  HL → expected sector ID
	ld c,01ah		;0274  C = port 0x1A (used by IN F,(C))
byte_ready_wait:
	in a,(019h)		;0276  poll port 0x19 bit 4 (byte ready)
	bit 4,a			;0278
	jr nz,byte_ready_wait	;027a  loop while not ready

	; Read sync byte (discarded) and sector ID
	in a,(01bh)		;027c  read sync byte from data stream
	defb 0edh,070h		;027e  IN F,(C)  — test sign of port 0x1A
	jp p,027eh		;0280  loop until sign bit set (byte ready)
	in a,(01bh)		;0283  read sector ID byte
	cp (hl)			;0285  compare to expected ID
	jr nz,$+41		;0286  ID mismatch → error

	; Read 256 data bytes
	ld hl,(04509h)		;0288  HL = destination RAM pointer
	xor a			;028b
	ld b,a			;028c  B = 0 → DJNZ counts 256 iterations
	ld d,a			;028d  D = running checksum accumulator
data_byte_loop:
	defb 0edh,070h		;028e  IN F,(C)  — byte ready?
	jp p,028eh		;0290  spin
	in a,(01bh)		;0293  read data byte
	ld (hl),a		;0295  store to RAM
	inc hl			;0296
	add a,d			;0297  accumulate checksum
	ld d,a			;0298
	djnz data_byte_loop	;0299  256 bytes

	; Read and verify checksum
	defb 0edh,070h		;029b  IN F,(C)
	jp p,029bh		;029d  spin
	in a,(01bh)		;02a0  read checksum byte from disk
	cp d			;02a2  compare to computed sum
	jr z,$+18		;02a3  match → success
	dec a			;02a5  allow off-by-one (Micropolis quirk)
	cp d			;02a6
	jr nz,$+8		;02a7  still mismatch → error
	xor a			;02a9
	ld (04501h),a		;02aa  clear double-sided flag (checksum tweak)
	jr $+8			;02ad
	; Checksum OK — advance sector pointer
	ld hl,04507h		;02af
	inc (hl)		;02b2  increment retry counter (used as progress)
	jr $+12			;02b3
	; Checksum error path
	xor a			;02b5
	ld (04507h),a		;02b6  reset retry counter
	ld (04509h),hl		;02b9  reset destination pointer
	call advance_sector	;02bc  → 0x02E9; increment logical sector
	call check_done		;02bf  → 0x02D0; check if all sectors loaded
	ld hl,00416h		;02c2  error string pointer
	jr $+9			;02c5

; Restore floppy to idle (motor off) and return
floppy_done:			;02c7
	ld a,(04500h)		;02c7
	add a,00ch		;02ca
	out (019h),a		;02cc  motor off / INT enable
	ei			;02ce
	ret			;02cf

; =============================================================================
; check_done — check if entire SYS.SY has been loaded
; =============================================================================
check_done:			;02d0
	ld a,(04507h)		;02d0
	cp 01fh			;02d3  31 sectors loaded?
	ccf			;02d5
	jr c,$+12		;02d6
	ld hl,(04503h)		;02d8
	ld de,(04505h)		;02db
	sbc hl,de		;02df
	ret c			;02e1  still sectors to go
	ld hl,00300h		;02e2
	ld (0450fh),hl		;02e5  patch indirect vector → next phase
	ret			;02e8

; =============================================================================
; advance_sector — increment logical sector/track counter
; =============================================================================
advance_sector:			;02e9
	ld hl,04503h		;02e9
	ld a,(hl)		;02ec
	inc a			;02ed
	and 00fh		;02ee  wrap sector within track (0–15)
	ld (hl),a		;02f0
	ret nz			;02f1  no track rollover
	inc hl			;02f2
	inc (hl)		;02f3  advance track
	ret			;02f4

; =============================================================================
; addr_to_track — convert logical address HL to track/sector encoding
; Shifts HL left 4 bits; low nibble of L becomes sector index
; =============================================================================
addr_to_track:			;02f5
	push af			;02f5
	ld a,l			;02f6
	and 00fh		;02f7  sector = low nibble
	add hl,hl		;02f9  HL <<= 1
	add hl,hl		;02fa
	add hl,hl		;02fb
	add hl,hl		;02fc  HL <<= 4 total
	ld l,a			;02fd  restore sector in L
	pop af			;02fe
	ret			;02ff

; =============================================================================
; 0x0300  — sector-advance "next phase" jumped to from indirect vector
; =============================================================================
	ex de,hl		;0300
	ld hl,(0450bh)		;0301  load return address
	ex (sp),hl		;0304  patch return on stack
	ex de,hl		;0305
	push af			;0306
	xor a			;0307
	out (019h),a		;0308  motor off
	pop af			;030a
	ret			;030b

; =============================================================================
; winchester_init — probe Winchester controller (0x030C–0x0398)
; =============================================================================
; Polls port 0x27 for status 0x50 (READY + SEEK_COMPLETE).
; Then sends RESTORE command via port 0x21.
winchester_init:		;030c
	ld hl,01a00h		;030c  timeout ~6656 iterations
	in a,(027h)		;030f  read WD status
	cp 050h			;0311  ready + seek complete?
	jr z,$+11		;0313  yes → proceed
	djnz $-6		;0315
	dec hl			;0317
	ld a,h			;0318
	or l			;0319
	jr nz,$-11		;031a  loop
	scf			;031c  timeout → CY=error
	ret			;031d

	; Send RESTORE command
	ld a,020h		;031e  RESTORE command byte
	out (021h),a		;0320
	ld de,00000h		;0322  cylinder 0, head 0
	call lba_encode		;0325  → 0x0370; encode and write LBA registers
	ld a,016h		;0328  SEEK command
	out (027h),a		;032a
	in a,(027h)		;032c  poll status
	or a			;032e
	jp m,032ch		;032f  loop while busy (bit 7 set)
	bit 4,a			;0332  SEEK COMPLETE?
	jr z,$-8		;0334
	srl a			;0336  shift error bits to carry
	ret			;0338

; =============================================================================
; winchester_path — load SYS.SY from Winchester (0x0339–0x036F)
; =============================================================================
winchester_path:		;0339
	or a			;0339
	sbc hl,bc		;033a
	push hl			;033c
	ld h,b			;033d
	ld l,c			;033e
	pop bc			;033f
	ex de,hl		;0340
	push bc			;0341
	call lba_encode		;0342  → 0x0370
	ld a,020h		;0345  READ SECTOR command
	out (027h),a		;0347
	in a,(027h)		;0349  poll for DRQ (bit 3) or ERROR
	or a			;034b
	jp m,00349h		;034c  busy → loop
	ld bc,00020h		;034f  32 words = 256 bytes (WD reads 16-bit words)
	inir			;0352  block-in from port C (0x20?) to (HL)
	pop bc			;0354
	srl a			;0355  check error bits
	jr nc,$+18		;0357  no error
	in a,(021h)		;0359  read error register
	bit 2,a			;035b  bad sector?
	ld hl,00416h		;035d
	jp z,00215h		;0360  → error display
	ld hl,00403h		;0363
	jp 00215h		;0366  → fatal error
	inc de			;0369
	dec bc			;036a
	ld a,b			;036b
	or c			;036c
	jr nz,$-44		;036d  more sectors → loop
	ret			;036f

; =============================================================================
; lba_encode — encode DE as LBA and write to Winchester registers
; =============================================================================
; Input: DE = logical block address (sector number)
; Writes: port 0x23 (sector), port 0x24 (sector count?),
;         port 0x25 (cylinder low), port 0x26 (cylinder high)
lba_encode:			;0370
	push de			;0370
	ld a,e			;0371
	and 01fh		;0372  bits [4:0] = sector within cylinder
	out (023h),a		;0374  → sector register
	ld b,005h		;0376
	srl d			;0378  DE >>= 5 (shift out sector bits)
	rr e			;037a
	djnz $-4		;037c  5 shifts total
	; Remaining DE = cylinder number; encode via divide-by-6 (skewed sectors)
	ld c,006h		;037e
	ld a,d			;0380
	ld b,008h		;0381
	scf			;0383
cylinder_loop:
	rl e			;0384
	rla			;0386
	sbc a,c			;0387
	jr nc,$+5		;0388
	add a,c			;038a
	res 0,e			;038b
	djnz cylinder_loop	;038d  8-bit divide
	out (026h),a		;038f  → cylinder high
	ld a,e			;0391
	out (024h),a		;0392  → cylinder low (or sector count)
	xor a			;0394
	out (025h),a		;0395  → head / LBA high = 0
	pop de			;0397
	ret			;0398

; =============================================================================
; String table
; =============================================================================

str_banner:			;0399  "ROM de chargement rev 1-7\0"
	defb "ROM de chargement rev 1-7",0

str_souple:			;03b3  "Disque souple\0"  (floppy selected)
	defb "Disque souple",0

str_dur:			;03c1  "Disque dur\0"     (Winchester selected)
	defb "Disque dur",0

str_simple:			;03cc  "simple face\0"   (single-sided)
	defb "simple face",0

str_double:			;03d8  "double faces\0"  (double-sided)
	defb "double faces",0

; (further error/prompt strings follow at 0x03E5 – 0x044C, not fully decoded)

; =============================================================================
; 0x044D – 0x0466: USART paper-tape fallback boot
; =============================================================================
; If key 0 pressed AND Z flag set at 0x006D → jump here.
; Reads bytes from USART paper port (0x04/0x05), echoes to buzzer,
; accumulates them into memory starting at 0x4080.
usart_boot:			;0458
	in a,(005h)		;0458  read USART paper status
	and 002h		;045a  bit 1 = RX ready
	jr z,usart_boot		;045c  loop until byte available
	in a,(004h)		;045e  read byte
	out (003h),a		;0460  echo to buzzer
	ld b,a			;0462
	add a,e			;0463  accumulate checksum
	ld e,a			;0464
	ld a,b			;0465
	ret			;0466

; =============================================================================
; 0x046D – 0x04C1: Full USART paper-tape load sequence
; Reads header (load address, byte count), then data, then verifies.
; Jumps to loaded code on success.
; =============================================================================
; (detailed sequence at 0x046D–0x04C1, standard Intel-HEX / paper-tape style)

; =============================================================================
; 0x04C2 – 0x07FF: "SYS.SY pre-loader stub"
; =============================================================================
; This region contains a secondary stub copied to 0x57C0 by the boot sequence
; (LDIR at 0x009D: src=0x0137, dst=0x57C0, len=0x000C).
; The stub patches the RST 30h indirect vector to point at the Winchester
; read routine in the loaded SYS.SY, then jumps to 0x5500.
;
; Also contains the display-driver and font-rendering routines used to paint
; the self-test/startup graphics on the graphic plane (0x058B – 0x07B4):
;   0x058B  draw_glyph_list  — walk a list of (x,y,char) tuples, render each
;   0x065B  draw_glyph       — render one character via Bresenham-style plotter
;   0x0699  draw_line        — vector line primitive
;   0x06B7  beep2            — alternate beep (same algorithm as 0x0106)
;   0x06CD  str_copy2        — second copy of str_copy (RST 28h)
;   0x070D – 0x07B4  — startup layout descriptors (x/y tables, char data)
;
; 0x07CB – 0x07FF: Filled with RST 38h (0xFF) — unused ROM pad / INT trap

; END of Phantom ROM (0x07FF)
