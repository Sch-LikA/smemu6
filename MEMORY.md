# MEMORY.md — Project Decision Log

Durable project rules, current architecture decisions, and recurring gotchas for
smemu6. Keep this short. Prefer the deeper technical docs under `docs/dev/`
for hardware details, reverse-engineering traces, and one-off investigations.

## Project Rules

- Update the relevant documentation before committing code changes.
- Commit after every source-code modification using `type: description`.
- Use the repo-local `tmp/` directory for logs, scratch files, and test output.
- Reuse already-extracted floppy contents when they exist instead of
  re-extracting the same `.dsk` again.
- Releases are automated from pushed tags. Do not propose manual GitHub release
  creation for this repo.
- Function-level documentation should explain behavior, side effects,
  assumptions, and hardware context when useful. Avoid comments that only
  paraphrase the function name.

## Build And Runtime Baselines

- Native build baseline: `cmake --build build --target smemu6`.
- Web build baseline on this Linux machine: use a repo-local
  `.emscripten-local` plus `.emscripten-cache`, then run web configure/build
  with `EM_CONFIG="$PWD/.emscripten-local"`.
- Local web smoke-test baseline: `tools/serve_web.sh` serves `build-web/` with
  the required COOP/COEP headers.
- For screen-output capture, use `-scrdump` together with `-no-display-off`.
  Either flag alone is not sufficient for the usual boot-to-prompt capture.
- `-no-launcher` is the fast path for emulator testing when the launcher UI is
  not under test.
- `-inject-str` fires when `machine_cli_prompt_visible()` reports that the CLI
  prompt is ready.
- DX0 boots automatically. Do not look for or reintroduce `-autoboot`.
- Use `-no-beeper` for routine automated runs.

## Current Architecture Decisions

### Web function keys use dedicated on-screen controls

Keep the native `F1`..`F7` mapping unchanged, but expose dedicated web controls
for CURSOR / COPY / KILL / PROGRA / SHOW / SEARCH / CHANGE.

Why:

- browsers and embedded webviews often reserve or swallow `F1`..`F7`
- the web UI needs reliable access to Smaky function keys after startup
- native semantics are preserved: left-press holds, release clears,
  right-click toggles a latch

### Clear host-owned ordinary key state on focus loss

When the SDL window loses focus, clear ordinary host-owned key state in the
keyboard layer rather than only resetting function bits.

Why:

- missing host key-up events during focus changes can otherwise leave ordinary
  keys latched and repeating indefinitely
- this matters especially in browsers, where focus transitions are frequent

### Mouse-latched function keys stay outside one-shot keyboard consumption

Keyboard-held function bits may be consumed one-shot for chord handling, but
mouse-latched status-bar or web-panel function bits remain persistent until the
user unlatches them.

Why:

- right-click function-key latches are a user-controlled UI state, not a
  transient keyboard scan event
- applying one-shot consumption to mouse latches breaks the intended hold/toggle
  behavior

### Held ordinary keys survive circular-buffer commits

Do not clear a physically held ordinary key just because SAMOS advanced the
keyboard circular-buffer write pointer and committed that same byte into the
buffer workspace.

Why:

- interactive programs such as SIGMA may still need the held key on the live
  CLA path after the buffered commit side effect
- collapsing the key to one-shot at `0x457C` advance causes the current
  selection to blink/reassert in place instead of moving
- one-shot cleanup still belongs only to explicitly buffered synthetic input via
  `release_after_buffer_commit`

### Reset must preserve the default caps-on keyboard layer

Keep the keyboard in its default caps-on layer across machine reset, and when
SDL text input supplies ordinary letters, resolve their final Smaky byte from
the matrix position and current layer rather than blindly trusting the host
text case.

Why:

- SIGMA menu navigation compares against uppercase `D/F/R/C` bytes, not the
  lowercase host text bytes
- the keyboard layer already models case through the Smaky matrix tables, so
  SDL text input should reuse that layer choice instead of bypassing it
- `keyboard_init()` alone was not enough because `machine_reset()` was
  immediately clearing `caps_lock_active` back to the lowercase layer


