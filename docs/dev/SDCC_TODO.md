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
- the runtime handoff evidence is now specific enough to narrow one part of that
  uncertainty: preserved ordinary `.SM` `entry` is **not** a reliable literal
  first-PC contract for the launch path.
  - `SHOW.SM` preserves `load=0x5600, entry=0x5602`, but after the confirmed
    bare-name `.SM` launch path reaches `0x6457 -> 0x647A`, the first observed
    live high-memory PC is `0x5600`, not `0x5602`
  - that is not just the missing-argument error path. A successful
    `SHOW HORLOGE.SR` probe follows the same confirmed `0x6457 -> 0x647A`
    launch family, first reaches live `pc=0x5600`, and then visibly prints the
    `SHOW TEXT FILE PROGRAM REV 1-5` banner followed by the `HORLOGE.SR`
    source listing
  - `TDISK.SM` preserves `load=0x5600, entry=0x5611`, but the same confirmed
    launch family reaches live `0x5602`, and in later passes also reaches live
    `0x5600` followed by `0x5602`
  - a focused handoff trace narrows where that split is decided. The live block
    at `0x18EC+` does **not** compute a launch offset from metadata: it pops the
    already-stacked target word (`0x5600` or `0x5602`), pushes that same word
    back onto the stack, and then enters helper `0x11D2`
  - the traces show that preservation directly. On the `TDISK` side,
    `pc=0x18EC` arrives with `top=0x5602`, then `pc=0x11D2` still has
    `top=0x5602`, and the next live high-memory PC is `0x5602`. On the
    successful `SHOW HORLOGE.SR` side, `pc=0x18EC` arrives with `top=0x5600`,
    `pc=0x11D2` still has `top=0x5600`, and the next live high-memory PC is
    `0x5600`
  - so the unresolved `0x5600` versus `0x5602` choice is now one hop tighter:
    it is fixed **before** the `0x18EA/0x18EC -> 0x11D2` trampoline runs. That
    makes the real remaining question an upstream stack-construction question,
    not an `entry`-field interpretation question inside the final handoff block
  - one more focused pre-handoff stack trace narrows it again: by the time the
    short setup script starts at `0x1DB9`, the eventual launch word is already
    present as the **third** stacked return word.
  - on the `TDISK` side, the dispatcher enters the script with
    `top=0x1DB9 next=0x18EA next2=0x5602`, then repeats that same `next2=0x5602`
    state at `0x1DBB` and `0x1DBD`, and only after the script finishes does that
    word move up to `next=0x5602` at `0x18EB`
  - on the successful `SHOW HORLOGE.SR` side, the same script starts with
    `top=0x1DB9 next=0x18EA next2=0x5600`, keeps `next2=0x5600` through
    `0x1DBB/0x1DBD`, and only then exposes it as `next=0x5600` at `0x18EB`
  - that is enough to rule out one more tempting interpretation: the `0x1DB9`
    short script does not select the program start offset. It only prepares the
    workspace before returning into a launch word that was already stacked
  - a final upstream check rules out one more candidate boundary. At the live
    `0x18BF` deep-tail hit, the future launch word is **not** on the visible
    stack yet.
  - on the `TDISK` side, `0x18BF` arrives with
    `top=0x0026 next=0x0041 next2=0x0E1C`; on the successful `SHOW HORLOGE.SR`
    side, the corresponding `0x18BF` hit arrives with
    `top=0x00E2 next=0x00E8 next2=0x0E1C`
  - only after that deep-tail return drops back into the low-RAM selector path
    does the stacked launch target appear: by the time the dispatcher reaches
    `0x1DB9`, `next2` has become `0x5602` for `TDISK` or `0x5600` for
    `SHOW HORLOGE.SR`
  - that leaves the current smallest unresolved window as: the launch word is
    introduced somewhere between the live `0x18BF` hit and the later
    `0x1DB9/0x1DBB/0x1DBD` selector-script entries, most likely inside the
    immediately following low-RAM dispatcher/setup path rather than in the deep
    tail itself
  - one more focused boundary probe narrows that again. The launch frame is
    already fully present by the time execution reaches the first visible
    `RST 20` vector entry at `0x0020`
  - on the `TDISK` side, the path goes from
    `pc=0x18BF top=0x0026 next=0x0041 next2=0x0E1C next3=0x1916` straight to
    `pc=0x0020 top=0x1DB9 next=0x18EA next2=0x5602 next3=0x00F3`
  - on the successful `SHOW HORLOGE.SR` side, the matching step goes from
    `pc=0x18BF top=0x00E2 next=0x00E8 next2=0x0E1C next3=0x1916` straight to
    `pc=0x0020 top=0x1DB9 next=0x18EA next2=0x5600 next3=0x00F3`
  - so the dispatcher body at `0x0127+` is no longer part of the unresolved
    insertion window. By the first `RST 20` vector handoff itself, the
    `1DB9/18EA/560x` frame has already been assembled
  - the remaining ABI-facing unknown is therefore narrower still: the frame is
    being built somewhere between the deep-tail `0x18BF` state and that first
    `0x0020` vector entry, likely in the tiny low-RAM wrapper/helper chain that
    sits just above the shared `0x0E1C/0x1916` tail rather than inside the
    selector script or dispatcher body
  - the next focused helper-chain probe resolves an important mistake in that
    intermediate model. On this path, `0x0E1C` is **not** acting as a return
    address. It is popped into `AF` by the live `0x18BF..0x18D0` block
  - the same probe shows that the true launch word is already on the stack at
    live `0x18BF`, just one slot deeper than the earlier tracer was printing.
    After `0x18BF` pops the two small selector words into `DE`/`HL`, the next
    visible stack state at `0x18C1` is already
    `top=0x0E1C next=0x1916 next2=0x5602 next3=0x18E0` on the `TDISK` side or
    `top=0x0E1C next=0x1916 next2=0x5600 next3=0x18E0` on the successful
    `SHOW HORLOGE.SR` side
  - the live `0x18BF..0x18D0` block behaves coherently with that layout:
    `POP DE`, `POP HL`, `SBC HL,DE`, copy the difference into `BC`, `POP AF`
    (which consumes the literal `0x0E1C`), then load `A` from `(IX+0x10)` and
    return to `0x1916`
  - `0x1916` then pops the real launch word directly from the stack. On the
    ordinary successful path it pops `HL=0x5602` (`TDISK`) or `HL=0x5600`
    (`SHOW HORLOGE.SR`), decrements it once for the non-zero test, restores it,
    and returns with `A=0x57` or `A=0xFF` respectively rather than taking the
    alternate `A=0x1C` carry-return case
  - the next small block `0x18E0 -> 0x18E3 -> 0x18E6` is then where the visible
    launch frame gets normalized into the later dispatcher shape. At `0x18E0`,
    the `TDISK` side arrives with `AF=0x5700 BC=0x1ABF DE=0x0026 HL=0x5602`
    while the `SHOW HORLOGE.SR` side arrives with
    `AF=0xFFAC BC=0x0519 DE=0x00E2 HL=0x5600`
  - by `0x18E3`, both cases have already been collapsed to the stable ABI-ish
    pair carried into the dispatcher: `BC=0x0080 DE=0x10D5` (`TDISK`) or
    `BC=0x0000 DE=0x45C0` (`SHOW HORLOGE.SR`)
  - by `0x18E6`, `IX` has been switched to `0x5800`; and the first following
    `RST 20` vector entry at `0x0020` now sees the assembled frame
    `top=0x1DB9 next=0x18EA next2=0x5602` (`TDISK`) or
    `top=0x1DB9 next=0x18EA next2=0x5600` (`SHOW HORLOGE.SR`)
  - decoding the live bytes at `0x18E0` resolves most of that remaining
    micro-question too. The block is:
    `POP BC; POP DE; POP IX; JR C,+0x0E; PUSH HL; CALL 0x1DB5; RST 20; DEC DE;
    POP DE; LD SP,HL; LD HL,(0x2BC7) ...`
  - and `0x1DB5` is not an unrelated helper; it is the small threaded-script
    wrapper that seeds the later selector stream directly:
    `LD HL,0x0000; RST 20; 0x04; RST 20; 0x02; RST 20; 0x1F; RET`
  - that explains why the dispatcher later sees `0x1DB9 -> 0x1DBB -> 0x1DBD`.
    Those are simply the inline script bytes immediately following the three
    `RST 20` opcodes in the `0x1DB5` helper body
  - on the ordinary successful path, the carry branch at `0x18E4` is not taken.
    So `0x18E0` does not compute the selector bytes from scratch; instead it
    installs the already-decided register pair (`BC/DE`), preserves the already
    stacked launch word in `HL`, and invokes the shared `0x1DB5` script wrapper
    to drive the standard `04, 02, 1F` setup sequence before returning to
    `0x18EA`
  - in other words, the user-visible `1DB9/18EA/560x` frame seen at `0x0020`
    is now almost fully accounted for:
    the launch word is already chosen before `0x18BF`, the helper chain carries
    it through `0x1916`, and `0x18E0` hands control to the fixed `0x1DB5`
    threaded-script wrapper whose inline bytes become the later
    `1DB9/1DBB/1DBD` selector entries
  - one more focused rerun sharpens the remaining `BC/DE` question. The
    `0x18E0` block is **not** where the post-token cursor gets computed:
    workspace word `0x2BC7` is already stable before `0x18E0` executes, and it
    stays unchanged through `0x18E3`, `0x18E6`, `0x18EA`, and the following
    `0x0020` re-entry
  - on the `TDISK` side, the ordinary successful path reaches `0x18E0` with
    `DE=0x0026`, `next=0x10D5`, and already has `0x2BC7=0x10DB`; on the
    `SHOW HORLOGE.SR` side, it reaches `0x18E0` with `DE=0x00E2`,
    `next=0x45C0`, and already has `0x2BC7=0x45C9`
  - after the initial `POP DE` at `0x18E1`, those same values become the stable
    pair carried all the way through the setup script and into the dispatcher:
    `DE=0x10D5, 0x2BC7=0x10DB` for `TDISK`; `DE=0x45C0, 0x2BC7=0x45C9` for
    `SHOW HORLOGE.SR`
  - the live RAM contents make that pairing meaningful. `0x45C0` is the start
    of the active line buffer containing `SHOW.SM  HORLOGE.SR`, while
    `0x2BC7=0x45C9` already points at the start of the next token `HORLOGE.SR`.
    Likewise on the `TDISK` side, `0x10D5` points into the zero-separated system
    name table at `CLI.SY`, while `0x2BC7=0x10DB` already points at the next
    entry after the terminating zero byte
  - that is enough to upgrade one more part of the ABI model from guess to
    evidence-backed interpretation: the non-branch path out of `0x18E0` is
    preserving an already-built token window, with `DE` behaving like the start
    pointer for the current token/candidate and `0x2BC7` behaving like the
    already-advanced cursor to the next scan position
  - `0x2BD1` still differs systematically between the same cases
    (`0xFFFF` for `TDISK`, `0x0000` for successful `SHOW HORLOGE.SR`), but its
    exact meaning remains open. It currently looks more like delimiter/qualifier
    state associated with that token window than a direct launch address input
  - one more control probe with `SHOW` **without** an argument did not produce a
    clean successful-`SHOW` comparison case. It did **not** re-enter the same
    `0x5600 / DE=0x45C0 / 0x2BC7=0x45C9 / 0x2BD1=0x0000` handoff seen for
    `SHOW HORLOGE.SR`
  - instead, the only visible launch window in that no-argument run stayed in
    the same no-second-token shape as the bare-name path:
    `DE=0x10D5`, `0x2BC7=0x10DB`, `0x2BD1=0xFFFF`, and launch through
    `0x5602`
  - that makes the control result consistent with the current reading of
    `0x2BD1` as “no second token / no following argument state”, but not strong
    enough to upgrade that reading to fact on its own, because `SHOW` without an
    argument does not follow the same successful ordinary-command launch path as
    `SHOW HORLOGE.SR`
  - a cleaner negative control is `NATHALIE.IM`, but only when invoked with the
    explicit `.IM` extension. Bare `NATHALIE` is just a CLI error because only
    `.SM` and `.MC` participate in the extensionless executable fallback
  - the explicit `NATHALIE.IM` run is useful precisely because it does **not**
    re-enter the ordinary executable launch family. After the system has booted,
    the command-time trace shows no fresh hits on the known `.SM` path anchors
    `0x57D2 -> 0x5812 -> 0x6457 -> 0x647A` and no later handoff through
    `0x18BF -> 0x1916 -> 0x18E0 -> 0x18EA`
  - live RAM still shows that the file load itself succeeds. The preserved
    `NATHALIE.IM` payload matches the post-run RAM dump byte-for-byte at
    `0x4600`, even though the extracted sidecar metadata records `load=0x4601`
  - so `NATHALIE.IM` is now a useful control for “file resolved and loaded, but
    not handed to the ordinary executable trampoline”. That helps bound the SDCC
    target more sharply: the `.SM` ABI work is about the later executable
    classifier/handoff path, not about generic file loading
  - a narrower refusal trace finally pins down that negative path too. The
    explicit `NATHALIE.IM` run does not go through the ordinary `.SM` anchors or
    the earlier `0x6FB0` non-`0x06` splitter; instead it reaches
    `CLI.SY:0x702E` with `A=0x1C`, and that site immediately calls the generic
    file-message formatter at `0x5CDA`
  - one hop earlier, the same run now shows where that selector is actually
    rejected: after the shared setup at `0x647A` (`RST 10 / 0x0B`), execution
    resumes at `0x647D` with `A=0x1C` and takes the inline test
    `CP 0x0A / JP NZ,0x7017`; the executable side therefore appears to be the
    accepted `0x0A` return, while `.IM` takes the non-zero/non-`0x0A` branch
    into the refusal helper
  - a tighter pass on the low-memory return path corrects one part of that
    reading: on the relevant explicit-`.IM` path, the visible `A=0x1C` at
    `0x647D` is not returned directly by helper `0x18A8`. Live RAM at
    `0x1900+` decodes that block as:
    `CALL 0x192E ; RET C ; SCF ; RET Z ; ... ; CALL 0x18A8 ; JR C,+0x0A ; POP HL ; DEC HL ; LD A,H ; OR L ; INC HL ; RET NZ ; LD A,0x1C ; SCF ; RET`
  - in the explicit `NATHALIE.IM` run, `0x18A8` returns on the non-carry side
    and the caller then takes that local fallback: at `0x1916` the stacked word
    is `0x0001`, so the `DEC HL / OR L / RET NZ` test falls through to the
    inline `LD A,0x1C ; SCF ; RET` at `0x191C`
  - a direct follow-up trace now names the source of that stacked word too. At
    entry to `0x18A8`, the value under the return address exactly matches the
    word loaded from `(IX+0x13/0x14)` just before the call:
    `NATHALIE.IM` shows `ix13_14=0x0001` and `next=0x0001`, while executable
    runs show `ix13_14=0x5600` or `0x5602` and the same value under the return
    address at `0x18A8` and again at the later `0x1916` test
  - that tightens the current interpretation of the non-exec split: the caller
    at `0x1916` is not inventing a value from thin air, it is classifying the
    already-selected descriptor word carried in the `(IX+0x13/0x14)` slot. The
    `.IM` refusal happens because that slot is `0x0001`; the executable path
    succeeds because the same slot carries `0x5600/0x5602`
  - one more byte-level correction removes the last ambiguity in that label.
    The normalized 0x18-byte cache record for `NATHALIE.IM` is stable in the
    directory walker itself at `IX=0x2558` and matches the extracted metadata:
    `+0x0A/+0x0B = 0x031C` (start sector), `+0x0C/+0x0D = 0x032B`,
    `+0x0E/+0x0F = 0x0001` (flags), `+0x0F/+0x10 = 0x0000`,
    `+0x12/+0x13 = 0x4601` (raw load), `+0x14/+0x15 = 0x0003` (entry),
    `+0x16/+0x17 = 0x8109` (date)
  - live bytes at `0x1908+` show why the traced selector value looked like a
    standalone field when it is not: `LD E,(IX+0x13) ; LD D,(IX+0x14) ; PUSH DE ;
    EX DE,HL ; ... ; CALL 0x18A8`. So the later `0x1916` test classifies the
    cross-word selector assembled from the low byte of the raw load word and
    the high byte of the entry word, while the effective runtime load address is
    already being carried separately in `HL/DE`
  - by contrast, the known-good executable paths reach the same caller with a
    nontrivial stacked word (`0x5600` or `0x5602` in current `TDISK` traces), so
    the same `0x1916` test returns early on the non-zero case instead of
    synthesizing selector `0x1C`
  - a focused `EDISK` probe disconfirms one tempting over-read of that fact.
    `EDISK.SM` carries preserved metadata `load=0x56AE, entry=0x5710`, so the
    same cross-word synthesis yields selector `0x57AE`, not the raw load word.
    The live launch still passes through `0x1913 -> 0x18A8 -> 0x1916` without
    touching `0x7017`, which means the gate here is **not** “selector must equal
    load” or “selector must equal `0x5600/0x5602`”. The current evidence says
    only the special value `0x0001` triggers the local `LD A,0x1C ; SCF ; RET`
    fallback at `0x191C`
  - a second negative control makes that `0x0001` value look generic rather
    than `.IM`-specific. Explicit `MATPAC.SY` has preserved metadata
    `load=0x2C01, entry=0x0005`, which produces the same cross-word selector
    `0x0001`; the live run reaches `pc=0x1913` with `DE=0x2C00`, `HL=0x0001`,
    then follows the same `0x1916 -> 0x7017 -> 0x702E` refusal tail and prints
    `pas d'ex cution, fichier: MATPAC.SY`
  - the register split is now concrete across three classes:
    `DE` at `0x1913` is the page-aligned load base (`load & 0xFF00` in current
    traces), while `HL`/the stacked selector is `(entry & 0xFF00) | (load &
    0x00FF)`. So the next unresolved SDCC-facing fact is no longer the caller's
    predicate itself; it is where the raw big-endian `load`/`entry` pair comes
    from for a synthetic record and which file classes are expected to produce
    selector `0x0001`
  - a follow-on trace one frame earlier falsifies one nearby hypothesis about
    where that pair is assembled. The `0x1743..0x1790` block does **not** build
    the `0x2300` cache; it already runs with `HL=0x2300`, `0x2318`, `0x2330`,
    ... and `IX` stepping over the same populated `0x18`-byte records. The
    observed `LDIR` at `0x1757` copies the first 10 bytes from the selected
    cache record into the `0x711A` workspace (`HL=0x2300`, `DE=0x711A`, then
    later `HL=0x2318`, `DE=0x7130`), so this slice is a formatter/consumer of
    the cache, not its constructor
  - that moves the unresolved producer one hop earlier again: by the time the
    `0x1743` formatter runs, the normalized `0x2300` records already contain the
    executable/non-executable split that later appears at `0x1913`. The next
    useful target is therefore the upstream routine that populates `0x2300`
    before this formatter consumes it, not the formatter itself
  - the current live return chain is now explicit enough to avoid reopening the
    same dead end: the visible executable path reaches `pc=0x1D8F` with
    `top=0x1743 next=0x711A next2=0x11B9`, then the new build trace shows
    `0x1743/0x1757/0x1771/0x1790` repeatedly walking `HL=0x2300, 0x2318,
    0x2330, ...` and copying each populated cache record into the `0x711A`
    workspace. So `0x11B9 -> 0x711A -> 0x1743` is now a confirmed
    consumer/formatter chain over a ready-made cache
  - a direct discriminating check against the next obvious constructor
    candidate also failed. The low-RAM wrapper at `0x19E1` decodes as
    `CALL 0x1DEB ; RET C ; CALL 0x1DDB ; CALL 0x1D29 ; JP 0x1F59`, which made
    it look like a plausible cache-init entry because `0x1DDB` zeros `0x2300`.
    But widening the focused `RST 10` follow set to include `0x1DEB`, `0x1DDB`,
    `0x1EDF`, and `0x1F59` still produced no hits on the successful `EDISK`
    launch path. So the current executable launcher slice does **not** visibly
    pass through that zero-fill wrapper, at least not under the present narrow
    trace budget
  - that narrows the next hop again: the producer of the normalized `0x2300`
    records is still upstream of `0x11B9 -> 0x711A -> 0x1743`, but the current
    evidence no longer supports `0x19E1` as the active constructor on this
    launch path. The most local unresolved target is now the mechanism that
    hands `0x1743` its already-populated `0x2300` buffer and the matching
    `0x711A` workspace pointer
  - this also means the preserved `SYS.SY` artifact should not be trusted
    blindly for `0x1913+`: the live RAM bytes from both current runs agree on
    the executable code above, while the static extracted file at that address
    looks like table data
  - that formatter invocation is the observed refusal: it emits
    `pas d'ex cution, fichier: NATHALIE.IM`, then the caller falls through the
    shared CLI tail at `0x7040+` and rejoins the already-known prompt/status
    sink at `0x56AE`; so the `.IM` case is now best read as “resolved file,
    classified as non-executable with selector `0x1C`, print refusal, reprompt”
  - that is enough to upgrade one SDCC-facing constraint from “unknown” to
    “unlikely”: the ordinary `.SM` launch contract is not a simple `JP entry`
    taken directly from preserved directory metadata. `load` still matches the
    real placement of the program image in RAM, but `entry` currently looks more
    like auxiliary loader metadata than like the literal first executed PC
