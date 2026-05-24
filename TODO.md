# Smemu6 — TODO / Known Gaps

<!-- markdownlint-disable MD013 -->

Items are grouped by subsystem.  Entries marked **[confirmed]** have been verified
against disassembly or hardware documentation.

---

## UI / Display

## PSG sound-card add-on

- Fetch `emu2149` through CMake `FetchContent`, pinned to a known upstream
  commit, instead of keeping a hand-copied third-party snapshot in-tree.
- Keep the Smaky-specific PSG layer thin and local: one wrapper around four
  AY-compatible chips, with SIGMA-derived odd-port register-select and
  even-port data semantics hidden behind the local API.
- Make the port ownership conflict explicit: `-psg` currently takes decoded
  ports `0x20..0x27`, so `-harddisk` / `-harddisk2` must stay mutually
  exclusive until hardware evidence proves a different decode or multiplexing
  scheme.
- Netlist result: the direct AY `CLOCK` net is `Net-(U1-CLOCK)` and is driven
  by `U16` pin `4`, not directly by the `SW1` / `U8` / `U7` control path.
- Next clock-correction slice: determine the fitted `RV1` value and the actual
  `U16` Schmitt-trigger RC oscillator frequency from the `1nF1` + `R6` + `RV1`
  network before replacing the current fallback `2.41152 MHz` in the emulator.
- Keep `SW1` / `U8` / `U7` in the notes as nearby control logic, but no longer
  treat that cluster as the direct unresolved AY clock source.

## Keyboard

- Current focused instrumentation slice: keep behavior unchanged and trace the
  exact simultaneous ordinary/function-key handoff by snapshotting CLA/status
  reads together with `0x457E`, `0x4580`, `0x4581`, `0x4582`, and `0x457C/0x457D`
  around the Stage 1 / Stage 2 path.
- Add a small scripted chord injector for interactive programs so headless runs
  can type a CLI command, wait a configurable number of frames, then assert a
  simultaneous function-key + ordinary-key combination such as `PROGRA+z`.
- Latest traced repro shows the repeated-`z` SMILE path reaching `0x457E` at
  `pc=0x0524`, outside the current `0x0516..0x0519` compatibility hook.
  Test the smallest possible widening there before changing broader keyboard
  timing behavior.
- Latest traced `PROGRA+END` repro shows the first chord delivery still arming
  ordinary CLA reassert (`code=04`, `reassert=1`, `cycles=400`) while
  `fonct=08` is held. That matches the delayed confirmation-line flicker
  report, so function-layer chords should stay one-shot on the ordinary-key
  side.
- Follow-up trace showed that disabling the ordinary reassert was not enough by
  itself: after the first successful `?GETFO` read, the no-key CLA path could
  still keep re-exposing the held `PROGRA` bit. The current slice therefore
  suppresses only that later no-key CLA function exposure until release, while
  still letting `?GETFO` see the held function bit for `PROGRA+z` at `pc=0x0524`.
- Status-bar function-key buttons must keep their original mouse semantics:
  left-click acts as a hold while pressed, and right-click toggles a persistent
  latch. The function one-shot logic should apply only to keyboard-held bits,
  not to mouse-latched bits.
- Host focus-loss handling must flush the active ordinary key, queued pending
  ordinary keys, and host text-input bookkeeping. Missing a key-up during a
  browser/native focus change otherwise leaves ghost repeat characters latched
  in the CLA path; this is especially visible in the web build.

### ~~Startup launcher window (SDL configuration dialog)~~ ✅ Done

Before the emulator window opens, show a small SDL launcher that lets users
configure common options without touching the CLI.  Implemented in
`src/launcher.c` / `src/launcher.h`.  `main.c` calls `launcher_run()` after
SDL_Init; it fills a `LauncherConfig` struct and returns; `main()` applies the
struct to the existing config fields and continues with the normal startup path.
Skip with `-no-launcher` or when `SDL_VIDEODRIVER=dummy` (headless).

#### Window layout (top to bottom)

##### Header

- Single centred line: *"Smemu6 -- Smart Emulator of the Smart Keyboard"*

##### Storage section

|Label|Control|Emulator option|
|---|---|---|
|`DX0:`|Drop-down: **Floppy** / **Harddisk**|`-floppy` / `-harddisk`|
|*(DX0 path)*|File-picker button → opens OS file dialog filtered to `*.dsk *.DSK`|path argument|
|`DX1:`|Static label: **Floppy** (no second harddisk)|`-floppy2`|
|*(DX1 path)*|File-picker button → opens OS file dialog filtered to `*.dsk *.DSK`|path argument|

File paths are shown truncated (last 46 chars with `…` prefix) on a sub-row
directly below each disk row (Browse + Clear buttons).  A small `×` button clears the selection.

- Winchester images mounted with `-harddisk` / `-harddisk2` now also use an
  in-memory write overlay: guest writes are visible inside the running session
  but are discarded on remount or emulator exit, leaving the `.DSK` file
  untouched.

##### TODO — host-directory virtual floppy for DX0 / DX1

- Initial native-only slice now exists for **DX1:** via `-floppy2-hostdir <dir>`.
- Current limitations of that first slice include DX1-only non-bootable use,
  explicit refresh only (`Ctrl+R` or `SIGUSR2`), no automatic file watching
  yet, and an in-memory writable overlay whose guest changes are lost on
  refresh or emulator exit and never written back to the host tree.
- Host file names must currently follow `NAME.TT` with 1-8 base characters,
  `_` allowed, and a known 2-character Smaky type such as `SM`, `BS`, `SY`,
  `IM`, `HP`, or `RF`.
- Host trees may now also contain nested `NAME.DR/` directories; entries inside
  each `.DR` container are encoded relative to that container, matching the
  SAMOS directory format.
- In the SAMOS CLI, directory path components omit the `.DR` suffix. That means
  runtime access uses forms such as `LIST DX1:BOX`, `TYPE DX1:BOX:INNER.BS`,
  and `CDIR BOX` even though the on-disk container file is `BOX.DR`.
- `.DR`-suffixed `CDIR` probes are invalid evidence: real floppy media also
  rejects forms such as `CDIR DX1:M.DR` with `fichier existant`.
- The original user manual documents `DX1:` as the device prefix and `:` as the
  subdirectory separator, so forms such as `CDIR DX1:DIR1:DIR2` are target
  syntax.
- Current emulator probes still fail that documented form on both real and
  virtual DX1 media: `CDIR DX1:M`, `CDIR DX1:ZZZ`, and `CDIR DX1:BOX` currently
  report `fichier inexistant`, even though direct paths such as `LIST DX1:BOX`
  and `TYPE DX1:BOX:INNER.BS` work. Treat that as an open emulator/runtime gap,
  not as evidence against the manual syntax.
- Optional sidecars `NAME.TT.meta.json` are now supported for regular files and
  `NAME.DR.meta.json` for directory containers, with `type` validation plus
  `flags`, `load`, `entry`, `date_month`, and `date_year`.
- `-dump-vfd-manifest <file>` now dumps the planned DX1 host-directory layout
  as JSON so sector placement can be inspected without external tools,
  including nested `.DR` entries and their encoded per-directory sector values.
- Add an additional floppy-backed storage option that mounts a live virtual floppy
  from a host directory instead of a `.dsk` image.
- For the straightforward development case, this can be used on **DX1:** with a
  repo-local directory such as `floppies/DX1/`, where every host file appears live
  on a virtual floppy mounted as **DX1:** inside the emulated machine.
- **DX0 should also be supported**, but it is a stricter case because the virtual
  floppy must be bootable: the generated on-disk directory / sector layout needs
  deterministic placement for system files such as `SYS.SY` and `CLI.SY`, not just
  a best-effort mirror of host files.
- Current finding from the first `Sys2-2.dsk` DX0 probe: a plain file extraction
  is not enough for bootable hostdir media. The rebuilt image reaches the ROM,
  then stops with `Erreur de lecture`. The original `Sys2-2.dsk` carries
  boot-critical per-entry metadata (`flags`, `load`, `entry`, dates) and a
  specific sector order starting with `SYS.SY` at sector 3; the hostdir export
  path therefore needs to preserve at least that metadata and ordering.
- That metadata-preserving path now exists via `tools/extract_samos_image.py` +
  `-floppy-hostdir <dir>`: exporting `floppies/Sys2-2.dsk` with preserved
  `start_sector`, `flags`, `load`, `entry`, and date fields produces a DX0
  hostdir tree that boots to the SAMOS CLI. The script now acts as a wrapper
  around the unified `smaky6_samos.py extract-all ... --metadata --clear`
  implementation.
- Maintain a separate technical note for the SAMOS filesystem and boot-media
  contract, so directory layout, file metadata, and DX0 boot requirements live in
  one dedicated reference file instead of being scattered across TODO entries.
  That note now exists in `docs/dev/SAMOS_BOOT_MEDIA.md`.
- That likely means the virtual-floppy layer needs two modes or policies:
  a simple live development mirror for non-boot disks, and a boot-system layout
  mode that can place required system files at the exact sectors/locations SAMOS
  expects for a bootable DX0 floppy.
- This would be especially useful for software development and quick testing,
  because updated host files could be reflected in the emulated floppy without
  rebuilding or manually repacking a disk image each time.
- If SAMOS-specific metadata must be preserved per file (for example load address,
  entry point, protection bits, or similar attributes), store it alongside the
  host files in hidden sidecar metadata files rather than forcing everything back
  through a monolithic disk image format.

##### Screen section