### The web build excludes archived ST export helper tools

Keep the archived `SM6.ST` / `FLO.ST` exporter utilities and generated-header
targets out of the Emscripten build graph.

Why:

- they are host-side archival/developer tools, not browser runtime dependencies
- trying to build and execute them under Emscripten breaks the web build for no
  runtime benefit

### Bootable hostdir media requires metadata sidecars

When building a bootable DX0 hostdir from a floppy image, use
`tools/extract_samos_image.py` so file metadata sidecars are preserved.

Why:

- plain extracted files are not enough for SAMOS boot media
- load address, entry point, flags, dates, and sector ordering matter

### PSG sound card stays optional and SIGMA is the first validation target

For the PSG branch, treat the add-on sound card as optional hardware behind an
explicit enable flag.  First implementation target is register-correct and
audibly correct output, not full cycle-accurate analog matching.

Why:

- the base machine should still run as a stock Smaky 6 when the add-on is not
  installed
- only the schematic and `SIGMA.dsk` are currently available as references, so
  software bring-up should focus on correct register behavior and plausible
  audio output first
- the AY parallel I/O ports can be ignored initially unless SIGMA proves they
  matter

### PSG core choice and first software-side port map

Use a vendored small MIT-licensed PSG core instead of writing a new AY engine
from scratch.  Current preferred candidate is `emu2149`; `ayumi` is also
license-compatible, but `emu2149` is closer to the register-driven integration
shape needed here.

Build integration choice: fetch `emu2149` through CMake `FetchContent`, pinned
to a specific upstream commit, and keep the Smaky-specific four-chip wrapper
local to this repo.

Why:

- both `emu2149` and `ayumi` are MIT-licensed, which is compatible with this
  GPL-3.0-or-later project when their copyright and license text are retained
- the user explicitly does not want to reinvent the wheel for the first PSG
  bring-up
- this repo already accepts pinned CMake-fetched third-party dependencies such
  as `Zeta` and `Z80`, so `emu2149` fits the existing build model better than a
  manual source copy
- current SIGMA software evidence points to four consecutive PSG port pairs:
  `0x20/0x21`, `0x22/0x23`, `0x24/0x25`, and `0x26/0x27`
- the observed SIGMA scripts suggest the odd port is register-select and the
  even port is data for each AY chip

### PSG and Winchester cannot currently coexist

When `-psg` is enabled, the emulator must give ports `0x20..0x27` to the PSG
card and reject `-harddisk` / `-harddisk2` at startup.

Why:

- existing Winchester emulation already owns decoded ports `0x20`, `0x21`,
  `0x23`, `0x24`, `0x25`, `0x26`, and `0x27`
- current SIGMA software evidence probes four PSG pairs across exactly the
  same decoded range: `0x20/0x21`, `0x22/0x23`, `0x24/0x25`, `0x26/0x27`
- allowing both silently would make the guest talk to two incompatible devices
  on the same I/O addresses and hide the real hardware conflict

### PSG measured default clock and audio path

Current PSG slice uses the measured `1.410 MHz` AY clock as its default,
allows manual override across the observed potentiometer range
`1.0 .. 2.4 MHz`, and mixes PSG output through the existing SDL audio frame
buffer.

Why:

- the visible bus connector pins in the PDF do not show an incoming clock pin,
  which already argued against the old host-clock assumption
- the KiCad netlist now resolves the direct AY clock driver: all four AY
  `CLOCK_22` pins share `Net-(U1-CLOCK)`, and that net is driven by `U16`
  pin `4`
- the same netlist shows `U16` is wired as a local Schmitt-trigger RC
  oscillator stage using `1nF1` to ground plus the `R6` / `RV1` resistor path;
  this replaces the earlier `U9`-centered guess as the best current hardware
  model for the AY clock source
- the resolved local topology is: `U16` pins `1`/`2` form the oscillator,
  with `U16 pin 1` tied to `1nF1` to ground and `RV1 pin 1`, `U16 pin 2`
  tied back to `U16 pin 3` plus `R6 pin 1`, `R6 pin 2` tied to `RV1 pin 2`,
  and `U16 pin 4` buffering that local oscillator onto the shared AY
  `CLOCK_22` net; `RV1 pin 3` is unconnected in the exported netlist
