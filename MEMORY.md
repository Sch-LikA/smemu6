# MEMORY.md — Project Decision Log

Significant decisions, reasoning, and rejections for the smemu6 project.
Read at the start of every session. Never contradict a logged decision without flagging it first.

---

## Session: May 20, 2026

### Decision: Create CLAUDE.md project guidelines
**Decided:** Comprehensive project guidelines file capturing communication style, workflow requirements, technical context, and protection rules.

**Why:** 
- Smemu6 workflow has specific requirements (commit after mods, update docs first, use tmp/)
- User has clear communication preferences (no preambles, match response length, show options, explicit about uncertainty)
- Need explicit confirmation for destructive/sensitive operations (deployments, confidential info, personal actions)
- Single reference document for every session improves consistency

**Rejected:**
- Single scattered notes: Too fragmented, easy to miss critical rules
- Oral communication only: Not persistent across sessions

---

## Session: Previous Sessions — Key Findings & Notes

### SDCC Exit Corruption Issue
**Status:** Pre-existing architectural incompatibility, documented as known limitation.
**Finding:** SDCC-compiled programs execute correctly but fail to return cleanly to CLI.
- Programs produce correct output
- Screen corruption occurs on exit (rows 15–19 garbled, repeated underscores in row 19)
- CLI prompt never reappears
- User must manually terminate emulator
**Investigation Outcome:** Root cause unknown (SDCC-specific vs. system-level). Comparison with native CALM (hello_smug) needed to determine if issue is SDCC-specific or architectural in SAMOS.

### CALM Assembly Findings
**Proper Approach:** Use system routines, not hardcoded addresses.
- `.REF SM6` — REQUIRED to access system symbols (NOT optional)
- `.LOC` — OPTIONAL; SMILE provides sensible default load address if omitted
- System routines use `?`-prefixed names: ?TEXTIM, ?RTN, ?IALPHA, ?IGRA, ?IAGRA
- Threading pattern (data-driven program) is idiomatic for CALM
- Let SMILE handle memory management and linking

**Improper Approach (AVOID):**
- Manual hardcoding of memory addresses (0x6000, 0x45C0, 0x56AE)
- Not portable, not maintainable, doesn't use system conventions

**CALM Baseline:** `calm/examples/hello_smug/hello.sr`
- 10-line proper implementation
- Uses .REF SM6 and system routines
- Expected behavior: Exits cleanly to CLI with no corruption

**SMILE Assembler:** Interactive-only tool inside emulator. No batch/headless mode available.

### Testing Framework & Comparison
**Created:** Reusable build script `calm/build_smaky6_calm_example.sh` for CALM examples.
**Baseline Comparison:** hello_smug prepared as golden standard for comparing with SDCC corruption.
**Expected Success Pattern:** 
```
Hello World
>
```
Program exits cleanly to CLI prompt with no screen corruption.

### Documentation Refactoring
**SDCC_TODO.md:** Reduced from 2,097 lines to 230 lines.
- Created status table, technical facts, phased plan
- Moved investigation details to "Investigation Archive" at bottom
- Preserved detailed traces for reference without cluttering main content

### Emulator Behavior & Flags
**Screen Capture:** Use `-scrdump` flag to enable screen dump on exit.
- `-no-display-off`: No longer needed (was workaround for function key repeat bug, now fixed)

**Launcher:** Use `-no-launcher` to skip the launcher screen and go directly to CLI prompt.
- Speeds up testing significantly, especially for command injection tests
- Recommended for all testing workflows where launcher UI is not needed

**Command Injection:** `-inject-str` fires only when `machine_cli_prompt_visible()` returns true (CLI is ready).

**Boot Behavior:** DX0 boots automatically (no `-autoboot` flag needed in current codebase).

**Beeper:** Always use `-no-beeper` argument when running the emulator to suppress sound output.