- The first direct look at the `Sys2-2` `CLI.SY` dispatcher falsifies one earlier
  assumption: in this preserved boot-media artifact, command-table entry `LOAD`
  points to handler `0x6B0D`, not `0x6A70`.
- The surrounding `0x69FE–0x6A7E` block is therefore a better candidate for a
  generic token-to-file dispatch or fallback path than the explicit `LOAD`
  command handler.
- Current scope note: this is confirmed analysis of the preserved `Sys2-2`
  artifact only. It does not yet prove the exact role of `0x69FE–0x6A7E`, but it
  does rule out the earlier identification of `0x6A70` as the `LOAD` entry point
  in this particular image.
- Version caution: `docs/dev/CLI_SY_analysis.md` was originally written against an
  older extracted `CLI.SY` artifact. Its command-table addresses and handler
  anchors should therefore be treated as version-specific historical notes, not
  as authoritative labels for the preserved `Sys2-2` boot-media `CLI.SY` now
  being traced here.
- The manual distinction above also makes the current `0x5FEF` compare against
  `0x09` easier to interpret: whatever that value means, it may well be an
  internal **program-load class** or loader-mode result rather than a direct
  encoding of the on-disk `SM` letters.
- A more detailed first-pass trace of the nearby `0x69FE–0x6A7E` block gives a
  useful control-flow outline for what may be the generic fallback file-dispatch
  path:
  - the block scans a token byte-by-byte and copies characters through helper
    `0x5F56`
  - helper `0x5E98` clears workspace flag `0x70E1`, scans the token for an
    optional `$...` qualifier, and specifically accepts `K` or `D` after `$`
    before continuing; this looks like qualifier parsing rather than the start
    of the explicit `LOAD` command path
  - the block then calls `0x5FD1` and `0x607C`, suggesting staged lookup and
    workspace preparation rather than a raw direct jump
  - helper `0x5FE6` starts with `RST 10 / 0x00` and soon compares `A` with
    `0x09` at `0x5FEF`; this still looks like a loader-class or file-kind check,
    but it is **not** a raw on-disk two-letter type comparison. The disk format
    used by both `tools/extract_samos_image.py` and `src/virtual_floppy.c`
    stores file type as two ASCII bytes such as `SM`, `SY`, `BS`, and `DR` in
    directory-entry bytes `8:10`.
  - a narrower caller-side pass shows the first clearly directory-derived memory
    walk a little earlier in the same helper family: `0x5F14` advances pointer
    `0x70D0` in `0x16`-byte steps, stops when the first byte of the current
    record is zero, and copies an 8-character basename plus optional extension
    bytes through `0x5F56` into the buffer pointed to by `0x70E8` / `0x70EA`
  - that `0x16` stride does **not** match the preserved on-disk SAMOS directory
    format, which is 24 bytes per entry with trailing BCD month/year fields, so
    this loop is most likely walking a CLI-side in-memory working structure or a
    normalized directory cache rather than raw floppy-directory bytes
  - the same workspace triple is initialized in other command paths too:
    `0x6513/0x651C/0x6522` seeds `0x70EA`, `0x70E8`, and `0x70D0`, and a later
    interactive path around `0x6E67–0x6F0B` reuses those same buffers while
    printing French prompts about the source directory (`repertoire source`), so
    these buffers are shared CLI file-search state rather than launch-only state
  - helper `0x607C` populates workspace around `0x710B`, `0x710E`, `0x7110`, and
    `0x7113`
  - the block then uses helpers `0x6036`, `0x603E`, and `0x6050`, which are not
    generic memory routines: each one selects between two different `RST 10`
    service numbers depending on qualifier flag `0x70E1` before returning to the
    caller
  - the actual handoff therefore seems to occur only after several metadata and
    workspace preparation stages, not immediately after a command-table miss
