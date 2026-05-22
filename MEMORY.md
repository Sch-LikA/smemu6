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

**Superseded by later documentation review:** the newer Smaky6_V4_clean manual page 10.4-2 explicitly states that when `FOUND=0`, the CLA read value corresponds to function keys. The conclusion below remains useful as a record of what the emulator did at the time, but it is no longer the hardware truth.

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
- Historical note only: the later manual review contradicted this; hardware actually documents function-key data on CLA when `FOUND=0`
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
- Port 0x00 (CLA) = character latch area for ordinary keys, and function-key bitmask when `FOUND=0`
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

## Session: May 21, 2026 — Function Key Handling (Hardware-Faithful Approach)

### Issue Identified: SMILE App Can't Read Function Keys

**Problem:** SMILE app uses `?GETFON` system call to read function keys, but it wasn't working. FLIPPER app worked (uses syscall 0x0E), but SMILE didn't.

**Initial Root Cause Analysis:**
Previous fix (commit 09054a9) changed `keyboard_read_cla()` to return only `0x80` to prevent "à" character echo. But this broke GETFON register update:
- SAMOS ISR Stage 1: `LD (0x4580),0x00; IN A,(0x00); AND 0x7F; LD (0x4580),A`
- Old code returned `0x80 | fonct_bits` → stored `fonct_bits` to 0x4580 ✓
- New code returned `0x80` → stored `0x00` to 0x4580 ✗
- SMILE's ?GETFON reads 0x4580 but it was always 0x00

**First Attempt (Commit 343d1dd) — NOT HARDWARE-FAITHFUL:**
Updated GETFON register (0x4580) directly in `refresh_function_bits()` whenever function key bits change. This worked for SMILE but **bypassed the OS**:
```c
m->bus[0x4580u] = m->kbd.fonct_bits;  /* Emulator doing what OS should do */
```

**Why This Was Wrong:**
- Real hardware: function key signals are physical → CLA → SAMOS ISR reads → stores to 0x4580
- Shortcut approach: emulator directly writes 0x4580, skipping the OS
- Violates goal of being "as close to original hardware as possible"

**Correct Solution (Commit ccb443d) — HARDWARE-FAITHFUL:**
Return function key bits directly from CLA so SAMOS ISR naturally updates 0x4580:

**Implementation:**
```c
uint8_t keyboard_read_cla(struct Smaky6 *m)
{
    if (m->kbd.found) {
        /* existing character latch logic */
    }
    
    /* When no character latched: return function key bits */
    return (uint8_t)(m->kbd.fonct_bits & 0x7Fu);  /* <-- NEW */
}
```

Now the chain works naturally:
1. CLA returns function key bits (bit 7=0, bits 0-6=F1-F7 bitmask)
2. SAMOS ISR Stage 1 reads CLA: `IN A,(0x00)`
3. SAMOS stores to GETFON: `AND 0x7F; LD (0x4580),A`
4. SMILE calls ?GETFON and reads the result from 0x4580

**Why Function Keys Don't Cause Character Echo:**
- Function keys are **out-of-matrix**, separate from the 64-position character matrix
- S471 PROM never returns character codes for function keys
- CLA returns bits 0-6 = function key bitmask, bit 7 = 0 (FOUND flag)
- This can never be confused with a character code (characters are latched separately via key_code)
- SAMOS treats bit 7=0 as "key present" and processes accordingly
- Function key bits reach CLA only when `found=0` (no character latched)

**Result:**
- FLIPPER: Works (reads 0x457E via syscall 0x0E, or reads CLA directly)
- SMILE: not yet revalidated; later user testing reported it still had never seen function keys
- F1-F7 don't echo as characters (never reach character code path)
- **GETFON updated by OS, not by emulator shortcut** (hardware-faithful)

**Commit History:**
- `09054a9` - fix: don't return function key bits from CLA [fixed echo, broke SMILE]
- `7aedd58` - fix: function key acknowledgment in port 0x01 [supporting]
- `343d1dd` - fix: keep GETFON register current for ?GETFON [worked but not hardware-faithful]
- `ccb443d` - refactor: switch to faithful hardware model [OS controls 0x4580]

