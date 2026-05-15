# Smaky 6 Host-Directory Virtual Floppy TODO

This file is a planning document for implementing a host-directory-backed virtual
floppy workflow for Smemu6.

Scope now: the first native DX1 slice exists, and this document tracks the
remaining implementation work.

The target is deliberately split into two milestones:

- Milestone 1: a non-bootable DX1 development floppy backed by a host directory.
- Milestone 2: a bootable DX0 system floppy backed by a host directory, with
  deterministic placement for `SYS.SY`, `CLI.SY`, and any other files the boot
  path depends on.

## Sources reviewed for this plan

Smemu6 sources and docs reviewed while preparing this TODO:

- `src/floppy.c`
- `src/floppy.h`
- `src/machine_internal.h`
- `docs/EMULATOR_GUIDE_EN.md`
- `docs/dev/HARDWARE.md`
- `docs/dev/PLAN.md`
- `docs/dev/SYS_SY_analysis.md`
- `docs/dev/CLI_SY_analysis.md`
- `TODO.md`

Sibling tooling project reviewed as reference material only:

- `../smaky6-tools/README.md`
- `../smaky6-tools/smaky6_samos.py`
- `../smaky6-tools/smaky6_fuse.py`

## What the repo already establishes

### Emulator-side floppy facts

- The emulator currently models Micropolis hard-sectored floppy images as flat
  256-byte-sector dumps.
- Supported geometries are already defined in `src/floppy.h`:
  - 40 tracks x 16 sectors x 256 bytes = 163840 bytes
  - 77 tracks x 16 sectors x 256 bytes = 315392 bytes
- `src/floppy.c` currently mounts a floppy by opening a host file with `fopen()`
  and then reading sectors directly from that flat image.
- The current floppy implementation is image-centric, not filesystem-centric:
  the FDC code knows how to stream sectors, but it does not know anything about
  host directories, SAMOS file trees, or sidecar metadata.
- The selected drive is already abstracted as DX0 or DX1, so from the Z80 side a
  virtual medium only needs to preserve the same sector-visible behavior.

### Filesystem and image-format facts

- The sibling `../smaky6-tools/smaky6_samos.py` project already documents and
  implements the current Smaky disk directory model, which is useful as a
  reference for the emulator-side implementation:
  - first 3 sectors store 32 directory entries of 24 bytes each
  - entries contain name, type, start sector, end sector, flags, exact final
    sector byte count, load address, entry point, and BCD date
  - `.DR` entries are subdirectory containers whose internal sector addresses are
    relative to the container start sector
- The sibling tool already supports:
  - `create`
  - `disk list`
  - `disk info`
  - `disk extract` and `extract-all`
  - `disk add`
  - `disk delete`
  - `disk compact`
  - `disk hexdump`
- `disk add` already preserves optional `--load` and `--entry` metadata.
- `smaky6_fuse.py` already mounts a `.dsk` image as a regular host filesystem and
  supports buffered writes back into the image on close.

Important constraint for this feature:

- the virtual floppy implementation itself should be self-contained in Smemu6 and
  must not require `smaky6-tools`, FUSE, or any external helper script as part of
  the normal developer workflow.

### Boot-related facts already documented

- `docs/dev/HARDWARE.md` describes the intended Phantom ROM boot flow as:
  - choose DX0 or DX1 at boot key time
  - read floppy sectors through the FDC stream path
  - load `SYS.SY`
  - later load `CLI.SY`
- `docs/dev/SYS_SY_analysis.md` states that on the current boot disk:
  - `SYS.SY` is 8960 bytes
  - it occupies sectors 3-38
  - it starts at track 0, sector 3, immediately after the 3 directory sectors
- `docs/dev/CLI_SY_analysis.md` states that `CLI.SY`:
  - is 6687 bytes
  - loads at RAM `0x5600`
- User-facing docs already assume that a normal floppy boot uses DX0 and that DX1
  is the secondary floppy.

## Important gaps and contradictions already visible