|Label|Control|Emulator option|Default|
|---|---|---|---|
|Scaling|Drop-down: **1×** / **2×** / **3×** / **4×**|`-scale N`|**1×**|
|Phosphor colour|Drop-down: **Green** / **White**|*(new `-phosphor white` option)*|Green|
|Scanlines|Drop-down: **Off** / **On**|`-scanlines`|On|
|Disable screen blanking|Toggle: **On** / **Off**|`-no-display-off`|On|
|Verbose video log|Toggle: **Off** / **On**|`-verbose-video`|Off|

> **Note:** `-phosphor white` is implemented. The launcher **Phosphor colour** drop-down is wired directly.
> **Note:** `-scanlines` is implemented and tested. The launcher control is wired directly.

##### Sound section

|Label|Control|Emulator option|Default|
|---|---|---|---|
|Beeper|Toggle: **On** / **Off**|`-no-beeper` when Off|On|
|Drive sounds|Static label: **Off** *(coming soon)*|`-drive-sound` (stub)|Off|

##### Button row (bottom)

- **Help** — opens a second SDL window (or overlaid panel) showing a
  two-paragraph summary of SAMOS usage (LIST, COPY, INIT, TAB shortcut) and a
  keyboard mapping table (host key → Smaky 6 key, same content as
  `docs/EMULATOR_GUIDE_EN.md` §5 Keyboard Controls).
- **Start** — closes the launcher window and starts the emulator with the
  chosen settings.  If a required ROM (`roms/samos_sys17.rom`) is missing,
  show an SDL_ShowSimpleMessageBox warning before starting.

#### Implementation notes

- Rendered with SDL2 primitives only — no `SDL_ttf`.  Text uses the authentic
  Smaky 6 chargen ROM embedded as `src/chargen_rom.h` (128 chars × 16 rows, LSB-first;
  rows 0–9 carry glyph data including descenders, rows 10–15 are zero).
  `FONT_ROWS 10` is used as the render loop bound so descenders (`p`, `q`, `g`, `y`, `j`)
  are shown; `FONT_H 8` is used for layout/centering.
- File picker: async `tinyfiledialogs` call running in a detached `SDL_CreateThread`
  so the event loop is never blocked.  `PickCtx` state machine (`PICK_IDLE` /
  `PICK_RUNNING` / `PICK_DONE`) is polled from the main loop.
  `SDL_HINT_VIDEO_X11_NET_WM_PING=0` prevents the WM from marking the window
  as unresponsive while the picker thread starts up.
- The launcher must work in headless mode (`SDL_VIDEODRIVER=dummy`): detect the
  dummy driver and skip the launcher, reading config from CLI only.
- All launcher settings are additive: CLI options passed alongside the binary
  pre-populate the launcher controls so the user can review and adjust them.
  After clicking Start the launcher values are authoritative for all settings
  (disk paths, type, scale, phosphor, scanlines, beeper).
- Window size: 460 × 464 logical pixels at 1:1; no scaling needed.

#### New options to add to main.c / README / guides

|New flag|Purpose|Default|
|---|---|---|
|~~`-phosphor white`~~|~~Use white-phosphor palette instead of green~~|✅ Done|
|~~`-scanlines`~~|~~Draw alternating dim lines over the framebuffer (CRT effect)~~|✅ Done|
|~~`-no-launcher`~~|~~Skip the launcher and go straight to the emulator~~|✅ Done|

---

### ~~Period-correct green-phosphor look~~ ✅ Done

The SDL renderer now draws lit pixels as `#00E700` (P31 green phosphor) on a near-black
dark green background `#000800` (`VIDEO_COLOR_LIT` / `VIDEO_COLOR_BG` in `video.h`).

### ~~Correct pixel aspect ratio~~ ✅ Done

The real CRT had non-square pixels (~2–4× taller than wide).  The machine pixel buffer
(512×240) is rendered 1:1 into the SDL logical window; the physical window is enlarged by
the `-scale` factor (default 2×), giving 1024×520 at the default setting.  The `VIDEO_ASPECT_H`
constant is kept equal to `VIDEO_PX_H` so no stretching is applied inside the logical window
— integer scaling via the SDL window size is the preferred approach.

### ~~Integer scaling option~~ ✅ Done

`-scale N` (N = 1..8, default 2) multiplies the physical window by N.  At default 2×,
the window is 1024 × 520 (240+20 status bar × 2).  SDL logical size stays fixed at
`VIDEO_WIN_W × VIDEO_WIN_H` so the rendering code is scale-independent.

### Hot-swap floppy disks while running

Allow the user to eject and replace a floppy image without restarting the emulator,
similar to how a real Smaky 6 user would physically swap disks.

#### Proposed UI

- Each DX0 / DX1 slot label in the status bar gets a small **⏏** (eject) button rendered
  next to the drive label (or revealed on hover).
- **Left-click ⏏**: ejects the current image (closes the file, sets the slot to empty,
  LED goes dark, label shows `---`).  The drive reports "no disk" to the FDC until a
  new image is loaded.
- A second click on the empty slot (or a dedicated **📂** button) opens the OS native
  file picker (via `tinyfiledialogs`) filtered to `*.dsk *.DSK`; on confirm the new
  image is mounted in place without rebooting.
- The swap is also accessible from the launcher settings panel so it can be configured
  before start as well as at runtime.

#### Hot-swap implementation notes

- `floppy.c` already has a per-drive media backend (`fdc.media[d]`), so hot-swap
  should extend that layer with explicit eject/load helpers instead of adding a
  new file-only abstraction.
- Eject should unmount the current backend, clear the drive description, and
  leave the slot empty until a replacement image or virtual source is mounted.
- Writes to the in-memory sector cache that haven't been flushed should be written back
  to the old image file before ejecting (dirty-flush).
- A short (≈500 ms) "motor spin-down" period after eject before the new disk is
  accepted would match real hardware, but is optional.
- Status bar hit-test in `main.c` / `video.c` needs a new clickable region per slot.

### ~~Live floppy track/sector visualisation~~ ✅ Done

The status bar (14 logical px `VIDEO_LED_H`, scales with `-scale`) shows per drive slot (DX0 / DX1).
Each slot auto-detects whether it holds a floppy or a harddisk image:

- **Floppy** (amber LED): `T:nn S:nn` — current track and sector
- **Harddisk** (orange-red LED): `C:n H:n S:nn` — last cylinder, head and sector accessed
- **Empty**: LED off, no info text

The label (`DX0:` / `DX1:`) is rendered in green for floppies and cyan for harddisks.

### RESET and BREAK buttons in status bar (first line) ✅ Done

Added **RESET** and **NMI** buttons at the right end of the first status bar row
(the floppy/harddisk track info row), rendered in vivid red (brighter than the
function-key buttons on the second row).

- Left-click **NMI** → `machine_nmi()` (same as host Pause/F11)
- Left-click **RESET** → `machine_reset()` (same as host Shift+Pause/Shift+F11)
- First click arms RESET (button blinks orange for 3 s); second click confirms the reset.

### ~~Function-key buttons: right-click to latch~~ ✅ Done

The seven function-key buttons on the second status bar row are **modifier keys** on the
real Smaky 6 — the user holds them while pressing another key.  Left-click behaviour
(held while button is down, released on mouse-up) stays as-is.

**Right-click toggles a persistent latch** for that bit in `fonct_bits` (`fonct_latched`
field in `struct kbd`).  A latched button stays active until right-clicked again, allowing
single-handed modifier+key combinations.  Latched buttons render in yellow (fill, border,
and text) to distinguish them from the red held state.

---

## Keyboard

### ESC / UNDO runtime reconciliation

External S471 PROM decoding suggests the top-left physical key position emits
`0x06` on the normal layer, with `0x1B` only on an alternate FNCT/ALT layer.
The emulator now follows that `0x06` result as its current working mapping.
What remains unresolved is how this should be reconciled with the older CLI audit
that previously pointed at a `0x04` cancel path.

Needed next check:

- verify the physical position map against live runtime behaviour and CLI.SY;
- determine whether `0x04` belongs to a different key position (the PROM points to
  the Q-row right-end candidate), while the top-left key is really `0x06`;
- reconcile the live CLI behaviour with the now-switched `0x06` working mapping.

### ~~Full S471 matrix modeling~~ ✅ Mostly done

The full four-layer S471 dump is now available.  It confirms exact per-position
outputs for normal, Shift, FNCT/ALT, and caps-like layers, including:

- top-left ESC / UNDO = `0x06 / 0x06 / 0x1B / 0x06`;
- Backspace position = `0x08 / 0x7F / 0x01 / 0x08`;
- Tab position = `0x09 / 0x0B / 0x03 / 0x09`;
- Return position = `0x0D / 0x0C / 0x0A / 0x0D`;
- Q-row right-edge candidate = `0x04 / 0x05 / 0x07 / 0x04`.

Current emulator state:

- ordinary keys now enter through the strict CLA-facing latch in `src/keyboard.c`,
  using host scancode position -> S471 layer lookup -> CLA / `SYS.SY` delivery;
- printable host text still uses `SDL_TEXTINPUT` compatibility handling, with a
  one-shot fallback for composed accented characters when SDL delivers decoded
  text without a fresh claimable host text scancode;
- overlapping SDL taps are queued as pending ordinary keys and promoted one by one
  once the active latch becomes idle;
- the direct `0x457E` helper byte is consumed on the `0x0519` accessor read, and
  new promoted ordinary keys clear any stale helper byte before exposing the next
  printable key;
- function-key aliases (`F1..F7`) remain convenience host bindings for the 7
  bottom-row Smaky function bits;
- held function keys are now also visible to direct helper readers through
  syscall `0x0E` when no ordinary staged byte is pending, so programs like
  `FLIPPER.SM` see stable held `CURSOR` / `CHANGE` input again;
- FNCT/ALT-layer printable outputs are still not broadly exposed through separate
  host bindings.

Remaining follow-up:

- add the remaining audited host-position mappings that are still absent from
  `HOST_MATRIX_KEYS[]`;
