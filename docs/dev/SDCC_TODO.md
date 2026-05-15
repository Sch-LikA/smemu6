# Smaky 6 SDCC Bring-up TODO

This file is a planning document for getting SDCC-built Z80 programs to run on a
Smaky 6 under SAMOS.

Scope for now: analysis and a concrete TODO list only. No SDCC integration has
been added to the repo yet.

## What the repo already establishes

- CPU target is Z80 at 2.5 MHz.
- The machine is the 64 KB Phantom model.
- Runtime RAM layout is partially known:
  - `0x0000-0x07FF`: Phantom ROM at reset, later replaced by SYSMON from `SYS.SY`
  - `0x0800-0x3FFF`: lower RAM / SAMOS area
  - `0x4000-0x44FF`: alpha screen buffer
  - `0x4500-0x45FF`: SAMOS workspace
  - `0x4600-0x54FF`: graphics buffer
  - `0x5500-0x77FF`: `SYS.SY` loaded by the boot process
  - `0x7800-0xFFFF`: upper RAM
- The CLI runs programs from `.SM` files by typing the basename without the
  extension.
- `LOAD` re-runs the last executed program, which implies SAMOS keeps loader state
  for the last `.SM` launch.
- Existing documentation already identifies some relevant system entry points:
  - `RST 08`: indirect dispatch through `0x450F`
  - `RST 10`: low-level disk hardware access
  - `RST 20`: SAMOS disk/software API dispatch
  - `RST 30`: main OS entry point after boot
  - `RST 38`: interrupt/beeper path
- Existing documentation also shows that sample `.SM` programs are not all loaded
  at one universal address:
  - `SM6WIN1` programs appear to load at `0x6000`
  - `SM6WIN0` programs span a wider address range
- The repo already has emulator-side automation that can support a future SDCC
  workflow:
  - boot with a chosen disk image
  - inject commands with `-inject-str`
  - mount custom disk images

## Main unknowns blocking an SDCC target

- Exact `.SM` file format:
  - whether the file itself contains load and entry metadata
  - or whether those values live only in the directory entry
- Exact loader contract for CLI-launched `.SM` programs:
  - entry address
  - initial register values
  - initial stack pointer
  - return-to-CLI convention
- Safe memory model for C programs:
  - code segment placement
  - writable data / BSS placement
  - heap policy, if any
  - stack growth limits relative to SAMOS and video RAM
- Stable callable SAMOS ABI for application code:
  - console output
  - keyboard input
  - file open/load/save operations
  - screen and graphics helpers
  - exit back to CLI
- Character-set expectations:
  - whether C strings should be plain ASCII only
  - or whether a Smaky-specific character translation layer is needed for accented
    glyphs and control codes
- Packaging workflow:
  - how to turn an SDCC output into a valid `.SM`
  - how to add that `.SM` to a Smaky disk image in a repeatable way

## Phase 1 - Nail down the program loading contract

- [ ] Reverse-engineer one or more existing `.SM` files from the boot disks and
  document their on-disk structure.
- [ ] Confirm whether `.SM` load address and entry point come from the file body,
  the SAMOS directory entry, or both.
- [ ] Trace the CLI code path that launches an `.SM` file and document:
  - the loader routine address
  - the in-memory destination address
  - the jump/call used to start the program
  - what happens when the program returns
- [ ] Identify at least one trivial existing `.SM` program that is safe to study as
  a baseline executable.
- [ ] Write down a minimal, reproducible "known-good `.SM`" description.

Deliverable:

- a short loader ABI note with one verified sample executable format.

## Phase 2 - Define a safe C runtime memory map

- [ ] Choose the first target layout for SDCC programs.
- [ ] Decide whether the first target should be:
  - a fixed-load application at `0x6000`
  - or a different verified address range once the loader audit is complete
- [ ] Establish a conservative first memory map for:
  - code
  - initialized data
  - BSS
  - stack
  - optional heap
- [ ] Verify that the chosen layout does not collide with:
  - SAMOS workspace at `0x4500-0x45FF`
  - alpha RAM at `0x4000-0x44FF`
  - graphics RAM at `0x4600-0x54FF`
  - active `SYS.SY` / CLI regions
- [ ] Decide whether the first SDCC target should avoid `malloc` entirely.

Deliverable:

- one documented memory model suitable for a first SDCC crt0.

## Phase 3 - Recover the minimum application ABI from SAMOS

- [ ] Identify the cleanest program-exit path back to SAMOS/CLI.
- [ ] Identify the minimum text I/O surface needed for C development:
  - put one character
  - print a zero-terminated string
  - read one key
- [ ] Determine whether those services are best reached through:
  - `RST 30`
  - `RST 20` subfunctions
  - direct jumps into `SYS.SY` / `CLI.SY`
  - or direct screen-memory writes for the first milestone