Before implementation starts, the repo should treat the following as real open
issues rather than papering over them.

### Gap 1: the boot documentation is not fully self-consistent

- `docs/dev/HARDWARE.md` and `docs/dev/SYS_SY_analysis.md` describe a normal cold
  boot that reads `SYS.SY` from the disk.
- `docs/dev/PLAN.md` Phase 1J contains an older contradictory conclusion claiming
  that the `0x5500` stub is an infinite self-test loop and that `SYS.SY` is not
  actually loaded during cold boot.
- A bootable DX0 virtual floppy cannot be specified cleanly while these two
  narratives coexist.

Required action:

- reconcile the developer docs and decide which cold-boot description is the
  trusted one for the current emulator and for real hardware.

### Gap 2: the emulator does not yet contain its own virtual-media builder

- The repo has reverse-engineered enough of the image format to implement the
  builder internally, but that code does not exist yet in Smemu6.
- The current emulator has no in-process logic for:
  - building an image from a host directory tree
  - preserving exact requested sector placement for selected files
  - emitting or inspecting an in-memory placement manifest
  - incremental resync when a host file changes
- Existing external tools are useful as reference implementations, but they should
  not be required by the feature.

### Gap 3: the emulator only knows `FILE *image[2]`

- `src/machine_internal.h` stores floppy media as `FILE *image[2]`.
- `src/floppy.c` seeks and reads directly from those file handles.
- There is no backend abstraction yet for:
  - flat image file
  - generated in-memory image
  - host-directory-backed synthetic image
  - layered copy-on-write image

### Gap 4: exact boot-disk placement beyond `SYS.SY` is still incomplete

- The docs give a strong placement statement for `SYS.SY`.
- The docs give a RAM load base for `CLI.SY`, but do not yet pin the exact disk
  sectors used by `CLI.SY` on the reference boot floppy in one clean place.
- A bootable DX0 generator will likely also need deterministic handling for at
  least `ER.SY` and possibly other system files once the full boot/runtime load
  path is traced end-to-end.

### Gap 5: there is no metadata convention yet for host files

- The image format has fields for type, load address, entry point, flags, date,
  and exact byte count.
- A host directory mirror needs a stable way to express those values for each
  host file.
- No repo-local manifest or sidecar format exists yet.

## Recommended architecture

The implementation should still be split into two layers rather than teaching the
FDC about directory trees directly, but both layers should live inside Smemu6.

### Layer A: internal image builder / synchronizer

This layer turns a host directory plus metadata into a deterministic in-memory
flat floppy image.

Responsibilities:

- scan the host directory
- interpret sidecar metadata
- assign Smaky file type, load, entry, and dates
- build the directory sectors
- allocate file sectors
- optionally pin specific files to fixed sectors
- emit a flat in-memory image buffer
- optionally emit a placement manifest for debugging

This layer belongs in Smemu6 because the feature requirement is a self-contained,
live-generated virtual floppy rather than an external build step.

### Layer B: emulator media backend

This layer makes the FDC read from one of several possible media sources while
presenting the same sector stream to the Z80 side.

Responsibilities:

- mount a normal `.dsk` file
- mount a generated in-memory `.dsk`
- remount or refresh a virtual floppy when the source host directory changes
- later support safe runtime media replacement without redesigning the backend
- preserve existing drive-selection and sector-stream semantics

This layer also belongs in Smemu6.

### Why this split is the least risky path

- It keeps the filesystem/image-building logic separate from the FDC timing logic.
- It keeps `src/floppy.c` focused on sector streaming and controller behavior.
- It avoids making the emulator depend on a sibling checkout or a Python runtime.
- It gives a clean staged path:
  - build deterministic images first
  - then add live refresh / hot-remount behavior
  - then add bootable DX0 placement policies

### Hot-swap position in the plan

Hot-swapping should be designed in from the start at the backend/API level, but it
should not be the first user-visible milestone.

Reason:

