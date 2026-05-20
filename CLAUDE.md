# CLAUDE.md — Smemu6 Project Guidelines

**Project:** Smemu6 — Z80 emulator of Smaky 6 (1978 Swiss computer)
**Repository:** https://github.com/Sch-LikA/smemu6
**Focus:** Cycle-accurate emulation, SAMOS OS, SDCC C + CALM assembly workflows

## Who You Are (Marcel)
**Role:** Senior Security Expert, Program Manager
**Expertise:** System admin, shell scripting, web dev, security, infrastructure, hardware, networking
**Learning:** Low-level coding, C, assembler
**Response Adjustment:** Never over-explain known areas. Never skip needed context for learning. Assume C/assembler reading competency; build foundational understanding.

## Communication Style
- **No preambles.** Start with the answer. Skip "Great question!" and warmups.
- **Match response length.** Simple questions = short. Complex = detailed. No padding or restatement.
- **Show approach options.** For significant tasks: present 2-3 approaches, wait for selection.
- **Be explicit about uncertainty.** If unsure: say so. Never fill gaps with plausible guesses.

## Critical Workflow

### 0. Strict Scope Discipline
Only modify what's directly related to current task. No refactoring, renaming, reorganizing, or reformatting unless explicitly requested. Note other issues at end but do not touch them.

Before making any change that significantly alters content you've already created (rewriting sections, removing paragraphs, restructuring flow, changing tone): stop. Describe exactly what you're about to change and why. Wait for confirmation before proceeding.

Before deleting any file, overwriting existing code, dropping database records, or removing dependencies: stop. List exactly what will be affected. Ask for explicit confirmation. Only proceed after you say yes in the current message. "You mentioned this earlier" is not confirmation.

The following require explicit in-session confirmation, no exceptions:
- Deploying or pushing to any environment
- Running migrations or schema changes
- Sending any external API call
- Sending confidential information (passwords, tokens, keys, secrets)
- Executing any command with irreversible side effects
- Sending, posting, publishing, or scheduling anything on your behalf (emails, calendar invites, document shares, or any action outside this conversation)

I must see you say yes in the current message before proceeding.

### 1. Commit After Every Code Modification
Each source code change commits immediately: `git commit -m "type: description"`. Never accumulate uncommitted changes.

### 2. Update Documentation First
Before committing code: update TODO.md, sdcc/SDCC_TODO.md, calm/README.md, feature docs, example READMEs.

### 3. Use repo-local tmp/
All temporary files, logs, test output → `tmp/` at project root, NOT `/tmp`.

## Problem-Solving Approach

For any task involving architecture decisions, debugging complex issues, or non-trivial features: work through the problem step by step before writing any code. Show your reasoning. Identify where you're uncertain. Then implement.

## Emulator Usage

**Screen Capture (both flags required):**
```bash
./smemu6 floppies/Sys2-2.dsk -scrdump -no-display-off > tmp/output.log
```

**Command Injection:** Only fires on `machine_cli_prompt_visible()`
```bash
./smemu6 floppies/Sys2-2.dsk -inject-str "CMD" -scrdump -no-display-off
```

**Boot:** DX0 automatic (no -autoboot flag needed)

## SDCC & CALM Workflows

**SDCC Status:** Executes correctly, exits corrupted (pre-existing issue). Manual termination required.
- Build: `sdcc/build_smaky6_sdcc_example.sh name`
- Stack critical: SP=0xF000 before main

**CALM Proper Syntax:**
```calm
.TITLE PROG
.REF SM6              ; Required for system symbols
.LOC 43000            ; Optional (SMILE chooses default)
START:
    .W ?TEXTIM        ; System routine call
    .ASCIZ /Hello/
    .W ?RTN           ; Clean CLI return
.END START
```
- `hello_smug/` is the baseline (proper approach)
- SMILE is interactive only (inside emulator, no batch mode)
- Never hardcode addresses (0x6000, 0x45C0, 0x56AE)

## Critical Memory Layout
```
0x0000–0x07FF   Phantom ROM / SYSMON
0x0800–0x22FF   SAMOS kernel
0x4000–0x44FF   Alpha screen (20×64)
0x45C0          BUFLIN (CLI buffer)
0x6000–0xEFFF   User program area
0xF000          Stack base (SP=0xF000)
```

## Git Workflow
Format: `type: description`
- `feat:` feature/example
- `fix:` bug fix
- `docs:` documentation
- `refactor:` code reorganization
- `test:` testing infrastructure

## Quick Commands
```bash
git status && git log --oneline -5           # Check status
git add -A && git commit -m "type: msg"      # Commit
sdcc/build_smaky6_sdcc_example.sh name       # Build SDCC
calm/build_smaky6_calm_example.sh name       # Build CALM
./smemu6 floppies/Sys2-2.dsk -scrdump -no-display-off > tmp/out.log  # Run
```

## System Routines (from .REF SM6)
- `?TEXTIM` — Output text
- `?RTN` — Return to CLI
- `?IALPHA`, `?IGRA`, `?IAGRA` — Display modes

## Known Issues
1. **SDCC exit corruption** — Pre-existing, documented. Manual termination.
2. **SMILE interactive only** — No batch/offline assembly available.
3. **Floppy DSK read-only** — extract_samos_image.py cannot modify.

---

**For details:** See PLAN.md (hardware), docs/dev/ (deep-dives), example READMEs, git history.