**Architectural Lesson:**
When an emulator shortcut works but bypasses OS logic, reconsider whether it maintains hardware fidelity. The faithful approach lets the OS do its job and creates fewer surprises later.

---

## Session: May 22, 2026 — ?GETFO Syscall Analysis & Implementation

### Decision: use a dedicated branch for the hardware-first keyboard refactor

**Decided:** carry the full keyboard/CLA cleanup on branch `refactor/keyboard-hardware-model` instead of continuing directly on `master`.

**Why:**
- the current implementation mixes hardware-faithful CLA behavior with compatibility mirrors and helper hacks
- keyboard state is currently mutated in multiple files (`keyboard.c`, `machine.c`, `main.c`, plus a helper in `memory.c`)
- reducing that to one owning abstraction is architectural work and needs safer isolation
- we now have stronger hardware documentation, so the refactor target is clear enough to pursue aggressively

**Acceptance criteria:**
- preserve DX0 autoboot and ordinary-key delivery
- preserve the documented `FOUND=0 => CLA returns function-key bits` behavior
- keep `0x4580` and `0x45BD/0x45BE` consistent for software that currently reads them
- revalidate FLIPPER, SMILE, and FKTEST before merge

**Rejected:**
- continuing directly on `master`: too easy to destabilize the current keyboard path while removing compatibility scaffolding
- big-bang rewrite with no staged validation: too risky given the number of existing helper paths

### First refactor slice on branch

**Completed:** centralized function-key state mutations behind keyboard-owned helpers.

**What changed:**
- added keyboard helpers for clearing all function bits, setting mouse-held function bits, and acknowledging function-key masks
- switched `main.c` focus-loss and mouse paths to those helpers
- switched `machine.c` port `0x01` ACK and reset/injection paths to those helpers
- corrected stale public CLA comment in `keyboard.h`

**Why:**
- this is the smallest safe step toward a hardware-first model with one owning abstraction
- it reduces the chance of `fonct_keyboard_bits`, `fonct_mouse_bits`, `fonct_bits`, and cache mirrors drifting apart

**Validation:**
- `make -C build smemu6 -j4` completed successfully on the refactor branch after the slice landed

### Second refactor slice on branch

**Completed:** moved the syscall `0x0E` / `0x457E` compatibility helper path under keyboard ownership.

**What changed:**
- added `keyboard_read_stage1_code()` to `keyboard.c` / `keyboard.h`
- changed `memory.c` to delegate the `0x457E` helper-path policy there instead of directly consulting `fonct_bits`

**Why:**
- `0x457E` fallback behavior is keyboard policy, not generic memory behavior
- this keeps hardware-primary behavior and compatibility shims in the same subsystem while the branch is being simplified

**Validation:**
- `make -C build smemu6 -j4` completed successfully
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)

### Current evaluation after the first two slices

**What now matches the updated documentation better:**
- CLA semantics and public comments are aligned with `FOUND=0 => function-key bits`
- function-key compatibility policy is being pulled back under keyboard ownership instead of being scattered across subsystems

**What is still intentionally not fully hardware-pure:**
- direct mirroring to `0x4580` and `0x45BD/0x45BE`
- the emulator-side function-key ACK behavior on port `0x01`
- the syscall `0x0E` compatibility helper path now lives in `keyboard.c`, but still exists

**What remains unimplemented from the newer manual page:**
- the special side effects of two and three consecutive `LOAD A,$CLA`
- finer-grained scan timing around the documented `300 kHz` keyboard oscillator and `~3 us` adjacent-key case

### Third refactor slice on branch

**Completed:** removed the direct `0x4580` compatibility mirror while keeping the `?GETFO` cache path intact.

**What changed:**
- `refresh_function_bits()` no longer writes `m->bus[0x4580u]`
- direct compatibility mirroring is now limited to `0x45BD/0x45BE`

