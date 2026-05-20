# CLAUDE.md - Smemu6 Project Guidelines

This file documents conventions, requirements, and preferences for working with the smemu6 project.

## Project Overview

**Smemu6** is a cycle-accurate emulator of the Smaky 6, a 1978 Swiss Z80-based personal computer. The project focuses on:
- Accurate CPU/hardware emulation (Z80 at 2.5 MHz)
- Complete SAMOS operating system support
- Native assembly (CALM) and SDCC C compilation workflows
- Comprehensive documentation and testing

**Key Repository:** https://github.com/Sch-LikA/smemu6

---

## Claude Response Style

**No filler preambles.** Never open responses with phrases like:
- "Great question!"
- "Of course!"
- "Certainly!"
- "I'd be happy to..."
- "No problem!"
- Similar warmups or acknowledgments

**Start every response with the actual answer.** No preamble, no acknowledgment of the question. Get directly to the point.

**Match response length to task complexity.** Simple questions get direct, short answers. Complex tasks get full, detailed responses. Never pad responses with restatements of the question or closing sentences that repeat what you just said.

**Show approach options for significant tasks.** Before any significant task, present 2-3 ways you could approach this work. Wait for you to choose before proceeding.

**Be explicit about uncertainty.** If you are uncertain about any fact, statistic, date, or piece of technical information, say so explicitly before including it. Never fill gaps in your knowledge with plausible-sounding information. When in doubt, say so.

---

## Critical Workflow Requirements

### 1. Commit After Every Code Modification
**MANDATORY:** Each source code change must be committed immediately with a clear message.

```bash
git commit -m "component: brief description of change"
```

Do NOT accumulate uncommitted changes. This is a hard requirement.

### 2. Update Documentation Before Committing
**MANDATORY:** When adding features or changing behavior, update ALL related documentation BEFORE committing the code change.

Documents to update:
- `TODO.md` (main project status)
- `sdcc/SDCC_TODO.md` (SDCC-specific progress)
- `calm/README.md` (CALM workflow)
- Example READMEs in `calm/examples/` or `sdcc/examples/`
- Feature-specific guides in `docs/`

### 3. Use Repo-Local tmp/ Directory
Store temporary logs, test outputs, and intermediate files in `tmp/` (at project root), NOT in system `/tmp`.

```bash
# ✅ DO THIS
./smemu6 -scrdump -no-display-off > tmp/test-output.log

# ❌ DON'T DO THIS
./smemu6 -scrdump -no-display-off > /tmp/test-output.log
```

---

## Emulator Usage Patterns

### Screen Capture for Testing
To capture program output for analysis:

```bash
./smemu6 floppies/Sys2-2.dsk -scrdump -no-display-off > tmp/output.log 2>&1
```

**Both flags are required:**
- `-scrdump`: Enable screen dump on exit
- `-no-display-off`: Prevent display cutoff (keeps alpha screen active)

Without both, the screen output may be incomplete or cut off.

### Command Injection
For automated testing, inject commands via `-inject-str`:

```bash
./smemu6 floppies/Sys2-2.dsk -inject-str "HELLO" -scrdump -no-display-off
```

**Important:** `-inject-str` fires only when `machine_cli_prompt_visible()` returns true (CLI is ready). Do NOT assume immediate injection.

### Boot Behavior
- Machine boots from **DX0 automatically** (no `-autoboot` flag needed)
- Default floppy: `floppies/Sys2-2.dsk`
- To use different floppy: `./smemu6 floppies/your-disk.dsk`

---

## SDCC Compilation Workflow

### Current Status
**⚠️ Known Limitation:** SDCC-compiled programs execute correctly but **DO NOT exit cleanly to CLI**.

- Programs produce correct output
- Screen corruption occurs on exit (rows 15–19 garbled, row 19 underscores)
- CLI prompt never reappears
- Issue is pre-existing, documented as architectural incompatibility
- User must manually terminate emulator

### Build Process
```bash
sdcc/build_smaky6_sdcc_example.sh example_name
sdcc/run_smaky6_sdcc_example.sh example_name
```

