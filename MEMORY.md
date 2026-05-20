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
**Screen Capture:** Both flags required together:
- `-scrdump`: Enable screen dump on exit
- `-no-display-off`: Prevent display cutoff (keeps alpha screen active)
Without both, screen output may be incomplete or cut off.

**Command Injection:** `-inject-str` fires only when `machine_cli_prompt_visible()` returns true (CLI is ready).

**Boot Behavior:** DX0 boots automatically (no `-autoboot` flag needed in current codebase).

**Beeper:** Always use `-no-beeper` argument when running the emulator to suppress sound output.

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


**Decision:** [Brief title]

**Decided:** [What was chosen and implemented]

**Why:** [Reasoning, context, constraints that led to this choice]

**Rejected:** [Alternative approaches considered]
- [Option 1]: Why not chosen
- [Option 2]: Why not chosen

---