**Why:**
- page 10.4-2 makes CLA the hardware source of truth for function keys when `FOUND=0`
- SAMOS Stage 1 already copies that CLA value into `0x4580`, so the emulator-side `0x4580` write was redundant if the hardware model is correct
- this is the smallest next reduction toward a hardware-first model without disturbing the currently observed `?GETFO` path

**Validation:**
- `make -C build smemu6 -j4` completed successfully after the change
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)

**New floor confirmed for now:**
- raw SYS.SY bytes still show direct reads from `0x45BD` at `0x0EF8` and `0x45BE` at `0x0EFC`
- until FLIPPER / SMILE / FKTEST are revalidated with a narrower change, `0x45BD/0x45BE` remain the compatibility floor for `?GETFO`

### Fourth refactor slice on branch

**Completed:** changed the emulator-side port `0x01` ACK handling so it no longer clears the host-held F-key state.

**What changed:**
- `keyboard_acknowledge_function_bits()` no longer mutates `fonct_keyboard_bits`
- ACK writes are now treated as software-side acknowledgment only, not as physical host key release

**Why:**
- `fonct_keyboard_bits` is the host-held F1..F7 state in the current model
- clearing it from the ACK path collapses two different meanings into one variable: physical host hold state and software-consumed visibility
- user revalidation says SMILE still has never seen function keys, so the older assumption that the current ACK behavior was correct is no longer credible

**Validation:**
- `make -C build smemu6 -j4` completed successfully after the change
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)
- later interactive revalidation showed FLIPPER still works correctly, including held function keys, with no repeat bug
- SMILE is still the remaining failing application path

### Application revalidation update

**Observed:** FLIPPER still works fine on the refactor branch.

**Confirmed:**
- function keys still work in FLIPPER
- keeping a function key held still works in FLIPPER
- the earlier repeat bug is still absent in FLIPPER

**Implication:**
- the current ACK/held-state behavior did not regress the already-working FLIPPER path
- the remaining keyboard bug is narrower and now appears specific to the SMILE-facing function-key path rather than to generic function-key hold/repeat handling

### SMILE.SM static function-key audit

**Observed from the binary itself:**
- `SMILE.SM` does not directly read `0x4580`, `0x45BD`, or `0x45BE`
- `SMILE.SM` does not directly `CALL 0x0EE7`
- it contains a single `RST 20h / 0x0E` function-key-related call site at `0x55D0`

**Documented syscall mapping:**
- `private/docs/disasm/SYS.SR` maps `CODE 0EH = ?GETFO`
- the same source lists `GETFO` as a read of octal `42576`, i.e. `0x457E`

**Important static implication:**
- for SMILE, the relevant path is syscall-mediated and keyed by exact bitmask values
- exported `FLO.ST` constants give `PROGRA=0x08`, `KILL=0x10`, `COPY=0x20`, `CURSOR=0x40`
- the current emulator mapping still uses a different assignment for those higher four function keys

**Most likely explanation for the current user report:**
- SMILE is reading function-key state, but the emulator is returning the wrong named bit for at least `PROGRA/F4`

### Fifth refactor slice on branch

**Completed:** reconciled the emulator's named function-key bit assignments with the canonical `FLO.ST` values while keeping host `F1`..`F7` as the physical input mapping.

**Canonical values from `FLO.ST`:**
- `CHANGE=0x01`
- `SEARCH=0x02`
- `SHOW=0x04`
- `PROGRA=0x08`
- `KILL=0x10`
- `COPY=0x20`
- `CURSOR=0x40`

**Why this is the right next step:**
- `SMILE.SM` uses `RST 20h / 0x0E = ?GETFO` and branches on exact bitmask values
- the current emulator mapping matches only the lower three bits and permutes the upper four
- that mismatch cleanly explains why SMILE misses named keys such as `PROGRA/F4` while FLIPPER can still appear fine

**What changed:**
- `keyboard.c` now maps the seven host function keys as
  `CURSOR=0x40`, `COPY=0x20`, `KILL=0x10`, `PROGRA=0x08`,
  `SHOW=0x04`, `SEARCH=0x02`, `CHANGE=0x01`
- `main.c` and `video.c` were updated to use the same canonical bit order for the on-screen function-key bar