- decide whether to add an explicit compatibility text-entry mode on top of the
  strict S471 baseline for accented / host-layout-friendly typing.

### Overlapping printable-key CLI delivery ✅ Done  [confirmed]

The remaining real-keyboard failure after the first matrix-model fix was no
longer the repeat storm: later printable keys were reaching the circular-buffer
path, but the CLI helper could still keep seeing a stale Stage 1 direct byte at
`0x457E`.

Validated result:

- runtime trace showed repeated `pc=0x0519` reads of the same stale `0x457E` byte
  while later printable keys had already been queued and promoted;
- consuming `0x457E` on the direct accessor read removed that sticky helper path;
- clearing `0x457E` again when a new ordinary key is latched stopped promoted keys
  from inheriting the previous direct byte;
- the traced overlapping `a s d` sequence now reaches visible CLI writes at
  `0x45C0..0x45C2` in order.

Regression coverage:

- `tools/check_keyboard_asd_trace.sh` now injects overlapping `keydown`/`keyup`
  events for `a`, `s`, and `d` against the SDL window;
- the script asserts visible CLI insertions at `pc=0x590F` for all three keys.

### Released promoted-key repeat disarm ✅ Done  [confirmed]

Manual `Shift+MSG` CLI tracing on 2026-05-13/14 exposed a narrow post-fix bug:
the third promoted key (`G`) inserted correctly once, then reappeared later via
SAMOS Stage 4 auto-repeat.

Validated result:

- the first successful `G` enqueue still armed `0x4558 = 0x23`, `0x4577 = 0x47`;
- later Stage 4 runs at `0x01DF..0x0205` re-injected `0x47` into `0x4596`, causing
  endless trailing `G` insertions;
- clearing only the emulator-side latch was insufficient;
- the fix is to clear both SAMOS repeat bytes (`0x4558` and `0x4577`) when a
  released promoted key is committed to the circular buffer via the `0x457C`
  advance hook in `src/memory.c`.

Validated runtime result (`tmp/manual_shift_msg_noreturn_fix11.log`):

- visible CLI insertions are exactly `M`, `S`, `G` at `0x45C0..0x45C2`;
- no later visible `G` appears at `0x45C3` or beyond.

### ~~Auto-repeat (SAMOS ≥ 1.3)~~ ✅ Done  [confirmed]

SAMOS ISR Stage 4 (`0x01DF–0x0206`) implements hardware-accurate key auto-repeat.

The two RAM locations controlling it:

|Address|Octal|Role|
|---|---|---|
|`0x4558`|`042530`|Initial-delay countdown. Set to `0x23` (35 frames = **700 ms**) on first keypress; decremented each frame; when it hits zero, reloaded to `3` (3 frames = **60 ms**) for the fast-repeat rate.|
|`0x4577`|`042567`|Repeat key code register. Stores the last key written to the circular buffer; re-injected each time the countdown fires.|

**Current status:** Physical keyboard now uses the strict CLA-facing latch plus
pending-key promotion model described above, while SAMOS Stage 4 still provides
the actual post-enqueue repeat timing through `0x4558` and `0x4577`. SDL
key-repeat events are still filtered out (`if (ev->repeat) return`), so **holding
a key produces one initial physical character** and then relies on the SAMOS
repeat registers.

✅ Done — the repeat registers are now armed from the `0x457C` circular-buffer
advance hook in `src/memory.c`, using the full 16-bit write pointer so Stage 4
tracks the real committed key code. Once that first enqueue happens, the
ordinary-key CLA latch is quiesced so later idle CLA polls do not zero `0x4558`
again through the Stage 1 helper path. Release / commit cleanup still clears
`0x4558` and `0x4577` explicitly when the key is no longer held.

See [docs/dev/keyboard_analysis.md](docs/dev/keyboard_analysis.md) for the full Stage 4 disassembly.

### Special / function keys  [codes confirmed from doc p.213]

The Smaky 6 keyboard has two categories of extra keys beyond the ASCII set.

#### Category 1 — 7 "touches de fonction" (bitmask, read via GETFON)

These 7 keys are NOT sent through the key FIFO.  When no regular key is pressed
(FOUND=0), the CLA port returns a 7-bit bitmask where each bit represents one
function key held down.  The SAMOS `?GETFON` / `GETFON` system calls read this
bitmask.  Codes confirmed from doc section 10.4, page 213 (octal):

|Key|Octal|Hex|Bit|
|---|---|---|---|
|CHANGE|`001`|`0x01`|0|
|SEARCH|`002`|`0x02`|1|
|SHOW|`004`|`0x04`|2|
|COPY|`010`|`0x08`|3|
|CURSOR|`020`|`0x10`|4|
|PROGRA|`040`|`0x20`|5|
|KILL|`100`|`0x40`|6|

To implement: add a `uint8_t fonct_bits` field to `struct kbd`; set/clear the
appropriate bit on SDL key down/up; return `fonct_bits` (with bit 7 clear = FOUND=0)
from `keyboard_read_cla()` when no regular key is pending.  The existing `0x80`
no-key sentinel already occupies the FOUND=1 / no-key case — the FOUND=0 path just
needs to return `fonct_bits` instead of `0x80`.

✅ Done — `fonct_bits` field added to `struct kbd`; FONCT[] table in `keyboard_event()`
sets/clears bits on KEYDOWN/KEYUP. Function keys work correctly with no character echo
and no infinite repeat.

**Refactor keyboard CLA to a single hardware-accurate model** ✅ Done

The hardware model (schematic §10.4):

|Condition|`IN A,(0)` returns|
|---|---|
|FOUND=1 (key held)|`key_code & 0x7F`|
|FOUND=0|`fonct_bits & 0x7F` (function-key bitmask, bit 7 clear)|

STROBE clears FOUND+FULCLA; scanner reasserts within ≤200µs if key still held.
Identical for Phantom ROM polling, SAMOS ISR, and monitor — no mode flag needed.

Confirmed by the newer manual page 10.4-2:
- **CLA (port 0x00) read:** when `FOUND=0`, the read value corresponds to function keys
- **Bit 7 alone distinguishes `FOUND=0` vs `FOUND=1`:** the separate keyboard status register is not required for normal reads
- **Two consecutive `LOAD A,$CLA` (<5 us):** enter joystick / potentiometer sampling mode for about 5 ms
- **Three consecutive `LOAD A,$CLA`:** toggle the speaker or lamp

Current emulator behavior:
- **CLA (port 0x00) read:** returns `fonct_bits & 0x7F` when no ordinary key is latched
- **GETFON (0x4580) register:** also mirrored in `refresh_function_bits()` as compatibility state for current software paths
- **?GETFO cache (0x45BD/0x45BE):** also mirrored in `refresh_function_bits()` because the current SYS.SY implementation reads those locations
- **Port 0x01 write:** acknowledges function key presses (prevents infinite repeat)

Implemented:

- `keyboard_read_cla()`: if `found` → clear found, re-assert if `physically_held`,
  and for post-boot injected held regular keys make the first CLA read return
  `0x80 | key_code`; otherwise return the normal `key_code & 0x7F`. If `found=0`,
  return `fonct_bits & 0x7F` to match the hardware-documented function-key path.
- `refresh_function_bits()`: updates `fonct_bits` from keyboard and mouse state,
  then mirrors it to `0x4580` and `0x45BD/0x45BE` for compatibility with the currently observed SAMOS / SYS.SY helper paths.
- Port 0x01 write handler: acknowledges function key presses by latching the current
  function key state, preventing infinite repeat on held keys.
- `physically_held=1` at power-on models FOUND latch SET (4013 FF2). Every CLA read
  re-asserts `found=1` while held, so all boot-phase `kbd_wait` loops exit
  automatically (Phantom ROM 0x00FD + SAMOS init 0x00B5). Released when SAMOS ISR
  vector `bus[0x4566..7]==0x003E` is installed.
- Removed: `samos_loaded`, `cla_seen`, `key_hold_frames` from `struct kbd`.
- Removed: `cla_seen=0` from port 0x01 ISR ACK handler.
- Confirmed by later probes: disabling the out-of-band `fonct_bits -> 0x4580`
  mirror does not change the mixed-path result; the surviving first `0x0171`
  payload comes from `SYS.SY` Stage 1 itself, not from a frame-time mirror.

#### ?GETFO-adjacent cache path  🚧 Under revalidation

Nearby SYS.SY code reads function-key-related state from memory cache locations
0x45BD and 0x45BE rather than directly from the CLA port. The exact callable
entry and software contract still need revalidation against trusted behavior,
especially SMILE itself.

Important distinction:
- **Hardware fact:** page 10.4-2 documents that when `FOUND=0`, a CLA read itself returns the function-key value
- **Current software detail:** observed SYS.SY code near the previously suspected helper path reads cached values from `0x45BD/0x45BE`
- **Current emulator choice:** keep the hardware-faithful CLA behavior, let SAMOS Stage 1 refresh `0x4580` from CLA, and retain the direct `0x45BD/0x45BE` compatibility mirrors only until SMILE revalidation proves they can be changed safely

**Implementation (commit fb6e029 + refactor follow-up):**
- `refresh_function_bits()` now writes directly only to `0x45BD/0x45BE` (?GETFO cache)
- Function key state updates reach `0x4580` through the normal SAMOS Stage 1 CLA read path rather than an emulator-side mirror
- No guard conditions — the surviving `0x45BD/0x45BE` cache writes don't affect boot (verified safe)