- `1nF1` is confirmed to be a fitted `1 nF` capacitor, so the remaining
  frequency uncertainty is centered on the actual `RV1` value/setting and any
  later effective division, not on the capacitor identity
- the visible bus connector pins in the PDF do not show an incoming clock pin,
  which further argues against the current host-clock assumption
- the wider top-left schematic crop shows `SW1` embedded in the `U8` (`74LS02`)
  and `U7` (`74LS32`) logic path that feeds the AY `CLOCK` backbone, so `SW1`
  is not currently supported as a simple selector between two standalone clock
  sources
- more specifically, the traced lower `SW1` branch runs directly into the
  `U8A` NOR-gate input path, while the other `SW1` branch continues down the
  same `U8`-side control trunk; this is now strong evidence that `SW1` is a
  logic/configuration input to the clock-shaping network rather than a direct
  raw-clock source selector
- real hardware was measured around `1.410 MHz`, which is now the emulator's
  default PSG clock instead of the older `2.41152 MHz` fallback
- the user also measured the potentiometer sweep at roughly `1.0 .. 2.4 MHz`,
  so the emulator now accepts that full range as an explicit override for
  SIGMA bring-up and A/B testing
- the remaining unknown is the realistic default `RV1` setting on power-up,
  not whether the card uses a local oscillator at all

### Integrated debugger is native-first and opt-in

disabled by default until the user presses `F12`.
Why:
- the native SDL build can open a second debugger window immediately with no
- the web build needs a dedicated HTML debugger panel rather than a second SDL
  abstraction
- keeping the debugger opt-in avoids changing normal emulator startup or
  cluttering the default runtime path

The current native debugger disassembly pane uses a small local decoder inside
`src/debug.c` instead of integrating `z80dasm` or another external tool.

Why:
- the debugger only needs display-oriented single-instruction decoding around
- keeping the decoder local avoids adding a new dependency, import path, and
  portability surface for both native and web builds
- the same local backend can later feed either the native SDL window or a web
  HTML debugger panel
### Integrated debugger step over uses Shift+F7 and the existing target-run path
Bind debugger step over to `Shift+F7` and implement it for `CALL`,
sequential PC. For other opcodes, fall back to a normal single-instruction

  ### 2026-05-25

  Worked on:

  - polished the integrated native debugger around stack/context visibility and
    low-risk navigation behavior
  - moved the compact stack-symbol hint out of the boxed STATE footer and into
    the disassembly header
  - made the stack hint actionable, then hardened the shared navigation rules
    with one more tiny helper/test slice
  - added lightweight current-symbol context so the live `PC` stays anchored to a
    nearby FLO routine name while stepping

  Completed:

  - moved the stack hint into the disassembly header in `9b8fea0`
    (`fix: move stack hint to disassembly`)
  - added direct `0`..`3` jumps from the stack hint into disassembly history in
    `61c70a1` (`feat: jump to stack hint targets`)
  - extracted shared debugger navigation gating and stack-target validity into a
    standalone helper/test in `b5e56d6`
    (`test: extract debug navigation helper`)
  - added live `PC` nearest-FLO-symbol context in the disassembly header in
    `129bb78` (`feat: show current flo symbol context`)

  In progress:

  - no open code change on the debugger right now; the remaining likely follow-up
    is a layout-only pass if the disassembly header feels crowded during longer
    interactive use

  Decisions made:

  - keep stack-symbol hints in the disassembly header, not inside the STATE box;
    see `Debugger UI polish stays execution-first` above
  - make stack hints actionable by reusing the existing disassembly follow-history
    path instead of creating separate stack-navigation state; see `Debugger UI
    polish stays execution-first` above
  - show current-symbol context as the live `PC`'s nearest FLO symbol plus offset
    in the header rather than adding another debugger pane; see `Debugger UI
    polish stays execution-first` above

  Next session priorities:

  - decide whether the disassembly header needs a spacing/layout cleanup after
    real debugger use
  - if debugger work continues, prefer another tiny helper/test slice only when a
    repeated rule appears again; otherwise keep changes directly in `src/debug.c`