**Validation:**
- `make -C build smemu6 -j4` completed successfully after the bit-order change
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)
- interactive SMILE and FLIPPER revalidation is still required after the semantic change

### Sixth refactor slice on branch

**Completed:** tested the hypothesis that `PROGRA` should enable the S471 `FNCT` layer when held.

**Root cause identified from user validation:**
- `PROGRA+z` degraded to plain `z` while assembling `HELLO.SR`
- `keyboard.c` already had a four-layer S471 table including `S471_LAYER_FNCT`
- but `current_layer()` only considered `shift_pressed` and `caps_lock_active`
- so concurrent `PROGRA` + matrix-key combinations were always resolved through the normal layer

**What changed at the time:**
- `current_layer()` was changed to return `S471_LAYER_FNCT` when `PROGRA` (`fonct_bits & 0x08`) was held

**Later correction:**
- subsequent user tests (`û`, then `ù`) showed that this model was wrong for SMILE shortcuts
- the final working direction is to keep `PROGRA` as a separate function bit and leave the ordinary key on the normal matrix layer

### Seventh refactor slice on branch

**Completed:** fixed the SDL text-input bypass while the `PROGRA` path is active.

**Refined root cause:**
- the sixth slice's S471-layer hypothesis was still in place at the time
- text-capable keys such as `z` were still skipped in `keyboard_event()` when they had SDL text input support
- their matching `SDL_TEXTINPUT` events then injected plain direct text, which is why `PROGRA+z` could still degrade to plain `z`

**What changed:**
- text-capable scancodes no longer bypass the matrix path when `PROGRA` is active
- `keyboard_text_event()` now ignores matching SDL text input while `PROGRA` is active

**Validation:**
- `make -C build smemu6 -j4` completed successfully after the change
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)
- interactive revalidation of `PROGRA+z` / SMILE assembly shortcuts is still required

### Eighth refactor slice on branch

**Completed:** corrected the `?GETFO` accessor policy for simultaneous function + ordinary keys.

**Refined root cause:**
- the prior slices fixed the host key path and the text-input bypass
- but the `0x0519` compatibility helper still preferred the staged ordinary byte in `0x457E`
- with `PROGRA+z`, SMILE therefore saw `A=0x1A` (`û`) from `?GETFO` instead of the held function bit `A=0x08`

**What changed:**
- `keyboard_read_stage1_code()` now returns the live held function bits directly
- the `?GETFO` helper no longer consumes or shadows ordinary-key staging from `0x457E`

**Validation:**
- `make -C build smemu6 -j4` completed successfully after the change
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)
- interactive revalidation of `PROGRA+z` / SMILE assembly shortcuts is still required

### Ninth refactor slice on branch

**Completed:** tested the host-layout hypothesis for alphabetic `PROGRA+letter` shortcuts.

**Refined root cause:**
- after the `?GETFO` fix, `PROGRA+z` still produced `ù` on the user's machine
- that value matches the FNCT-layer code for the Smaky `y` position, not `z`
- the remaining bug was host-layout dependent: on a QWERTZ keyboard, the key labeled `z` arrives from SDL as `scancode=Y`

**Later correction:**
- this addressed the intermediate `ù` symptom but did not solve the actual behavioral mismatch for SMILE
- the remaining issue was deeper: SMILE wanted plain `z` plus a separate `PROGRA` bit, not an FNCT-layer ordinary byte at all

### Tenth refactor slice on branch

**Completed:** removed `PROGRA` from the matrix-layer selector.

**Refined root cause:**
- the static SMILE dispatch preserves the ordinary key separately in `B` and reads `PROGRA` through `?GETFO`
- treating `PROGRA` as a matrix FNCT-layer selector therefore changed the ordinary key into the wrong byte (`û` / `ù`) instead of leaving it as plain `z`

**What changed:**
- `current_layer()` no longer switches to `S471_LAYER_FNCT` when `PROGRA` is held
- the existing matrix-path and `?GETFO` fixes remain, so `PROGRA` can coexist with a plain ordinary key instead of rewriting it