**Testing:**
- GUI must be visible for keyboard input (headless mode with `-no-display-off` doesn't capture SDL events)
- `FKTEST.SM` currently exists only as an unvalidated draft probe and must not be treated as evidence yet

#### Hardware-first refactor branch  🚧 In progress

Branch: `refactor/keyboard-hardware-model`

Refactor goal:
- make CLA / FOUND / FULCLA semantics the primary source of truth
- treat `0x4580` and `0x45BD/0x45BE` as derived compatibility mirrors only
- centralize keyboard state ownership inside the keyboard subsystem
- remove ad-hoc direct mutations of `fonct_bits` and related cache state from unrelated files

Acceptance criteria for the branch:
- DX0 autoboot still works from the power-on virtual Enter path
- ordinary keys still enter through the S471 / CLA path correctly
- function keys still do not echo visible characters unexpectedly
- function keys no longer depend on scattered direct state mutations outside the keyboard subsystem
- current compatibility readers still see consistent state: `0x4580`, `0x45BD`, `0x45BE`, and any retained helper paths
- FLIPPER and SMILE behavior must be revalidated before merge

Planned staging:
- stage 1: centralize function-key state recomputation and cache mirroring in keyboard code  ✅ first slice landed
- stage 2: route main.c and machine.c function-key updates through keyboard-owned helpers
- stage 3: re-audit `memory.c` helper-path hacks and keep only the ones still justified by real software behavior
- stage 4: validate against boot, CLI, FLIPPER, SMILE, and FKTEST

First slice completed:
- `keyboard.c` now owns helper APIs for clearing all function bits, setting mouse-held function bits, and acknowledging function-key masks
- `main.c` focus-loss and mouse-button paths now call keyboard-owned helpers instead of writing `fonct_*` fields directly
- `machine.c` port `0x01` ACK path and reset/injection helpers now call keyboard-owned helpers for function-key clearing/acknowledgment
- `keyboard.h` CLA comment corrected to match the current hardware-faithful implementation (`FOUND=0 -> fonct_bits & 0x7F`)

Second slice completed:
- the syscall `0x0E` / `0x457E` compatibility helper path is now owned by `keyboard.c` via `keyboard_read_stage1_code()`
- `memory.c` no longer contains function-key fallback policy directly; it delegates that keyboard-specific decision to the keyboard subsystem
- DX0 boot to CLI revalidated after the helper move

Remaining gaps versus the latest manual:
- **Direct ?GETFO cache mirrors remain:** `refresh_function_bits()` still writes `0x45BD/0x45BE` directly, which is a compatibility choice rather than pure hardware behavior
- **Port 0x01 ACK remains emulator policy:** the branch now preserves host-held F-key state across ACK writes, but the exact hardware/software contract still needs interactive SMILE revalidation
- **Double / triple CLA side effects are not modeled yet:** two `LOAD A,$CLA` within `<5 us` should enter joystick mode, and three should toggle speaker / lamp
- **Keyboard scan timing is only approximated:** the code models a generic reassert delay, not the documented `300 kHz` scan or the `~3 us` adjacent-key edge case
- **SMILE still needs branch revalidation:** boot to CLI is confirmed after the current slices, FLIPPER has now been revalidated successfully, but SMILE still does not see at least PROGRA/F4; `FKTEST.SM` is not yet trusted as evidence

Third slice completed:
- removed the direct `fonct_bits -> 0x4580` mirror from `refresh_function_bits()`
- DX0 boot to CLI still works without that shortcut, confirming `0x4580` no longer needs an emulator-side mirror for startup
- SYS.SY still directly reads `0x45BD` at `0x0EF8` and `0x45BE` at `0x0EFC`, so those two compatibility mirrors remain the current floor for `?GETFO`

Fourth slice completed:
- port `0x01` ACK no longer clears `fonct_keyboard_bits`, so ACK writes no longer destroy the host-held F-key state
- DX0 boot to CLI still works after that ACK change
- interactive SMILE revalidation is still required before treating this as the final fix

Current interactive validation status:
- FLIPPER still works correctly on this branch
- holding a function key in FLIPPER still works correctly
- FLIPPER still shows no repeat bug
- SMILE still does not see at least PROGRA/F4

Fifth slice completed:
- static `SMILE.SM` analysis shows it reads function keys through `RST 20h / 0x0E = ?GETFO`
- exported `FLO.ST` constants give the canonical named-bit order:
  `CHANGE=0x01`, `SEARCH=0x02`, `SHOW=0x04`, `PROGRA=0x08`,
  `KILL=0x10`, `COPY=0x20`, `CURSOR=0x40`
- the emulator function-key tables now use that canonical order
- build and DX0 boot still work after the bit-order change

Sixth slice completed:
- initial hypothesis only: treat `PROGRA` as an S471 `FNCT`-layer selector for concurrent matrix-key combinations
- later user validation falsified that model for SMILE: it changed plain `z` into remapped bytes (`û`, then `ù`) instead of leaving the ordinary key plain while `PROGRA` stayed a separate function bit
- the surviving useful part of this investigation was that text-capable keys had to stay on the matrix path while a function key is held, rather than falling back to plain SDL text injection

Seventh slice completed:
- text-capable keys now go through the S471 matrix path when any function key is active instead of being skipped as plain SDL text keys
- matching `SDL_TEXTINPUT` events are ignored while any function key is active, so combinations such as `PROGRA+z` are no longer overwritten by plain `z`
- build and DX0 boot still work after the text-path fix

Eighth slice completed:
- the `?GETFO` / `0x0519` compatibility helper now returns held function bits directly instead of preferring the staged ordinary byte in `0x457E`
- root cause: with `PROGRA+z`, SMILE's `RST 20h / 0x0E = ?GETFO` call was receiving the staged ordinary code `0x1A` (`û`) instead of the held function bit `0x08`
- ordinary-key staging in `0x457E` is no longer consumed by the `?GETFO` helper path
- build and DX0 boot still work after the `?GETFO` fix

Ninth slice completed:
- intermediate hypothesis only: if `PROGRA` really selected the matrix FNCT layer, alphabetic shortcuts would also need logical-letter mapping on QWERTZ hosts
- later user validation falsified that broader model too, because SMILE still wanted plain `z` rather than an FNCT-layer byte

Tenth slice completed:
- `PROGRA` no longer changes the matrix layer; it remains a separate function bit while ordinary keys stay on the normal/Shift/Caps lookup path
- refined root cause: for SMILE assembly shortcuts such as `PROGRA+z`, remapping the ordinary key through `S471_LAYER_FNCT` was itself the bug
- the `PROGRA` path still forces text-capable keys through matrix delivery instead of plain SDL text injection, so the ordinary key can coexist with the separate function-bit read
- build and DX0 boot still work after removing `PROGRA` from the matrix-layer selector

Eleventh slice completed:
- intermediate hypothesis only: remove the `0x80 | key_code` prefix from the first ordinary CLA read
- later user validation falsified it immediately: ordinary keyboard input stopped working at the CLI prompt

Twelfth slice completed:
- restored the bit-7-first ordinary CLA delivery for normal post-boot keys
- refined root cause: SAMOS still depends on that first `0x80 | key_code` ordinary CLA value to seed the live `0x4580` workspace and reach the CLI buffer path
- this rollback restores ordinary typing while preserving the separate `PROGRA` function-bit model and the direct `0x4580` mirror
- build and DX0 boot still work after restoring the ordinary-key CLA prefix

Thirteenth slice completed:
- corrected the `?GETFO` / `0x457E` interception to match the actual `GETFO` routine window `0x0516..0x0519` instead of the stale single `0x0519` PC check
- refined root cause: the existing traces already showed the `LD A,(0x457E)` read at `pc=0x0516`, so the narrower hook could miss live function-bit delivery to SMILE even though ordinary matrix input still worked
- build and DX0 boot still work after the `GETFO` hook correction

Fourteenth slice completed:
- restored the bit-7-set CLA idle/function return: `FOUND=0` now yields `0x80 | fonct_bits` instead of bare function bits
- refined root cause: the bit-7-clear idle return was not hardware-faithful and made the no-key/function path look like an ordinary matrix byte source rather than the separate Stage 2 function/no-key path audited in `SYS.SY`
- build and DX0 boot still work after restoring the hardware-style CLA idle return

Fifteenth slice completed:
- intermediate hypothesis only: narrowed the ordinary bit-7 prefix while a function key was already held
- later user validation falsified it: `PROGRA+z` and `PROGRA+END` then had no visible effect at all

Sixteenth slice completed:
- removed the unconditional live function-key workspace mirrors from `refresh_function_bits()`
- refined root cause: publishing synthetic function state directly into `0x4580` / `0x45BD` / `0x45BE` outside the real CLA timing is not hardware-faithful and could explain why SMILE briefly applies then cancels simultaneous `PROGRA+ordinary` input
- build and DX0 boot still work after removing the live function mirrors

Seventeenth slice completed:
- intermediate hypothesis only: narrowed the `?GETFO` override to a staged-byte-first policy
- later user validation falsified it: the old repeat bug returned and function keys again echoed regular characters

Eighteenth slice completed:
- restored the previous always-function `?GETFO` override policy
- refined root cause: staged-byte-first `?GETFO` is not compatible with the current keyboard pipeline and reintroduces known regressions before it fixes SMILE
- build and DX0 boot still work after rolling back the failed `?GETFO` change

SDL mapping (current):

|Key|SDL scancode|Host key|
|---|---|---|
|CURSOR|`SDL_SCANCODE_F1`|F1|
|COPY|`SDL_SCANCODE_F2`|F2|
|KILL|`SDL_SCANCODE_F3`|F3|
|PROGRA|`SDL_SCANCODE_F4`|F4|
|SHOW|`SDL_SCANCODE_F5`|F5|
|SEARCH|`SDL_SCANCODE_F6`|F6|
|CHANGE|`SDL_SCANCODE_F7`|F7|

Host arrow-key aliases (current):

|Smaky chord|SDL scancode|Host key|
|---|---|---|
|`CURSOR+r`|`SDL_SCANCODE_UP`|Up Arrow|
|`CURSOR+d`|`SDL_SCANCODE_LEFT`|Left Arrow|
|`CURSOR+f`|`SDL_SCANCODE_RIGHT`|Right Arrow|
|`CURSOR+c`|`SDL_SCANCODE_DOWN`|Down Arrow|

Decision for this branch:
- keep the seven bottom-row function keys on `F1`..`F7` only
- use the canonical `FLO.ST` bit values for those seven keys
- do not add alternate host mappings such as `LCTRL`, `LALT`, `RALT`, `LGUI`, `Home`, `End`, or `Insert`

**Category 2 — regular FIFO keys with special codes** ✅ Done

These keys send a code through the normal FIFO path (same as letters/digits).
Codes confirmed from doc section 10.4, page 213 (octal):

|Key|Octal|Hex|Glyph|Host key|Note|
|---|---|---|---|---|---|
|MACRO|`036`|`0x1E`|`«`|F8|French opening guillemet|
|DEF(INE)|`037`|`0x1F`|`»`|F9|French closing guillemet|

Implemented in `KEY_TABLE[]` in `keyboard.c`:

```c
{ SDL_SCANCODE_F8,  0x1E },   /* MACRO  → «  */
{ SDL_SCANCODE_F9,  0x1F },   /* DEFINE → »  */
```

**REP / FNCT** shows code `0` in the doc — this is the hardware repeat / function
modifier key.  It generates no independent code; skip for now.

### Swiss-French accented characters and "Mise en page" codes  [confirmed from doc p.214/p.215]

**"Mise en page" (formatting) control codes** — doc section 10.4, page 215:

These codes are in the standard ASCII C0 range and behave as formatting / color-select
controls for the display and printer.  `0x0E` and `0x0F` are dual-purpose: they act
as color-switch codes in a formatting context, but the chargen ROM also maps `0x0F`
to the ü glyph when used as a display character (see table below).

|Oct|Hex|Name|Meaning|
|---|---|---|---|
|000|`0x00`|NUL|null (also listed under "Special")|
|007|`0x07`|BEL|bell — triggers the speaker (also "Special")|
|011|`0x09`|TAB|horizontal tab — mapped to `SDL_SCANCODE_TAB` ✅|
|012|`0x0A`|LF|line feed|
|013|`0x0B`|VT|vertical tab|
|014|`0x0C`|FF|form feed|
|015|`0x0D`|CR|carriage return|
|016|`0x0E`|Red|switch to red ink/color|
|017|`0x0F`|Black|switch to black ink / also ü glyph|
|033|`0x1B`|ESC|escape (printer/serial control prefix — also "Special"; chargen *displays* this as ä)|

**"Communication Requests"** — serial/data-link control codes (doc p.215).
Left column listed in hex, right column in octal.
Glyph shapes read directly from `roms/chargen.rom` (stride 16 bytes/char, 8 rows used):

|Oct|Hex|Name|Standard meaning|Chargen glyph (from ROM)|
|---|---|---|---|---|
|001|`0x01`|SOH|Start of heading|empty rectangle (box outline)|
|002|`0x02`|STX|Start of text|box with small mark above it|
|003|`0x03`|ETX|End of text|decorative cross / diamond|
|004|`0x04`|EOT|End of transmission|`<` left-pointing chevron|
|005|`0x05`|ENQ|Enquiry|`>` right-pointing chevron|
|006|`0x06`|ACK|Acknowledge|letter `A` shape|
|025|`0x15`|NAK|Negative acknowledge|ê|
|026|`0x16`|SYN|Synchronous idle|ï|
|027|`0x17`|ETB|End transmission block|î|
|030|`0x18`|CAN|Cancel|ô|
|031|`0x19`|EM|End of medium|ù|
|032|`0x1A`|SUB|Substitute|û|

Also confirmed from ROM:

|Oct|Hex|Chargen glyph|
|---|---|---|
|0136|`0x5E`|`^` circumflex accent (top 3 rows of A, no bar/legs)|
|0177|`0x7F`|solid filled block ▓ (7 rows of `0x7E`)|

|Oct|Hex|Name|Chargen glyph (same byte, display context)|
|---|---|---|---|
|034|`0x1C`|FS|ö|
|035|`0x1D`|GS|ç|
|036|`0x1E`|RS|« (= MACRO key code)|
|037|`0x1F`|US|» (= DEFINE key code)|

**"DC" paper-tape reader/punch controls** — doc p.215, codes in **octal**:

|Oct|Hex|Name|Meaning|Chargen glyph|
|---|---|---|---|---|
|021|`0x11`|DC1|Reader on|â|
|022|`0x12`|DC2|Aux on|é|
|023|`0x13`|DC3|Reader off|è|
|024|`0x14`|DC4|Aux off|ë|

**Summary of dual-use principle:** the entire range `0x0F–0x1F` (and a few others such
as `0x00`, `0x07`, `0x08`, `0x09–0x0D`) carries two meanings depending on context:
as a **control/format code** when interpreted by the OS, printer driver, or serial
handler; and as a **Swiss-French display glyph** when rendered by the chargen ROM.
The emulator must honour both: pass the raw byte to the chargen for display, and
let the OS/SAMOS handle the control semantics.

**"Correction" codes** — doc section 10.4, page 215:

|Oct|Hex|Name|Meaning|
|---|---|---|---|
|010|`0x08`|BS|backspace — mapped to `SDL_SCANCODE_BACKSPACE` ✅|
|177|`0x7F`|DEL|delete-forward — mapped to `SDL_SCANCODE_DELETE` ✅; also the filled-block glyph in chargen|

Note on `0x1B` dual use: when written to the display it renders the ä glyph (Prom
2716 chargen mapping); when sent to the printer or serial port it acts as an escape
sequence prefix.  This is why `SDL_SCANCODE_ESCAPE` must **not** be mapped to `0x1B`
— the top-left UNDO/ESC key is now treated with the current working code `0x06`,
while the older `0x04` / `0x05` CLI interpretation remains under re-audit.

**ESC / UNDO key** ✅ Working `0x06` mapping / 🔲 Runtime reconciliation

Older CLI notes had interpreted two separate codes for cancel/recall:

- `0x04` (EOT `<`) — cancel/clear the current command line
- `0x05` (ENQ `>`) — recall the previous command

The emulator now maps `SDL_SCANCODE_ESCAPE` to `0x06` as its current working
hardware code. The remaining task is the same one called out above: reconcile
live CLI behaviour against that switch and decide whether the older `0x04` /
`0x05` interpretation belongs to a different physical key path.

**Emulator-side auto-repeat fixes** ✅ Done (commit `0ec70d7`):

1. Enter (`0x0D`) never arms SAMOS ISR Stage 4 auto-repeat (`0x4558`/`0x4577`),
  preventing a spurious stray-Enter loop at the next prompt.
1. Physical keystrokes have bit 7 set in the FIFO entry; injected characters
  (inject-str, etc.) do not. Auto-repeat is only armed for physical keys.

#### TODO — ESC recall convenience feature (not yet implemented)

SAMOS native `0x05` recall is unreliable: the line editor overwrites `0x45C0[0]`
with the cursor `'-'` when initialising a new input session, erasing the previous
command before the recall handler can read it.

Implement emulator-side recall instead:

1. Add `uint8_t prev_cmd[128]; int prev_cmd_len;` to `struct kbd` in
   `machine_internal.h`.
1. In `keyboard_event()`, when Return (`0x0D`) is pressed on a non-empty line,
   snapshot `m->bus[0x45C0 .. (0x7014)−1]` into `kbd.prev_cmd` (before the
   Enter is delivered to the FIFO so the buffer is still intact).
1. When ESC is pressed on an empty CLI prompt (detected via
   `machine_cli_prompt_visible()`), push each `kbd.prev_cmd[i]` byte into the
   FIFO **without** bit 7 (no auto-repeat).  SAMOS echoes the characters and
   leaves the cursor at the end of the recalled line, ready for editing.
   Add `#include "machine.h"` to `keyboard.c` for the detection call.

**Keyboard status summary:** the main keyboard bring-up is complete. Remaining
work is limited to three narrower items:

1. ESC / UNDO runtime reconciliation (`0x06` hardware mapping vs older `0x04` /
  `0x05` CLI interpretation).
1. Optional emulator-side ESC recall convenience feature.
1. Completeness work for remaining audited host-position mappings / broader
  FNCT/ALT-layer printable exposure.

**Chargen glyph block `0x0F–0x1F`** — the keyboard EPROM (Prom 2716) maps this range
to Swiss-French glyphs instead of the standard ASCII C0 control codes.  The full
mapping (octal → hex → character), confirmed from doc section 10.4, page 214:

|Oct|Hex|Char|
|---|---|---|
|017|`0x0F`|ü|
|020|`0x10`|à|
|021|`0x11`|â|
|022|`0x12`|é|
|023|`0x13`|è|
|024|`0x14`|ë|
|025|`0x15`|ê|
|026|`0x16`|ï|
|027|`0x17`|î|
|030|`0x18`|ô|
|031|`0x19`|ù|
|032|`0x1A`|û|
|033|`0x1B`|ä ← (not ESC!)|
|034|`0x1C`|ö|
|035|`0x1D`|ç|
|036|`0x1E`|«|
|037|`0x1F`|»|

Additional remaps outside the standard ASCII printable range (same page):

|Oct|Hex|Char|Note|
|---|---|---|---|
|043|`0x23`|`#`|same as ASCII|
|133|`0x5B`|`[`|same as ASCII|
|134|`0x5C`|`\`|same as ASCII|
|135|`0x5D`|`]`|same as ASCII|
|136|`0x5E`|`^`|circumflex accent (confirmed from ROM)|
|137|`0x5F`|`_`|underscore (shown as `–`)|
|140|`0x60`|grave accent|backtick|
|173|`0x7B`|`{`|same as ASCII|
|174|`0x7C`|vertical bar|same as ASCII|
|175|`0x7D`|`}`|same as ASCII|
|176|`0x7E`|`~`|same as ASCII|
|177|`0x7F`|▓|solid filled block (confirmed from ROM)|

**Note on character generator variants (doc p.214):** The table on that page compares
four ROM variants: Prom 2716, Versatec, Motorola 6571, and 74S262.  The Versatec
mapping is very close to the Prom 2716 (only minor differences in the upper range).
The Motorola 6571 and 74S262 are substantially different — do not use them as a
reference for the emulator.  The `roms/chargen.rom` image should correspond to the
Prom 2716 layout documented above.

**SDL mapping for accented keys:** Implemented in `keyboard_text_event()`. ✅
The function now decodes 2-byte UTF-8 sequences and looks them up in `ACCENT_TABLE[]`
(Unicode codepoint → Smaky 7-bit code).  Single-byte printable ASCII (0x20–0x7E) is
handled as before; 3+ byte sequences are skipped gracefully.  Both lowercase and
uppercase Unicode variants map to the same Smaky code (keyboard is uppercase-only).

Mapped accents (doc §10.4 p.214):

|Unicode|Smaky|Char|
|---|---|---|
|U+00FC|0x0F|ü|
|U+00E0|0x10|à|
|U+00E2|0x11|â|
|U+00E9|0x12|é|
|U+00E8|0x13|è|
|U+00EB|0x14|ë|
|U+00EA|0x15|ê|
|U+00EF|0x16|ï|
|U+00EE|0x17|î|
|U+00F4|0x18|ô|
|U+00F9|0x19|ù|
|U+00FB|0x1A|û|
|U+00E4|0x1B|ä|
|U+00F6|0x1C|ö|
|U+00E7|0x1D|ç|

---

## Display / Video

### ~~Lowercase character support~~ ✓ DONE

The chargen ROM already has lowercase glyphs (0x61–0x7A).  The fix was on the
keyboard input side: printable host text now enters through `keyboard_text_event()`
via `SDL_TEXTINPUT`, while Backspace, Tab, Return, function keys, and other
non-text keys stay on the strict scancode-to-matrix path.  The host OS applies
shift / Caps Lock so `a` and `A` arrive as intended for the active host layout,
and the text event is converted into one transient Smaky keycode without binding
printable input to a specific host physical key position.

### ~~Display-off mode~~ ✓ DONE

Writing `0x00` to port `0x00` now blanks the machine area (black pixels).
Port writes with bit 0 = 1 re-enable the display.  The status bar remains
visible in both states.  Implemented via `vid.display_on` flag.

### ~~CRT phosphor colour option~~ ✅ Done

Add a `-phosphor <colour>` CLI option (and a matching control in the launcher)
to select the screen palette:

|Value|Lit colour|Background|Real-world CRT|
|---|---|---|---|
|`green`|`#00E700`|`#000800`|**Default** — P31 green phosphor (current)|
|`white`|`#E8E8E8`|`#080808`|White phosphor — some Smaky 6 units shipped with this|

