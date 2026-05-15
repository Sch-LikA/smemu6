# Smaky 6 SDCC Bring-up TODO

This file is a planning document for getting SDCC-built Z80 programs to run on a
Smaky 6 under SAMOS.

Scope for now: analysis and a concrete TODO list only. No SDCC integration has
been added to the repo yet.

## Current Status

This file is intentionally a forward-looking plan, not an implementation log.
The phases below are open work unless a later repository change explicitly says
otherwise.

- No SDCC toolchain integration, sample program, or build target exists yet in
  this repo.
- The plan is grounded in emulator behavior that is already verified elsewhere in
  the dev docs: the machine now boots fully to the CLI, supports prompt-time
  command injection, and can run repeatable disk-based workflows.
- The main unknown is still the real `.SM` loader contract under SAMOS. The rest
  of the plan should be treated as dependent on that ABI recovery work, but some
  supporting pieces are no longer unknown: the syscall trampoline, CLI load base,
  and virtual-floppy metadata path are now documented.
- References to `FLO.ST` and `SM6.ST` are still leads rather than confirmed ABI
  definitions, but preserved copies from the reference `Sys2-2` boot media have
  now been inspected directly.

## What the repo already establishes

- CPU target is Z80 at 2.5 MHz.
- The machine is the 64 KB Phantom model.
- Runtime RAM layout is partially known, with more precision than this plan had
  originally:
  - `0x0000-0x07FF`: Phantom ROM at reset, later replaced by SYSMON copied from
    `SYS.SY`
  - `0x0800-0x22FF`: SAMOS OS content from `SYS.SY`
  - `0x4000-0x44FF`: alpha screen buffer
  - `0x4500-0x45FF`: core workspace area used by SYSMON and SAMOS dispatch
  - `0x4600-0x54FF`: graphics buffer
  - `0x5600`: verified `CLI.SY` load base
  - `0x7800-0xFFFF`: upper RAM still available for application study, subject to
    loader findings
- The CLI runs programs from `.SM` files by typing the basename without the
  extension.
- `LOAD` is a built-in CLI command, but its exact handler and no-argument
  semantics still need to be tied to the currently analyzed artifact with more
  care.
- That does **not** prove that the explicit `LOAD` command handler is the
  same code path used for the initial bare-basename launch of an `.SM` file.
- Manual note still relevant to loader recovery: separate documentation for
  `LOAD NOM.EX` from peripheral `$PR` into directory `REP:` states that `.SM`
  and `.SY` are recognized as **program loads** and expect PDP-11 binary input,
  while other extensions are treated as ordinary files and loaded byte-for-byte.
  This strongly suggests that SAMOS already distinguishes a higher-level
  "program vs file" class during `LOAD`, not just raw two-letter disk types.
- Existing documentation now identifies the core runtime entry and dispatch
  surfaces with better precision:
  - `RST 08`: indirect dispatch through `(0x4562)` in SYSMON after boot
  - `RST 10`: indirect dispatch through `(0x4564)`
  - `RST 20`: syscall trampoline through `(0x455C)`, initially targeting
    dispatcher `0x012D`, with an inline syscall byte after the `E7` opcode
  - `RST 30`: indirect dispatch through `(0x456A)`
  - `RST 38`: indirect dispatch through `(0x4566)`
- The documented default syscall table already gives some concrete SDCC-relevant
  surface area:
  - syscall `0x11`: `MODE A`
  - syscall `0x12`: `MODE G`
  - syscall `0x13`: `MODE 2`
- Existing documentation also shows that sample `.SM` programs are not all loaded
  at one universal address:
  - `SM6WIN1` programs appear to load at `0x6000`
  - `SM6WIN0` programs span a wider address range
- Separate monitor notes also identify `0x5600` and `0x4100` as common working
  start addresses in monitor-driven workflows, with `0x4100` being explicitly
  screen-visible.