- the first problem to solve is correctness of the generated sector image and the
  basic mount path
- runtime hot-swap adds extra state-management risk because the guest may be in
  the middle of floppy polling, motor activity, or a multi-sector command when the
  medium changes
- an explicit refresh/remount path exercises most of the same backend seams with
  less behavioral risk than fully asynchronous runtime media replacement

Practical rule for implementation order:

- build the backend so `mount`, `unmount`, and `refresh` already exist
- use that first for startup DX1 mounting and explicit refresh
- add true runtime hot-swapping only after the basic DX1 virtual floppy path is
  stable and validated

External tools remain useful as audit references and for one-time cross-checking,
but not as part of the feature architecture.

## Proposed host-directory format

The first implementation should standardize one repo-local on-disk layout for the
source directory and metadata.

Suggested structure:

```text
floppies/
  DX1/
    HELLO.SM
    HELLO.SM.meta.json
    README.HP
    DEMOS/
      INDEX.DR.meta.json
      TEST.IM
      TEST.IM.meta.json
```

Suggested rules:

- Real content files use their Smaky visible names directly when practical,
  including the two-character type suffix.
- Hidden sidecars store metadata that is not recoverable from plain host files.
- The first version should prefer a simple per-file JSON sidecar over an implicit
  filename convention.
- Metadata should be explicit rather than guessed.

Suggested sidecar fields:

- `type`
- `load`
- `entry`
- `flags`
- `date_month`
- `date_year`
- `container`
- `boot_pinned_start_sector`
- `boot_required`

Open question:

- whether a per-directory manifest is better than per-file sidecars for bootable
  DX0 images where exact placement matters.

## Milestone 1 - DX1 non-bootable development floppy

This is the first target because it gives the developer workflow value without
depending on unresolved boot-loader questions.

### Milestone 1 goals

- mount a host directory as DX1
- expose its files as a synthetic floppy image
- support normal SAMOS file access from DX1
- avoid any promise that the disk is bootable
- preserve file metadata needed for `.SM`, `.BS`, `.IM`, `.SY`, `.HP`, and `.DR`
- keep the design compatible with later DX0 boot-system mode

### Current implementation status

The repo now contains a first native-only DX1 slice:

- `-floppy2-hostdir <dir>` mounts a host directory as a writable in-memory DX1
  overlay at emulator startup
- the floppy backend now supports both file-backed and memory-backed media
- the generated image is deterministic and uses contiguous allocation
- the emulator prints a startup log line that DX1 is a writable in-memory
  overlay and still non-bootable in this first slice

Intentional limitations of the current implementation:

- top-level host files only for now
- optional per-file JSON sidecars now exist for top-level files
- no `.DR` subdirectory encoding yet
- explicit refresh only for now (`Ctrl+R` or `SIGUSR2`); no automatic file
  watching yet
- guest writes live only in the in-memory overlay; they are lost on refresh,
  remount, or emulator exit and are not written back to the host tree

### Phase 1A - Lock down the source-of-truth format

- [ ] Decide the canonical host-directory root path convention:
  - repo-local `floppies/DX1/`
  - arbitrary external path via CLI
- [x] First metadata convention for milestone 1: JSON sidecars per top-level
  file.
- [ ] Decide whether bootable DX0 media should stay with per-file sidecars or
  switch to one manifest per directory tree.
- [ ] Document how nested `.DR` directories are represented in the host tree.
- [ ] Decide whether unsupported host files are:
  - ignored
  - warned about
  - or treated as hard errors

Current sidecar keys implemented in the builder:

- `type` (validation only; must match the filename suffix)
- `flags`
- `load`
- `entry`
- `date_month`
- `date_year`

Deliverable:

- one documented host-directory schema for non-bootable virtual floppies.

### Phase 1B - Implement an internal builder surface in `smemu6`

- [x] Add a repo-local image-builder module in Smemu6 that can take a host
  directory tree and build a deterministic floppy image from it in memory.