### Key Points
- Keep examples standalone (minimal CMake coupling)
- Use symbols from `SM6.ST` / `FLO.ST` (generated headers)
- SP=0xF000 **critical** before calling main
- No full libc startup; minimal crt0 only
- Exit sequence (A=0x44, HL=BUFLIN, JP 0x56AE) still produces corruption

### Examples
- `sdcc/examples/hello_alpha/` — Minimal C program (BASELINE)
- `sdcc/examples/sm6peek/` — Reads workspace variables
- `sdcc/examples/sm6emitz/` — System call testing

---

## CALM Assembly Workflow

### Proper CALM Syntax (CORRECT)
Use system routines, not hardcoded addresses:

```calm
.TITLE MYPROGRAM
.REF SM6                    ; Reference system symbols

    .LOC 43000              ; Optional (SMILE chooses default if omitted)
START:
    .W ?TEXTIM              ; Call text output routine
    .ASCIZ /Hello World/    ; String to output
    .W ?RTN                 ; Return to CLI cleanly
.END START
```

**Key points:**
- `.REF SM6` **required** to access system routine symbols
- `.LOC` **optional** — SMILE provides sensible default if omitted
- System routines have `?`-prefixed names: ?TEXTIM, ?RTN, etc.
- Let SMILE handle memory management and linking
- Never hardcode memory addresses like 0x6000 or 0x45C0

### Improper Z80 Approach (AVOID)
Do NOT write raw Z80 assembly with hardcoded addresses:

```asm
ORG 0x6000                  ; ❌ WRONG
LD A, 0x44                  ; ❌ Hardcoded magic values
LD HL, 0x45C0               ; ❌ Hardcoded memory addresses
JP 0x56AE                   ; ❌ Hardcoded entry point
```