- The official user manual identifies two symbol-table files commonly present on
  system media:
  - `FLO.ST`: used by programs with `.REF FLO`
  - `SM6.ST`: short symbol table used by programs with `.REF SM6`
- Later boot verification confirms both files are visible on a clean CLI listing
  from the reference system media, so they are not hypothetical artifacts.
- Those `.ST` files may be a valuable bridge between archived system media and a
  future SDCC toolchain, because they could provide recoverable names for OS,
  floppy, and runtime symbols that would otherwise need to be rediscovered from
  disassembly alone.
- The repo already has emulator-side automation that can support a future SDCC
  workflow:
  - boot with a chosen disk image
  - inject commands with `-inject-str`
  - mount custom disk images
- The repo also now has a repo-local metadata-preserving disk workflow that is
  directly relevant to SDCC packaging experiments:
  - `tools/extract_samos_image.py` exports files with `.meta.json` sidecars
  - sidecars already preserve `type`, `flags`, `load`, `entry`, `date_month`,
    `date_year`, and `start_sector`
  - `src/virtual_floppy.c` consumes those sidecars when rebuilding a virtual
    floppy image

## Current artifact evidence from the reference boot media

- Preserved extracted reference files already exist under
  `private/floppies/extracted/Sys2-2-boot`.
- `FLO.ST` and `SM6.ST` are not plain-text symbol lists:
  - `FLO.ST` size: `2744` bytes
  - `SM6.ST` size: `2008` bytes
  - both sizes are exact multiples of 8, which strongly suggests fixed-width
    binary records
  - the first records contain 2 leading bytes followed by 6 bytes that look like
    high-bit-marked uppercase text, but the exact encoding and field order are
    still unresolved
- Ordinary `.SM` programs already have preserved directory metadata in the
  extracted sidecars, so `load` and `entry` are definitely present at least in
  the directory/image layer even before the file-body format is understood.
- Real `.SM` metadata varies substantially across the same reference boot image,
  so there is no evidence for a single universal executable load base:
  - `SHOW.SM`: size `1305`, load `0x5600`, entry `0x5602`
  - `TDISK.SM`: size `1343`, load `0x5600`, entry `0x5611`
  - `CCOPY.SM`: size `5147`, load `0x5500`, entry `0x5503`
  - `SMILE.SM`: size `13006`, load `0x5500`, entry `0x5508`
  - `FPRINT80.SM`: size `896`, load `0x2CF6`, entry `0x2C28`
- The preserved ordinary `.SM` `entry` values cannot yet be treated as confirmed
  literal code-entry addresses:
  - `SHOW.SM` records entry `0x5602`, which lands in the middle of the file's
    opening instruction stream rather than at a clean obvious entry label
  - `TDISK.SM` records entry `0x5611`, which lands inside printable banner text
    (`"EST PROG"`) in the extracted file body
  - some samples even have `entry < load`, for example `BASIC.SM`, `FPRINT80.SM`,
    and `TSTFLO.SM`
- Current conclusion: ordinary `.SM` directory `load` and `entry` fields are
  certainly important, but their exact semantics are still unresolved. They may
  be true runtime addresses interpreted together with loader logic, they may use
  a different origin convention, or some program classes may encode them
  differently.
- The first direct look at the `Sys2-2` `CLI.SY` dispatcher falsifies one earlier
  assumption: in this preserved boot-media artifact, command-table entry `LOAD`
  points to handler `0x6B0D`, not `0x6A70`.
- The surrounding `0x69FE–0x6A7E` block is therefore a better candidate for a
  generic token-to-file dispatch or fallback path than for the explicit `LOAD`
  command itself.
- Current scope note: this is confirmed analysis of the preserved `Sys2-2`
  artifact only. It does not yet prove the exact role of `0x69FE–0x6A7E`, but it
  does rule out the earlier identification of `0x6A70` as the `LOAD` entry point
  in this particular image.