- A new pass finally identified that command-table miss on this same artifact:
  - the CLI scanner at `0x5797+` loads `HL = 0x60B3` directly and compares the
    normalized token against the embedded command records one by one; on local
    mismatch it advances through each record by scanning for the inline `JP`
    opcode byte `0xC3` and then checking the trailing end-of-entry marker
  - when that scan reaches the final negative marker, the miss path falls
    through into `0x57D2+` rather than re-entering any of the explicit command
    handlers
  - that `0x57D2+` path extracts the unmatched token, searches for an existing
    dot, and when no extension is present explicitly builds `.SM` in workspace:
    it stores the dot at `0x57FC`, writes `S` and `M` at `0x5802/0x5804`,
    updates pointer `0x70EC`, and then jumps to `0x6457`
  - that is the first artifact-specific evidence in this session that the
    ordinary bare-name fallback really is “treat unknown token as `.SM`
    program”, and it moves the main launch anchor earlier: the relevant next
    boundary is now `0x57D2+ -> 0x6457`, not the later command-owned
    `0x69FE–0x6B94` neighborhood
  - however, the very next block at `0x6457` is still not a clean final launch
    handoff. In the current artifact it has exactly one caller, this same
    `0x5812` miss path, but once entered it immediately joins the previously
    noted macro-sensitive logic: when `0x454B == 0`, it overwrites the suffix at
    `0x70EC` with `MC`, calls `RST 10 / 0x14`, stores the returned value back to
    `0x454B`, clears `0x70B0/0x70B1`, and jumps to `0x569E`
  - the `MC` handling around that split now also looks parser-level rather than
    launch-specific. The active-macro side at `0x645C+` calls shared scanners
    `0x5DC4` and `0x5DCE`, then uppercases bytes through `0x5BB2` before testing
    for `C` and `M`; the same scanner family is already used by the ordinary
    bare-name miss path at `0x57D7+`
  - the smallest shared helper there is `0x5DAE`, which compares `(DE)` against
    a tiny delimiter table containing `0x20`, `0x09`, `0x2C`, `0x2F`, and
    `0x00`, terminated by `0x80`. That makes the surrounding `0x5DC4/0x5DCE`
    pair look like generic token skipping over CLI separators, not a dedicated
    `.SM`-loader contract boundary
  - a follow-up caller check narrows the neighboring branches further: the bytes
    at `0x612F+`/`0x6138+`/`0x6143+`/`0x614C+`/`0x6158+` embed the command names
    `ENTER`, `RENAME`, `DELETE`, `COMPRESS`, and `CLEAR`, and those table entries
    jump straight to `0x64A7`, `0x64E7`, `0x6506`, `0x658F`, and `0x65A4`
    respectively
  - that means the nearby `0x64A7–0x65E5` paths and their shared sink at
    `0x66D5` are explicit command-handler territory too, not an alternate clean
    ordinary-program continuation hiding next to the `0x57D2+ -> 0x6457` miss
    path
  - the user-manual description of `SHOW` needs a narrower reading than the one
    above: `SHOW HORLOGE.SR` documents the syntax of the external viewer tool,
    not a proof that `SHOW` is a CLI-resident built-in. For the current notes,
    `SHOW`, `BASIC`, and `TDISK` should all be treated as ordinary out-of-
    `CLI.SY` program names rather than command-table keywords
  - those runtime probes therefore all belong to the same evidence family: live
    bare-name invocation can reach `.SM` suffix staging. The cleanest exits are
    still `BASIC` and `TDISK`, which left buffer `0x45C0` holding `BASIC.SM`
    and `TDISK.SM` respectively, directly matching the static `0x57D2+` append
    path
  - a longer focused `TDISK` rerun (30 s timeout, with dedicated `[flow-sm]`
    watchpoints) finally turned that from indirect support into direct runtime
    confirmation. The trace reaches `0x57D2`, `0x57D7`, `0x5812`, and `0x6457`
    in order after the screen fragment `* TDISK-`; at `0x57D2/0x57D7` the line
    buffer still holds bare `TDISK`, the CLI writes `.` / `S` / `M` at
    `0x57FE/0x5804/0x5807`, and by `0x5812` and `0x6457` the same live buffer is
    already `TDISK.SM` with `0x70EC = 0x45C6`
  - that longer run also explains why the first focused probe missed the target:
    the shorter timeout was simply cutting off the interesting part. So the
    live evidence now agrees with the static control-flow claim that ordinary
    bare-name dispatch really does enter the preserved `0x57D2+ -> 0x6457`
    path, rather than only leaving behind late post-tool RAM signatures
  - an even narrower follow-up on that same `TDISK` run confirms the first
    post-`0x6457` decision too: with `0x454B = 0`, execution reaches `0x647A`
    (the zero-branch target after `push de` / `jr z,+0x17`) while the live line
    buffer is still `TDISK.SM` and `0x70EC` is still `0x45C6`
  - after that point the next visible activity is the `RST 10 / 0x0B` service
    path itself rather than a return into `0x647D` / `0x648C` / `0x649B`: the
    trace falls into the low-level service flow (`0x2187`, then low RAM around
    `0x0148+`) and does not, within the same 30 s window, surface either the
    `0x7017` failure branch or the later `0x649B` / `0x64A5` store-and-jump
    sites
  - that `0x2187` hit is at least locally coherent with the existing hardware
    notes too: `0x2187` is already identified elsewhere as the floppy-step
    helper used during post-ROM disk work. So the live `0x647A -> RST 10 /
    0x0B -> 0x2187` sequence still looks more like a real disk-hardware service
    call than like control flow escaping into unrelated code
  - the remaining gap is therefore narrower than before. Runtime evidence now
    covers entry through the zero-`0x454B` branch and into the low-level disk
    service path, but not yet the eventual return into `0x647D+` or proof that
    this call later reaches the `0x649B` / `0x64A5` store-and-jump tail
  - a one-shot follow-up trace armed exactly at `0x647A` sharpens the return
    side further. Immediately after that site the first visible step is still
    `0x2187`, but the next low-RAM steps are now pinned down more precisely:
    `0x0048`, `0x004A`, `0x0148`, `0x014B`, `0x014D`, `0x004D`, then `0x0050`
  - that resolves the earlier `0x004D` ambiguity. The live RAM image from the
    same run shows `0x0048: OUT (0x01),A; 0x004A: CALL 0x0148; 0x004D: CALL
    0x0195; 0x0050: LD HL,(0x456E) ...`, so the `0x004D` stack-top seen while
    executing `0x0148` / `0x014B` / `0x014D` is simply the return site of the
    `CALL 0x0148` at `0x004A`, not a direct return target back into `CLI.SY`
  - the low-RAM bytes at `0x0130..0x014D` from that same dump also match the
    already-noted RAM dispatcher pattern (`... LD HL,0x0F30 ... LD A,(0x4554) ;
    EX (SP),HL ; RET`) rather than the older boot-ROM-only interpretation of
    that address range
  - a later armed trace now fills in the layer above that wrapper too. In the
    same live `TDISK` path, execution reaches the current `RST 10` vector target
    at `0x1063`, then `0x1074`, then `0x1087` / `0x1088`, before landing at
    `0x18D6`
  - that extra hop is consistent with the live RAM image rather than a guess:
    `(0x4564) = 0x1063`, and the bytes at `0x1063+` implement another inline-
    byte dispatcher rooted at `0x101B`; the `0x0B` entry from that live table
    decodes to `0x18D6`
  - runtime then continues through `0x1902` and `0x192E`, and the next visible
    helper chain is now pinned down one hop further as `0x1AC4 -> 0x1E23 ->
    0x1A53`
  - that last site is informative statically too: the live RAM bytes at
    `0x1A53` are `CALL 0x1A63 ; XOR A ; LD (0x2B9A),A ; JP 0x1F47`, so the
    armed `TDISK` path is now consistent with the older note elsewhere in this
    file that `0x1F47` is one of the transfer-oriented low-memory helpers behind
    the `0x1003/0x1006` stub family
  - a later armed trace removes the remaining gap inside that transfer helper.
    The live path now shows `0x1F47 -> 0x1F6D -> 0x1F79 -> 0x2186`, and because
    `0x2186` is just the one-byte entry immediately ahead of the already-known
    step helper body at `0x2187`, the runtime evidence now directly ties this
    `0x1F47` helper chain to the `0x2187` floppy-step logic rather than only
    placing them near each other in time
  - the same trace also shows the helper calling back into `0x2186` a second
    time, then reaching `0x1F90 -> 0x21A8 -> 0x217D` before the path falls into
    the low-RAM wrapper `0x0048 -> 0x004A -> 0x0148 -> 0x004D -> 0x0050`
  - a follow-up aimed at that exact tail tightens the interpretation of the
    `0x0048` wrapper too. At each visible `0x217D` hit the stack top is still
    `0x1FAE` (with next word `0x1F4A`), i.e. the active return slot still points
    back into the same low-memory transfer-helper chain rather than anywhere in
    `CLI.SY`
  - the next trace then resolves what that interleaving actually is. In the same
    live RAM image, `(0x4566) = 0x003E`, i.e. `RST 38` is still routed to the
    installed SAMOS 50 Hz interrupt handler rather than to `0x0048` directly
  - and the armed runtime sequence after each visible `0x217D` hit is now
    explicit: `0x217D -> 0x003E -> 0x0041 -> 0x0048 -> 0x004A -> 0x0148 ...`
    with the stack switching from the helper-chain return slot at `0x1FAE` to
    an interrupt-style frame rooted at `0x2180`
  - so the recurring `0x0048` wrapper is no longer best described as generic
    low-memory service work. On this path it is specifically arriving through
    the installed `RST 38` / 50 Hz ISR, nested on top of the transfer-helper
    chain before that chain has unwound, not as evidence that control has
    already returned toward `0x647D+`
  - a dedicated tail-only follow probe then shows that the underlying helper
    does in fact resume after that interrupt interleave. Once the ISR burst is
    out of the way, control returns to `0x1FAE` and continues through
    `0x2155 -> 0x21B7 -> 0x213A -> 0x1FD8 -> 0x212F -> 0x1FEC`, with repeated
    small-loop traffic still rooted on low-memory return slots such as
    `0x1F4A`, `0x2021`, and later `0x18BF`
  - a later version of that same tail-only probe shows the chain advancing one
    layer beyond the first `0x1FAE` loop too. After the early `0x1FAE -> ... ->
    0x1FEC` activity, the live path reaches `0x1F57`, then repeatedly cycles
    through `0x213A -> 0x206F -> 0x2021`, and on some passes through
    `0x212F -> 0x2075` or `0x213A -> 0x20B2`
  - a second-stage tail follow then confirms that the deeper return-slot values
    are not just passive stack artifacts. On this same live path, `0x1F57 /
    0x2021` can lead into actual execution at `0x18BF`, `0x194B`, `0x1D08`,
    `0x1D8F`, and `0x1DAB`, and those sites in turn feed back into the already-
    known low-memory helper family around `0x1A53` / `0x1F47`
  - the `0x18BF` and `0x1D8F` hits are especially useful because the stack
    shape is visible one step earlier. In the same run, `0x2021` appears with
    `next=0x18BF`, then `0x1F57` appears with `top=0x18BF next=0x0026`, and
    the second-stage follow finally lands at live `pc=0x18BF` with
    `top=0x0026 next=0x0041`; likewise `0x2021` appears with `next=0x1D8F`,
    then `0x1F57` with `top=0x1D8F next=0x1743`, and finally live
    `pc=0x1D8F` with `top=0x1743 next=0x711A`
  - the immediate post-hit continuation is low-memory too. After the live
    `0x18BF` hit, the same trace falls straight into `0x0127 -> 0x0128 ->
    0x012B -> 0x012C -> 0x012D -> 0x012E`; after the live `0x1D8F` hit it
    likewise falls into `0x0127 -> 0x0128 -> 0x012B -> 0x012C`. So even these
    deeper executions still rejoin the low-RAM dispatcher region rather than
    surfacing any visible `CLI.SY` continuation
  - a focused dispatcher-state rerun sharpens that result further. The re-entry
    surface itself stays stable across both cases: at `0x0127+`, the installed
    vectors are still `(0x455C)=0x0127`, `(0x4562)=0x2005`,
    `(0x4564)=0x1063`, and `(0x4566)=0x003E`. What changes is the local
    payload being fed through that dispatcher, not the dispatcher target
  - after live `0x18BF`, the dispatcher arrives with `AF=0x5700`,
    `DE=0x10D5`, `0x4554/0x4555 = 0x57/0x00`, and by `0x012C` has moved
    `HL` onto `0x1DB9`; the stacked continuation visible there is
    `top=0x0000 next=0x18EA`
  - after live `0x1D8F`, the same dispatcher arrives with `AF=0x5305`,
    `DE=0x0003`, `0x4554/0x4555 = 0x53/0x04`, and by `0x012C` has moved
    `HL` onto `0x1CF4`; the stacked continuation visible there is
    `top=0x0003 next=0x1D99`
  - the live bytes make the dispatcher contract there much clearer. The code at
    `0x0127+` is the already-known RAM selector stub: it stores the incoming
    `A` in `0x4554`, swaps `HL` with the stacked pointer, reads one byte from
    `(HL)`, advances that stacked pointer, and then indexes the shared table at
    `0x0F30` when the byte is below `0x80`
  - that means `0x1DB9` and `0x1CF4` should currently be read as **byte-source
    pointers feeding the dispatcher**, not as alternate dispatcher targets.
    The first `0x18BF` re-entry lands on byte `0x04` at `0x1DB9`, which maps
    through the live `0x0F30` table to handler `0x0299`; the first `0x1D8F`
    re-entry lands on byte `0x2E` at `0x1CF4`, which maps through that same
    table to handler `0x0E4F`
  - a follow-up pass shows that the `0x1DB9` case is a short **threaded service
    script**, not a one-byte event. After the first `0x04 -> 0x0299` dispatch,
    the same selector immediately re-enters with `top=0x1DBB next=0x18EA` and
    then `top=0x1DBD next=0x18EA`, so the stream at `0x1DB9` is consumed as the
    sequence `0x04`, `0x02`, `0x1F`, followed by the terminal `RET` byte at
    `0x1DBE`
  - the selected handlers on that short stream are concrete too: `0x04` maps to
    `0x0299`, `0x02` maps to `0x0291`, and `0x1F` maps to `0x029D`. All three
    are tiny `LD (workspace),HL ; RET` stubs in the `0x0291..0x02AD` family, so
    this branch currently looks like a compact workspace-vector setup script
    that finally returns toward `0x18EB`
  - the exact workspace targets for that short script are now visible in the
    live RAM bytes too: `0x0291` writes `HL` to `(0x456E)`, `0x0299` writes
    `HL` to `(0x4572)`, and `0x029D` writes `HL` to `(0x455E)`. So the
    `0x1DB9` stream is not arbitrary low-memory traffic; it is explicitly
    preparing a small set of workspace pointer slots before returning
  - the immediate post-script continuation is visible too. After the `0x1DB9`
    stream finishes at `RET` byte `0x1DBE`, the same selector re-enters with
    `top=0x18EB` and `next=0x5602` in one pass of the trace, and with
    `top=0x18EB` and `next=0x5600` in another. The selector then consumes byte
    `0x1B` from `0x18EB` and advances to `0x18EC`
  - a focused follow trace resolves that last ambiguity. The selector byte
    `0x1B` at `0x18EB` maps through the live `0x0F30` table to handler
    `0x028D`, and `0x028D` is the tiny `LD HL,(0x4560) ; RET` stub. In the same
    run, that stub returns with `(0x4560)=0xFFFF`, re-enters the threaded stream
    at live `pc=0x18EC`, and then immediately reaches live `pc=0x5602`
  - the high-memory target is not the resident `CLI.SY` image. The captured RAM
    bytes at `0x5600` are `0E 14 E7 56 21 7E 57 E7 0C E7 5E 44 49 53 4B 20`,
    which match the preserved head of `TDISK.SM` exactly. So this short branch
    really does unwind out of the low-memory selector machinery and into the
    freshly loaded ordinary `.SM` program image
  - the `0x1CF4` case behaves differently. Its first byte still dispatches
    `0x2E -> 0x0E4F`, but after that helper returns the same path re-enters the
    selector at a later stacked pointer `0x1CFE` while keeping `next=0x1D99`
  - that second re-entry is important because it shows the deeper branch is not
    just repeating one isolated compare. It is walking a longer threaded stream:
    `0x1CF4` starts with byte `0x2E`, and a later nested entry at `0x1CFE`
    begins with byte `0x2E` again, selecting the same `0x0E4F` compare helper
    with a new stacked continuation `top=0x25E8 next=0x1CFF`
  - in the current `trace18` live RAM image, that nested `0x25E8` node is not a
    visible code block at all: the bytes at `0x25E8..0x2608` are all zero. So
    the longer branch is at least sometimes comparing against or threading
    through an empty buffer/work area rather than stepping into another obvious
    executable helper region
  - a focused follow trace now sharpens that branch too. The first `0x1CF4`
    selector byte still dispatches to `0x0E4F`, which runs with
    `DE=0x0003, HL=0x0003` and returns into the selector re-entry at `0x1CFE`
  - from that nested re-entry, the same dispatcher advances to
    `top=0x25E8 next=0x1CFF`, then routes straight back to `0x0E4F` again. On
    that second compare call, the live state is `DE=0x2300, HL=0x25E8` while
    the bytes at `0x25E8..0x25EB` are still all zero
  - after that second `0x0E4F` call, execution reaches live `pc=0x1CFF` with
    `top=0x1D99 next=0x1743`, and the very next visible state falls back into
    the same selector loop at `0x0127` with `top=0x1CF4 next=0x1D99` and
    `DE=HL=0x0026`
  - one more focused pass explains where that `0x0026` comes from. The live
    bytes at `0x1CFF` are `CD 72 1E`, and the callee at `0x1E72` is the tiny
    `LD E,(IX+0x0C) ; LD D,(IX+0x0D) ; RET` helper. In the same trace window,
    `IX=0x2300` with `(IX+0x0C)=0x0026`, and the selector re-entry immediately
    after `0x1E72` arrives with `DE=HL=0x0026`
  - the companion step at `0x1CF6` is the matching base-pointer leg: it starts
    with `PUSH IX ; POP DE`, so the first nested compare call uses `DE=IX`
    itself (`0x2300`, then later `0x2318`, `0x2330`, `0x2348`) while
    `HL=0x25E8`
  - across those repeats, `BC` stays `0x0018`, `IX` advances by that same
    stride after each compare cycle, and the traced word pairs move in lockstep:
    `IX+0x0A/0x0C = 0x0003/0x0026`, then `0x0026/0x0041`, then
    `0x0041/0x0044`, then `0x0044/0x0047`
  - that makes the long branch more concrete than before: it is iterating a
    `0x18`-byte `IX`-backed record stream, comparing first against the record
    base and then against the record word at offset `0x0C`, while repeatedly
    routing those values back through the same low-memory selector/comparison
    loop
  - the live record bytes identify that stream as a normalized directory cache,
    not an abstract helper table. Starting at `0x2300`, the `0x18`-byte entries
    decode as visible file records such as `SYS.SY`, `CLI.SY`, `ER.SY`,
    `OKI80LP.SY`, `OKI82LP.SY`, `OKI84LP.SY`, `MATPAC.SY`, `HP.SY`, `FLO.ST`,
    `SM6.ST`, and later `HORLOGE.SR`, `BIORY.BS`, and `TSTFLO.SM`
  - that also resolves the role of `0x25E8`: it is not just an arbitrary zero
    work buffer. `0x25E8 - 0x2300 = 0x02E8`, which is exactly `31 * 0x18`, and
  - a refreshed focused run makes the `0x1D8F -> 0x1CD6/0x1CF6` loop more
    concrete on the SDCC-relevant path:
    - the loop starts with `DE=0x0003` and `IX=0x2300`, so the first
      `RST 20 / 0x2E` compare at `0x1CF6` matches the normalized record whose
      `+0x0A/+0x0B` start sector is `0x0003` (`SYS.SY`)
    - after the first compare succeeds, `0x1CFF -> 0x1E72` reloads
      `DE = (IX+0x0C/+0x0D)`, so the next pass looks for the record whose start
      sector equals the previous record's end sector: `0x0026` (`CLI.SY`), then
      `0x0041` (`ER.SY`), then `0x0044` (`OKI80LP.SY`), and so on through the
      later `.SM` region including `SMILE.SM`, `SHOW.SM`, `EDISK.SM`,
      `TDISK.SM`, and `CCOPY.SM`
    - the live `flow-long` trace shows that this walk preserves the first byte
      of the current normalized record in `A` (`0x53` for `SYS`, `0x43` for
      `CLI`, `0x45` for `ER`, `0x4D` for `MATPAC`, `0x45` for `EDISK`,
      `0x54` for `TDISK`, ...), while the compare helper only toggles flags
    - that is a useful negative result for the accepted-class investigation:
      the upstream `0x1D8F` scan is a contiguous-sector enumerator over the
      normalized cache, but it does **not** itself synthesize the later
      accepted `A=0x18` seen at `0x194B -> 0x1DAB -> 0x18E0`
    - the refreshed `tail2` probe also shows the range-tracking workspace words
      `0x2B80/0x2B82/0x2BC5/0x2BDC` still zero on the later accepted-return
      path. So the `A=0x18` transition is downstream of this contiguous-record
      walk and downstream of the `0x1D57` range-selection scratch state
  - one more focused hop on the immediate `0x1D99 -> 0x1743` handoff narrows
    that downstream boundary further:
    - the refreshed `flow-build` trace now prints the real normalized record at
      `IX` plus the first bytes of the `0x711A` output buffer. On the successful
      `EDISK` run, `0x1771` sees `IX=0x2300` with record bytes
      `SYS     SY`, start/end `0x0003/0x0026`, flags `0x10 0x85`; the next pass
      sees `IX=0x2318` with `CLI     SY`, then `0x2330` for `ER      SY`, and
      so on
    - the `0x711A` output bytes on those same passes are just the padded record
      name being copied into the formatter workspace (`SYS     ` for the first
      pass). They do not expose the later accepted class byte or any new
      synthesized `0x18`
    - the `best/cur` scratch values still evolve like a ranking or length
      metric while the current record advances across `0x2300`, `0x2318`,
      `0x2330`, ...; but the record metadata reaching `0x1743..0x1790` is still
      exactly the already-normalized cache content, not a new class-specific
      structure
    - so the local hypothesis for this hop holds: `0x1D99 -> 0x1743..0x1790`
      is a consumer/formatter over the normalized cache and the `0x711A`
      workspace, not the place where the later accepted `A=0x18` is created.
      That pushes the remaining SDCC-relevant unknown one hop later than the
      `0x1743` formatter path
  - a direct trace of the synthetic return confirms that the next hop is not
    `0x11B9` either:
    - extending the same `flow-build` slice to include `0x11B9` shows control
      returning there with the formatter already finished and the `0x711A`
      workspace populated from the normalized cache walk
    - the live `EDISK` run does not show any class-like rewrite at `0x11B9`.
      Instead, that point behaves as a plain unwind back into the CLI-side
      high-memory caller (`next=0x709B` or `0x70F6` in the captured returns)
    - so the earlier static decode is now backed by dynamic evidence too:
      `0x11B9` is just part of the synthetic return/unwind path paired with the
      `0x1737` `LD HL,0x11B9 ; PUSH HL ; CALL 0x1AC4` entry, and the accepted
      `A=0x18` must be synthesized later on the high-memory caller side rather
      than anywhere inside `0x1743..0x1790` or the immediate `0x11B9` return
  - one more narrow hop identifies that immediate high-memory caller:
    - the apparent `next=0x709B/0x70F6` words captured at `0x11B9` are data
      pointers, not code PCs. The real code-side return from the dispatcher is
      the stack-top caller at `0x6752/0x675A`
    - static bytes around that site decode as:
      `PUSH AF ; LD BC,0x711A ; RST 10 / 0x0E ; JP C,0x7017 ; LD DE,0x70F6 ; PUSH BC ; RST 10 / 0x1B ; ...`
    - the live trace matches that decode exactly: after the formatter path
      returns through `0x11B9 -> 0x11D2`, control resumes at `0x6752` with
      `AF=0x0044`, so the `JP C,0x7017` reject branch is *not* taken on the
      successful `EDISK` run
    - that means the immediate post-formatter accept/reject gate is the
      CLI-side `RST 10 / 0x0E` service on the populated `0x711A` workspace.
      The next ordinary successful step after that gate is the follow-on
      `RST 10 / 0x1B` call with `DE=0x70F6`
  - tracing that follow-on `RST 10 / 0x1B` call narrows the next boundary too:
    - the preceding `RST 10 / 0x0E` service is not a classifier. Its live
      target is `0x17B9`, which decodes to `LD HL,0x2BE3 ; LD BC,0x0012 ; LDIR ; JP 0x11B9`,
      so on this path it only copies the prepared 18-byte workspace record to
      `DE=0x70F6` and unwinds
    - the following `RST 10 / 0x1B` from `0x675A` does **not** use the nearby
      `0x17C4` wrapper on this artifact. The active jump-table entry lands at
      `0x11C0`
    - that `0x11C0` wrapper immediately calls `0x1D29`, copies the returned
      `HL` difference into `BC`, loads `HL` from `0x2BDE`, uses `OR L` only to
      refresh flags, then reloads `A` from workspace byte `0x2BE2`, restores
      registers, and returns
    - the callee `0x1D29` is itself tiny: `PUSH DE ; LD HL,(0x2B86) ; LD DE,(0x2B84) ; OR A ; SBC HL,DE ; POP DE ; RET`
    - on the successful `EDISK` run, that means the post-formatter `0x1B`
      service returns to the CLI caller with `AF=0x2044` and `BC=0x04D0`.
      The important correction there is byte order: this is `A=0x20` with
      flags `F=0x44`, not `A=0x44`
    - a direct watch on the snapshot service confirms where that `A=0x20` comes
      from. The active low-memory producer path reaches `0x1F2C`, which issues
      `RST 10 / 0x1C`; the service target at `0x1238..0x126D` then snapshots
      `0x2B88 -> 0x2BE2` and the `0x2B84/0x2B86` pair into `0x2BDE/0x2BE0`
    - on the traced `EDISK` run, that snapshot happens exactly once and changes
      the byte from `0x2B88=0x00` on entry to `0x2B88=0x20` by the time the
      `0x1253` copy block runs; final RAM still shows `0x2BE2=0x20`
    - a direct follow trace inside that same service pins down the local writer:
      during the `0x1238` path, execution reaches `0x1B04` with `0x2B88` still
      at `0x00`, and the next observed snapshot point at `0x1253` already sees
      `0x2B88=0x20`
    - that matches the static bytes at `0x1AF8..0x1B06`, where the active path
      falls through to the literal `LD A,0x20 ; LD (0x2B88),A` sequence at
      `0x1B02/0x1B04` before the `0x1238` helper copies the value into `0x2BE2`
    - so on the successful ordinary `EDISK` run, the `A=0x20` later returned by
      `0x11C0` is now traced one hop further back: it is the producer-side
      default byte materialized by `0x1B04`, then snapshotted by service
      `RST 10 / 0x1C`
    - so the post-copy `0x11C0 -> 0x1D29` wrapper is now fully explained on
      this path: it returns the saved producer-side byte `A=0x20` from `0x2BE2`
      plus the saved `0x2B86 - 0x2B84 = 0x04D0` span in `BC`. That path is not
      manufacturing a new class byte locally
    the full `0x18`-byte slot at `0x25E8` is all zero. So the long branch is
    comparing against a one-past-end zero sentinel immediately after a 31-entry
    cached directory table
  - the words at record offsets `+0x0A` and `+0x0C` are monotonic across that
    cache (`0x0003/0x0026`, `0x0026/0x0041`, `0x0041/0x0044`, ...,
    `0x03E5/0x03F8`, `0x03F8/0x03FF`), which fits the current control-flow
    picture: this branch is walking per-record boundary values inside the cached
    directory table rather than chasing executable continuations
  - in that focused window there are no hits at the watched high-memory program
    PCs (`0x5500`, `0x5503`, `0x5600`, `0x5602`, `0x5611`). So the current best
    reading is that the `0x1CF4/0x1CFE/0x25E8` branch is still low-memory
    threaded compare/workspace traffic that loops back into the same selector
    stream, not a real unwind into a loaded `.SM` image
  - that makes the current best reading more specific: the deep tail does not
    escape low memory by swapping to a new vector surface. It re-enters the
    same low-RAM dispatcher with different per-path work values, which then
    continues deeper into low-memory helper state
  - that matters because it removes another escape hatch: by this point the
    chain is no longer merely carrying low-memory return words such as `0x18BF`,
    `0x194B`, or `0x1D8F` on the stack. It is actively executing them, and the
    visible result is still more low-memory helper traffic rather than a return
    to any `CLI.SY` site
  - importantly, those deeper passes still do not surface any `CLI.SY` return
    site. Their active return slots remain low-memory values like `0x1F57`,
    `0x194B`, and `0x18BF`, while `0x2021` behaves coherently with the existing
    note that `0x200B..0x2021` is a rebuilt loading-batch setup block
  - that is the strongest negative result so far against an early unwind back
    into `CLI.SY`: even after the nested `RST 38` activity finishes, the live
    path continues executing internal low-memory transfer helpers rather than
    surfacing `0x647D`, `0x649B`, `0x64A5`, or `0x7017`
  - that still does not prove an eventual return into `0x647D+`. It only shows
    that after `0x647A` the path drops through at least two RAM-resident service
    layers: `RST 10` first dispatches via `(0x4564) -> 0x1063 -> 0x18D6`, then
    later reaches the separate low-RAM wrapper that calls the `0x0148`
    dispatcher and continues at `0x004D` / `0x0050`
  - the `SHOW` run remains useful too, but only conservatively: by dump time it
    had already mutated the same buffer to `-HOW.SM`, with `0x454B = 0`,
    `0x70B0 = 0`, `0x70B1 = 0`, `0x70B4 = 0x45C0`, `0x70C8 = 0x45C0`,
    `0x70CA = 'S'`, `0x70EC = 0`, `0x70E3 = 'C'`, and `0x7115 = 0x0003`. That
    is still too late-state to use as a clean snapshot of the initial transfer,
    but it no longer belongs in a separate "not a real program launch" bucket
  - current runtime context makes one narrower explanation for that malformed
    `-HOW.SM` state more plausible: the screen trace first lists `SHOW.SM` in
    the ordinary directory display, later shows `HORLOGE.SR` as a candidate
    document, and then reaches a screen fragment `* SHOW-` immediately before
    the CLI writes at `0x57FE/0x5804/0x5807` append `.SM` into the line buffer.
    That is consistent with `SHOW` having launched as an ordinary external tool
    and then failing early because no filename argument followed it, though the
    current trace still does not prove the exact viewer-side failure path
  - all three final RAM snapshots are still poor proxies for the exact handoff
    moment, because the targets are substantial tools and the dumps occur after
    additional parser or application work. `TDISK` ended with the familiar
    CLI-side signature (`0x454B = 0`, `0x70B0 = 0`, `0x70B1 = 0`,
    `0x70B4 = 0x45C5`, `0x70C8 = 0x45C0`, `0x70EC = 0x45C6`, `0x7115 = 0x0003`),
    while `BASIC` had already dirtied much more workspace (`0x70B4 = 0xED78`,
    `0x70C8 = 0x11C1`, `0x70EC = 0xFFFB`, `0x7115 = 0xDD33`), which is more
    consistent with post-launch application behavior than with the moment of
    loader transfer
  - the `CLEAR` side confirms that interpretation further. Its `0x65D0+` parser
    uppercases the token and explicitly accepts `DX0`, `DX1`, or `DX2`; after
    that it emits the inline prompt `Etes-vous s..r ?`, can print
    `INITIALISATION D'UN REPERTOIRE DU WINCHESTER`, and on success falls into
    `0x6738`
  - `0x6738+` therefore also reads as shared command UI/state handling rather
    than any executable handoff: it seeds workspace bytes `0x70E3` and `0x7115`,
    calls more CLI helpers around `0x5E69`, `0x5E7E`, and `0x63DD`, and then
    prints spacer-style text blocks rather than transferring into user code
  - a direct follow-up on those workspace bytes tightens that further:
    `0x6776/0x67A7` reload `0x70E3` as a simple loop counter while the nearby
    inline text at `0x67B0+` contains listing/header strings such as `nom.ex`,
    `etat`, and `attrib`; `0x7115` is seeded with `0x0003` at `0x6740` as part
    of the same CLI-side setup, not as a program-entry target
  - likewise, the shared sink at `0x56AE+` is visibly prompt/status formatting:
    after `0x5B87` clears workspace, it rewrites display bytes at `0x4388+`
    using flag-derived letters like `S/R/E/G`, `L/M`, and `W/D` before later
    callers such as `0x702E+` continue with `0x70EC`; so the nearest confirmed
    state after `0x6457` is still CLI UI maintenance, not executable transfer
  - the `NATHALIE.IM` control run now names one such caller precisely:
    `0x702E` enters with `A=0x1C`, `BC=0x1C04`, and `DE=0x45C0`, calls
    `0x5CDA`, and only then continues into the shared `0x7040+ -> 0x56AE`
    reprompt path. That makes `0x702E` the confirmed no-exec message branch,
    while `0x56AE` remains only the common CLI sink that follows it
  - the immediate predecessor is now confirmed too: `0x647D` compares the class
    returned by `RST 10 / 0x0B` against `0x0A` and jumps to `0x7017` on any
    other value. In the explicit `.IM` run that value is `0x1C`; in the known
    good `TDISK` launch the path reaches the same `0x647A` setup but does not
    surface `0x7017`, consistent with the accepted `0x0A` side of that test
  - a final pass over the remaining direct `0x569E` callers does not uncover a
    cleaner launch continuation either. The dense cluster at `0x62F7–0x6402`
    stays CLI-local: it decodes nibbles into workspace byte `0x45B7`, updates
    temporary bytes `0x5600/0x5601`, branches through small tables around
    `0x6392`, clears `0x70B0/0x70B1`, and even emits the inline date-style prompt
    `jj mm aa:` before calling helper `0x6448` and jumping back to `0x569E`
  - the other surviving direct callers are equally non-launch-like: `0x69D5`
    returns to `0x569E` immediately after CLI-local helper `0x5E8D`, while
    `0x702A` is still gated by the macro latch `0x454B` and byte `0x70B0`
  - current artifact-specific conclusion: no direct caller of `0x569E` found so
    far provides a nearby non-command-owned or non-UI-owned bridge from the
    proven bare-name `.SM` miss path into executable user-code entry
  - the current artifact now gives a clearer reason to treat `0x454B` as
    macro-related state rather than a generic launch flag: helper `0x5B60`
    gates on `0x454B` before printing the inline text `FIN DU FICHIER MACRO`,
    and the later `0x7058+` return path tests the same byte before emitting
    `MACRO STOPPEE A LA LIGNE`
  - that does not yet prove the exact encoding stored in `0x454B`, but it does
    make the local post-`.SM` continuation at `0x6457` look like a transition
    into macro-file or command-file handling layered on top of the shared CLI
    line-state sink, not a direct executable entry handoff
  - direct `CLI.SY` use sites now make the value semantics look even simpler:
    every currently found read of `0x454B` (`0x575C`, `0x645C`, `0x6483`,
    `0x65A4`, `0x65B9`, `0x66D5`, `0x7020`, `0x7051`, plus the helper entry at
    `0x5B60`) immediately reduces it with `OR A` and branches only on zero
    versus nonzero, while the only direct write seen in the preserved artifact
    remains `0x649A`
  - so, within this artifact, `0x454B` behaves less like a multi-valued handle
    and more like an effectively boolean “macro active / macro input source
    present” latch, even if the nonzero byte originally stored there still comes
    from a received-character path
  - the best current low-level reading of the write at `0x649A` is also more
    specific than before: under the default `Sys2-2` `RST 10` dispatcher,
    service `0x14` maps to `SYS.SY:0x057D`, which waits on port `0x05`, reads a
    byte from port `0x04`, and mirrors it through port `0x03`; the preserved
    `CLI.SY` artifact contains no direct write to the `RST 10` vector word
    `(0x4564)`, so the local evidence currently favors “store one received
    byte into `0x454B` to mark active macro state” over any handle-like
    interpretation
  - that remains a version-specific and still slightly qualified conclusion,
    because `RST 10` itself is indirect through `(0x4564)` and `SYS.SY` does
    expose a setter for that vector at `0x02A5`; so far, though, no nearby CLI
    evidence shows the `.SM` miss path retargeting it before the `0x6457`
    sequence runs
  - a narrower pass on that sink shows that `0x569E` is shared CLI line-buffer
    state, not a dedicated launch tail: `0x569B` first calls `0x5B60`, saves the
    current line pointer from `0x70B4` into `0x70C8`, saves the byte currently at
    `0x45C0` into `0x70CA`, repoints `0x70B4` to `0x45C0`, and then falls into
    `0x56AE`
  - that `0x56AE+` block immediately issues `RST 20 / 0x1B`, calls reset helper
    `0x5B87` which clears workspace words `0x4550`, `0x4552`, and `0x70EC`, and
    then rebuilds CLI-facing text/workspace around `0x4388`, `0x438D`, and
    `0x4555`; this is consistent with prompt or line-editor maintenance, not a
    direct transfer into `.SM` user code
  - the same `0x70B4/0x70C8/0x70CA` trio is restored later by the shared text
    loop around `0x588A–0x589E`, confirming that these bytes act like save/restore
    state for the active line buffer rooted at `0x45C0`
  - nearby flag flow also supports that interpretation: other paths write
    `0x70B0` or `0x70B1` and immediately jump to `0x569E` (`0x63C8`, `0x63D3`),
    while the earlier input loop at `0x568B` tests `0x70B1` before returning to
    command scanning at `0x5798`; these look like shared CLI re-entry flags, not
    launch-only file state