**Validation:**
- `make -C build smemu6 -j4` completed successfully after the change
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)
- interactive revalidation of `PROGRA+z` / SMILE assembly shortcuts is still required

### Eleventh refactor slice on branch

**Completed:** tested the hypothesis that removing the ordinary-key bit-7 prefix would fix simultaneous `PROGRA+ordinary` handling.

**Refined root cause:**
- after keeping `PROGRA` separate from the matrix layer, `PROGRA+END` still echoed the ordinary END glyph instead of triggering SMILE's function-key action
- the remaining culprit was the `regular_prefix_pending` path: the first ordinary CLA read still returned `0x80 | key_code`
- that let SAMOS route an ordinary key through the same workspace path used for function/no-key payloads, clobbering live function state with the ordinary key code

**Later correction:**
- this hypothesis was falsified immediately by user retest: ordinary keyboard input stopped working at the CLI prompt
- the audited SAMOS model still requires the first ordinary CLA read to be `0x80 | key_code` for normal post-boot typing

### Twelfth refactor slice on branch

**Completed:** restored the bit-7-first ordinary CLA delivery.

**Refined root cause:**
- removing the prefix broke the normal SAMOS Stage 1 / Stage 3 path for ordinary post-boot input
- the earlier audit around `pc=0x0162` / `pc=0x0171` remains the controlling evidence: the first ordinary key still needs to appear as `0x80 | key_code`

**What changed:**
- `keyboard_read_cla()` again prefixes the first ordinary latched read with bit 7 when `regular_prefix_pending` is set
- the direct `0x4580` compatibility mirror remains in place from the previous slice

**Validation:**
- `make -C build smemu6 -j4` completed successfully after the change
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)
- ordinary CLI typing should be restored; simultaneous `PROGRA+ordinary` behavior still needs interactive revalidation

**Validation:**
- `make -C build smemu6 -j4` completed successfully after the change
- DX0 boot with `floppies/Sys2-2.dsk` still reaches the CLI prompt (`* -`)
- interactive revalidation of `PROGRA+z` / `PROGRA+END` in SMILE is still required

### Host mapping decision correction

**Decided:** keep the seven bottom-row function keys on host `F1`..`F7` only.

**Why:**
- the previously written note that mapped them to `LCTRL`, `LALT`, `RALT`, `LGUI`, `Home`, `End`, and `Insert` does not match the live keyboard matrix code
- some of those host keys are already used elsewhere in the ordinary matrix path, so broad alternate mappings are not a safe drop-in change
- user explicitly wants the canonical `F1`..`F7` behavior retained and the alternate mappings removed

**Effect:**
- PROGRA remains `F4`
- no alternate host mapping is retained for the bottom-row function keys on this branch

### Correction: FKTEST and the exact ?GETFO entry are not trusted evidence yet

**Corrected understanding:**
- `FKTEST.SM` is currently untested and must not be treated as proof of correct function-key behavior
- the `CALL 0x0EE7` in `FKTEST.SM` is also not trustworthy as a confirmed `?GETFO` entry point
- a direct blob dump from `SYS.SY` shows that `0x0EE7` sits in the middle of an existing routine, so the earlier conclusion "?GETFO is the callable routine at `0x0EE7`" is not established

**What still remains useful from that investigation:**
- the nearby SYS.SY code does directly read `0x45BD` and `0x45BE`
- those addresses are still relevant compatibility state, but the exact callable contract around them needs re-checking from trusted software behavior, especially SMILE itself

### Documentation Clarification from Smaky6_V4_clean page 10.4-2

The newer manual resolves the remaining ambiguity in the hardware description:
- when `FOUND=0`, the value read from CLA corresponds to the function keys
- bit 7 is sufficient to distinguish `FOUND=0` from `FOUND=1`
- the keyboard status register is therefore not required for ordinary key/function-key discrimination
- two consecutive `LOAD A,$CLA` within less than 5 us enter joystick / potentiometer sampling mode for about 5 ms
- three consecutive `LOAD A,$CLA` toggle the speaker or lamp
- the keyboard scan oscillator is 300 kHz
- for two adjacent keys in scan order, only about 3 us separates clear of `FOUND` from reassertion of `FOUND`