- The manual distinction above also makes the current `0x5FEF` compare against
  `0x09` easier to interpret: whatever that value means, it may well be an
  internal **program-load class** or loader-mode result rather than a direct
  encoding of the on-disk `SM` letters.
- A more detailed first-pass trace of the nearby `0x69FE–0x6A7E` block gives a
  useful control-flow outline for what may be the generic fallback file-dispatch
  path:
  - the block scans a token byte-by-byte and copies characters through helper
    `0x5F56`
  - it then calls `0x5E98`, `0x5FD1`, and `0x607C`, suggesting staged lookup and
    workspace preparation rather than a raw direct jump
  - helper `0x5FE6` starts with `RST 10 / 0x00` and soon compares `A` with
    `0x09` at `0x5FEF`; this still looks like a loader-class or file-kind check,
    but it is **not** a raw on-disk two-letter type comparison. The disk format
    used by both `tools/extract_samos_image.py` and `src/virtual_floppy.c`
    stores file type as two ASCII bytes such as `SM`, `SY`, `BS`, and `DR` in
    directory-entry bytes `8:10`.
  - helper `0x607C` populates workspace around `0x710B`, `0x710E`, `0x7110`, and
    `0x7113`
  - the block then uses helpers `0x6036`, `0x603E`, and `0x6050` with the staged
    workspace before reaching the `0x6A71` onward sequence
  - the actual handoff therefore seems to occur only after several metadata and
    workspace preparation stages, not immediately after a command-table miss
- This directly matters for SDCC planning: small existing programs in the boot
  image often live in the `0x5500–0x56FF` region, which overlaps the known
  resident CLI area. Those examples are useful for reverse-engineering the loader
  contract, but they are not automatically safe templates for a standalone C
  target layout.

## Main unknowns blocking an SDCC target

- Exact `.SM` file format:
  - whether ordinary `.SM` files carry load and entry metadata internally
  - or whether those values live only in the directory entry or image metadata
  - how this differs from the already-understood special handling for `SY` files
  - whether the file body contains its own header, relocation data, or checksum
- Exact meaning of ordinary `.SM` directory metadata:
  - how `load` should be interpreted by the CLI loader
  - how `entry` should be interpreted when the preserved values do not line up
    with obvious file-body code starts in several sampled programs
  - whether the meaning depends on program class, flags, or an external loader
    transform
- Exact loader contract for CLI-launched `.SM` programs:
  - entry address
  - initial register values
  - initial stack pointer
  - return-to-CLI convention
  - which helper routines in the relevant launch path interpret directory `load`
    / `entry` metadata before control reaches the program body
  - whether the `0x5FEF` compare against `0x09` is an internal loader-class value,
    a filesystem helper return code, or some other derived classification rather
    than a raw directory type byte
  - whether first launch by bare basename shares the same helper chain as the
    explicit `LOAD` command or only reuses parts of its saved state
  - what the no-argument `LOAD` command actually does in current SAMOS builds,
    independent of any archived user-guide wording
  - how the documented `LOAD NOM.EX` from `$PR` path relates to the no-argument
    `LOAD` reload path and to ordinary bare-basename execution from disk
  - whether the `0x69FE–0x6A7E` block is in fact the command-table miss path that
    turns a bare token into file execution
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
  - which metadata fields must be carried in the `.meta.json` or directory record
    for ordinary executable files

## Phase 1 - Nail down the program loading contract

- [ ] Extract and inspect `FLO.ST` and `SM6.ST` from the reference system media.
  Use the repo-local extraction workflow first so preserved metadata stays next
  to the recovered files.
- [ ] Determine the on-disk format and semantic content of those `.ST` files:
  - whether they are plain symbol dictionaries
  - how names are encoded
  - whether they include addresses, areas, or only short aliases
  Current evidence: the preserved `Sys2-2-boot` copies are binary, fixed-width,
  and not directly human-readable text dumps.