- [ ] Encode the already-known directory format directly in the emulator code:
  - [x] directory entry encoding
  - [x] `.DR` relative sector addressing
  - [x] load / entry metadata
  - [x] contiguous allocation
- [ ] Add a dry-run or trace mode that prints the planned directory table and
  sector map from inside the emulator.
- [x] Add a manifest output mode, for example JSON or structured log output, so
  tests can inspect file placement deterministically.

Reference-only guidance:

- `smaky6_samos.py` may be used during development as a correctness reference,
  but the implementation must not shell out to it or depend on importing it.

Deliverable:

- a self-contained emulator-side builder that can generate a floppy image from a
  host directory.
- the current builder now also supports `-dump-vfd-manifest <file>` for JSON
  inspection of the planned DX1 layout.

### Phase 1C - Add a virtual-media backend in `smemu6`

- [x] Replace the direct `FILE *image[2]` assumption with a media backend layer.
- [x] Keep the existing flat-image backend as the default implementation.
- [x] Add a generated-image backend that reads from an in-memory sector buffer
  built from the host directory.
- [x] Ensure `floppy_read_data()` continues to see the same 256-byte sectors and
  checksum behavior regardless of backend type.
- [x] Preserve existing geometry detection and per-drive track state.

Suggested backend API shape:

- `mount`
- `unmount`
- `read_sector(track, sector, out256)`
- `write_sector(...)` or explicit write refusal / overlay handling for milestone 1
- `sync`
- `describe`

Deliverable:

- the emulator can mount either a real `.dsk` or a generated virtual image.

### Write-support position in the plan

Write support should be anticipated in the backend/API design, but it should not
be part of the first virtual-floppy milestone.

Reason:

- the first requirement is to generate a correct readable disk image and expose it
  consistently through the existing FDC path
- once guest writes are allowed, the project must define the source of truth for a
  host-directory-backed disk:
  - reject writes
  - store them in an overlay only
  - mirror them back into host files
  - or support a hybrid policy
- guest writes also expand the problem from file payloads to directory mutation,
  deletes, compaction, `.DR` updates, metadata rewrites, and conflict handling

Practical rule for implementation order:

- design the backend so write support can be added later without redesign
- keep milestone 1 read-only for virtual media
- settle write semantics only after DX1 read-only and DX0 bootable media are both
  working and documented

### Phase 1D - Hook DX1 host-directory mounting into the user surface

- [x] Add a CLI option such as:
  - `-floppy2-hostdir <dir>`
- [ ] Optionally add a DX1 launcher choice later, but do not block milestone 1 on
  launcher UI work.
- [x] Keep this feature native-only. The web build is explicitly out of scope for
  host-directory virtual floppies.
- [x] Print a clear startup log line showing that DX1 is virtual and non-bootable.

Deliverable:

- one supported way to start the emulator with a host-directory-backed DX1.

### Phase 1E - Choose the refresh model

The word "live" can mean several different things. The first implementation
should choose one explicitly.

Options:

- rebuild only at emulator start
- rebuild on explicit user refresh
- rebuild automatically when host files change
- rebuild automatically and hot-remount DX1 while the emulator is running

Recommended first slice:

- start with rebuild-at-mount and explicit refresh
- only add automatic file watching after the basic mount path is stable
- defer true runtime hot-swap until after explicit refresh works reliably

Current state:

- the implemented slice rebuilds at mount time
- explicit refresh now works via `Ctrl+R` or `SIGUSR2`
- `NAME.DR/` host directories now build recursive container images with
  container-relative child sector encoding
- automatic file watching and true hot-swap are still not implemented

Reason:

- it avoids coupling filesystem watching, remount behavior, and FDC timing in the
  very first milestone.

Deliverable:

- one explicit refresh policy for DX1 virtual floppies.

### Phase 1F - Validation and acceptance tests for DX1

- [x] Add a deterministic builder test in Smemu6 for a small sample host
  directory.