#### Implementation

1. ~~Add two `uint32_t` constants to `video.h` for the white palette
   (`VIDEO_COLOR_LIT_WHITE`, `VIDEO_COLOR_BG_WHITE`), keeping the existing
   green constants as-is.~~ ✅
1. ~~Add a `phosphor` field (enum or int) to `struct vid` in `machine_internal.h`.~~ ✅ (`PhosphorColour` enum)
1. ~~In `video_render_frame()`, select `LIT`/`BG` based on `m->vid.phosphor`.~~ ✅
1. ~~Add `machine_set_phosphor(m, phosphor)` in `machine.c` / `machine.h`.~~ ✅
1. ~~Parse `-phosphor green|white` in `main.c`; call `machine_set_phosphor()`.~~ ✅
1. Wire to the **Phosphor colour** drop-down in the launcher (already stubbed
   in the launcher TODO above).

### ~~CRT phosphor remanence (P31 persistence)~~ ✅ Done

The SAMOS 50 Hz ISR turns the display off for one full frame each second frame
(25 Hz blink), causing a visible flicker.  A per-pixel float persistence buffer
now simulates P31 phosphor decay, blending between the current lit state and
the previous frame value with a configurable decay factor.

#### CLI options

|Flag|Effect|Default|
|---|---|---|
|`-phosphor-decay <0.0..0.99>`|Set per-frame decay factor (0=instant, ~0.70≈P31 medium)|`0.70`|
|`-no-phosphor`|Disable persistence buffer entirely|—|