This means the hardware-faithful CLA model is:
- ordinary key latched: read ordinary key code
- `FOUND=0`: read function-key bitmask directly from CLA

The remaining direct writes to `0x45BD/0x45BE` are documented as a compatibility mirror for the currently observed `?GETFO` software path, not as the source of the hardware truth. `0x4580` has now been returned to the normal SAMOS Stage 1 update path.

### Discovery: nearby SYS.SY code reads 0x45BD/0x45BE directly

**Investigation:** User reported SMILE still couldn't detect function keys despite hardware-faithful CLA fix. Disassembly around the previously suspected SYS.SY function-key helper path showed direct reads from `0x45BD` and `0x45BE`.

**Key Finding:**
A nearby SYS.SY routine does **not** read from CLA port (0x00). It reads cached state from memory locations:
- `0x45BD`: Primary function key state cache
- `0x45BE`: Secondary cache (used by ?GETFO for decoding)

**Disassembly Evidence:**
```
0x0EF8: LD A,(0x45BD)    ; Read first cache location
0x0EFC: LD A,(0x45BE)    ; Read second cache location
```
No `IN` (`0xDB`) opcodes were found in that nearby read/dispatch path.

### Problem: 0x45BD Write Caused Boot Hang

**Initial Approach (Commit 7608a5f):**
- Guarded write to 0x45BD: only write after `machine_cli_prompt_visible()` returns true
- Intended to protect boot initialization from corruption
- **Result: Boot hung at "ROM de chargement"**

**Root Cause:** Unclear why guarded write broke boot. The guard condition itself may have been evaluating incorrectly, or 0x45BD write was always unsafe during early boot phases.

### Solution: Unconditional Cache Writes (Commit fb6e029)

**Revised Implementation:**
Remove guard condition and write to the `?GETFO` cache locations unconditionally:
```c
/* In refresh_function_bits() */
m->bus[0x45BDu] = m->kbd.fonct_bits;   /* ?GETFO cache primary */
m->bus[0x45BEu] = m->kbd.fonct_bits;   /* ?GETFO cache secondary */
```

**Result:** Boot works with the direct `0x45BD/0x45BE` cache path, and later branch validation also confirmed that boot still works after removing the redundant direct `0x4580` mirror.

**Key Insight:** Unlike `0x4580` (which SAMOS ISR updates from CLA), `0x45BD/0x45BE` are the current software compatibility floor for `?GETFO`. Writing those two unconditionally is safe for boot.

### Testing Tool: FKTEST.SM

Created simple CALM assembly tool intended to probe function-key behavior:
- **Location:** `private/floppies/extracted/Sys2-2-SMILE/FKTEST.SM`
- **Current status:** untested; do not treat it as proof of the correct syscall entry or return convention yet

**Important:** GUI must be visible for SDL keyboard input. Headless mode (`-no-display-off -scrdump`) cannot receive key presses.

### Commits Made:
- `7608a5f` - feat: update GETFON cache (0x45BD) after boot for ?GETFO syscall [tried guard, hung boot]
- `fb6e029` - feat: write function key state to 0x45BD/0x45BE cache for ?GETFO syscall [final working solution]
- `508da61` - docs: document ?GETFO syscall implementation and FKTEST tool

### Architecture Summary:
- **CLA (port 0x00):** Returns function key bits when no character latched
- **0x4580 (GETFON):** Updated by SAMOS ISR Stage 1 (reads from CLA)
- **0x45BD/0x45BE:** ?GETFO cache locations (emulator writes directly now)
- **Exact `?GETFO` entry:** still unresolved; `0x0EE7` is not yet trusted as the callable entry point

### Status: not yet complete
- hardware-faithful CLA approach is still the branch target
- cache locations are still kept current by the emulator where currently modeled
- user revalidation says SMILE has still never seen function keys so far
- `FKTEST.SM` exists as a draft probe only and is not yet verification evidence



---