- [x] Add emulator-side inspection helpers that verify the generated directory
  table and sector layout without external tools.
- [x] Add test assertions that verify the generated directory table and sector
  layout automatically.
- [x] Boot `smemu6` with a real system disk on DX0 and the virtual disk on DX1.
- [ ] Use `-inject-str` to exercise at least:
  - [x] `CDIR DX1:` or equivalent drive access path
  - [x] `LIST`
  - [x] loading one known `.SM` or `.BS` file from DX1
- [x] Confirm that `.DR` subdirectories work if included in the sample tree.

Suggested definition of done for milestone 1:

- a file added or changed in the host directory can be made visible on DX1 without
  manually rebuilding a `.dsk` by hand

Current status note:

- Smemu6 now has a native `smemu6_virtual_floppy_test` CTest that creates a
  small host directory on disk, runs the internal builder, and verifies both the
  serialized directory entries and the JSON manifest output for the flat DX1
  case, one `.DR` container case, and sidecar metadata.
- Smemu6 now also has a native Unix CTest acceptance check that boots from the
  real DX0 hard disk, mounts a DX1 host directory, injects `LIST DX1:`, and
  checks the screen dump for the expected virtual-floppy entries.
- Smemu6 now also has a native Unix CTest acceptance check that injects
  `TYPE DX1:TEXTFILE.BS` and verifies that the screen dump echoes the hosted
  DX1 text-file contents.
- Smemu6 now also has a native Unix CTest acceptance check that injects
  `TYPE DX1:BOX:INNER.BS` and verifies runtime access to a file stored inside a
  `.DR` container using the official extensionless CLI path syntax.
- Smemu6 now also has a native Unix CTest acceptance check that injects
  `LIST DX1:BOX` and verifies runtime listing of a `.DR` container through the
  same extensionless CLI syntax.
- Direct access through `LIST DX1:BOX` and `TYPE DX1:BOX:INNER.BS` remains
  confirmed.
- Save this CLI rule explicitly: directory names also omit `.DR`, including
  `CDIR` arguments. `CDIR DX1:M.DR` returns `fichier existant` even on the real
  floppy image `Burotic.dsk`, so `.DR`-suffixed CLI probes are not valid
  evidence against host-directory virtual media.
- Manual-backed syntax note: the user guide documents `DX1:` as the device
  prefix and `:` as the nested subdirectory separator, so forms such as
  `CDIR DX1:DIR1:DIR2` are the expected behavior.
- Correct semantic anchor: `CDIR` creates a directory; it is not a
  change-directory oracle. The read-only DX1 hostdir fixture should therefore
  reject `CDIR DX1:NEWBOX`, while a writable copied floppy image can create the
  corresponding `NEWBOX.DR` entry.
- The earlier controller investigation still matters: asserting port `0x19`
  bit 7 removed the old pre-read `disque protégé` abort and let `CDIR` reach
  the later controller state. The missing piece for harddisk-backed SAMOS was
  the post-ROM port-`0x18` write stream; once implemented, `CDIR` stopped
  hard-failing and writable floppy images could create directories again.
- The headless automation surface is now a little stronger: `-inject-str`
  accepts `\f` to wait for the next CLI prompt before injecting the next segment,
  which allows reliable multi-step command probes such as
  `"LIST DX1:\n\fLIST DX1:\n"`.
- the resulting virtual disk is readable by SAMOS
- the disk is explicitly documented as non-bootable
- the virtual medium uses a writable in-memory overlay in the first milestone

## Milestone 2 - bootable DX0 host-directory virtual floppy

This milestone is more than "DX1 but on another drive". It is a boot-system
generator problem.

### Milestone 2 goals

- mount a host directory as DX0
- make it bootable under the Phantom ROM boot path
- guarantee deterministic placement for `SYS.SY`
- guarantee correct boot-time visibility of `CLI.SY` and any additional required
  system files
- keep the system-disk policy reproducible rather than relying on accidental file
  ordering

### Phase 2A - Reconcile the real DX0 boot contract