step.

Why:

- it keeps the execution shortcuts clustered without widening the debugger UI
- reusing the existing run-to-cursor stop path avoids adding a second
  long-running execution mode
- limiting the first slice to call-like instructions matches the common
  debugger expectation without inventing ambiguous semantics for jumps or block
  repeat instructions
- `DJNZ` needs the same treatment in practice because a taken counted loop can
  legitimately return to the same disassembly row after `F6`, which looks like
  a no-op unless the user is watching register `B`

### Integrated debugger FLO annotations come from the checked-in symbol dump

The native debugger now consumes the generated FLO ST export header through the
build graph and uses the compiled-in table only for compact disassembly hints:
exact-address labels and followable control-flow target suffixes.

Why:

- the build already knows how to generate a FLO export header from `FLO.ST`, so
  reusing that path removes the debugger's runtime dependency on a separate
  symbol text file
- keeping the first slice display-only avoids coupling emulator execution to
  symbol availability
- exact-address and direct-target hints are usually useful, while dumping every
  matching low-memory constant into the pane would add noise
- CALM-threaded service calls are 2-byte vectors whose first byte is already
  the restart opcode (`D7`, `E7`, or `EF`) and whose second byte is the service
  code; decoding them as standalone 1-byte `RST` instructions produces bogus
  repeated rows and hides the symbol names

### Integrated debugger can follow selected control-flow targets

Allow the disassembly cursor to jump to the decoded destination of the selected
instruction when the user presses `Enter` on a direct `call` / `jp` / `jr` /
`djnz` / `rst` row.

Why:

- browsing is much faster when the user can follow the currently highlighted
  control-flow edge without manually retyping or scrolling to the target
- reusing one target-decoder helper keeps the navigation behavior aligned with
  the disassembly suffix hints instead of growing a second inconsistent decoder

Implementation note:

- keep a small disassembly-history stack so `Backspace` can jump back after a
  follow-target action instead of forcing manual reverse navigation
- keep the control-flow target decoding and step-over candidate rules in a tiny
  standalone helper with a dedicated regression test so navigation and stepping
  stay consistent
- keep the history stack push/pop policy in its own tiny helper with a
  dedicated regression test so cursor-history behavior can be validated
  independently of the full debugger UI
- show a compact selected-target summary in the top status strip so the user can see
  the follow destination before committing to `Enter`

### Integrated debugger disassembly centers the active focus

Keep the live `PC` row near the middle when the disassembly cursor is synced to
execution, but recenter the viewport around the selected disassembly cursor
while the user is browsing away from the live machine state.

Why:

- the previous slice only decoded a short window before the live `PC`, so

### 2026-05-30

Worked on:

- re-read the project workflow/docs, cut release tag `v1.3`, and checked why the
  web deployment did not refresh from that tag push
- traced the GitHub Actions split between desktop release artifacts and Pages
  deployment, then fixed the Pages trigger to include pushed `v*` tags
- cleaned the top-level TODO wording so the release/web deployment note now
  reflects the completed workflow behavior

Completed:

- created and pushed annotated tag `v1.3` on `3211ab1`
  (`fix: preserve sigma uppercase navigation keys`)
- committed the Pages workflow fix in `cebd78c`
  (`ci: deploy web pages on release tags`) and pushed `master`
- committed the TODO cleanup in `623ee91`
  (`docs: mark release web deploy note done`)

In progress:

- `v1.3` still points to the pre-workflow-fix commit, so the GitHub Release tag
  and the live web deployment may reference different commits until a later tag
  such as `v1.3.1` is cut
- local uncommitted workspace state still includes this `MEMORY.md` update and
  the untracked PDF `docs/Smaky 6 La programmation du Smaky I.pdf`

Decisions made:

- treat GitHub Pages deployment as a first-class release path: version tags
  should trigger the web rebuild in addition to the desktop release workflow