- A second pass on the same block strengthens the fallback-launch hypothesis:
  - after token copy, `0x6A11` -> `0x5E98` parses the optional qualifier and
    `0x6A14` -> `0x5FD1` immediately switches between two `RST 10` entry modes
    depending on whether `0x70E1` was set
  - `0x6A1D` -> `0x607C` resets `0x710B`, then performs additional mediated setup
    before the `0x6A42+` staged transfer logic starts; the same helper also seeds
    workspace pointers `0x70C6` and `0x70B4` with `0x45C0` and then immediately
    dispatches through `RST 10 / 0x12`, so it still looks like setup for a later
    directory or file operation rather than the final launch itself
  - the launch-specific branch becomes visible again only after that shared setup:
    at `0x6F60+`, the code either calls `0x5FE6` directly or falls back to
    `RST 10 / 0x00`, stores the resulting class in `0x70CC`, then uses current
    workspace state `0x70D4`, `0x70CB`, and the selected record before checking
    specifically for result code `0x06`
  - only after that `0x06` check does the path normalize the current record via
    `0x5ECC`, reload `DE = 0x711A`, and continue with additional `RST 10`
    mediation. That makes `0x6F60+` the clearest current boundary between shared
    directory-selection infrastructure and the later launch-oriented logic
  - the next continuation beyond that boundary is the first place where the path
    clearly leaves purely local CLI helpers: around `0x6FD2+` it begins calling
    low-memory SAMOS ROM entry stubs at `0x1003`, `0x1006`, `0x1009`, `0x100C`,
    and on error `0x1000`
  - tracing those stubs into the matching `Sys2-2` `SYS.SY` shows that they are
    not generic string helpers:
    - `0x1003` -> `0x1F47` and `0x1006` -> `0x1F59`, both of which converge on
      common helper `0x1F6D`, manipulate ROM workspace around `0x2B84–0x2BA1`,
      and then issue direct floppy-controller commands through ports `0x19` and
      `0x18`; this looks like a paired transfer setup / execute sequence rather
      than a cosmetic CLI callback
    - `0x1009` -> `0x1AC4`, which parses and canonicalizes a `DXn:`-style drive
      selector into SAMOS ROM workspace
    - `0x100C` -> `0x1D43`, which enters a range/check helper that feeds later
      mediated calls rather than returning immediately
    - `0x1000` -> `0x1FFA`, an error/timeout-style fallback that sets workspace
      byte `0x45BF`
    - the same deeper ROM path also contains explicit retry / progress state:
      helper `0x2124` increments a counter at `0x2B96` and compares it against
      `0x1E`, while helpers `0x2067`, `0x20B6`, and `0x21A8` poll or program the
      controller ports directly
    - the common helper `0x1F6D` now looks especially important: it derives two
      ranges from the current base-plus-span inputs, stores them in ROM
      workspace (`0x2B91`, `0x2B94`, `0x2B9D`, `0x2B9F`, `0x2BA1`), and then
      iterates while writing 16-bit destinations into the sector table rooted at
      `0x2BA3` that `docs/dev/SYS_SY_analysis.md` already identifies as the
      per-hole RAM-destination map
    - that is strong evidence that this branch is already computing a concrete
      sector-to-RAM transfer plan here, even though the final entry jump is
      still not named in the current trace
  - a follow-up check on the CLI-side return path after those ROM calls adds an
    important caution: the continuation around `0x7040+` is explicitly
    macro-aware, tests flag `0x454B`, emits the French string `MACRO STOPPEE A
    LA LIGNE`, and earlier setup around `0x648B–0x64A1` literally writes `MC`
    into workspace before enabling that same flag
  - this means the currently traced post-`0x06` continuation is definitely in a
    real file-transfer path, but it is not yet safe to label the whole return
    path as the ordinary bare-name `.SM` launch handoff; the traced branch is at
    least entangled with macro / command-file behavior
  - a second disambiguation pass tightens that further: within the current
    `CLI.SY` artifact, the low-memory ROM transfer stubs (`0x1003`, `0x1006`,
    `0x1009`, `0x100C`, `0x1000`) are only called from this single `0x6FD2+`
    slice, so there is no separate sibling caller inside `CLI.SY` that already
    exposes a cleaner ordinary-program continuation
  - even the `0x454B == 0` side of the post-transfer return does not jump into
    user code: after the shared copy at `0x7040+`, it lands at `0x56AE`, which
    immediately issues `RST 20 / 0x1B`, calls the reset helper `0x5B87`, and
    then rewrites CLI workspace around `0x4388`, `0x438D`, and `0x4555` while
    synthesizing flag-derived text bytes such as `S/R/E/G`, `L/M`, `8/6`, and
    `C/P`; that looks like CLI prompt/status maintenance, not a final `.SM`
    entry handoff
  - following the state prepared after that shared sink keeps reinforcing the
    same conclusion: the buffer pair `0x70F4/0x70F6` feeds helper `0x6865`,
    which formats attribute-style letters and then jumps to `0x6974`, where the
    inline text clearly includes directory/reporting strings such as `Blocs
    libres`; nearby text around `0x67F7` also contains a listing header
    (`nom.ex`, `etat`, `attrib`, `octets`, `blocs`, `date`)
  - stepping one hop back to the controlling split at `0x6F8F` does not rescue
    a cleaner launch path either: the non-`0x06` branch through `0x6FB0`
    immediately re-enters CLI-local handling, either jumping to the shared CLI
    sink at `0x66D5` or printing a tiny inline message before looping back to
    `0x6F15`
  - the three tiny helpers used by `0x6A42+` are now easier to decode once the
    inline service bytes are aligned correctly: they select between `RST 10 /
    0x00` vs `0x13`, `0x06` vs `0x15`, and `0x08` vs `0x16` depending on byte
    `0x70E1`
  - `0x70E1` itself no longer looks like a directory-derived file class. The
    main setter at `0x5E98+` clears it, scans the current command text for a
    `$` suffix, and only stores recognized qualifier letters `D` or `K`; a
    later command-specific path around `0x6DE8+` also forces it to either zero
    or `0xFF`. That makes the `0x6A42+` selector look even more like
    command-mode object handling than ordinary bare-name program launch
  - the surrounding command-table bytes finally make that local conclusion hard
    to avoid: around `0x6178+`, the artifact contains an explicit CLI dispatch
    table with names such as `PLIST`, `LIST`, `PRINT`, `PUNCH`, `SP`, `SC`,
    `XFER`, `LOAD`, `MON`, and `COPY`, and it jumps directly to `0x6A81` for
    `PUNCH`, `0x6A11` for `XFER`, `0x6B0D` for `LOAD`, and `0x6DD7` for `MON`
  - that also demotes the former `0x69FE–0x6A11` “generic fallback” candidate:
    in this artifact, `0x6A11` is itself the direct `XFER` entry from the
    command table, and the inner `0x69FE` entry is only re-entered from inside
    the surrounding `PUNCH` handler block at `0x6ABD`
  - the remaining shared-directory helpers look command-owned too. The earlier
    setup helper at `0x5E0D` has only one internal caller in this artifact,
    `0x667E`, and that caller sits under the `CHATR` dispatch entry at
    `0x616F`; the later users of `0x70D0/0x70E8/0x70EA` that were already traced
    sit under `DELETE` (`0x6149 -> 0x6506`) and `COPY` (`0x61C6 -> 0x6DE8`)
  - taken together, the traced uses of this normalized directory machinery are
    currently all command-owned in the `Sys2-2` artifact; no ordinary
    non-command-table caller into `0x5E0D`, `0x5F14`, or the later
    `0x70D0/0x70E8/0x70EA` setup slices has been identified yet
  - that puts the whole `0x6A11–0x6B94` neighborhood back into explicit
    command-handler territory. So while some of its helper structure is still
    useful for understanding SAMOS service contracts, it should no longer be
    treated as the main lead for ordinary bare-name `.SM` launch
  - tracing the matching `Sys2-2` `SYS.SY` syscall table adds a first
    version-specific mapping for some of those service bytes:
    - the shared low-bit dispatcher table rooted at `0x0F30` confirms several
      service-byte mappings directly: `0x00 -> 0x042F`, `0x06 -> 0x0490`,
      `0x14 -> 0x057D`, `0x15 -> 0x0582`, `0x16 -> 0x0587`, `0x17 -> 0x05AB`,
      and `0x18 -> 0x05B0`
    - `0x042F` still looks like a single-character output or dispatch primitive,
      while `0x0490` walks a zero-terminated byte string in `HL` and repeatedly
      invokes `0x042F`
    - the `0x14–0x18` family is now clearer too: `0x057D`, `0x0582`, and
      `0x0587` wait on bit 1 of ports `5`, `7`, and `3` respectively, then read
      from ports `4`, `6`, and `2`, and finally mirror the returned byte back
      through port `3`; neighboring handlers `0x05AB` and `0x05B0` stay in that
      same port-polling / channel-I/O family
    - correlating those ports with `docs/dev/HARDWARE.md` makes the family more
      concrete on this artifact: `0x14` is the USART0/permanent-I/O receive
      path (`0x05/0x04`), `0x15` is the USART1/cassette receive path
      (`0x07/0x06`), and `0x16` is the parallel-port receive path via
      `SPAR/PAR` (`0x03/0x02`)
    - that `0x14` interpretation also matches the annotated SYSMON `usart_boot`
      sequence at `0x0458`, which waits on port `0x05` bit 1, reads a byte from
      port `0x04`, and echoes it through port `0x03`
    - the same table also shows that several lower service bytes still land on
      tiny `LD (0x45xx),HL ; RET` stubs around `0x0291–0x02B5`, so the broader
      observation about workspace-setter services remains valid even though the
      earlier quick byte-to-handler examples were provisional
  - raw-byte recovery around the adjacent `0x6AC0+` continuation also reveals a
    concrete service-byte cluster on this same artifact: `RST 10 / 0x03`,
    `RST 10 / 0x06`, `RST 10 / 0x04`, together with `RST 20 / 0x2E`,
    `RST 20 / 0x45`, and `RST 20 / 0x44`
  - continuing the same `Sys2-2` `SYS.SY` trace resolves those three `RST 20`
    anchors to concrete handlers even if their full SAMOS meaning remains open:
    - `RST 20 / 0x2E` -> handler `0x0E4F`, a tiny helper that compares `HL`
      against `DE` via `or a` / `sbc hl,de` and returns with the flags set
    - `RST 20 / 0x45` -> handler `0x0C81`, which performs a fixed 64-iteration
      loop around lower-level service `RST 20 / 0x16` via helper `0x0C78`
    - `RST 20 / 0x44` -> handler `0x0C9F`, which saves state in workspace slot
      `0x4546`, invokes `0x0C81`, performs additional setup math, and then
      tail-jumps to `0x094D`
    - raw-byte recovery at `0x094D` shows that this tail target is itself another
      `RST 20`-heavy mediated phase, with an inline service-byte cluster
      including `0x56`, `0x1A`, `0x53`, `0x23`, `0x0C`, `0x0B`, `0x40`, and
      `0x31`; this still does not look like a simple direct `jp entry`
  - a first decode of that `0x094D` service cluster makes the mediated shape
    more concrete:
    - `RST 20 / 0x0B` -> `0x02B1` and `RST 20 / 0x0C` -> `0x02B5` are tiny
      setters that store `HL` into workspace slots `0x456A` and `0x456C`
    - `RST 20 / 0x53` -> `0x049A` saves `HL` in `0x45BA`, invokes `RST 20 /
      0x4F`, then immediately invokes the zero-terminated string helper
      `RST 20 / 0x06` before restoring `HL`
    - `RST 20 / 0x56` -> `0x0335` writes byte `0x5F` to workspace slot
      `0x4576`, then chains `RST 20 / 0x10`, `0x52`, and `0x11` before
      returning, so it is another wrapper around multiple OS services rather
      than a simple arithmetic helper
    - `RST 20 / 0x1A` -> `0x0368` loads `A = 0x83` and immediately dispatches
      through `RST 20 / 0x3E`, which again looks like a wrapper into a lower
      service family
    - `RST 20 / 0x31` -> `0x025E` dispatches through `RST 20 / 0x32`, stores
      the returned `A` in workspace byte `0x0438`, and then jumps onward to
      `0x05DF`; that continuation is another tiny pointer-unwind helper that
      swaps through the stack and reloads a 16-bit value through `DE`, not a
      direct executable transfer
    - the nearby `RST 20 / 0x53` helper also stays in this generic-utility
      family: after saving `HL` in `0x45BA` it calls `RST 20 / 0x4F`, which is
      another stack/pointer shuffler, then invokes the zero-terminated string
      emitter `RST 20 / 0x06`
    - the remaining direct call in the cluster, `RST 20 / 0x40` -> `0x0D1C`,
      enters a much larger helper block; a first raw-byte decode there shows
      repeated `DAA`-based BCD adjustment, threshold checks against `0x60` and
      `0x24`, and a tiny local lookup table at `0x0D6A+`, which makes it look
      much more like metadata normalization or date/time-style rollover logic
      than an executable-launch trampoline
    - the same `0x0D1C` block also delegates to tiny service targets near
      `0x08AE`, `0x08BE`, and `0x08C3`, plus state-save helpers near `0x0AAF`;
      those dependencies reinforce that this is still an OS utility phase inside
      the fallback chain rather than the point where user code is entered
    - one of those downstream targets, `0x0AAF`, even embeds register-label
      strings such as `A`, `B`, `C`, `D`, `E`, `HL`, `IX`, `IY`, `SP`, and
      `PC`, and calls a helper at `0x0B1F` that is built from formatting-style
      services (`RST 20 / 0x38`, `0x41`, `0x42`, `0x23`); that is strong local
      evidence that this whole family is monitor / formatting infrastructure,
      not the file-body or entry handoff for an ordinary `.SM` launch
  - not all of those exact service meanings are resolved yet, but the recovered
    table entries are useful stable anchor points for future SYS-side tracing
    because they come from the matching `Sys2-2` artifacts rather than from older
    `CLI.SY` notes or potentially misaligned linear disassembly alone
  - this overall shape looks much closer to "interpret typed token, classify the
    requested object, stage metadata, then load or launch" than to a direct
    immediate jump into user code