- [ ] Check whether `.REF FLO` and `.REF SM6` map to reusable symbol namespaces
  that could seed SDCC headers, linker symbols, or ABI notes.
- [ ] Reverse-engineer one or more existing `.SM` files from the boot disks and
  document their on-disk structure.
- [ ] Start that audit with a small real sample such as `SHOW.SM` or `TDISK.SM`,
  while keeping in mind that their directory metadata places them in the
  `0x5600` area already occupied by the resident CLI image.
- [ ] Confirm whether `.SM` load address and entry point come from the file body,
  the SAMOS directory entry, or both.
- [ ] Cross-check that result against the current virtual-floppy metadata model,
  which already preserves `load`, `entry`, `flags`, and `start_sector` for host
  files.
- [ ] Explain the observed mismatch between preserved ordinary `.SM` `entry`
  values and obvious file-body code starts in current samples such as `SHOW.SM`,
  `TDISK.SM`, and `BASIC.SM`.
- [ ] Trace the CLI code path that launches an `.SM` file and document:
  - the loader routine address
  - the in-memory destination address
  - the jump/call used to start the program
  - what happens when the program returns
- [ ] Distinguish the two candidate launch surfaces explicitly:
  - initial execution by typing a program basename
  - re-execution via the `LOAD` command
- [ ] Decode the `CLI.SY` `LOAD` handler at `0x6A70` far enough to identify which
  helper routine actually consumes ordinary `.SM` directory metadata and where
  executable control is finally transferred.
- [ ] Name the roles of the currently identified first-pass helpers with evidence:
  - `0x5E69`: path or drive-qualified name handling
  - `0x5DC4` and `0x5DCE`: filename token scan or normalization
  - `0x5FE6`: directory lookup plus probable type validation
  - `0x607C`: workspace staging for the actual load
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
  - with explicit awareness that `CLI.SY` itself occupies `0x5600+` during the
    ready-state system
- [ ] Treat the current `.SM` samples as loader evidence, not as proof that
  `0x5500` or `0x5600` is safe for a fresh SDCC program under the normal CLI.
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
  - active SYSMON or SAMOS low-RAM regions
  - active `CLI.SY` region at `0x5600+`
- [ ] Decide whether the first SDCC target should avoid `malloc` entirely.

Deliverable:

- one documented memory model suitable for a first SDCC crt0.

## Phase 3 - Recover the minimum application ABI from SAMOS

- [ ] Cross-check any recovered `FLO.ST` / `SM6.ST` symbols against the current
  disassembly notes before naming SDCC-facing entry points.
- [ ] Identify the cleanest program-exit path back to SAMOS/CLI.
- [ ] Identify the minimum text I/O surface needed for C development:
  - put one character
  - print a zero-terminated string
  - read one key
- [ ] Determine whether those services are best reached through:
  - `RST 30`
  - `RST 20` subfunctions, now that the inline-byte dispatcher mechanism is known
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
- [ ] Reuse the repo-local image and virtual-floppy tooling path where possible.
- [ ] Prefer the in-repo metadata workflow before adding any new external helper:
  - `tools/extract_samos_image.py` for known-good reference exports
  - `.meta.json` sidecars for `type`, `load`, `entry`, `flags`, dates, and
    placement hints
  - `src/virtual_floppy.c` as the first target for packaging experiments
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
- A closely related risk is assuming the already-known `SY` metadata behavior
  applies unchanged to ordinary `.SM` files. Current docs justify treating that
  as a hypothesis, not a fact.
- The second risk is assuming a generic Z80 memory model will work without
  colliding with live SAMOS data structures.
- The third risk is overcommitting to libc too early. The first milestone should
  stay extremely small and avoid standard-library expectations.

## Definition of done for the first usable milestone

- An SDCC-built C file can be compiled into a `.SM` program.
- The program can be placed on a Smaky disk image reproducibly.
- The emulator can boot that disk and run the program from the CLI by name.
- The program can write visible output and return cleanly to the CLI.