- keep the fix minimal by extending `pages.yml` triggers rather than merging
  the Pages build into `release.yml`
- do not fold unrelated existing `MEMORY.md` edits into the workflow-fix commit;
  keep release/workflow commits narrowly scoped

Next session priorities:

- if release parity matters, cut and push a follow-up tag so desktop release
  assets and the live web deployment point at the same commit
- otherwise continue with the highest-impact open TODO items: SDCC exit-path
  investigation, keyboard/SMILE follow-up validation, or floppy hot-swap/write
  support

### 2026-05-29

Worked on:

- finished the SIGMA inverse-selection investigation after the inverse-video
  renderer fix, focusing on why `r/d/f/c` only blinked the current block
- traced the keyboard path end-to-end with visible SDL validation, `-scrdump`
  inverse-mask rows, and targeted workspace logging
- narrowed the issue from renderer vs keyboard down to Sigma-side expectations
  for uppercase navigation bytes

Completed:

- confirmed and kept the inverse-mask `-scrdump` diagnostics and the held-key
  circular-buffer preservation work as the validated keyboard-debug floor
- rejected and reverted two wrong paths: removing the ordinary-key prefix and
  forcing `r/d/f/c` through a separate matrix-only route (`0dc929c`,
  `5bc653d`)
- identified from Sigma segment disassembly that the menu compares against
  uppercase `D/F/R/C`
- fixed ordinary SDL text letters to resolve through the Smaky layer table and
  preserved the default caps-on layer through machine reset in `3211ab1`
  (`fix: preserve sigma uppercase navigation keys`)
- validated the final fix visibly: plain host `f` now reaches Sigma as `0x46`
  (`C6` first read, then `46`) and moves the inverse selection block right

In progress:

- no open code change in this slice; only optional follow-up validation remains
  for the other Sigma directions (`d`, `r`, `c`) if needed

Decisions made:

- treat Sigma navigation as a keyboard-layer case problem, not a renderer
  refresh problem; the inverse mask already repaints correctly
- keep the default caps-on keyboard layer across reset so ordinary letters use
  the Smaky uppercase table unless the guest or user changes state
- when SDL text input is used for ordinary keys, resolve the final guest byte
  from the Smaky matrix position and active layer instead of trusting host text
  case

Next session priorities:

- if Sigma input work continues, verify the remaining directions (`d`, `r`,
  `c`) against the same final path
- otherwise move on; the current Sigma menu navigation regression is fixed and
  committed
  repeated `Shift+F6` presses could move the selected line outside the visible
  pane after only a few steps
- centering the live row makes the default paused view easier to read, while
  centering the moved cursor keeps reverse and forward browsing usable without
  constantly losing the selection

Implementation note:

- keep the row-window math in a tiny standalone helper with a dedicated native
  regression test so future debugger UI work can verify the centering rules
  without spinning up SDL rendering or full emulator state

### Debugger UI polish stays execution-first

Prefer small debugger UI improvements that make stop state and execution flow
clearer before adding new panes or symbolic features.

Why:

- a status strip, stop reason, register-delta highlighting, and clearer PC vs
  cursor rendering improve day-to-day debugging immediately
- these changes stay local to `src/debug.c` and the existing debugger state
- they avoid committing to a larger pane/layout model too early

Implementation note:

- keep footer separators out of the bitmap-font glyph area; even a 1-pixel line
  through the watch row is visibly noisy in the native debugger
- reuse the existing FLO symbol lookup to annotate top stack words in the STATE
  preview path, but place the compact symbol hint where it does not collide with
  box separators or footer lines
- when making stack hints actionable, reuse the existing disassembly
  follow-history path instead of adding a separate stack-navigation model;
  direct `0`..`3` jumps are enough for the top four stack words
- keep the shared debugger navigation gate and stack-target validity checks in
  a tiny standalone helper so follow-target, history-back, and stack-jump rules
  cannot drift apart silently
- keep current-symbol context lightweight by showing the live PC's nearest FLO
  symbol plus offset in the disassembly header instead of adding another panel

### Web debugger starts as a snapshot-driven HTML panel