- All of the address-level conclusions in this section are therefore tied
  specifically to the preserved `private/floppies/extracted/Sys2-2-boot/CLI.SY`
  artifact unless stated otherwise.
- This directly matters for SDCC planning: small existing programs in the boot
  image often live in the `0x5500–0x56FF` region, which overlaps the known
  resident CLI area. Those examples are useful for reverse-engineering the loader
  contract, but they are not automatically safe templates for a standalone C
  target layout.
- A tighter pass on the active `EDISK` and `NATHALIE.IM` launch traces finally
  identifies the nearest confirmed producer of the live `0x2300` buffer:
  - the active path does **not** touch the earlier speculative `0x6F55..0x6FE6`
    `CLI.SY` branch at all. Adding those PCs to the live trace yields no hits on
    the successful `EDISK` run, so that command-owned `XFER`-side code is not
    the active upstream handoff for ordinary bare-name launch on this path
  - the stack-scripted handoff into the `0x1743` formatter is now slightly more
    concrete too: raw bytes at live `0x1737` are `21 B9 11 ; E5 ; CD C4 1A`,
    i.e. `LD HL,0x11B9 ; PUSH HL ; CALL 0x1AC4`, so `0x11B9` is a synthetic
    return target intentionally pushed ahead of the low-memory helper chain, not
    a normal caller discovered by code search
  - the same live runs show the real active producer-side loop one hop earlier:
    `0x1FAE` executes with `DE=0x2300` and immediately calls `0x2155`, while
    repeated `0x1F57` hits sit under return sites `0x1E34`, `0x194B`,
    `0x18BF`, and later `0x1D8F`. The surrounding bytes at `0x1F6D+` build ROM
    workspace ranges (`0x2B91`, `0x2B94`, `0x2B9D`, `0x2B9F`, `0x2BA1`) and a
    sector-to-RAM table rooted at `0x2BA3`, then `0x1FAE/0x2155/0x21B7` perform
    the actual transfer into `0x2300`, `0x2400`, `0x2500`, ...
  - that changes the local model of the `0x2300` cache materially: the nearest
    confirmed producer is no longer an abstract constructor candidate around
    `0x1743` or `0x19E1`, but the active ROM-side transfer/read loop centered on
    `0x1F47/0x1F57/0x1F6D`, with `0x18BF`, `0x194B`, and `0x1D8F` acting as
    successive consumers or post-read stages over the freshly filled pages
  - the negative control confirms this producer is shared. `NATHALIE.IM` hits
    the same `0x1FAE (DE=0x2300)` and repeated `0x1F57` landmarks before later
    reaching `0x18BF` with `IX=0x2558` and `ix13_14=0x0001`, then `0x1916`,
    `0x7017`, and `0x702E`. So executable vs non-executable behavior still
    splits on the normalized selector word, but that split happens **after** the
    same producer-side transfer machinery has already populated the candidate
    record/cache state
  - a caution from the same `NATHALIE.IM` dump: late snapshots can overwrite the
    original `0x2300` cache with subsequent message text, so the stable evidence
    for this producer comes from the trace sequence and the earlier record hits
    (`0x2438` for `EDISK.SM`, `0x2558` for `NATHALIE.IM`), not from the final
    post-refusal `0x2300` bytes alone
  - a focused decode of the next two active helpers removes another tempting but
    wrong interpretation:
    - `0x2155` is **not** the byte-transfer loop. Its live bytes are a seek
      routine: it calls helper `0x2188`, which chooses track variable
      `0x2B8B` or `0x2B8C` from drive-control byte `0x2B88`; then it stores the
      target track from `0x2B92`, computes the signed delta, and steps through
      ports `0x19` and `0x1A` with explicit delay loops. So the `0x2155` hits in
      the producer trace are head-positioning work, not record normalization
    - `0x21B7` is only a tiny compare helper,
      `PUSH HL ; OR A ; SBC HL,DE ; POP HL ; RET`, used by the active producer
      path to compare a destination/window boundary while preserving `HL`
    - the small helper at `0x213A` is also now clear: it masks the low three
      bits of the current hole index, selects one of the inline bit masks
      `01 02 04 08 10 20 40 80` at `0x214D`, and returns `HL = 0x2B9F` or
      `0x2BA0` depending on bit 3 of the hole index. That matches the live trace
      where later calls see `HL=0x2B9F` or `0x2BA0` before pulling per-hole RAM
      destinations from the confirmed sector table at `0x2BA3`
    - the resulting narrow conclusion is more useful than the original guess:
      the active producer chain around `0x1F47/0x1F57` separates into at least
      three roles already visible in live RAM:
      `0x1F6D` builds the hole/destination plan in `0x2B91..0x2BA3`,
      `0x2155` performs seek/head motion, and the real raw-transfer work sits
      one hop earlier again
    - the active transfer split is now specific enough to be useful:
      - `0x2067` is the real 256-byte sector-read path. It reads the current
        hole index from port `0x19`, uses `0x213A` plus the bitfields at
        `0x2B9F/0x2BA0` to decide whether that hole is still pending, fetches
        the destination address from the confirmed table at `0x2BA3`, validates
        the header track byte against the selected track variable
        (`0x2B8B/0x2B8C`), then reads `256` data bytes from port `0x1B` into
        `DE` while accumulating a checksum in `H`; the final byte is compared
        against that checksum, and on success the matching pending-hole bit is
        cleared in `0x2B9F/0x2BA0`
      - `0x20B2` is **not** another producer-side read candidate after all. Its
        live bytes show the sibling write path: it clears the pending-hole bit,
        reloads the same destination from `0x2BA3`, then pushes a framed block
        out through port `0x18` (sync bytes, selected track byte, `256` payload
        bytes via `OUTI`, checksum, trailing zero). So it belongs to the same
        sector-plan machinery, but on the write-back side rather than the cache-
        fill side used by launch
      - that leaves a cleaner current model for the active `0x2300` producer:
        `0x1F6D` constructs the hole-to-destination plan,
        `0x2155` seeks the target track,
        `0x2067` performs the actual sector read into RAM pages, and only after
        those pages exist do later low-memory consumers such as `0x18BF`,
        `0x194B`, and `0x1D8F` reinterpret them into normalized launch/cache
        records
    - the next active boundary is narrower now too: `0x194B` is not another raw-
      page parser. It already sits on the normalized-cache side of the split
      between raw transfer and executable selection
      - the critical local helper is `0x1D08`. Its live bytes are a 32-slot scan
        over the `0x2300` cache with stride `0x18`: it compares the first 10
        bytes of each record against a 10-byte lookup buffer in `DE`, returns with
        carry set on a match, and otherwise advances `HL += 0x18` until the slot
        count is exhausted. The live trace confirms `DE=0x2B76` whenever
        `0x194B` falls into this scan
      - `0x1E23` is the companion token-builder that feeds that lookup. Its live
        body starts with `HL=0x2B76`, calls `0x1A84` to build a padded filename
        token there, then `0x1EA9` skips following spaces/tabs, stores the
        resulting post-token cursor in `0x2BC7`, calls `0x1A53`, and returns with
        `DE=0x2B76`
      - the helper `0x1A53` is now concrete enough to explain why `0x194B` sits on
        the normalized side. It does `CALL 0x1A63 ; XOR A ; LD (0x2B9A),A ;
        JP 0x1F47`, and `0x1A63` simply seeds `BC=0x0000`, `HL=0x0003`, and
        `DE=0x2300`. So this stage explicitly re-enters the already-known
        transfer machinery with a destination rooted at `0x2300`
      - taken together, the `0x192E..0x195F` sequence now has a more specific
        interpretation:
        `0x1AC4` parses or canonicalizes the object prefix,
        `0x1E23` builds the 10-byte lookup token at `0x2B76` and refreshes the
        `0x2300` normalized-cache workspace through `0x1A53 -> 0x1F47`,
        and `0x1D08` then scans those already-normalized `0x18`-byte records for
        a matching name. So `0x194B` is best treated as the cache lookup / entry-
        selection stage immediately before the later selector tests, not as the
        place where raw sector pages are first interpreted into structure
    - the token-builder itself is now locally decoded enough to be useful:
      - `0x1A84` calls `0x1EA9` to skip leading spaces and tabs, then copies up
        to 8 characters into the destination buffer, padding the remainder with
        spaces via `0x1EB3`
      - each copied character is validated by `0x1CA0` against the delimiter
        table at `0x1CB1` (`'['`, space, tab, CR, `'/'`, NUL, `0x80`) and by
        `0x1C85`, which accepts digits directly, uppercases letters by clearing
        bit 5, and rejects other punctuation
      - if a `'.'` is encountered, the second loop copies up to 3 extension
        characters and again space-pads the remainder; otherwise it falls into
        the same filler path immediately. In practice this yields the exact
        `8+2/3` padded token shape later seen at `0x2B76`
      - the live `EDISK` buffer matches that decode directly:
        `0x2B76 = "EDISK   SM" ...`, while `0x1D08` compares exactly the first
        10 bytes of each normalized `0x18`-byte record against that token. So
        by the time the cache scan runs, filename canonicalization has already
        collapsed to a plain padded lookup key rather than a richer parser state
    - the matched-record handoff into `0x18BF -> 0x1916` is also more concrete
      now, and the byte layout matters because the launcher uses overlapping
      fields rather than cleanly separated words:
      - on the successful `EDISK` record at `0x2438`, the visible bytes are
        `... +0x0A/+0x0B = 00 E8`, `+0x0C/+0x0D = 00 F5`, `+0x10 = 6F`,
        `+0x11/+0x12 = 00 56`, `+0x12/+0x13 = 56 AE`,
        `+0x14/+0x15 = 57 10`
      - on the refusal control `NATHALIE.IM` at `0x2558`, the same overlapping
        pattern holds:
        `... +0x0A/+0x0B = 03 1C`, `+0x0C/+0x0D = 03 2B`, `+0x10 = 00`,
        `+0x11/+0x12 = 00 46`, `+0x12/+0x13 = 46 01`,
        `+0x14/+0x15 = 00 03`
      - those bytes line up exactly with the later launch trace:
        - at `0x18BF`, the stack-top pair matches record words
          `+0x0A/+0x0C` (`0x00E8/0x00F5` for `EDISK`, `0x031C/0x032B` for
          `NATHALIE`), and `A` later comes from byte `+0x10`
        - in the direct-hit path at `0x1968+`, `LD L,(IX+0x11) ; LD H,(IX+0x12)`
          returns the page-aligned load base (`0x5600` for `EDISK`, `0x4600`
          for `NATHALIE`)
        - then at `0x1908+`, `LD E,(IX+0x13) ; LD D,(IX+0x14)` pushes the
          cross-word selector (`0x57AE` for `EDISK`, `0x0001` for `NATHALIE`)
          built from the raw-load low byte and the entry high byte
      - that makes the normalized record contract sharper than before:
        the matched `0x2300` record already carries every byte later needed by
        the launch classifier in-place. The launcher does not derive those words
        from raw sectors at `0x18BF/0x1916`; it only reuses pre-normalized bytes
        from the chosen `0x18`-byte cache entry, with the aligned load base and
        the selector both assembled by overlapping adjacent record fields
    - the `0x0E -> 0x0A` split is now resolved enough to separate two nearby
      paths that were previously conflated:
      - the earlier CLI-visible accepted return really does come from
        `0x195F: LD A,0x0A ; SCF ; RET`, but only on the recursive
        `0x194B` path that stays on the transient workspace at `IX=0x5800`
      - the live successful `EDISK` trace for that branch is:
        `0x194B (AF=0x184D, IX=0x5800) -> CALL C,0x1DAB -> JR C,0x1956 ->
        0x1956..0x195D -> 0x195F -> 0x18E0 (AF=0x0A09, IX=0x5800)`
      - byte decode makes that local decision concrete. `0x1DAB` is just:
        `CP 0x18 ; SCF ; RET Z ; CP 0x11 ; SCF ; RET Z ; POP HL ; RET`
        So on the observed `EDISK` path, `A=0x18` makes the `CALL C,0x1DAB`
        site at `0x194C` return with carry still set, which takes the
        `JR C,+5` at `0x194F`
      - from there, `0x1956` tests bit 6 of workspace byte `0x2B88`
        (`LD A,(0x2B88) ; LD H,0x40 ; AND H ; LD A,H ; JR Z,0x1941`). In the
        accepted run, the immediately preceding `0x1A53` refresh has already
        promoted `0x2B88` from `0x20` to `0x40`, so the loop does **not** jump
        back and `0x195F` returns the accepted `A=0x0A`
      - the later direct-hit path through the matched normalized record is a
        different outcome. On the same successful `EDISK` launch, once
        `0x1D08` has matched `HL=0x2438`, execution goes
        `0x193D -> 0x1963 -> 0x1C4D`, but this is **not** the accepted `0x0A`
        return. On the live `EDISK.SM` record (`+0x0E=0x01, +0x0F=0x00`), the
        `0x1C4D` bitmask helper falls through its `RET Z` case, then the caller
        loads `HL=(IX+0x11/+0x12)=0x5600` and returns `A=0x0E`
      - that keeps the role of `0x1C4D` narrow and consistent with the earlier
        decode: it is still a generic pre-filter over record bits `+0x0E/+0x0F`,
        not the source of the accepted CLI-visible `0x0A`. The later ordinary
        executable-vs-refusal split still happens at the selector check
        `0x1916` (`0x57AE` accepted, `0x0001` refused)
      - the post-`0x18E0` jump target at `0x11D2` remains just a small
        unwind/restore stub (`POP IX ; PUSH AF ; LD A,(0x457F) ; OUT (0),A ;
        POP AF ; EI ; RET ...`), so it is also not the missing translator

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
- [ ] Confirm whether `0x69FE–0x6A7E` is the command-table miss or fallback path
  that turns a bare CLI token into file execution.
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