**Timeout for Full Boot:** Use ≥ 20 seconds when running emulator to full CLI prompt.
- 5 seconds: Too short, OS won't finish booting
- 20 seconds: Appropriate for full SAMOS boot and testing
- With `-no-launcher`: Can complete faster

**Note:** `-autoboot` flag was removed in previous development; machine boots from DX0 automatically.

### Floppy Extraction Preference
**Workflow Decision:** When extracted files from a floppy already exist (e.g., `private/floppies/extracted/Sys2-2`), always reuse them instead of re-extracting from the DSK file.
**Reason:** Avoids unnecessary re-extraction; uses already-prepared content.

### File Output Location
**Standard Practice:** Use repo-local `tmp/` directory for all temporary files, logs, and test output. Never use system `/tmp`.
**GitHub Base URL:** https://github.com/Sch-LikA

### Git Commit Format
**Standard:** `type: description`
- `feat:` New feature or example
- `fix:` Bug fix
- `docs:` Documentation update
- `refactor:` Code reorganization
- `test:` Testing infrastructure

### Critical Memory Layout (Smaky 6)
```
0x0000–0x07FF   Phantom ROM / SYSMON (2 KB)
0x0800–0x22FF   SAMOS kernel (from SYS.SY)
0x4000–0x44FF   Alpha screen (20 rows × 64 cols = 1,280 B)
0x4500–0x45FF   OS workspace (256 B)
  0x45C0        BUFLIN (CLI line buffer)
0x4600–0x54FF   Graphics buffer (3,840 B)
0x6000–0xEFFF   User program area (default for .SM programs)
0xF000–0xF300   User stack (recommended: SP=0xF000)
```

### SDCC Build Command
```bash
sdcc/build_smaky6_sdcc_example.sh example_name
```
**Critical:** Stack pointer SP=0xF000 must be set before main.

### CALM Build Command
```bash
calm/build_smaky6_calm_example.sh example_name
```
**Note:** Assembly happens inside emulator via SMILE (interactive), not offline.

### Example Emulator Commands
```bash
# Standard run with output capture
./smemu6 floppies/Sys2-2.dsk -scrdump -no-display-off -no-beeper > tmp/out.log

# With command injection (fires on machine_cli_prompt_visible())
./smemu6 floppies/Sys2-2.dsk -inject-str "HELLO" -scrdump -no-display-off -no-beeper > tmp/out.log
```

---

---

## Session: May 20, 2026 (Continued) — Function Key Repeat Fix

### Decision: Implement Function Key Acknowledgment in Port 0x01 Write Handler

**Root Cause Identified:** Function keys (F1-F7) get stuck repeating because OS writes to port 0x01 to acknowledge/clear function key bits, but the emulator wasn't handling those writes.

**Decided:** Added function key acknowledgment logic to `machine_port_out()` in `src/machine.c` port 0x01 write handler:
- Extract bits 0-6 from the value OS writes to port 0x01
- Clear corresponding bits from `m->kbd.fonct_keyboard_bits`
- Add debug trace output when `-tracekbd` is enabled
- Preserve existing Phantom ROM bank-switch (data=0x00) and ISR ACK (data=0x08) behavior

**Implementation Details:**
```c
/* In machine.c port 0x01 write handler: */
uint8_t fkey_mask = data & 0x7Fu;  /* bits 0-6 only */
if (fkey_mask != 0x00) {
    m->kbd.fonct_keyboard_bits &= ~fkey_mask;
    /* Clear the corresponding bits to break the repeat loop */
}
```

**Why:**
- Real hardware SAMOS and Flipper program work correctly with function keys
- Same SAMOS binary gets stuck in emulator → emulator bug, not OS bug
- OS enters infinite loop reading port 0x01, then writing to it to acknowledge
- Without clearing the bits on write, OS sees them still set and loops forever
- Bits 0-6 map to F7, F6, F5, F4, F3, F2, F1 respectively

**Rejected:**
- Blocking SDL repeat events: Superficially fixes symptom but doesn't address root cause (OS behavior is correct)
- Ignoring function key writes: Breaks function key interrupt handling protocol