This pattern is:
- Not portable (hardcoded addresses)
- Not maintainable (magic numbers)
- Not proper CALM (doesn't use system conventions)

### SMILE Assembler
- **Interactive tool** that runs inside the emulator
- No batch/headless mode available
- Must be run manually inside emulator
- Reads `.SR` files from floppy
- Outputs `.SM` executables

### Testing CALM Programs

**Use hello_smug as baseline:**
```bash
calm/build_smaky6_calm_example.sh hello_smug
# Boot emulator with hello_smug.SR on Sys2-2 floppy
# > SMILE (inside emulator)
# [Assemble hello.SR]
# > HELLO (run it)
```

**Expected success:**
```
Hello World
> 
```
Program exits cleanly to CLI prompt with no corruption.

### CALM Examples
- `calm/examples/hello_smug/` — **RECOMMENDED baseline** (uses proper syntax, system routines)
- `calm/examples/hello_calm/` — Learning example only (hardcoded addresses, not recommended)

---

## System Routines & Memory

### Critical Memory Layout
```
0x0000–0x07FF   Phantom ROM / SYSMON (2 KB)
0x0800–0x22FF   SAMOS kernel (from SYS.SY)
0x4000–0x44FF   Alpha screen (20 rows × 64 cols = 1280 B)
0x4500–0x45FF   OS workspace (256 B)
  0x45C0        BUFLIN (CLI line buffer) — used for exit sequence
0x4600–0x54FF   Graphics buffer (3840 B)
0x6000–0xEFFF   User program area (default for .SM programs)
0xF000–0xF300   User stack (recommended: SP=0xF000)
```

### Exit Sequence for Native Programs
```asm
.W ?RTN                     ; Proper way: system routine
```

Or low-level (if ?RTN unavailable):
```asm
LD A, 0x44                  ; Magic value
LD HL, 0x45C0               ; BUFLIN address
JP 0x56AE                   ; CLI reprompt sink
```

### System Routines (from .REF SM6)
- `?TEXTIM` — Output text (string pointer in HL)
- `?RTN` — Return cleanly to CLI
- `?IALPHA` — Set alpha-only display mode
- `?IGRA` — Set graphics-only mode
- `?IAGRA` — Set superimposed mode

---

## Documentation Standards

### README Files
Create comprehensive READMEs for all examples:
- Purpose and goals
- Technical details (memory layout, entry points)
- Step-by-step testing procedure
- Expected results (success AND failure cases)
- Debugging tips
- Comparison with related examples

### TODO/Progress Files
Keep these up to date:
- `TODO.md` — Main project status
- `sdcc/SDCC_TODO.md` — SDCC-specific (refactored, concise)
- `calm/README.md` — CALM development guide

### Code Comments
- Document Z80 assembly with what registers do
- Explain memory addresses used
- Note system routine contracts
- Cross-reference with hardware docs

---

## Investigation & Testing

### Known Issues to Avoid
1. **SDCC exit corruption** — Documented, not fixed. Programs must be manually terminated.
2. **SMILE is interactive only** — Cannot automate assembly (no batch mode).
3. **Floppy DSK manipulation** — `extract_samos_image.py` is read-only. Need external tool for adding files.

### Testing Methodology
1. Write/compile program
2. Stage on floppy (manual for CALM)
3. Boot emulator with `-scrdump -no-display-off`
4. Capture output to `tmp/`
5. Analyze results
6. Document findings in example README

### Screen Capture Analysis
```bash
tail -50 tmp/output.log     # See final screen state
grep -i "corruption\|error\|prompt" tmp/output.log  # Search for issues
```

---

## Git Workflow

### Commit Messages
Format: `type: concise description`

Types:
- `feat:` New feature or example
- `fix:` Bug fix
- `docs:` Documentation update
- `refactor:` Code reorganization
- `test:` Testing infrastructure

Examples:
```
feat: add hello_smug CALM example
docs: clarify .LOC is optional in CALM
fix: SDCC stack initialization issue
```

### Never Accumulate Changes
```bash
git status                  # Check current state
git add -A                  # Stage all changes
git commit -m "..."         # Commit immediately
```

---

## Project Context

### Smaky 6 Hardware
- **CPU:** Z80 at 2.5 MHz
- **RAM:** 64 KB (Phantom ROM model)
- **ROM:** 2 KB (SYS17) that bank-switches out after boot
- **Display:** 20×64 alphanumeric + 256×120 graphics (1 bpp)
- **Boot:** From floppy (DX0) or hard drive (Winchester)
- **I/O:** Floppy, Winchester, serial, parallel, keyboard, sound

### Key Architectural Points
- SAMOS OS provides system calls via RST vectors
- Phantom ROM loads SYS.SY (SYSMON + SAMOS) from disk into RAM
- After bank-switch, all code/data runs from RAM
- Display refreshed at 50 Hz via interrupt

### Build Chain
- **SDCC:** C → Z80 assembly → binary (.SM)
- **CALM/SMILE:** Assembly (.SR) → binary (.SM) (inside emulator)
- Both produce `.SM` executables with metadata (load, entry, flags)

---

## When Starting Work

1. **Read this file** to understand conventions
2. **Check relevant README** in feature directory
3. **Review git log** for recent changes
4. **Check TODO.md** for current status
5. **Follow workflow:** code → test → document → commit
6. **Use tmp/ directory** for all temporary files

---

## Quick Reference

| Need | Command/Path |
|------|--------|
| Build SDCC example | `sdcc/build_smaky6_sdcc_example.sh example_name` |
| Test SDCC program | `sdcc/run_smaky6_sdcc_example.sh example_name` |
| Prepare CALM example | `calm/build_smaky6_calm_example.sh example_name` |
| Boot with screen capture | `./smemu6 floppies/Sys2-2.dsk -scrdump -no-display-off > tmp/out.log` |
| Inject command | `./smemu6 floppies/Sys2-2.dsk -inject-str "CMD" -scrdump -no-display-off` |
| View output | `tail -50 tmp/out.log` |
| Check status | `git status && git log --oneline -5` |
| Commit changes | `git add -A && git commit -m "type: description"` |
| CALM baseline | `calm/examples/hello_smug/` |
| SDCC baseline | `sdcc/examples/hello_alpha/` |
| Project plan | `docs/dev/PLAN.md` |
| Main TODO | `TODO.md` |

---

## Questions?

Refer to:
- `PLAN.md` — Hardware architecture details
- `docs/dev/` — Technical deep-dives
- Example READMEs — Specific workflow for that example
- Git history — See how similar work was done before
