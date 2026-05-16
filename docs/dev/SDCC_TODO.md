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
  - the `0x1CF4` case behaves differently. Its first byte still dispatches
    `0x2E -> 0x0E4F`, but after that helper returns the same path re-enters the
    selector at a later stacked pointer `0x1CFE` while keeping `next=0x1D99`
  - that second re-entry is important because it shows the deeper branch is not
    just repeating one isolated compare. It is walking a longer threaded stream:
    `0x1CF4` starts with byte `0x2E`, and a later nested entry at `0x1CFE`
    begins with byte `0x2E` again, selecting the same `0x0E4F` compare helper
    with a new stacked continuation `top=0x25E8 next=0x1CFF`
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
