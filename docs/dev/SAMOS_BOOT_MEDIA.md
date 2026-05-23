# SAMOS Boot Media Contract

This note consolidates the filesystem and placement rules that matter for a
bootable Smaky 6 system floppy, especially when reproducing that floppy from a
host directory with `-floppy-hostdir`.

## Scope

The goal is not to describe every SAMOS file operation. The goal is to pin down
the parts of the on-disk contract that the emulator must preserve so that a
DX0 floppy boots reliably from the Phantom ROM through the SAMOS CLI.

## Directory format facts

- A floppy directory occupies the first 3 sectors of its container.
- Each directory entry is 24 bytes.
- A directory can therefore hold 32 entries.
- Entry fields include name, 2-character type, `start_sector`, `end_sector`,
  `flags`, exact final-sector byte count, `load`, `entry`, and BCD month/year.
- `.DR` entries are directory containers. Their child entries store sector
  numbers relative to the container start, not absolute floppy sectors.

## Boot-critical system files

The reference `Sys2-2.dsk` image establishes the current bootable DX0 layout
contract used by the emulator work.

- The current minimum system-file set to reach a bootable SAMOS CLI prompt is:
  `SYS.SY`, `CLI.SY`, and `ER.SY`.
- `SYS.SY` is the boot-critical loader/OS image.
- `CLI.SY` is the command-line shell loaded after the initial SAMOS handoff.
- `ER.SY` supplies the human-readable error-message table expected by the
  running system.
- Other `.SY` files can be present on reference disks, but the current docs
  should treat those three files as the minimal DX0 system baseline.

- `SYS.SY` starts at sector 3, immediately after the 3 root directory sectors.
- `SYS.SY` ends at sector 38.
- `SYS.SY` carries load `0x60C0` and entry `0x5720` in its directory entry.
- `CLI.SY` starts at sector 38 and ends at sector 65.
- `CLI.SY` carries load `0x5602` and entry `0x5625` in its directory entry.
- The disk boots when those files appear in the original preserved order with
  their original metadata.

## Why plain extraction is not enough

A host tree created by plain file extraction but rebuilt with alphabetical file
ordering reaches the Phantom ROM and then stops with `Erreur de lecture`.

The important failure mode was not missing file bytes. It was loss of the
original SAMOS-facing layout contract:

- per-entry metadata such as `flags`, `load`, `entry`, and dates
- original sector order, starting with `SYS.SY` at sector 3

For bootable DX0 hostdir media, reproducing only file contents is therefore not
sufficient.

## Current repo-local workflow

The repo now contains a metadata-preserving extractor:

```bash
python3 tools/extract_samos_image.py floppies/Sys2-2.dsk tmp/Sys2-2-hostdir
./build/smemu6 -floppy-hostdir tmp/Sys2-2-hostdir
```

`tools/extract_samos_image.py` preserves that repo-local interface, but now
delegates to the unified `smaky6_samos.py extract-all ... --metadata --clear`
implementation from the companion `smaky6-tools` project.

That exporter writes host files plus `NAME.TT.meta.json` or `NAME.DR.meta.json`
sidecars containing:

- `type`
- `flags`
- `load`
- `entry`
- `date_month`
- `date_year`
- `start_sector`

`src/virtual_floppy.c` then uses those sidecars when rebuilding the virtual
floppy image. In particular, `start_sector` is treated as a preferred ordering
hint so boot-critical files keep their original sector sequence.

## Emulator-side expectations

For a bootable DX0 hostdir floppy, the builder must preserve all of the
following:

- SAMOS-visible file contents
- per-entry metadata fields required by the directory format
- deterministic root-entry ordering compatible with the source image
- relative sector numbering inside `.DR` containers

It does not need to preserve the original source image byte-for-byte in unused
regions. The contract is behavioral: the Phantom ROM and SAMOS must observe the
same effective directory and sector layout for the files they load.

## Regression anchor

The current executable regression for this contract is:

```bash
ctest --test-dir build -R smemu6_virtual_floppy_dx0_hostdir_boot
```

That test:

- exports `floppies/Sys2-2.dsk` through `tools/extract_samos_image.py`
- boots the exported hostdir via `-floppy-hostdir`
- checks that `SAMOS 2-2` reaches the CLI without `Erreur de lecture`

## Relationship to DX1 hostdir media

DX1 hostdir media is less strict because it is not part of the boot path.
It still uses the same filesystem encoding rules and sidecar metadata, but it
does not require the exact system-file placement constraints that DX0 boot media
does.
