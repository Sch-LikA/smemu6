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

### PSG first clock assumption and audio path

Current PSG slice still uses a temporary fallback AY clock and mixes PSG output
through the existing SDL audio frame buffer.

Why:

- the PDF confirms the card has a local RC timing section around `U9`
  (`74LS00`) plus `R1` and `C1`, so the earlier “host clock” rationale is not
  reliable
- the visible bus connector pins in the PDF do not show an incoming clock pin,
  which further argues against the current host-clock assumption
- until the exact oscillator/divider path is traced, the emulator keeps a
  temporary fallback clock so `-psg` remains testable instead of silent

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