For the first browser debugger slice, expose a small HTML panel inside the web
shell and feed it with exported debugger snapshot/control functions instead of
trying to mirror the native second SDL window.

Why:

- the web build already has a thin `Module.ccall` bridge, so a compact snapshot
  plus a few controls is the smallest useful browser debugger surface
- this keeps one debugger backend in `src/debug.c` instead of inventing a
  second independent JS-side debugger model
- it avoids overcommitting to full native-parity layout before the browser
  control/status shape is validated in real use

### CALM service catalog uses disassembly tables before later manuals

For Smaky 6 CALM documentation, treat `private/docs/disasm/SYS.SR` as the
authoritative source for the `SM6` runtime service map. The shipped symbol
tables also confirm that `SM6` is a strict name subset of `FLO`.

Why:

- `SYS.SR` already exposes a named `RST 20H` dispatch table with concrete
  `SM6` service codes, which is stronger evidence than later-manual guidance
- direct comparison of `sdcc/SM6.symbols` and `sdcc/FLO.symbols` shows all 251
  `SM6` names inside `FLO`, with 92 additional `FLO` exports
- the only mismatch found so far is `MINI` (`SM6=0001`, `FLO=0000`), which
  currently looks like a symbol-set constant difference rather than a runtime
  service incompatibility
- later CALM manuals are still useful hints, but for Smaky 6-era syntax and
  ABI details they must not override the local source corpus

## Known Limitations

### SDCC programs still do not return cleanly to the CLI

Current status:

- SDCC-compiled programs can run correctly but may corrupt the lower screen and
  fail to restore the CLI prompt on exit
- this is still a known limitation, not a resolved issue

Working baseline:

- use the CALM example `calm/examples/hello_smug/hello.sr` as the clean return
  baseline when comparing behavior

Implication:

- do not treat successful program output alone as proof that SDCC integration is
  correct; clean return-to-CLI behavior is part of the acceptance check

## Useful Pointers

- Hardware and reverse-engineering details belong in `docs/dev/`.
- Web-build usage and browser-specific behavior belong in `web/README.md`.
- Repeated failed approaches and recovery notes belong in `ERRORS.md`, not
  here.

## Session Summaries

### 2026-05-25

Worked on:

- traced the PSG board clock path from PDF crops, KiCad schematic coordinates,
  and finally the KiCad netlist
- corrected the earlier assumption that the AY clock might come from the host
  clock or directly from the `U9` / `R1` / `C1` block
- answered the narrower hardware question about `SW1`

Completed:

- documented the PSG clock trace direction in `84637fd` (`docs: record PSG
  clock trace direction`)
- synchronized the README clock wording with the revised hardware evidence in
  `2693df4` (`docs: refine PSG clock trace notes`)
- replaced the old direct-clock-source uncertainty with the netlist-backed
  `U16` driver result in `4b13f60` (`docs: trace PSG clock from netlist`)
- recorded the resolved `U16` oscillator topology in `b25d4c7`
  (`docs: record U16 oscillator topology`)
- recorded that `1nF1` is confirmed as `1 nF` in `e8b1c34`
  (`docs: note PSG cap value`)

In progress:

- the remaining hardware unknown is the fitted `RV1` value / default setting
  that corresponds to the measured `1.410 MHz` operating point
- any later effective division or shaping between the raw `U16` oscillator and
  the final AY clock still needs confirmation against runtime SIGMA behavior

Decisions made:

- use the measured `1.410 MHz` value as the default emulator PSG clock and
  expose the measured `1.0 .. 2.4 MHz` potentiometer sweep as a manual
  override; see `PSG measured default clock and audio path` above
- treat `SW1` as decode/configuration logic input, not as the direct AY clock
  source selector; see `PSG measured default clock and audio path` above

Next session priorities:

- determine the fitted `RV1` value or default knob position from BOM, board
  markings, or user confirmation
- correlate SIGMA runtime behavior against the new measured default and nearby
  `-psg-clock` overrides
- decide whether the emulator should later model the potentiometer as a named
  hardware control rather than only as a raw Hz override