**Implementation:** `vid.phosphor_buf` (heap float array, `VIDEO_PX_W × VIDEO_ASPECT_H`)
allocated in `video_init()`, freed in `video_fini()`.  `video_render()` runs a
post-processing pass: lit pixels → `pb[i]=1.0f`; unlit → `pb[i] *= decay`;
output colour interpolated between BG and LIT.  `video_set_phosphor_decay()` /
`machine_set_phosphor_decay()` API added.  `PHOSPHOR_DECAY_DEFAULT = 0.70f`
defined in `video.h`.

---

## Floppy

### Write support

The floppy controller is **read-only**.  `floppy_read_data()` streams sector bytes
from the `.dsk` image; `floppy_write_cont()` only handles head-seek stepping.
There is no path that writes sector data back to the image file — port `0x1B` writes
(which the OS uses to send data bytes to the controller) are silently ignored.

#### Preferred approach — copy-on-write RAM buffer (non-destructive mode)

1. At image load time, `malloc` a shadow buffer equal to the full image size and
   `fread` the entire `.dsk` into it.  All `floppy_read_data()` calls serve bytes
   from the shadow buffer instead of directly from the file.
1. Add a write-phase state machine in `floppy_write_cont()` / a new
   `floppy_write_data()` handler (port `0x1B` OUT) that collects 256 bytes +
   checksum and patches the correct offset **in the shadow buffer only** —
   the on-disk image is never touched during the session.
1. Track a `dirty` flag in the FDC state (`fdc.dirty`); set it on the first write.
1. On emulator exit, if `dirty` is set, present a save prompt via SDL message box
   (`SDL_ShowMessageBox`) or a CLI `y/N` question so the user can choose to
   flush the shadow buffer to disk (overwrite the original image), discard
   changes, or save to a new file (keeping the original intact).

An optional `-write-through` flag could bypass the buffer and write directly to
the image file (the simpler `r+b` / `fseek` / `fwrite` path), for users who
prefer permanent writes.

### ~~Second floppy drive (DX1)~~ ✅ Done

The emulator supports two floppy drives. Mount via `-floppy <img>` (DX0) and
`-floppy2 <img>` (DX1). Drive selection uses port `0x19` (IC7 LS475, Plan F5):
DRISEL1 (bit 5 = `0x20`) selects DX0, DRISEL2 (bit 6 = `0x40`) selects DX1;
the latch updates whenever MOTORON (bit 3) is asserted. Stepping and sector reads
respect `fdc.selected_drive`. The status bar shows `DX0:` and `DX1:` labels.

---

## ~~Winchester / Hard Disk~~ ✅ Done

### WD1000/WD1001/WD1002-compatible controller emulated in `src/winchester.c` / `src/winchester.h`

Port map (ports `0x20–0x27`, `0x2B`; 6-bit mask `port & 0x3F`):

|Port|R/W|Description|
|---|---|---|
|`0x20`|R|Data register (IN) — read next sector byte|
|`0x20`|W|Data register (OUT) — write sector byte (stub, discarded)|
|`0x21`|R|Error register — `0x00` = no error|
|`0x21`|W|Write pre-compensation (ignored)|
|`0x23`|W|Sector number register — bits[4:0], 0-based|
|`0x24`|W|Cylinder low byte|
|`0x25`|W|Cylinder high byte|
|`0x26`|W|SDH — bits[2:0]=head (0–5), bit[3]=drive select (0/1)|
|`0x27`|R|Status — `0xFF`=no image, `0x50`=RDY+SC, `0x58`=RDY+SC+DRQ|
|`0x27`|W|Command — `0x1n`=RESTORE, `0x2n`=READ SECTOR, `0x3n`=WRITE(stub), `0x7n`=SEEK|
|`0x2B`|W|Unknown — no-op|

Geometry (confirmed from Phantom ROM disassembly at `0x0370–0x0398`):

- **6** heads/cylinder, **32** sectors/track, **256** bytes/sector
- CHS→LBA: `cyl×192 + head×32 + sec`; `DE` in Phantom ROM = LBA

Disk images mounted via CLI: `-harddisk <img>` (drive 0), `-harddisk2 <img>` (drive 1).

Status without image → `0xFF` (BSY forever → *Disque inactif* message on boot).
WRITE SECTOR command is a stub (bytes counted but discarded).

---

## USART / Serial

`usart.c` is a bare stub.  The 8251 devices on ports `0x04/0x05` (permanent I/O)
and `0x06/0x07` (cassette) return fixed ready-bits only.  No actual byte transfer.

The paper-tape bootstrap path (ROM `0x046D–0x04C1`) is therefore also non-functional
from the USART side, though it is unreachable under normal boot anyway.

---

## ~~RTC (Real-Time Clock)~~ ✅ Done

### Hardware confirmed from extension board schematic (R. Forster, Oct 1979)

Chip **E405/08** (IC5), port **0x08** R/W.  Proprietary 3-wire synchronous
bit-bang serial protocol (not SPI — predates the standard).  Bit3=CK,
bits1–2=CS/direction control (held high during transaction), bit0=bidir data.
Command phase: 4 bits LSB-first (0x0F=read, 0x07=write).  Data phase: 7 BCD
bytes LSB-first per byte.