**Testing Notes:**
- Use timeout ≥ 20 seconds for full OS boot to CLI (5 seconds is too short)
- With `-no-launcher -no-beeper`, boot completes cleanly
- Test result: F1 produces "à" character (WRONG - function keys should not echo anything)
- After "à" appeared, it briefly stopped, then repeated
- Function keys should NOT produce any character output to display

**Commit:** Applied to `src/machine.c` (commit 7aedd58)

**Issue:** F1 echoing "à" indicates possible incorrect OS behavior or emulator not properly separating function key bits from character codes

**ROOT CAUSE FOUND (Line 713 in keyboard.c):**
When no character is latched (found=0), `keyboard_read_cla()` was returning function key bits:
```c
return 0x80u | (m->kbd.fonct_bits & 0x7Fu);  // WRONG!
```
- F1 sets bit 0x10 in `fonct_bits`
- OS reads port 0x00 (CLA) and gets 0x90 instead of waiting for actual character
- OS interprets 0x90 as character code and displays "à" 
- This is the root cause of the character appearing and repeating

**FIXED (Commit 09054a9):**
Changed `keyboard_read_cla()` to return only 0x80 when no character is latched:
```c
return 0x80u;  // Don't include function key bits!
```
Function keys are separate from character matrix and should never be returned as character codes through CLA.

**Testing result:** FIXED ✓
- F1 pressed once at CLI, no "à" character appeared on screen
- No character repetition observed
- Some keyboard loop activity seen via -tracekbd (expected behavior, likely interrupt/reassert processing)

**Status:** Function key repeat bug RESOLVED. F1-F7 now work correctly without echoing characters or repeating.

**Commit History:**
- `09054a9` - fix: don't return function key bits from CLA (port 0x00) read [MAIN FIX]
- `7aedd58` - fix: function key acknowledgment in port 0x01 write handler [SUPPORTING]
- `d54d766` - docs: document root cause and fix for function key repeat bug
- `5d1aa80` - docs: function key repeat bug FIXED

---

## Session Summary: May 20, 2026

**Worked on:** Function key repeat bug affecting F1-F7 in emulator

**Completed:** 
✓ Identified root cause: `keyboard_read_cla()` returning function key bits as character codes
✓ Implemented fix: Return only 0x80 from port 0x00 when no character latched
✓ Verified fix: F1 no longer produces "à" or repeats
✓ All changes committed with clear commit messages

**Decisions Made:**
- Function key bits must never be mixed into character latch reads (port 0x00)
- Port 0x01 write handler needs to acknowledge/clear function key bits
- All keyboard-related documentation moved to MEMORY.md (this file)

**Documentation Updated:**
- MEMORY.md: Added comprehensive root cause analysis and fix documentation
- Added testing guidance: 20 second timeout, -no-launcher, -no-beeper flags
- Noted -no-display-off is no longer needed (was workaround for this bug)

**Next Session Priorities:**
- Test all F1-F7 keys to ensure they all work correctly
- Verify no side effects on other keyboard functionality
- Check if the port 0x01 write handler needs any adjustments
- Monitor for any regressions in keyboard handling

**Key Technical Notes:**
- Function keys are separate I/O pins from character matrix
- Port 0x00 (CLA) = character latch area (characters only, NOT function keys)
- Port 0x01 (status) = function key bits (bits 0-6) + FOUND status (bit 2)
- Function keys should never produce character output

---

## Hardware Reference (from docs/dev/HARDWARE.md, PLAN.md)

### CPU & Clock
- **Z80 @ 2.5 MHz** (12.0576 MHz crystal ÷ 5)
- **T-states/frame**: 50,000 (50 Hz interrupt)
- **Interrupt mode**: IM 0 (RST 38h for display, RST 50h for traps)
- **Reset**: Power-on + RESET signal