- [ ] Resolve the contradiction between `docs/dev/HARDWARE.md`,
  `docs/dev/SYS_SY_analysis.md`, and the older contradictory conclusions in
  `docs/dev/PLAN.md`.
- [ ] Decide which document becomes the single source of truth for the DX0 floppy
  boot sequence.
- [ ] Verify with fresh traces, if needed, whether the emulator's current cold
  boot really expects:
  - `SYS.SY` at sectors 3-38
  - any specific directory ordering
  - any specific sector layout for `CLI.SY`
- [ ] Record the confirmed rules in one dedicated boot-media note.

Deliverable:

- one reconciled DX0 boot contract doc.

### Phase 2B - Measure the reference boot disk precisely

- [ ] Add or use repo-local diagnostics in Smemu6 to inspect the reference boot
  floppy and produce a machine-readable placement map.
- [ ] Capture exact start/end sectors for at least:
  - `SYS.SY`
  - `CLI.SY`
  - `ER.SY`
  - any other files loaded automatically during boot or immediately after boot
- [ ] Identify whether the boot path depends only on `SYS.SY` placement or also on:
  - directory entry order
  - `load` / `entry` metadata encoding
  - track boundaries
  - contiguous placement
- [ ] Preserve a checked-in manifest of the known-good reference placement.

Gap likely to surface here:

- the current docs are strong on `SYS.SY`, but weaker on the exact on-disk role of
  the remaining system files.

Deliverable:

- a reference boot-disk placement manifest.

### Phase 2C - Add a pinned-placement builder mode

- [ ] Extend the builder so selected files can be placed at exact sectors.
- [ ] Support at least two policies:
  - free contiguous allocation for ordinary DX1 dev images
  - pinned boot layout for DX0 system images
- [ ] Decide how pinned placement is expressed:
  - manifest file
  - per-file sidecar keys
  - or a special template profile
- [ ] Fail loudly if the host directory contents cannot satisfy the pinned boot
  layout instead of silently moving files.

Deliverable:

- a builder that can reproduce a bootable layout deterministically.

### Phase 2D - Decide how boot-system content is sourced

There are two realistic approaches.

Approach 1: reference-template boot disk

- keep a known-good base system disk image
- replace only selected non-system files from the host directory
- preserve the original system-file placement

Approach 2: pure host-tree boot system build

- host directory contains every required system file and placement manifest
- builder reconstructs the whole boot disk from scratch

Recommended first bootable path:

- start with a reference-template approach

Reason:

- it avoids betting milestone 2 on still-unsettled knowledge about every boot-time
  file and every exact placement rule.

Later, once the rules are fully understood, the project can graduate to a full
from-scratch boot-disk builder.

Deliverable:

- one explicitly chosen boot-system sourcing policy.

### Phase 2E - Add DX0 host-directory mount mode to `smemu6`

- [ ] Add a CLI option such as:
  - `-floppy-hostdir <dir>`
  - or `-floppy-hostdir-boot <dir>`
- [ ] Ensure the mount path logs whether the generated disk is:
  - non-bootable
  - boot-template based
  - or full host-built bootable
- [ ] Reuse the same backend abstraction introduced for milestone 1.

Deliverable:

- one bootable DX0 host-directory mount path in the emulator.

### Phase 2F - Validate the bootable DX0 path

- [ ] Boot directly from the generated DX0 virtual floppy.
- [ ] Confirm that the Phantom ROM boot sequence reaches the normal SAMOS prompt.
- [ ] Confirm that `SYS.SY` and `CLI.SY` load correctly.
- [ ] Confirm that `SHIFT-BREAK` continues to reboot from the same DX0 virtual
  disk layout.
- [ ] Add a regression script that boots the generated DX0 system disk and checks
  for the visible SAMOS prompt.

Suggested definition of done for milestone 2:

- a host-directory-backed DX0 can boot the emulator reproducibly without requiring
  a manually curated `.dsk`