- [ ] Document calling convention details for each chosen service:
  - argument registers
  - return registers
  - clobbered registers
  - blocking vs non-blocking behavior
- [ ] Decide whether the first milestone should bypass SAMOS console output and
  write directly to `0x4000` screen RAM instead.

Deliverable:

- a minimal application ABI table that C wrappers can target.

## Phase 4 - Define the first SDCC target shape

- [ ] Choose the exact SDCC command-line model for Smaky 6 builds.
- [ ] Decide whether to start with plain `sdcc` plus custom linker flags, or add a
  small wrapper script / Makefile first.
- [ ] Design a `crt0` for Smaky 6 that does only the minimum:
  - set stack pointer if needed
  - initialize data/BSS as required by SDCC
  - call `_main`
  - return cleanly to SAMOS or trap in a defined way
- [ ] Decide how to express the link layout:
  - SDCC linker flags only
  - generated linker script / area layout
  - custom post-link relocation step if the loader requires it
- [ ] Confirm the binary output form needed before `.SM` packaging:
  - raw binary
  - Intel HEX converted to binary
  - another intermediate format

Deliverable:

- one documented compiler/linker recipe for producing a first runnable binary.

## Phase 5 - Build the smallest possible milestone program

- [ ] Target a first milestone that avoids file I/O and complex libc needs.
- [ ] Prefer this sequence:
  - direct screen RAM write test
  - keyboard wait / simple loop
  - clean exit to CLI
- [ ] Define a canonical first test program, for example:
  - write `HELLO` into the alpha screen buffer
  - wait for a key
  - return to the CLI
- [ ] Use that milestone to validate:
  - load address correctness
  - code generation correctness
  - stack correctness
  - return path correctness

Deliverable:

- one tiny SDCC-built test binary with no dependencies beyond the chosen crt0.

## Phase 6 - Package binaries as real Smaky programs

- [ ] Document how to turn the linked output into a valid `.SM` artifact.
- [ ] Reuse the existing Smaky disk-image tooling path where possible.
- [ ] Determine whether the external `smaky6_samos.py` workflow should be:
  - imported into this repo
  - invoked from a sibling checkout
  - or replaced by repo-local packaging helpers
- [ ] Automate these steps:
  - build C source with SDCC
  - convert to the correct binary form
  - wrap or label it as `.SM`
  - inject it into a test disk image
- [ ] Preserve one clean reference disk image and generate writable copies for SDCC
  test runs.

Deliverable:

- a repeatable "build program -> place on disk -> boot emulator -> run program"
  workflow.

## Phase 7 - Create a developer test loop inside this repo

- [ ] Add a dedicated example program directory for Smaky 6 C experiments.
- [ ] Add a build target or script for SDCC examples.
- [ ] Add an emulator smoke-test command that:
  - boots a prepared disk image
  - injects the program name at the CLI with `-inject-str`
  - captures enough output to verify success
- [ ] Define what a pass/fail result looks like for the first example.
- [ ] Keep the workflow independent from the web build.

Deliverable:

- one command that rebuilds and smoke-tests a sample SDCC program locally.

## Phase 8 - Grow from "hello world" to a usable support library

- [ ] Add thin wrappers for the minimum useful runtime surface:
  - console output
  - keyboard input
  - beeper
  - optional function-key polling
- [ ] Decide what not to support initially:
  - dynamic allocation
  - full stdio
  - floating point
  - file system writes
- [ ] Add direct-memory helpers for screen output if SAMOS console calls remain too
  unclear.
- [ ] Define a Smaky-specific character/output policy so C code does not silently
  misuse control bytes that SAMOS interprets specially.

Deliverable:

- a tiny `libsmaky6`-style support layer suitable for simple games and demos.

## Phase 9 - Only then consider nicer integration

- [ ] Add CMake support for SDCC only after the manual workflow is proven.
- [ ] Consider adding a dedicated C example disk image.
- [ ] Consider adding emulator-based regression tests for SDCC examples.
- [ ] Consider documenting a stable public ABI for homebrew authors once the loader,
  exit path, and console API are verified.

## Suggested order of attack

1. Loader format and CLI launch path
2. Safe fixed load address for one test program
3. Minimal crt0 and exit path
4. Direct screen-memory hello-world
5. `.SM` packaging automation
6. SAMOS wrappers for text I/O
7. Repo-integrated developer workflow

## Risks to resolve early

- The biggest technical risk is not SDCC itself; it is the undocumented Smaky
  program loader contract.
- The second risk is assuming a generic Z80 memory model will work without
  colliding with live SAMOS data structures.
- The third risk is overcommitting to libc too early. The first milestone should
  stay extremely small and avoid standard-library expectations.

## Definition of done for the first usable milestone

- An SDCC-built C file can be compiled into a `.SM` program.
- The program can be placed on a Smaky disk image reproducibly.
- The emulator can boot that disk and run the program from the CLI by name.
- The program can write visible output and return cleanly to the CLI.