Register layout confirmed empirically from SAMOS display output:

|Byte|Content|Range|SAMOS field|
|---|---|---|---|
|0|hours|00–23|time **hh**|
|1|minutes|00–59|time **mm**|
|2|day|01–31|date **DD**|
|3|month|01–12|date **MM**|
|4|year|00–99|date **YY**|
|5|weekday|1–7|day name (1=Mon…7=Sun)|
|6|seconds|00–59|time **ss**|

Implemented in `src/rtc.c` / `src/rtc.h`:

- Seeded from host `localtime()` on start
- Frame tick advances seconds every 50 frames (50 Hz)
- Full read/write protocol emulated (command nibble decode, falling-edge MISO
  preload, LSB-first accumulation on write)

---

## ~~Sound / Beeper~~ ✅ Done

Port `0x03` bit 0 drives the buzzer.  Each write calls `sound_set_bit()` which
maps the current T-state frame position (`m->snd.frame_base + m->cpu.cycles`)
to a sample index and fills a per-frame `int16_t[882]` buffer with the previous
level.  `sound_end_frame()` (called by `machine_run_frame`) flushes the frame
buffer to SDL via `SDL_QueueAudio` (push mode — no callback thread race).

This gives sample-accurate buzzer reproduction: a software loop toggling port
0x03 at 2500 Hz produces a 2500 Hz square wave in the audio output.

---

## PSG sound-card add-on  🚧 Planned

Rare side-bus expansion card for the Smaky 6.  The provided schematic shows:

- four `AY-3-8910` PSG chips
- direct connection to the side `MUBUS` connector
- shared `DA0..DA7` data lines plus `AD0..AD5` address lines
- bus/control signals including `WRITELOW` and `RESETLOW`
- discrete decode / glue logic built from `74LS138` and `74LS00` / `74LS32`

### Hardware reverse-engineering / documentation

- Derive the exact MUBUS-visible register map from the schematic: which
  address combinations select which AY chip, and how the decode logic drives
  each chip's `BC1`, `BC2`, and `BDIR` pins.
- Confirm whether the card is write-only from the host point of view or
  whether any AY register reads are possible / used by software.
- Identify the effective PSG clock source and divisor from the card so tone /
  noise / envelope timing matches the real hardware.
- Treat the AY parallel I/O ports as disconnected for the first
  implementation unless SIGMA proves otherwise.
- Document whether the card uses any additional MUBUS handshake behavior
  (`NOTREADYLOW`, interrupts, bus wait states) or is just a normal write-only
  peripheral.

### Emulator architecture

- Add a dedicated PSG add-on device model instead of folding the card into the
  existing beeper helpers.  The card is optional hardware hanging off MUBUS,
  not part of the base machine speaker path.
- Keep the existing SDL push-audio pipeline in `src/sound.c`, but add a second
  synthesized source path for the PSG card and mix it with the beeper / drive
  sounds at frame end.
- Add explicit card enable / disable configuration so the machine can still run
  as a stock Smaky 6 without the expansion installed.
- First target: register-correct and audibly correct output.  Exact cycle- or
  analog-level matching can follow later if needed.
- Use a vendored small MIT-licensed core instead of writing a fresh AY engine
  from scratch.  Current preferred candidate: `emu2149`.

### PSG device implementation

- Add card state for four AY chips: register file, selected register latch,
  tone/noise counters, envelope state, mixer bits, and per-chip output levels.
- Implement host-visible bus writes according to the decoded `BC1` / `BC2` /
  `BDIR` behavior: register-address latch, data write, and any valid read path
  if the hardware actually supports it.
- Reset all PSG state from the card reset path driven by `RESETLOW`.
- Generate the three tone channels, noise generator, and envelope generator for
  each AY, then mix all four chips into one mono output path first.
- Start with a faithful mono mix that matches the physical card; only add
  synthetic stereo placement later if there is hardware evidence for it.

### Bus integration

- Hook the card into the machine's MUBUS-visible I/O path at the exact port /
  address decode point derived from the schematic.
- Current SIGMA software evidence strongly suggests four PSG port pairs:
  `0x20/0x21`, `0x22/0x23`, `0x24/0x25`, and `0x26/0x27`, with the odd port
  acting as register select and the even port acting as data.
- Keep the owning abstraction local: bus decode in the machine / I/O layer,
  AY behavior inside the PSG card implementation, final PCM mix in `sound.c`.
- Add targeted tracing for PSG register writes and chip-select decisions so the
  first software bring-up can be debugged without broad audio logging.

### UI / configuration / docs

- Add CLI options to enable the PSG card and, if useful, to select a stricter
  hardware mode versus a developer-forced mode for testing.
- Extend the launcher sound section once the backend exists so users can see
  whether the PSG card is installed, without cluttering the base-machine path.
- Document the hardware, configuration flags, software expectations, and any
  known limitations in `README.md`, the emulator guide, and a dedicated dev note
  under `docs/dev/`.

### Validation

- Build a minimal host-side PSG test plan: write-register smoke tests, known
  tone-period checks, envelope checks, and noise-generator checks.
- Add a deterministic non-interactive test that writes a short PSG register
  sequence and verifies chip state or rendered sample hashes / bounds.
- Use `floppies/SIGMA.dsk` / the SIGMA software as the first real-software
  validation target for the card.
- If possible, capture reference audio or register traces from real hardware to
  validate the chosen clock, mixer scaling, and write semantics.

### Current implementation assumptions

- Use `SIGMA.dsk` as the first real-software bring-up target.
- Keep the card optional behind an explicit enable flag; the default machine
  remains a stock Smaky 6.
- First version target is register-correct plus audibly correct output.
- Ignore the AY parallel I/O ports initially unless the software proves they
  are needed.
- Only the schematic and SIGMA software are currently available as hardware /
  software references.
- Use a vendored small MIT-licensed AY/YM2149 core.  Current preferred
  candidate: `emu2149`.

---

## MAME / FPGA

- **Phase 2**: MAME driver integration (not started)
- **Phase 3**: MiSTer FPGA RTL implementation (not started)

---

## ~~Web / Emscripten~~ ✅ Done

Browser-playable build of the emulator via Emscripten/WebAssembly.
See [web/README.md](web/README.md) for build and serving instructions.

### What works

- **SDL2** — Emscripten built-in port (`-s USE_SDL=2`); no source changes needed.
- **Z80 / Zeta** — pure C, no platform deps; compiles as-is.
- **`tinyfiledialogs` guarded** — `#ifdef __EMSCRIPTEN__` skips the native dialog;
  replaced by JS `<input type="file">` + `FileReader` callback into the virtual FS.
  `EMSCRIPTEN_KEEPALIVE` C functions `smemu6_pick_done()` / `smemu6_pick_cancel()`
  receive the result from JS.  No SDL thread needed on the web.
- **Main loop** — extracted into `main_loop_iter()` with a `MainLoopCtx` struct;
  `emscripten_set_main_loop(main_loop_iter, 0, 1)` replaces `while (running)`.
- **Launcher skipped** — `-no-launcher` is forced on Emscripten; the HTML shell
  provides equivalent controls.
- **ROM preloading** — `--preload-file roms@/roms` (and optionally `floppies`)
  embedded into `smemu6.data` at build time.
- **CMake integration** — `cmake/Emscripten.cmake` toolchain + `CMakePresets.json`
  "web" preset.  Build with `cmake --preset web && cmake --build build-web`.
- **Host-only ST export helpers skipped on web** — the archived `.ST` symbol
  exporter utilities remain native-only build-time tools and are now excluded
  from the Emscripten target graph so `build-web` does not try to execute wasm
  helper binaries during header generation.
- **HTML shell** — `web/index.html` custom Emscripten shell with green-phosphor
  styling, DX0/DX1 file-load buttons, reset button, dedicated Smaky function-key
  buttons, fullscreen, and stderr log.

### Web remaining work

- **Responsive layout for all screen sizes** ✅ Done — `web/index.html` now uses
  viewport-capped shell widths, full-window canvas sizing for the complete
  `512 × 508` machine view, `clamp()`-based UI typography, narrow-screen
  breakpoints that stack the front-panel controls below the screen, and 44 px
  minimum touch targets for buttons, toggles, and selects.
- **Bundled floppy library** ✅ Done — the web build now preloads the repository
  `floppies/` directory into `/floppies`, the launcher populates DX0 / DX1 dropdowns
  from `FS.readdir('/floppies')` before startup, `Demo_ABC.dsk` is preselected for DX0,
  and clicking **Start** routes the selected bundled image through the existing
  `-floppy` / `-floppy2` startup path. The existing **Use own…** file picker still
  stages user-supplied `.dsk` files into the same virtual filesystem before boot.
- **Touch-device on-screen keyboard button** ✅ Done — the running web front panel
  now shows a dedicated **Keyboard** button on coarse-pointer devices. It focuses
  a hidden editable element instead of relying on canvas focus alone, which gives
  mobile browsers a real text target and lets phones/tablets open the system
  on-screen keyboard after the emulator has started.
- **SDL launcher in browser** — `launcher_run()` is a blocking event loop and
  cannot run as-is under Emscripten.  To enable it: refactor into
  `launcher_init()` + `launcher_frame()` (called from `emscripten_set_main_loop`);
  when the user clicks Start, cancel the launcher loop, apply the `LauncherConfig`,
  and start the main emulator loop.  `tinyfiledialogs` would be replaced by an
  HTML `<input type="file">` picker (already guarded).  Remove the
  `no_launcher = 1` override in `main.c` for Emscripten once done.
- **Hot-mount / floppy hot-swap** — loading a disk image after the emulator has
  started currently requires a page reload. Implement live disk swapping without
  reset.