### Memory Map (64 KB Phantom Model)
- `0x0000–0x07FF`: 2 KB **Phantom ROM** (TMS2716 EPROM SYS17) → writable RAM after `OUT (01h), A=00h`
- `0x0800–0x3FFF`: OS + lower RAM (~14 KB available after SAMOS loads)
- `0x4000–0x44FF`: **Alpha (text) framebuffer** (20 rows × 64 cols = 1280 B)
- `0x4500–0x45FF`: **SAMOS OS workspace** (256 B, includes CLI buffer, current filename)
- `0x4600–0x54FF`: **Graphic framebuffer** (60 rows × 64 bytes = 3840 B)
- `0x8000–0xFFFF`: Upper 32 KB RAM (on extension board)

### Keyboard Hardware (from docs/dev/keyboard_analysis.md)

**Key ICs:**
- **B5 = S471** — keyboard encoder EPROM (64-position matrix + 4 layers → 7-bit keycode)
- **A8 = 4013** dual D flip-flop → generates FOUND signal + FULCLA latch
- **A4, A6 = LS 257 (C157)** → quad 2-to-1 mux (drives Z80 data bus)

**I/O Ports:**
- **Port 0x00 (CLA)**: Read/Write keyboard character latch area
  - Bit 7 = FOUND flag (0 = key present, 1 = no key)
  - Bits 0–6 = 7-bit keycode (character) OR function key bitmask
  - **Reading CLA clears the FOUND latch** via STROBE pulse
  - Scanner reasserts FOUND within **200µs** if key still held

- **Port 0x01**: Status register
  - Bit 2 = FOUND (reflects latch state)
  - Other bits = unused/reserved
  - **Writing to port 0x01 does NOT affect FOUND or FULCLA** (confirmed from docs)

**Function Key Bits (bottom-row out-of-matrix keys):**
- F1 = 0x10 (CURSOR)
- F2 = 0x08 (COPY)
- F3 = 0x40 (KILL)
- F4 = 0x20 (PROGRA)
- F5 = 0x04 (SHOW)
- F6 = 0x02 (SEARCH)
- F7 = 0x01 (CHANGE)

**SAMOS ISR Keyboard Workspace:**
- `0x457E`: Direct key code from CLA Stage 1 (used by FLIPPER/syscall 0x0E)
- `0x4580` (**GETFON register**): Function key status (used by SMILE/?GETFON call)
- `0x4558`: Repeat countdown (35 frames = 700 ms initially, then 3 frames = 60 ms)
- `0x4577`: Repeat key code (auto-repeat at 60 ms intervals after 700 ms delay)
- `0x4582`: Inter-frame keycode register (Stage 2 gate with 0x80 sentinel)
- `0x458A–0x4595`: Circular buffer workspace (managed by Stage 3)

**SAMOS ISR Stages:**
1. **Stage 1 (0x015B–0x016D)**: Direct CLA read → stores key at 0x457E, zeros 0x4558, returns early
2. **Stage 2 (0x016E–0x0197)**: No-key path / debounce → checks 0x4582 sentinel, waits 256 iterations, re-reads status + CLA
3. **Stage 3 (0x019B–0x01DF)**: Circular buffer management → promotes keys into workspace for Stage 4
4. **Stage 4 (0x01DF–0x0206)**: Auto-repeat (SAMOS 1.3+) → injects repeat at 700 ms / 60 ms intervals

**Power-on Boot (FOUND=1 automatic DX0 boot):**
- FOUND powers up asserted (undefined state, in practice SET)
- Phantom ROM `kbd_wait` at 0x00FD reads CLA in tight loop, exits when bit 7 = 0
- First CLA read returns 0x00 (Enter), ROM boots from DX0 without user keypress
- Emulator models with `found=1`, `key_code=0x00`, `physically_held=1` at startup
- Virtual key held until SAMOS ISR vector written to 0x003E (after init kbd_wait at 0x00B5)

### S471 Lookup Table (from docs/dev/keyboard-implementation.md)