- the placement of system files is deterministic and inspectable
- failures in the boot layout are reported as build-time errors, not silent boot
  hangs

## Reference knowledge available from `../smaky6-tools`

These are reference implementations and format clues, not runtime dependencies.

### Useful reference facts to mirror internally

- image geometry constants
- directory entry encoding and decoding
- contiguous allocation logic
- `.DR` relative-address handling
- load/entry metadata handling
- create/add/compact behavior as a baseline for expected image layout

### Explicit non-goal

- Smemu6 should not require `smaky6_samos.py`, `smaky6_fuse.py`, Python, or a
  sibling repository checkout for normal virtual-floppy use.

## Remaining documentation gaps to close during implementation

- [ ] Reconcile the contradictory DX0 boot narrative in the dev docs.
- [ ] Add one place that records the exact reference boot-disk sector layout for
  `SYS.SY`, `CLI.SY`, and the other required `.SY` files.
- [ ] Document the chosen host sidecar or manifest format.
- [ ] Document the exact meaning of the `load` and `entry` fields for boot-system
  `SY` files versus ordinary `.SM` files.
- [ ] Document whether the first DX1 milestone supports `.DR` creation or only
  flat root-directory files.
- [ ] Document whether virtual-floppy writes from SAMOS are:
  - ignored
  - mirrored back into the host directory
  - or stored in an overlay layer

## Tooling follow-up for `../smaky6-tools`

- [ ] Add a new `smaky6_samos.py` option that converts an existing floppy image
  into a host-directory tree compatible with Smemu6 virtual floppies.
- [ ] Emit host files using the same visible `NAME.TT` convention as the
  emulator-side builder expects.
- [ ] Emit sidecar metadata files for any fields that are not preserved by the
  plain host file payload alone:
  - `flags`
  - `load`
  - `entry`
  - `date_month`
  - `date_year`
- [ ] Preserve enough boot-disk detail that a reference DX0 floppy can be turned
  into a future bootable host-directory template without losing special system
  layout knowledge.
- [ ] Decide how the export handles boot-specific placement details for files
  such as `SYS.SY`, `CLI.SY`, and any other system files whose exact placement or
  ordering matters.
- [ ] Decide whether the export should emit additional manifest data for:
  - directory entry order
  - pinned start sectors
  - boot-required flags
  - `.DR` container-relative layout
- [ ] Cross-check the exported host-directory tree by round-tripping it back
  through the Smemu6 virtual-floppy builder and comparing the resulting layout
  manifest against the source image.

## Suggested order of attack

1. Reconcile the boot docs and capture one trusted DX0 boot contract.
2. Implement an internal host-directory-to-image builder in Smemu6.
3. Add a generated-image backend in `smemu6` without changing FDC-visible behavior.
4. Ship DX1 non-bootable virtual floppy support first.
5. Add explicit refresh/remount support using the same backend seams.
6. Add deterministic placement support and a boot-template policy.
7. Ship DX0 bootable virtual floppy support only after the boot contract is
   pinned by tests and manifests.
8. Add true runtime hot-swapping only after the non-bootable DX1 and bootable DX0
  paths are both stable.
9. Add virtual-media write support only after the read-only workflow and media
  replacement workflow are both stable.

## Definition of done for the full feature

- developers can point the emulator at a host directory and use it as a Smaky
  floppy without manually rebuilding `.dsk` files
- DX1 works as a non-bootable development disk
- the first shipped virtual-floppy path is read-only unless and until write
  semantics are explicitly implemented
- DX0 works as a reproducible bootable system disk
- file metadata is preserved through an explicit host-side convention
- the emulator remains self-contained and does not require external tools for the
  live virtual-floppy workflow
- the feature is intentionally native-only and is not implemented for the web build
- the floppy stack stays layered enough that FDC timing logic is still separated
  from host-directory/image-building logic
- the documentation clearly states what is implemented, what is template-based,
  and what remains constrained by unresolved boot-loader behavior