- Export `smemu6_eject(int drive)` and `smemu6_mount(int drive)` as
  `EMSCRIPTEN_KEEPALIVE` C functions (mirroring the desktop eject/load helpers
  planned in the hot-swap TODO above).
- In `web/index.html`, add an **Eject** button and a hidden `<input type="file">`
  per drive slot (DX0 / DX1). Clicking Eject calls `smemu6_eject(n)`; the drive
  LED goes dark. Clicking the slot (or a **Load disk…** button) triggers the
  file input; `FileReader` reads the selected `.dsk` file into the Emscripten
  virtual FS and calls `smemu6_mount(n)` with the virtual path, mounting it live.
- The drive label and LED in the HTML status area update to reflect the new image.
- Remove the `no_launcher = 1` override in `main.c` for Emscripten once the SDL
  launcher is also ported, so disk selection can happen before boot too.
- **COOP/COEP headers** — hosting requires both
  `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` for `SharedArrayBuffer` (not
  needed for sound or the CPU loop; only needed if SDL threads are ever used).
- **Floppy write support** — see Floppy section above; same gap applies to web.

---

## Distribution / CI

### ~~macOS .dmg via GitHub Actions~~ ✅ Done

`release.yml` builds a `.dmg` on `macos-latest` using `brew install sdl2 create-dmg` and `dist-mac.sh`, triggered on version tag push.

### ~~Windows zip via GitHub Actions~~ ✅ Done

`release.yml` cross-compiles on `ubuntu-latest` with MinGW-w64, downloads the SDL2 MinGW dev package, runs `dist-win.sh`, and produces a `.zip` artifact.

#### Windows release remaining work

- Create a Windows installer (e.g. NSIS or Inno Setup) in addition to the plain zip, so users get a proper install/uninstall experience

### ~~Linux AppImage + Debian package via GitHub Actions~~ ✅ Done

`release.yml` builds both an `.AppImage` (via `dist-linux.sh` / linuxdeploy) and a `.deb` (via `dist-deb.sh`) on `ubuntu-latest`.

### ~~GitHub Release asset upload~~ ✅ Done

The `release` job in `release.yml` collects all platform artifacts and creates a GitHub Release with `gh release create --generate-notes`, attaching `.AppImage`, `.deb`, `.zip`, and `.dmg`.

### ~~Web / GitHub Pages deployment~~ ✅ Done

`pages.yml` builds the Emscripten web target on every push to `master` using `mymindstorm/setup-emsdk@v14` (SDK 3.1.6) and deploys to GitHub Pages. Live at <https://sch-lika.github.io/smemu6/>.

### Experimental SDCC scaffold not yet in CI

- A first standalone SDCC proof-of-execution scaffold now exists under
  `sdcc/examples/hello_alpha` with helper scripts
  `sdcc/build_smaky6_sdcc_example.sh` and
  `sdcc/run_smaky6_sdcc_example.sh`.
- Current scope is intentionally narrow: ordinary `.SM`, `flags=1`,
  `load=entry=0x6000`, and direct alpha-RAM output.
- **Status: KNOWN LIMITATION — Programs execute correctly but cannot cleanly return to CLI**.
  - Programs build, launch, and run correctly with proper output to alpha screen.
  - When `main()` returns, screen corruption occurs: rows 15-18 become garbled,
    row 19 loops with underscore repetition until timeout.
  - CLI prompt never reappears; user must manually quit via emulator termination.
  - This issue is pre-existing (confirmed across multiple investigation sessions)
    and appears to be an architectural mismatch between SDCC program state and
    SAMOS/CLI exit handler expectations at `0x56AE`.
- The attempted exit sequence (A=0x44, HL=0x45C0, jp 0x56AE) was previously
  claimed as "verified" but was later reverted as "false claims" (commits
  44d21f3 → 2dff971). Investigation confirms the exit path remains broken for
  SDCC programs while native `.SM` programs can exit cleanly, suggesting a
  fundamental incompatibility in how SDCC programs set up the exit state.
- The example now exposes a tiny reusable first-target header,
  `sdcc/examples/hello_alpha/smaky6.h`, for alpha-RAM write primitives.
- That header now also provides row/column helpers so future SDCC examples can
  address the alpha screen without open-coding raw offsets.
- The helper now also provides a minimal row-clear and whole-screen-clear
  helper so standalone SDCC probes start from a readable blank alpha plane.
- The helper scripts now accept an example directory name under
  `sdcc/examples/`, so new one-file SDCC probes can reuse the same
  build/stage/run path without script edits.
- The standalone build script can now also derive an SDCC-friendly macro header
  from archived `SM6.ST` evidence, and the first-target helper header consumes
  `SMAKY6_SM6_ALPHA` from that generated file when available.
- The standalone build script now also accepts example-local layout overrides
  through `sdcc/examples/<name>/layout.conf`, so focused probes can move the
  `.SM` load base without forking the shared build path.
- There is now a second standalone SDCC example under `sdcc/examples/sm6peek`
  that uses generated `SM6.ST` symbols and displays live reads from `OUTCAR`
  and `MAXMEM`.
- There is a third example under `sdcc/examples/sm6emitz` that probes callable
  SM6 routines by wrapping the documented `RST 20 / 0x06` zero-terminated
  string helper; execution is correct, but exit still exhibits the same
  corruption pattern as hello_alpha.
- A first standalone C-side dumper for `FLO.ST` / `SM6.ST` now exists at
  `sdcc/dump_smaky6_st_symbols.c`; it is useful for analysis but the exact
  6-byte symbol encoding is only partially confirmed.
- That parser is now factored into reusable C sources
  `sdcc/smaky6_st_symbols.h` / `sdcc/smaky6_st_symbols.c`, and a companion
  exporter can emit JSON or a generated C header for downstream use.
- Native CMake builds now also expose utility targets for the symbol-table
  tooling and a generated-header consumer example when the archived `SM6.ST`
  and `FLO.ST` files are present.
- **TODO: Resolve the SDCC program exit issue.**
  - Requires deeper understanding of SAMOS 0x56AE entry contract
  - May need disassembly of SAMOS or tracing of native .SM program exits
  - Consider if architectural workaround is needed (e.g., infinite loop, or launcher termination)
- Keep full CMake integration deferred until the exit issue is understood or
  explicitly accepted as a known limitation.

---

## Known Bugs

### ~~Function key buttons write characters to screen~~  ✅ Fixed

#### Root cause (confirmed from schematic doc 10.4)

When FOUND=0, CLA hardware returns `fonct_bits` with bit 7 clear.
SAMOS ISR Stage 1 stores `CLA & 0x7F = fonct_bits` to `0x4580` (GETFON register).
Stage 2 CLA Read #2 would echo `fonct_bits` as a character — but the old claim that this
path is permanently blocked by `0x4582=0x80` has been withdrawn pending re-audit.

**Fix — unified hardware-accurate CLA model** ✅ (see refactor item above)

- `keyboard_read_cla()` returns `fonct_bits & 0x7F` when no regular key is held.
- Stage 1 stores `fonct_bits` to `0x4580` automatically via `AND 0x7F; LD (0x4580),A`.
- Whether Stage 2 CLA Read #2 is reached post-boot is under re-audit; the old
  `0x4582=0x80` permanent-block explanation is no longer trusted.
- No `cla_seen` or `is_stage1` flag needed.

---

## Debugger

### Live CPU register window

An optional secondary SDL window (toggled with a key, e.g. F12 / Ctrl+D) showing
all Z80 registers updated every frame:

|Column 1|Column 2|
|---|---|
|AF / AF'|BC / BC'|
|DE / DE'|HL / HL'|
|IX|IY|
|SP|PC|
|I / R|IFF1 / IFF2 / IM|
|T-states this frame|Frame count|

Should also show the current disassembly around PC (5 lines back, 10 ahead)
using a simple Z80 disassembler (the `z80` / `zeta` dep may already expose one;
otherwise a minimal standalone table is ~200 lines of C).

### Memory editor

Hex editor panel (or a second region of the debug window) showing a 256-byte
view of any address range, with the ability to:

- Navigate with arrow keys / Page Up / Page Down
- Jump to an arbitrary address (hex input)
- Edit individual bytes in place (single keypress replaces nibble)
- Highlight ROM-protected ranges differently from writable RAM
- Show the alpha-plane or graphic-plane at a known offset for quick inspection

### Pause and single-step execution

- **Pause** (e.g. `Space` in debug window, or `F11` if not used for BREAK):
  suspends the Z80 between frames; the display keeps rendering (phosphor decays
  naturally while paused).
- **Step instruction** (`F6` or `S`): execute exactly one Z80 instruction, update
  registers, redraw debug window.
- **Step frame** (`F7`): run until the next 50 Hz frame boundary (one ISR cycle).
- **Run to cursor** (`F8`): execute until PC reaches the address highlighted in
  the disassembly view.
- **Breakpoints**: set/clear a breakpoint on any address; execution halts
  automatically when PC reaches it.  Store as a small fixed-size array
  (e.g. 16 breakpoints) in the debug struct.

#### Debugger implementation notes

- The existing `debug.c` / `debug.h` files already have `debug_toggle()` and
  `trace_kbd` / `trace_regs` flags.  Extend rather than replace.
- The debug window can be a second `SDL_Window` + `SDL_Renderer` created on demand;
  no extra dependencies needed beyond SDL2 + chargen ROM font.
- All breakpoint and step state lives in `struct dbg` (in `machine_internal.h`).
- The main loop already has a `freeze_cpu` flag; pause can reuse it.
- Single-step requires a new `step_pending` flag checked in `machine_run_frame()`:
  execute exactly one instruction then set `freeze_cpu=1` again.