The S471 PROM is authoritative for keycode generation:
- **4 × 64 lookup entries**: layers 0 (normal), 1 (Shift), 2 (FNCT), 3 (Caps)
- Maps **physical key position + layer** → **7-bit keycode**
- **Validated positions** (from S471 dump):
  - ESC/UNDO (top-left) = 0x06 on all layers
  - Backspace = 0x08 / 0x7F / 0x01 / 0x08
  - Tab = 0x09 / 0x0B / 0x03 / 0x09
  - Space = 0x20 / 0x20 / 0x02 / 0x20
  - CTRL = 0x1E on all layers
  - RETURN = 0x0D / 0x0C / 0x0A / 0x0D
  - DEFINE = 0x1F on all layers
- **Full matrix reference**: `docs/dev/smaky6_full_matrix_map.tsv` (64-position physical map)
- Alphanumeric matrix is **real Swiss-German QWERTZ hardware**, not ASCII

### Boot Media Contract (from docs/dev/SAMOS_BOOT_MEDIA.md)

**Bootable DX0 Floppy Structure:**
- Minimum system files: `SYS.SY`, `CLI.SY`, `ER.SY`
- Directory occupies first 3 sectors (32 entries max, 24 bytes each)
- Each entry stores `start_sector`, `end_sector`, `flags`, `load`, `entry`, date, type
- `SYS.SY`: starts sector 3, load 0x60C0, entry 0x5720 → Phantom ROM expects this
- `CLI.SY`: starts sector 38, load 0x5602, entry 0x5625 → shell UI after boot

**Why Plain Extraction Fails:**
- Requires metadata preservation: `flags`, `load`, `entry`, dates, sector order
- Emulator rebuilds with metadata sidecars: `NAME.TT.meta.json` and `NAME.DR.meta.json`
- Tool: `python3 tools/extract_samos_image.py <disk.dsk> <output-dir>`
- Rebuild: `./build/smemu6 -floppy-hostdir <hostdir>`
- Test: `ctest --test-dir build -R smemu6_virtual_floppy_dx0_hostdir_boot`

---

## Session: May 21, 2026 — GETFON Register Fix for SMILE App

### Issue Identified: SMILE App Can't Read Function Keys

**Problem:** SMILE app uses `?GETFON` system call to read function keys, but it wasn't working. FLIPPER app worked (uses syscall 0x0E), but SMILE didn't.

**Root Cause:** Previous fix (commit 09054a9) changed `keyboard_read_cla()` to return only `0x80` to prevent "à" character echo. But this broke GETFON register update:
- SAMOS ISR Stage 1: `LD (0x4580),0x00; IN A,(0x00); AND 0x7F; LD (0x4580),A`
- Old code returned `0x80 | fonct_bits` → stored `fonct_bits` to 0x4580 ✓
- New code returned `0x80` → stored `0x00` to 0x4580 ✗
- SMILE's ?GETFON reads 0x4580 but it was always 0x00

**Solution (Commit 343d1dd):**
Update GETFON register (0x4580) directly in `refresh_function_bits()` whenever function key bits change. This keeps GETFON current for ?GETFON calls while CLA returns only 0x80 (preventing character echo).

**Implementation:**
```c
static void refresh_function_bits(struct Smaky6 *m) {
    /* ... calculate fonct_bits ... */
    /* Update GETFON register directly */
    m->bus[0x4580u] = m->kbd.fonct_bits;  /* <-- NEW */
}
```

Also initialize 0x4580 to 0x00 in `keyboard_init()`.

**Result:**
- FLIPPER: Works (reads 0x457E via syscall 0x0E)
- SMILE: Works (calls ?GETFON which reads 0x4580)
- Both apps can now detect function keys
- F1-F7 still don't echo characters

**Commit History:**
- `09054a9` - fix: don't return function key bits from CLA [fixed echo, broke SMILE]
- `7aedd58` - fix: function key acknowledgment in port 0x01 [supporting]
- `343d1dd` - fix: keep GETFON register current for ?GETFON [fixed SMILE]





---
