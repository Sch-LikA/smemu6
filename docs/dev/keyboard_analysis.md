# Smaky 6 Keyboard Hardware & SAMOS ISR Analysis

## Current Snapshot

This document mixes stable hardware facts, current emulator behavior, and dated
probe notes from the keyboard bring-up. Treat the sections below in this order:

- `Hardware Model`, `Power-on FOUND=1`, and `keyboard_read_cla()` describe the
    current emulator model and the hardware-backed reasoning behind it.
- `Why Keys Reach the CLI`, `ESC key`, `Power-on Enter / inject`, and the later
    S471 mapping audits describe the current observed post-boot behavior.
- Dated probe notes remain in place for archaeology, but they are evidence logs,
    not standalone user guidance.

Current high-level state:

- Power-on boot uses the low-level CLA path with a virtual held Enter; no
    separate `autoboot` shortcut remains.
- Ordinary post-boot key injection uses the same CLA-facing path as physical
    keys; `-inject-str` is still a higher-level CLI automation shortcut.
- The emulator's strict host-matrix mapping is aligned with the currently known
    S471 facts, but it is still not a full physical keyboard model.

## Hardware Model

The Smaky 6 keyboard uses a **dedicated encoder EPROM (S471)** that interfaces with the Z80 via two I/O ports:

| Port | Direction | Description |
|------|-----------|-------------|
| 0x00 | IN (read) | **CLA** — Current Latched Key code (7-bit, bit 7 = 0 means key present) |
| 0x01 | IN (read) | **Status** — bit 2 = FOUND (key physically held) |

### Confirmed hardware latch topology (from schematic Nov 1978 — J. Zahn, and official Smaky 6 doc §10.4 CLAVIER)

Key ICs on the keyboard board:

- **B5 = S471** — keyboard encoder EPROM (scans matrix, generates 7-bit keycode)
- **B4, B6 = 4051** — 8-channel CMOS mux for key matrix column scanning
- **A8 = 4013** — dual D flip-flop; **generates the FOUND signal**
- **A5 = 4024** — 7-stage binary ripple counter, clocked by 300 KHz oscillator (scan timing)
- **B8 = 4093** — quad NAND Schmitt trigger (300 KHz oscillator / debounce)
- **A4, A6 = LS 257 (C157)** — quad 2-to-1 tri-state mux (drives the Z80 data bus)
- **A3 = LS 138** — 3-to-8 decoder (I/O address decode, "INTERLOW")

**FOUND and FULCLA latches (4013 FF2):**

- **SET** when the keyboard scanner detects a pressed key during its scan cycle.
- **RESET by the CLA read itself** — the `IN A,(0x00)` instruction generates a STROBE pulse
  that simultaneously reads the key code and clears both FOUND and FULCLA.
  (Official doc: *"Les deux bascules sont remises à zéro par la lecture de l'information."*)
- Writing port 0x01 (ISR ACK `OUT`) has **no effect** on the FOUND or FULCLA latches.

**ARRIVE** is the 300 KHz oscillator divided by the 4024 counter (~2.3 KHz). It controls
*scan timing* (triggers a new scan cycle), not FOUND clearing. Our earlier analysis that
ARRIVE directly resets the 4013 FF2 was incorrect.

**Reassertion within 200µs:** After the CLA read clears FOUND, the scanner immediately
restarts. If the key is still held, it finds the same key and reasserts FOUND+FULCLA within
**200µs** (one complete scan cycle at 300 KHz). The official doc notes that for two
consecutive keys in scan order, only **3µs** may separate the clear from the next FOUND=1.

**Status register not necessary:** The official doc states *"le registre de status du clavier
n'est en fait pas nécessaire"* — bit 7 of the CLA byte encodes the FOUND state directly:

- Bit 7 = **0** → FOUND=1, a regular key code is in bits 0–6
- Bit 7 = **1** → FOUND=0, bits 0–6 carry the **function key** bitmask (F1–F7)

**Critical consequence:** FOUND is cleared by **reading CLA (port 0x00)**. The emulator's
`keyboard_read_cla()` correctly clears the `found` software flag when it returns a key code,
directly mirroring this hardware behavior.

### Power-on FOUND=1 (automatic DX0 boot)

On real hardware the FOUND latch (4013 FF2) powers up in an undefined state, in practice
asserted.  The Phantom ROM boot menu at `0x003E` calls `kbd_wait` (`0x00FD`), which reads
CLA in a tight loop and exits as soon as bit 7 = 0 (FOUND=1).  With FOUND=1 at power-on
the first CLA read returns `0x00` (Enter) and the ROM immediately proceeds to boot from
DX0 — no user key-press required.

The emulator replicates this with `found=1`, `key_code=0x00`, and `physically_held=1`
in `keyboard_init()`.  `physically_held=1` models the scanner continuously reasserting
FOUND (200µs reassertion) as long as a key is held, so every `kbd_wait` loop in the
boot sequence exits immediately — both the Phantom ROM menu (0x00FD) and the SAMOS
init kbd_wait at 0x00B5 ("Disque souple ...").

`keyboard_frame_tick()` releases the virtual key (clears `physically_held` and `found`)
when the SAMOS ISR vector at `bus[0x4566..7]` is written to `0x003E` (SAMOS init at
`0x00CD`, **after** the init kbd_wait exits).  No `samos_loaded` flag, FIFO handoff, or
`iff1`-dependent CLA branch is needed in the current model.

No `samos_loaded` flag or `iff1` branch is needed in `keyboard_read_cla()`.  The pure
hardware model works identically for all callers.

### Emulator keyboard flags

- **`found`** — FOUND latch.  Set by power-on init and by `machine_inject_key()`.  `keyboard_read_cla()` clears it when returning a key code, then immediately re-sets it if `physically_held=1` (scanner reassertion model).
- **`physically_held`** — scanner reassertion gate.  `1` at power-on (models FOUND latch SET) until the SAMOS ISR vector is installed; also set by `machine_inject_key()` and cleared by `machine_release_key()`.  While `1`, every CLA read re-asserts `found=1` after returning the key code — exactly matching real hardware where the scanner refires within 200µs while a key is physically held.

Port 0x01 bit 2 (FOUND) reflects `found` (the latch state), not `physically_held` (the raw physical signal).  After a CLA read: `found=0`, `physically_held=1`, `reassert_pending=1` — bit 2 correctly returns 0 until the scanner reasserts.

---

## SAMOS ISR Keyboard Pipeline

The SAMOS interrupt service routine fires via RST 38h at 50 Hz (every ~20 ms).  The full keyboard section occupies addresses `0x014E–0x0197`.

### Stage 1 — Direct CLA read (0x015B–0x016D)

```asm
015B  LD HL, 0x4580     ; HL → "last-read" register
015E  LD (HL), 0x00     ; clear it
0160  IN A, (0x00)      ; *** CLA READ #1 *** clears FOUND; key in bits 0-6, bit7=0 if present
0162  BIT 7, A
0164  JR NZ, 016E       ; bit7=1 → no key → go to no-key path
0166  LD (0x457E), A    ; store key code at 0x457E
0169  XOR A
016A  LD (0x4558), A    ; (0x4558) = 0
016D  RET               ; early return — key captured in 0x457E
```

Result when key is pressed: key code stored at `0x457E`, `(0x4558)=0`, return.  
**FOUND is cleared** by the CLA read. If the key is still held, FOUND reasserts within 200µs.

### Stage 2 — No-key path / debounce (0x016E–0x0197)

Only reached when Stage 1's CLA read returned 0x80 (no active key press):

```asm
016E  AND 0x7F          ; mask bit7 (A = 0)
0170  LD (HL), A        ; (0x4580) = 0
0171  INC HL            ; HL = 0x4581
0172  LD (HL), 0x00
0174  INC HL            ; HL = 0x4582
0175  LD A, (HL)        ; A = (0x4582)  [inter-frame keycode register]
0176  CP 0x80
0178  RET Z             ; if sentinel 0x80: no prior key pending, return
0179  DEC HL            ; HL = 0x4581
017A  LD B, 0           ; B=0 → 256 iterations
017C  DJNZ 017C         ; busy-wait debounce (~2560 T-states)
017E  IN A, (0x01)      ; *** READ STATUS PORT ***
0180  AND 0x04          ; test FOUND bit
0182  RET Z             ; FOUND=0 after debounce → key released, discard
0183  IN A, (0x00)      ; *** CLA READ #2 *** — reads key into buffer path
0185  AND 0x7F
0187  LD B, A           ; B = key code
…    ; → continues into circular buffer management at 0x0188–0x0197
```

**What is now proven vs still unresolved:**

- Direct `SYS.SY` binary audit confirms the ISR really reads `0x4582` at `0x0175`.
- The same audit confirms the init sentinel write at `0x00A1` is to `0x458A`, not `0x4582`.
- Therefore the old `0x4582=0x80` permanent-block explanation is withdrawn.
- Live post-boot trace now confirms that `SYS.SY` repeatedly executes
    `0x0175 -> 0x0179 -> 0x019B -> 0x01C9 -> 0x01DF` with `0x4582 = 0x00`,
    `0x4581 = 0x00`, `0x458A = 0x80`, and `0x457C = 0x4596`, so Stage 2 and the
    early Stage 3 workspace walk are active even while the workspace is empty.
- Therefore the current gate is specifically `0x4582 == 0x80`, not `0x4582 == 0`.
- The precise semantic roles of `0x4581`, `0x4582`, and `0x458B..0x4595` remain
    unresolved. The missing piece is now the producer that seeds this workspace.

### Stage 3 — Circular buffer management (0x019B–0x01DF)

Reached from the Stage 2 / workspace path, not from the regular-key Stage 1 early
return at `0x016D`.

- Direct disassembly shows Stage 3 starts at `0x019B` with `DE=0x4580` and `HL=0x458A`.
- `0x458A` is initialised to `0x80` at `0x00A1`; `0x4595` and `0x45B6` are guard
  sentinels written by the same init block.
- Stage 3 does more than a simple one-byte enqueue: it scans and compacts an
  internal workspace structure before Stage 4 uses the circular-buffer write pointer
  at `0x457C`.
- Live trace confirms that the idle-state walk into `0x019B` and `0x01C9` can happen
  with `0x4581 = 0x00`, `0x4582 = 0x00`, `0x458A = 0x80`, and `0x457C = 0x4596`.
  So this path is not itself proof of a key being promoted; it can also be an empty
  housekeeping pass over the workspace.
- The exact semantic roles of `0x4580..0x4582` and `0x458B..` are still under
  re-audit, so the old simplistic description has been intentionally narrowed to
  binary-backed facts only.

### Stage 4 — Auto-repeat (0x01DF–0x0206)

Introduced in SAMOS **version 1.3** (confirmed from release notes). Runs at the end of every ISR frame.

The two key RAM locations are:

| Address | Octal   | Name              | Purpose |
|---------|---------|-------------------|---------|
| `0x4558`| `042530`| Repeat countdown  | Set to `0x23` (35 frames = **700 ms**) on first keypress; decremented each ISR frame; when it hits 0 it is reloaded to `3` (3 frames = **60 ms**) for the fast-repeat rate |
| `0x4577`| `042567`| Repeat key code   | Stores the last key code that was written to the circular buffer; re-injected each time the countdown fires |

```asm
01DF  LD HL,(0x457C)    ; HL = write pointer
01E2  DEC HL            ; HL → last-written slot
01E3  BIT 7,(HL)        ; test "slot filled" marker
01E5  RET Z             ; buffer empty → no repeat
01E6  LD HL,0x4558      ; HL → countdown
01E9  LD A,(HL)
01EA  OR A
01EB  RET Z             ; countdown=0 → repeat disabled
01EC  DEC (HL)          ; --countdown
01ED  RET NZ            ; not expired yet → wait
01EE  LD (HL),3         ; reload fast rate (3 frames = 60 ms)
01F0  LD A,(0x4577)     ; A = repeat key code
01F4  LD (0x4577),A     ; refresh
01F7  LD B,A            ; B = key code
01F8  LD HL,(0x457C)    ; HL = write pointer
01FB  LD A,(HL)
01FC  CP 0x80           ; next slot free?
01FE  JR Z,0205         ; skip if full
0200  LD (HL),B         ; [ptr] = key code  ← repeat injection
0201  INC HL
0202  LD (0x457C),HL    ; advance pointer
0205  RET
```

**Summary:** Initial delay = 35 × 20 ms = **700 ms**. Fast rate = 3 × 20 ms = **60 ms** (≈ 16.7 chars/sec).  Setting `(0x4558) = 0` disables repeat for that key (used by Stage 1 path at `0x016A` when the key is consumed non-blockingly via syscall 0x0E).

### Syscall consumers

| Syscall | Address | Source buffer |
|---------|---------|---------------|
| 0x0D (blocking char read) | 0x04F6      | Circular buffer at `0x457C`/`0x458A+` |
| 0x0E (non-blocking check) | 0x0516/0x0519 | `0x457E` (Stage 1 direct store, consumed on read in the emulator model) |

**The CLI uses syscall 0x0D.**  Direct binary audit confirms `0x0509` loops on the
`0x04F6` circular-buffer routine, while `0x0516` is the separate `0x457E` accessor.
Therefore normal typed input must reach the circular-buffer path somehow.  Exactly
how Stage 1, Stage 2, and Stage 3 cooperate post-boot is still under re-audit.

**Validated emulator-side fix (2026-05-14):** later printable matrix keys were still
failing visibly even after they reached the circular buffer because the direct
`0x457E` path was effectively sticky.  Runtime trace showed the CLI helper repeatedly
returning through `0x5B2B` while syscall `0x0E` re-read the same stale byte at
`pc=0x0519`.  Making the emulator consume `0x457E` on that accessor read removed the
stale direct-key path, after which the same traced `a s d` run reached visible
insertions at `pc=0x590F` for all three keys.

---

## Why Keys Reach the CLI

The SAMOS ISR fires at 50 Hz.  Each frame:

1. **ISR ACK** (`OUT (0x01), 0x08` at `0x0048`): no keyboard state change in the unified model.
2. **Stage 1** (CLA read #1 at `0x0160`): `keyboard_read_cla()` returns `key_code` if `found=1`, else `0x80 | fonct_bits`.
   - If bit7=0 (regular key): SAMOS stores it to `0x457E` (syscall 0x0E) and returns early — circular buffer NOT touched.
   - If bit7=1: `AND 0x7F` strips bit7 and stores `fonct_bits` to `0x4580` (GETFON register). Falls into Stage 2.
3. **Stage 2** (no-key path, 0x016E): reads `0x4582` at `0x0175` and returns early only if that byte equals `0x80`.  Direct binary audit of `SYS.SY` on 2026-05-13 confirmed the init sentinel write is to `0x458A` at `0x00A1`, not to `0x4582`, so the old "Stage 2 is permanently blocked" claim is not supported by the binary.
4. **Runtime Stage 2 / 3 state:** a repo-local trace (`tmp/kbd_stage2_probe_trace.log`) now shows that `SYS.SY` repeatedly executes `0x0175 -> 0x0179 -> 0x019B -> 0x01C9 -> 0x01DF` post-boot with an empty workspace (`0x4581 = 0x00`, `0x4582 = 0x00`, `0x458A = 0x80`, `0x457C = 0x4596`).  So Stage 2 and early Stage 3 are alive at runtime even before any bridge candidate has been identified.

**Current emulator path:** physical keys enter through the CLA-facing latch in
`keyboard_read_cla()`. Ordinary host scancodes resolve through the S471 matrix,
become `found/key_code`, and are then observed by `SYS.SY`; printable host text
uses the same ordinary-key path after `SDL_TEXTINPUT` decoding. Once `SYS.SY`
commits a still-held key into the circular buffer, the emulator arms `0x4558`
and `0x4577` from the `0x457C` advance hook and drops the CLA-visible hold so
Stage 4 owns subsequent repeat timing.

**Real post-boot hardware path:** resolved one step further, but still not complete.
The binary proves that the CLI waits on the circular buffer, and two runtime audits on
2026-05-13 narrowed the split clearly:

- delayed `-inject-keycode 0x41` at the live `Sys1-H.dsk` CLI prompt follows the
  Stage 1 path exactly: status goes to `0x0C`, the next CLA read at `pc=0x0162`
  returns `0x41`, and SYS.SY writes `0x41` to `0x457E` at `pc=0x0169`;
- in that one-key CLA trace window there was no write to `0x457C`/`0x457D`, no
  circular-buffer store, and no visible prompt-line update;
- control injection via `-inject-str "A"` did produce a visible `A` on the CLI line,
  and traceflow showed that visible input goes through the blocking-read + line-editor
  path in `CLI.SY`, not through `0x457E`.

The confirmed visible path is:

1. `CLI.SY` line-input loop at `0x5857` calls key helper `0x5B29`.
2. `0x5B29` issues SAMOS syscall `0x47`, then SAMOS blocking read syscall `0x0D`
    (encoded as `RST 20h` followed by byte `0x0D` at runtime `0x5B2C`).
3. The returned character is dispatched at `0x585D..0x586D`.
4. Normal character insertion flows through helper `0x58DD..0x590F`.
    Runtime trace of visible `A` showed:
    - `pc=0x58FD`: cursor pointer `0x7014` advanced from `0x45C0` to `0x45C1`;
    - `pc=0x590E`: line buffer `0x45C0` changed from `'-'` to `0x41` (`'A'`).
5. Cursor redraw then runs through `0x5A7B..0x5AF9`, restoring `'-'` at the new cursor
    position (`0x45C1`).

**Held-CLA probe (2026-05-13):** a delayed `-inject-keycode 0x41` held for 20 ISR
frames at the live CLI prompt still did not seed the Stage 2 / 3 workspace. Runtime
trace showed repeated cycles of:

- status read `pc=0x0044` returning `0x0C`;
- ISR CLA read at `pc=0x0162` returning `0x41`;
- Stage 1 store at `pc=0x0169` rewriting `0x457E`.

During the hold window there was no workspace mutation attributable to the raw key:
the injected key kept exercising the Stage 1 `0x457E` path, but the Stage 2 / 3
idle-state values (`0x4581 = 0x00`, `0x4582 = 0x00`, `0x458A = 0x80`, `0x457C = 0x4596`)
did not change and there was still no circular-buffer pointer advance at `0x457C`.
This rules out the simplest hypothesis that merely holding a regular CLA key causes
`SYS.SY` to seed its internal workspace and promote that key into the circular-buffer
path on later ISR frames.

So the remaining open question is now narrower: what producer writes the
`0x4581..0x4595` workspace that the already-active Stage 2 / 3 machinery walks,
and how a real hardware-originating key becomes represented there given that the
direct CLA Stage 1 path only updates `0x457E`.

### Archived Probe Notes

The notes below are preserved because they record discriminating runtime probes
that shaped the current model. They should be read as dated evidence, not as a
separate replacement for the current snapshot above.

**Archived shortcut comparison (2026-05-13):** an earlier repo-local trace using
`-inject-str "A"` through the old direct circular-buffer path confirmed that a
shortcut enqueue could reach the CLI without touching the Stage 2 / 3 workspace.
That observation remains useful as archaeology, but it is **not** the current
physical-key implementation any more.

**Prompt-time workspace poke probes (2026-05-13):** a temporary
`-poke-on-prompt <addr> <byte>` probe hook in `src/main.c` now schedules RAM writes
once the CLI prompt is stable and applies them at the start of the next frame,
before `machine_int()` / `machine_run_frame()`.

- A prompt-time poke of `0x4580 = 0x41` (`tmp/kbd_workspace_poke_trace.log`) does
    land before the next frame, but the very next ISR pass still shows
    `0x4580 = 0x00`, `0x4581 = 0x00`, `0x4582 = 0x00` at `0x0175` / `0x019B`.
    That matches the disassembly at `0x015B..0x0174`: `SYS.SY` explicitly resets
    `0x4580` and `0x4581` every pass before the Stage 3 walker runs.
- A prompt-time poke of `0x4582 = 0x80` (`tmp/kbd_gate_poke_trace.log`) persists
    into the next frame and suppresses the `0x0179` debounce / second-CLA-read block.
    The trace shows `0x0175` followed directly by the later `0x019B` / `0x01C9` /
    `0x01DF` housekeeping path with `0x4582 = 0x80` still visible.

These probes establish two more constraints on the missing producer:

- writing `0x4580` before the ISR is not enough, because Stage 2 overwrites it;
- `0x4582` is a live gate, but it is not itself the missing character source.

**Function-bit probe (2026-05-13):** a second prompt-time probe now sets
`m->kbd.fonct_bits` directly (`-fonct-on-prompt 0x01`), letting the existing
keyboard model drive a real nonzero `0x4580` through the normal CLA read path.
That produces the first confirmed non-idle Stage 3 behavior at the live prompt:

- the ISR no-key path rewrites `0x4580` from `0x00` to `0x01` every frame;
- `SYS.SY` toggles `0x458B` between `0x01` and `0x81` (`pc=0x01B2` / `0x01D9`),
    matching the Stage 3 duplicate-mark / compaction logic;
- the circular-buffer pointer at `0x457C` advances from `0x4596` to `0x4597` at
    `pc=0x0205`, then the blocking-read path consumes the byte and restores it to
    `0x4596` at `pc=0x04EB`;
- the CLI line buffer at `0x45C0` briefly receives `0x01` and restores `'-'`, so
    the enqueued byte is a non-printable function-bit code rather than a normal
    visible character.

So a live nonzero `0x4580` is sufficient to make the existing Stage 3 / 4 path do
real work. That narrows the remaining regular-key question further: the missing
producer for post-boot visible keyboard input is not "any source of nonzero
`0x4580`", but specifically the source that seeds the regular-key workspace / data
which Stage 3 later promotes into the circular buffer.

**PC-timed live-prompt probes (2026-05-13):** to test the regular-key side more
precisely, `src/main.c` now has a temporary `-poke-at-pc <pc> <addr> <byte>` probe
hook that arms only after the CLI prompt is stable and writes once when execution
reaches the specified PC.

- `-poke-at-pc 0x0179 0x4581 0x41` at the live prompt does land exactly where
    intended, but the later Stage 3 / CLI behavior is unchanged from the baseline
    function-bit case: the emitted byte remains `0x01`. So `0x4581` is not the
    payload source for this already-live Stage 3 path.
- `-poke-at-pc 0x0179 0x4580 0x41` at the live prompt does propagate. The trace
    shows:
    - `0x4580` rewritten from `0x01` to `0x41` at `pc=0x0179`;
    - Stage 3 storing `0x41` into `0x458B` at `pc=0x01BB`;
    - Stage 4 exposing `0x4577 = 0x41` at `pc=0x0200` and advancing `0x457C`;
    - the blocking-read path later dequeuing that byte, with `AF=0x4100` visible at
        `pc=0x04EE`.

**Prompt-line insertion follow-up (2026-05-13):** the remaining ambiguity after the
first `0x04EE` dequeue trace was whether the forced `0x41` actually reached the
visible CLI line buffer or was consumed by some intermediate helper. A narrower trace
slice on the `CLI.SY` line editor resolves that directly:

- after the forced dequeue, `CLI.SY` reaches the normal insertion helper at
    `0x58DD..0x590F` with `AF=0x411A` / `AF=0x4142`;
- at `pc=0x590E`, the cursor has advanced from `0x45C0` to `0x45C1` and the pending
    character is still `0x41`;
- at `pc=0x590F`, the prompt buffer write occurs: `[45C0] 0x2D -> 0x41` (`'-' -> 'A'`).

So the forced regular-key payload does reach the standard visible `CLI.SY` insertion
path, not just the SAMOS blocking-read dequeue path. The earlier broad greps missed
this because they were checking neighboring cells (`0x45C1`, `0x45C2`, etc.) rather
than the actual first insertion at `0x45C0`, and later function-bit cycles then moved
the cursor and changed subsequent cells.

So the currently reproduced Stage 3 / 4 payload is sourced from the live `0x4580`
value, not from `0x4581`. The unresolved regular-key question is therefore even
sharper: what real hardware-side process causes a post-boot regular key to appear
as the right live Stage 2 source value, given that raw CLA injection never produces
that state on its own.

**CLA override probe (2026-05-13):** a new one-shot `-cla-at-pc <pc> <byte>` hook
was then used to override exactly one `keyboard_read_cla()` result at the live prompt.
Forcing the Stage 1 read at `pc=0x0162` from the held regular key's natural `0x41`
to `0xC1` is sufficient to reproduce the whole previously forced `0x4580` success path:

- the override fires at `pc=0x0162` and `SYS.SY` immediately stores `0x4580 = 0x41`
    at `pc=0x0171`;
- the existing Stage 2 / 3 path then runs unchanged: `0x0175 -> 0x0179 -> 0x019B ->
    0x01BB -> 0x0200 -> 0x0205`;
- the promoted byte later dequeues at the SAMOS blocking-read path with
    `pc=0x04EE` showing `AF=0x4144` / `A=0x41`;
- `CLI.SY` reaches the normal insertion helper and writes `[45C0] 0x2D -> 0x41`
    at `pc=0x590F`.

So the missing ingredient is narrower again: for a post-boot regular key, SAMOS only
needs to see a bit-7-set CLA value whose low seven bits still carry the regular code.
The remaining hardware-fidelity question is what real producer or timing edge causes
that mixed-format CLA byte on original hardware.

**Status-timing probe (2026-05-13):** the next discriminating check targeted the
FOUND re-check between the two CLA reads. A held raw key (`0x41`) was kept active,
the first CLA read at `pc=0x0162` was forced to `0x81`, and the keyboard status read
at `pc=0x0180` was forced to `0x0C` so `SYS.SY` had to take the second-read branch.
That probe shows:

- the forced status does open the branch: `SYS.SY` reaches `pc=0x0183`;
- the actual second CLA read happens at `pc=0x0185`, not `0x0183`;
- that second CLA read still returns `0x80`, so the held key has not reasserted in
    time for the same ISR pass;
- the downstream payload therefore remains the old function-bit value `0x01`, and
    the visible CLI insertion stays `[45C0] 0x2D -> 0x01` rather than `'A'`.

So the remaining gap is now narrower still: it is not just "take the second-read
branch," because that can be forced. The missing behavior is the hardware timing that
reasserts FOUND / CLA early enough for the second CLA read at `pc=0x0185` to see the
regular key instead of `0x80`.

**Second-read payload probe (2026-05-13):** the next probe targeted the actual second
CLA read directly. With `-fonct-on-prompt 0x01`, forced status `0x0C` at `pc=0x0180`,
and forced CLA `0x41` at `pc=0x0185`, the trace shows:

- `SYS.SY` keeps the original Stage 2 function-bit payload in `0x4580 = 0x01`;
- the second CLA read writes a separate later payload path, with `0x4581 = 0x41` and
    a later dequeue showing `pc=0x04EE` / `AF=0x4100`;
- but the first visible CLI insertion still remains the earlier `0x01`, because that
    first queue item is still emitted before the later `0x41` entry reaches the line editor.

So the second CLA read is not irrelevant. It can carry the regular code, but in the
current execution order it becomes a later queue item, not the first visible character.

**First-payload suppression probe (2026-05-13):** when that same second-read `0x41`
probe is combined with a live-prompt `-poke-at-pc 0x0179 0x4580 0x00`, the result is
the full visible regular-character path again:

- the first `0x01` payload is suppressed before Stage 3 consumes `0x4580`;
- Stage 3 then stores `0x41` into `0x458B` at `pc=0x01BB` and Stage 4 exposes
    `0x4577 = 0x41` at `pc=0x0200`;
- the blocking-read path later dequeues `A` (`pc=0x04EE`, `AF=0x4144`);
- `CLI.SY` reaches the standard insertion helper and writes `[45C0] 0x2D -> 0x41`
    at `pc=0x590F`.

This means the remaining discrepancy is now even more precise: the hardware-faithful
bridge is not only about making the second CLA read see the held key, but also about
why the first `0x4580` / function-bit payload does not occupy the first visible slot
on original hardware when a regular key is processed post-boot.

**Ordering rule now confirmed from the dual-payload traces:** the second-read path is
re-entering the same store logic around `0x0171`, but with different `HL` depending on
what survived the earlier pass.

- In the dual-payload run (`0x4580 = 0x01`, second CLA forced to `0x41`), the first
    Stage 3 pass promotes `0x4580` and advances `0x457C` from `0x4596` to `0x4597`.
    The re-entry after `pc=0x0198` then stores the second-read `0x41` into `0x4581`,
    not `0x4580`, and a second Stage 3 pass promotes that later entry.
- In the suppression run (`0x4580` poked to `0x00` at `pc=0x0179`), the same re-entry
    path instead stores the forced second-read `0x41` back into `0x4580`, which is why
    that `0x41` becomes the first visible CLI insertion.

So the remaining hardware-side question is now about exact ordering / slot selection:
what real machine condition makes the regular-key payload win slot `0x4580` instead of
being queued behind an earlier function-bit value.

**Late-clear probe at `pc=0x0198` (2026-05-13):** clearing `0x4580` only after the
second CLA read is not sufficient. In the dual-payload run with forced status `0x0C`
and forced second-read `0x41`:

- `0x4580` is successfully cleared at `pc=0x0198`;
- the re-entry still stores the regular-code payload into `0x4581`, not `0x4580`;
- the immediate Stage 3 walk then takes the empty-slot housekeeping path
    `0x019B -> 0x01C9 -> 0x01DF` and does **not** promote the lone `0x4581 = 0x41`
    entry into the first visible CLI slot.

So the problem is narrower than "clear `0x4580` before Stage 3."  The regular-code
payload must win slot `0x4580` itself; a lone `0x4581` value is not enough for the
current Stage 3 walk to make it the first visible character.

**First-walk slot-occupancy probe (2026-05-13):** forcing the dual-payload case to
restore `0x4580 = 0x41` exactly at the first `pc=0x019B` walk is sufficient to make
the first visible CLI insertion become `A` again.

- the probe leaves the second-read payload alive and also restores `0x4580 = 0x41`
    right before Stage 3 consumes it;
- the first Stage 3 pass then stores `0x41` into `0x458B` and exposes
    `0x4577 = 0x41` at `pc=0x0200`;
- the first dequeue reaches `pc=0x04EE` with `AF=0x4144`, and `CLI.SY` writes
    `[45C0] 0x2D -> 0x41` at `pc=0x590F`.

So the remaining discrepancy is now pinned to a single concrete condition: on real
hardware, the regular-code payload must occupy slot `0x4580` by the time the first
`0x019B` Stage 3 walk runs. If it is only present in `0x4581`, it loses the first
visible slot.

**Early-clear probe at `pc=0x0175` (2026-05-13):** clearing `0x4580` at the start of
the Stage 2 gate logic is already sufficient to make the forced second-read `0x41`
re-enter slot `0x4580` naturally.

- the first `0x0171` write still briefly stores `0x01` into `0x4580`;
- clearing `0x4580` at `pc=0x0175` makes the later re-entry from the second-read path
    write `0x41` back into `0x4580`, not `0x4581`;
- the first Stage 3 pass then promotes that `0x41`, and the first visible CLI write
    is again `[45C0] 0x2D -> 0x41`.

So the remaining hardware-side condition is now even tighter: the first function-bit
payload must be absent from `0x4580` before the second-read re-entry happens. Once
that slot is empty, the regular-code payload naturally wins it.

**Strongest current conclusion (2026-05-13):** the earliest successful suppression
point is already the first `pc=0x0171` write itself. Clearing `0x4580` immediately
after that write is enough for the second-read `0x41` to re-enter slot `0x4580`
without any later Stage 3-time patching:

- `pc=0x0171` first writes `0x01` into `0x4580`;
- the probe clears that value at the same `pc=0x0171` stop point;
- after forced status / second-read, the re-entry again reaches `pc=0x0171` and now
    writes `0x41` into `0x4580`;
- the first Stage 3 walk promotes that `0x41`, and the first visible CLI write is
    `[45C0] 0x2D -> 0x41`.

So the emulator-side behavior is now pinned down definitively: to reproduce the
original visible regular-key path, the initial function-bit value written at the
first `0x0171` stop must not survive until the second-read re-entry. Once that value
is gone, the rest of the software path behaves correctly.

**Rejected emulator-local hypothesis (2026-05-13):** disabling the emulator's
out-of-band `keyboard_frame_tick()` mirror of `fonct_bits -> 0x4580` does **not**
change the mixed-path result. With that mirror gated off, the same probe still shows:

- `pc=0x0171` writes `0x01` into `0x4580` from `SYS.SY` Stage 1 itself;
- the forced second CLA read still writes `0x41` into `0x4581`, not `0x4580`;
- the first visible CLI write remains `[45C0] 0x2D -> 0x01`, and only the later
    dequeue reaches `pc=0x04EE` with `AF=0x4100`.

So the definitive remaining mismatch is **not** the emulator's live GETFON mirror.
It is the CLA / Stage-1 timing semantics that let the first `0x0171` function-bit
payload survive long enough to occupy slot `0x4580` before the second-read regular
payload can re-enter.

**Positive model experiment (2026-05-13):** a new one-shot model hook now makes the
first CLA read of an injected held regular key return `0x80 | key_code` directly,
without any PC-specific `-cla-at-pc` override.  With `-inject-keycode 0x41`
`-inject-keycode-bit7-first-cla` at the prompt, the trace shows:

- the first CLA read logs `[inject] first CLA held-key bit7 -> C1 at pc=0162`;
- `SYS.SY` Stage 1 immediately stores `0x4580 = 0x41` at `pc=0171`;
- the first Stage 3 walk promotes that `0x41` from slot `0x4580`;
- the first dequeue reaches `pc=04EE` with `AF=4144`;
- the first visible CLI write is `[45C0] 0x2D -> 0x41`.

This is now the strongest concrete reproduction of original post-boot regular-key
behavior.  The best current hardware-side explanation is therefore no longer just
"the first `0x0171` function-bit payload must not persist"; more specifically, the
first relevant CLA value likely needs to appear to `SYS.SY` as a **bit-7-set regular
code** (`0x80 | key_code`), so that Stage 1 stores the regular code into `0x4580`
while still taking the Stage 2 / Stage 3 path.

**Model promoted into the injected CLA path (2026-05-13):** the emulator now uses
that bit-7-first rule by default for post-boot `machine_inject_key()` / `-inject-keycode`
held keys.  Re-running the same prompt probe with plain `-inject-keycode 0x41`
(no `-inject-keycode-bit7-first-cla` flag) still shows:

- `[kbd] first CLA held regular bit7 -> C1 at pc=0162`;
- `pc=0171` writes `0x4580 = 0x41`;
- the first dequeue reaches `pc=04EE` with `AF=4144`;
- the first visible CLI write is `[45C0] 0x2D -> 0x41`.

So the emulator's injected low-level CLA model now matches the current best
hardware-side hypothesis directly.  The remaining gap is no longer the injected CLA
path itself; it is whether real physical post-boot SDL keypresses should eventually
be routed through that same low-level path instead of the current FIFO shortcut.

The extracted boot modules were then scanned for direct absolute references to the
workspace bytes. Among `CLI.SY`, `ER.SY`, `LP.SY`, and `SYS.SY`, only `SYS.SY`
contains direct references to `0x4580..0x4582`, and they are exactly the already
known `0x015B` / `0x019B` Stage 2 / 3 sites. So the remaining producer is not an
obvious second absolute writer in those boot modules.

One stale emulator-side assumption was corrected later: the special `?GETFO`
read hook had drifted to a single `pc == 0x0519` check, but the trace evidence
for the actual `LD A,(0x457E)` access already showed `pc == 0x0516`. The hook
now recognizes the full `0x0516..0x0519` routine window instead of a single PC.

**OS-boundary narrowing (2026-05-13):** for this machine, the OS is only composed of
the Phantom ROM, `SYS.SY` (SAMOS), and `CLI.SY`. A full disassembly scan of the
extracted post-boot modules on `Sys1-H.dsk` therefore matters only insofar as it can
exclude `CLI.SY` or identify non-OS consumers of the same APIs. The concrete result is:

- only `SYS.SY` references `0x457E` / syscall `0x0E` at all, and those references are
    just the Stage 1 store (`0x0166`) plus the accessor definition (`0x0516`);
- `CLI.SY` does not directly read `0x457E` or call `0x0516`;
- `CLI.SY` does consume the blocking-read path for visible command-line input;
- the Phantom ROM is only relevant during boot and is bank-switched out before the
    post-boot CLI path under audit here;
- `BASIC.SM` also references the circular-buffer path, but it is an application-level
    BASIC interpreter, not part of the OS keyboard pipeline.

Therefore, for the specific post-boot OS-side bridge question, `SYS.SY` is now the
only remaining software candidate. That is still not proof that `SYS.SY` performs the
bridge, but it is the only defensible remaining place to look.

---

## Emulator Implementation (`src/keyboard.c`, `src/machine.c`)

> **Important audit correction (2026-05-13):** the ISR still does `LD A,(0x4582);
> CP 0x80; RET Z` at `0x0174–0x0178`, but a direct `SYS.SY` binary audit showed
> the init code writes `0x80` to `0x458A` at `0x00A1`, not to `0x4582`.  The old
> `0x4582=0x80` permanent-block explanation is therefore unsupported.

> **Additional audit result (2026-05-13):** direct disassembly of `0x04F6..0x051A`
> shows syscall `0x0D` / the blocking keyboard read loops on the circular-buffer
> routine at `0x04F6`, while syscall `0x0E` is the separate `LD A,(0x457E)` path at
> `0x0516`.  This confirms the CLI does not poll `0x457E` while waiting for input.

### Three delivery paths

| Caller | Path | Destination |
| ------ | ---- | ----------- |
| Physical ordinary key (`SDL_KEYDOWN`/`KEYUP`) | Host scancode position → S471 lookup → CLA-visible `found/key_code` latch; overlapping taps queue in `pending_ordinary[8]` until promoted | `SYS.SY` Stage 1 / 2 / 3 / 4 → SAMOS circular buffer → syscall 0x0D (CLI blocking read) |
| Function key F1–F7 (`SDL_KEYDOWN`/`KEYUP`) | Sets/clears `fonct_bits`; returned when CLA has no ordinary key latched, and returned directly by the `?GETFO` accessor at `0x0519` | `0x4580` via GETFON semantics and any callers that poll function-bit state through CLA or syscall `0x0E` |
| Power-on / inject (`machine_inject_key()`) | Sets `found=1`, `key_code`, `physically_held=1` for the injected key; the power-on virtual-Enter hold is tracked separately so post-boot injections are not auto-cleared by the boot release logic | Phantom ROM kbd_wait / SAMOS ISR Stage 1 → `0x457E` (syscall 0x0E) |

**Syscall 0x0E and function keys:** syscall `0x0E` is the function-key helper path. The current
emulator model now treats the `0x0519` accessor as function-only and returns the currently-held
`fonct_bits` directly, even when an ordinary key byte is staged in `0x457E`. The CLI blocking read
remains entirely on the circular-buffer path, and ordinary-key staging remains on the ordinary-key
delivery side instead of being consumed by `?GETFO`.

**FLIPPER.SM analysis:** `FLIPPER.SM` detects flippers exclusively via syscall 0x0E followed by
`AND 0xF0` (left flipper: CURSOR, fonct\_bit=`0x10`) and `AND 0x0F` (right flipper: CHANGE,
fonct\_bit=`0x01`).  The fonct\_bit values happen to satisfy both masks perfectly with no
cross-activation.

### Physical keyboard: strict CLA-centric delivery

`keyboard_event()` now handles ordinary keys primarily by host scancode position.  It resolves the
matrix position through the audited S471 table, then latches the resulting 7-bit code into the
CLA-visible `found/key_code` state.

One narrow exception now exists while `PROGRA` is held: text-capable alphabetic
keys are still forced through the matrix path rather than plain SDL text
injection, and the emulator may prefer SDL's logical letter symbol
(`ev->keysym.sym`) over the raw physical scancode for `A`..`Z`.  This keeps a
plain ordinary key available alongside the separate `PROGRA` function bit.

One tested alternative was to remove the `0x80 | key_code` prefix from the first
ordinary CLA read. That was falsified by immediate runtime behavior: ordinary CLI
typing stopped working. So the current model keeps the audited bit-7-first
ordinary CLA delivery for post-boot keys.

If another ordinary key arrives while one is already latched or awaiting reassertion, the second key
is stored in `pending_ordinary[8]` together with its already-resolved key code.  That preserves the
original layer decision even if Shift changes before the queued key is promoted.

`keyboard_frame_tick()` no longer drains a FIFO into the circular buffer.  Its post-boot role is now
only to release the virtual power-on Enter hold once the SAMOS ISR vector is installed, then promote
the next pending ordinary key when the active latch becomes idle.

Two release-side details were validated by the `Shift+MSG` CLI traces:

- a promoted key that was already released before promotion must survive one synthetic reassert so
  `SYS.SY` can still see it;
- once `SYS.SY` commits such a released promoted key into the circular buffer (observed via the
  `0x457C` advance hook), the emulator must clear both SAMOS repeat bytes `0x4558` and `0x4577`.
  Clearing only the emulator latch was not enough: Stage 4 would otherwise re-inject the same code
  endlessly ~700 ms later.

Validated runtime result (`tmp/manual_shift_msg_noreturn_fix11.log`): manual `Shift+MSG` now produces
exactly one visible `M`, `S`, `G` insertion at `0x45C0..0x45C2`, with no later trailing `G`.

### ESC key: current strict mapping

The host `Escape` key is currently just the top-left Smaky matrix position
(`HOST_MATRIX_KEYS[0]`), which resolves through the S471 table to hardware code
`0x06` in the normal layer. There is no command-history capture or special
empty-prompt recall path in the current code.

---

### Power-on Enter / inject: current delivery split

`machine_inject_key()` is the low-level held-CLA helper used by `-inject-keycode`.
It sets:

```c
m->kbd.key_code        = code;
m->kbd.found           = 1;
m->kbd.physically_held = 1;
```

While `physically_held=1`, CLA reads continue to observe that ordinary key until
the emulator explicitly releases it.  Once `SYS.SY` commits the key into the
circular buffer, the `0x457C` advance hook arms `0x4558` / `0x4577` and then
quiesces the CLA-visible hold so Stage 4 repeat owns subsequent repeats.

`-inject-str` does **not** use `machine_inject_key()`: `main.c` waits for the CLI
prompt and then appends characters directly through `machine_inject_to_circ_buf()`.

### `keyboard_read_cla()` — pure hardware model

The hardware model is uniform across all callers (Phantom ROM kbd_wait, SAMOS ISR
Stage 1 / Stage 2, monitor).  No `iff1` or mode-flag branching:

```c
uint8_t keyboard_read_cla(struct Smaky6 *m)
{
    if (m->kbd.found) {
        m->kbd.found = 0;
        /* Scanner immediately reasserts FOUND if key still held (§10.4: 200µs). */
        if (m->kbd.physically_held)
            m->kbd.found = 1;
        return m->kbd.key_code & 0x7Fu;   /* bit7=0: regular key */
    }
    /* FOUND=0: return function key bitmask (0x80 if none = idle).
     * SAMOS Stage 1 stores (CLA & 0x7F) to 0x4580 (GETFON register). */
    return 0x80u | m->kbd.fonct_bits;
}
```

The current branch had later drifted away from this audited rule by returning
bare `fonct_bits` when `FOUND=0`. That was corrected again after user retest of
SMILE still showed plain ordinary-key behavior for `PROGRA+matrix` input.

One further branch-local refinement was then applied to the ordinary-key prefix:
the validated bit-7-first ordinary CLA delivery remains in place for plain
post-boot typing, but it is no longer armed when a function key is already held.
That keeps simultaneous function+ordinary input on the plain ordinary CLA path
instead of forcing it into the same Stage 2 workspace race as ordinary prompt
typing.

**Why returning `0x80 | fonct_bits` is correct:**  
SAMOS Stage 1 at `0x015E–0x016D` does: `LD (0x4580),0x00; IN A,(0x00); AND 0x7F; LD (0x4580),A; BIT 7,A; JR NZ,0x016E`.  When CLA returns `0x80 | fonct_bits`, the `AND 0x7F` strips bit 7 and stores exactly `fonct_bits` to `0x4580` — the GETFON register.  The old claim that Stage 2 (CLA Read #2 at 0x0183) is permanently blocked by `0x4582=0x80` has been withdrawn after direct binary audit showed the init sentinel write is to `0x458A`, not `0x4582`.

### ISR ACK (machine.c port 0x01 write, data≠0)

```c
case 0x01:
    if (data == 0x00) memory_unprotect_rom(m, 0x0000, 0x0800);
    /* else: ISR ACK — no keyboard state change needed in unified model */
    break;
```

### Function-key status bar (video.c / main.c)

A 14 px strip at the bottom of the SDL window (`VIDEO_FKEY_Y = VIDEO_ASPECT_H + VIDEO_LED_H`)
contains 7 clickable buttons: CHANGE, SEARCH, SHOW, COPY, CURSOR, PROGRA, KILL.  Button
labels are drawn with the chargen font.  Active buttons (corresponding `fonct_bits` bit
set) are rendered bright red; inactive buttons are dark red; the hovered button is
highlighted regardless of state.

Mouse event handling in `main.c`:

- **`SDL_MOUSEBUTTONDOWN`** in the bar region: sets the corresponding bit in
  `m->kbd.fonct_bits`.
- **`SDL_MOUSEBUTTONUP`**: clears the bit **and** writes `m->bus[0x4558] = 0` to
  disable SAMOS Stage 4 auto-repeat for the released function key (without this,
  the repeat countdown left over from a prior regular key would re-fire the fkey
  code on the next ISR frame).

The bar is purely an input convenience overlay — the same bits are set by the
dedicated host keyboard scancodes (`Right Ctrl`, `Left Alt`, etc.).

## SAMOS Circular Buffer Mechanics (confirmed by trace)

The CLI blocking read (syscall 0x0D, address `0x04D4`) loops calling the peek
routine at `0x04F6` until it returns NZ (key present), then calls consume at `0x04E4`.

**Write** (by `SYS.SY` Stage 3 / 4, or by `machine_inject_to_circ_buf()`):

```asm
[ptr] = key_code;   ptr++
```

After: `[0x4596] = key`, `ptr = 0x4597`.

**Peek** (`0x04F6`):

```asm
HL = (0x457C)         ; load write pointer
A  = [HL]             ; read [ptr]
CP 0x80               ; sentinel check
JR Z → dec ptr, store ; ptr==base + [ptr]==0x80 means empty
OR A                  ; set flags
SBC HL, DE            ; HL -= 0x4596 (base)
; returns NZ if ptr > base (data present), Z if ptr == base (empty)
```

Note: peek reads `[ptr]` (the slot *after* the last-written byte), not `[base]`.
After writing one key, `[ptr] = [0x4597]`, which is typically `0x00` from uninitialized
memory — not `0x80`, so the `CP 0x80` guard does not falsely signal empty.

**Consume** (`0x04E4`):

```asm
dec ptr; store       ; ptr = 0x4596
HL = DE = 0x4596
A = [0x4596]         ; read character
INC HL               ; HL = 0x4597
LDIR (BC = ptr-base = 0)  ; copies 0 bytes
```

After consume: `ptr = 0x4596`, `[0x4596]` retains the consumed character (LDIR did nothing).
The emulator's repeat-arm hook keys off the same `0x457C` advance event.

## CLI Visible Character Path (confirmed by CLI.SY disassembly + trace)

Visible prompt-line edits are performed by `CLI.SY`, not by the SAMOS Stage 1 latch path.

- `CLI.SY` line-input routine `0x5857` calls helper `0x5B29` to fetch one key.
- `0x5B29` uses SAMOS blocking read syscall `0x0D`, not syscall `0x0E`.
  In disassembly this appears as `RST 20h` at `0x5B2C` followed by byte `0x0D`
  (rendered by `z80dasm` as `DEC C` because it does not understand the syscall ABI).
- Normal character insertion then uses helper `0x58DD..0x590F`:
  - `0x58F8..0x58FD`: load current cursor from `0x7014`, increment it, store back;
  - `0x5900..0x590B`: if needed, shift the tail right with `LDDR`;
  - `0x590E`: store the new character into the line buffer;
  - `0x590F`: return.
- Cursor redraw then uses `0x5A7B..0x5AF9`; trace confirmed `0x5A88` writes `'-'`
  at the current cursor and later redraws it at the new cursor position.

Additional runtime confirmation (2026-05-13): during the live-prompt
`-poke-at-pc 0x0179 0x4580 0x41` probe, the dequeued `0x41` does reach this exact
insertion helper. The trace shows `pc=0x58DD`, `pc=0x58F8`, `pc=0x590E`, then a
visible prompt-buffer write at `pc=0x590F`: `[45C0] 0x2D -> 0x41`.

This is why `-inject-str "A"` visibly changes the prompt line while a one-shot
CLA key injected through `machine_inject_key()` does not.

---

## Notes on `SDL_VIDEODRIVER=dummy`

When `SDL_VIDEODRIVER=dummy` is set (CI / automated test mode), SDL delivers **no real keyboard events**.  Only the key injection path (`machine_inject_key()`) works.  Interactive keyboard testing requires a real display:

```sh
DISPLAY=:0 ./build/smemu6 -floppy floppies/Sys1-H.dsk
```

## Trace Regression Check

The repository now includes `tools/check_keyboard_asd_trace.sh`, a small Linux/X11
regression script for the traced printable matrix-key path. It launches
`build/smemu6`, discovers the emulator window from the PID banner, injects
overlapping `a`, `s`, and `d` `keydown`/`keyup` events through `xdotool`, then
asserts that the trace contains visible prompt-buffer writes for all three
characters at `pc=0x590F`.

```sh
tools/check_keyboard_asd_trace.sh
```

Prerequisites:

- a real X11 display so SDL delivers host keyboard events;
- `xdotool` on `PATH`;
- a built emulator at `build/smemu6`.

---

## Memory Map Summary

| Address | Purpose |
| ------- | ------- |
| `0x457E` | Last key code from Stage 1 (used by syscall 0x0E) |
| `0x4558` | Debounce / key-repeat countdown (0x23 = 35 frames) |
| `0x4580` | **Function-key bitmask (GETFON register)** — written by SAMOS ISR Stage 1 (`AND 0x7F; LD (0x4580),A`) from the CLA return value each frame.  Since `keyboard_read_cla()` returns `0x80 | fonct_bits` when no regular key is held, Stage 1 stores exactly `fonct_bits` here automatically.  No extra write from `main.c` needed. |
| `0x4566` | SAMOS 50 Hz ISR vector (written to `0x003E` by SYS.SY at `0x00CD`, **after** boot-menu kbd_wait exits; used as the power-on virtual key release trigger in `keyboard_frame_tick()`) |
| `0x458A+` | Circular buffer storage (slots marked with bit 7 when filled) |
| `0x45BF` | Timer countdown register (unrelated to keyboard) |
| `0x45C0` | **CLI line buffer start** — typed characters stored here; overwritten by `-` cursor at start of each new input session |
| `0x7014:0x7015` | **Cursor pointer** (little-endian) — points one past the last typed character in the line buffer |

---

## Key Codes — Hardware Reference (doc section 10.4, pages 213–215)

### Category 1 — "Touches de fonction" (7 function keys, bitmask)

These 7 keys are **not** delivered through the ordinary-key latch / queue. When no regular key is pressed
(FOUND=0), the CLA port returns a 7-bit bitmask — one bit per function key held.
The SAMOS `?GETFON` / `GETFON` system calls read this bitmask.

Codes confirmed from doc section 10.4, page 213 (octal):

| Key     | Octal | Hex    | Bit | Primary SDL scancode |
|---------|-------|--------|-----|----------------------|
| CURSOR  | `020` | `0x10` | 4   | `SDL_SCANCODE_F1`    |
| COPY    | `010` | `0x08` | 3   | `SDL_SCANCODE_F2`    |
| KILL    | `100` | `0x40` | 6   | `SDL_SCANCODE_F3`    |
| PROGRA  | `040` | `0x20` | 5   | `SDL_SCANCODE_F4`    |
| SHOW    | `004` | `0x04` | 2   | `SDL_SCANCODE_F5`    |
| SEARCH  | `002` | `0x02` | 1   | `SDL_SCANCODE_F6`    |
| CHANGE  | `001` | `0x01` | 0   | `SDL_SCANCODE_F7`    |

**Implemented:** `uint8_t fonct_bits` field in `struct kbd`.  `keyboard_event()`
sets/clears the corresponding bit on `SDL_KEYDOWN`/`SDL_KEYUP` for the F1–F7
scancodes. Mouse clicks on the function-key status bar also contribute through
`fonct_mouse_bits` while the button is held.

The SAMOS ISR Stage 2 (`AND 0x7F; LD (0x4580), A`) stores `fonct_bits` to the
GETFON register automatically when `keyboard_read_cla()` returns `0x80 | fonct_bits`
(FOUND=0).  No post-frame bus write is needed.

### Category 2 — Special keys in the normal matrix path (codes confirmed from doc p.213)

| Key      | Octal | Hex    | Chargen glyph | SDL scancode              |
|----------|-------|--------|---------------|---------------------------|
| TAB      | `011` | `0x09` | (tab)         | `SDL_SCANCODE_TAB`        |
| MACRO    | `036` | `0x1E` | `«`           | `SDL_SCANCODE_F8`         |
| DEF(INE) | `037` | `0x1F` | `»`           | `SDL_SCANCODE_F9`         |

`REP / FNCT` (code `0` in the doc) is the hardware repeat/function modifier; it
generates no independent code and can be ignored for now.

**Tab note:** SAMOS uses code `0x09` at the CLI prompt to insert the string `DX1:`,
expanding the tab as a drive prefix shortcut.  In the current strict implementation,
Tab is one of the explicitly mapped host matrix positions in `HOST_MATRIX_KEYS[]`.

### Current host-mapping coverage vs. S471 evidence (audit status: 2026-05-13)

Based on the currently shared S471 PROM findings, the emulator's present host-key
coverage falls into three distinct buckets:

| Status | Mapping surface | Current state |
|--------|-----------------|---------------|
| Confirmed strict host-position mapping | `Escape`, digits, `Backspace`, `Tab`, letters `A`..`Z`, brackets, backslash, the ISO post-`L` cluster (`;`, `'`, non-US `\`), `Return`, `Space`, comma, period, minus | Explicitly mapped in `HOST_MATRIX_KEYS[]` and resolved through the audited S471 layers. |
| Confirmed function-bit mapping | `F1`..`F7` | Exposed as `fonct_bits`; returned on CLA only when no ordinary key is latched. |
| Remaining convenience / non-physical bindings | `F9`, BREAK / reset host shortcuts | Still documented as emulator conveniences rather than original keyboard-position claims. |

This means the current codebase is already consistent with the PROM-derived facts
that have actually been shared so far:

- top-left `ESC / UNDO` currently emits `0x06`;
- `Backspace` emits `0x08`;
- `Tab` emits `0x09`;
- `Return` emits `0x0D`;
- `Space` is accepted through the ASCII text-input path as `0x20`.

What is **not** established by the current implementation is broader physical-key
fidelity for the whole S471 matrix.  In particular:

- only the currently mapped host scancode positions are modeled; the rest of the
    original Smaky matrix still needs explicit host-position coverage;
- the alternative host bindings that remain are limited conveniences, not verified
    S471 position matches;
- the FNCT/ALT-layer outputs implied by the S471 dump are not broadly modeled as
    separate physical-key positions yet;
- the boot-only `FUNCTION-SHIFT-BREAK` and `FUNCTION-BREAK` combinations remain
    documented but not implemented.

So the broad audit conclusion is narrow but concrete: the emulator is aligned with
the S471-backed keycode facts currently available, but it is still **not** a full
physical keyboard matrix model.

### Exact S471 ROM-dump findings now available (4 layers x 64 positions)

The full S471 dump provided on 2026-05-13 sharpens the audit beyond the earlier
spot facts.  The four hardware layers are now known explicitly:

- **Layer 0** = normal typing
- **Layer 1** = Shift
- **Layer 2** = FNCT / ALT-like layer
- **Layer 3** = caps-like layer

The most relevant confirmed positions are:

| Physical position class | Normal | Shift | FNCT/ALT | Caps-like | Current emulator state |
|-------------------------|--------|-------|----------|-----------|------------------------|
| Top-left ESC / UNDO     | `0x06` | `0x06` | `0x1B` | `0x06` | **Implemented** as host `Escape -> 0x06` |
| Backspace position      | `0x08` | `0x7F` | `0x01` | `0x08` | Position is mapped in `HOST_MATRIX_KEYS[]`; active output follows the current S471 layer |
| Tab position            | `0x09` | `0x0B` | `0x03` | `0x09` | Position is mapped in `HOST_MATRIX_KEYS[]`; active output follows the current S471 layer |
| Return position         | `0x0D` | `0x0C` | `0x0A` | `0x0D` | Position is mapped in `HOST_MATRIX_KEYS[]`; active output follows the current S471 layer |
| Space position          | `0x20` | `0x20` | `0x02` | `0x20` | Position is mapped in `HOST_MATRIX_KEYS[]`; normal / Shift / Caps now come from strict matrix lookup |
| Q-row right-edge candidate | `0x04` | `0x05` | `0x07` | `0x04` | **Not explicitly host-mapped**; this is now the strongest candidate for the older `0x04` / `0x05` CLI interpretation |
| MACRO key               | `0x1E` | `0x1E` | `0x1E` | `0x1E` | Implemented as host `F8` convenience mapping |
| DEFINE key              | `0x1F` | `0x1F` | `0x1F` | `0x1F` | Implemented as host `F9` convenience mapping |

The dump also confirms that the alphanumeric matrix itself is genuinely
**Swiss-German QWERTZ** at the hardware level:

- normal layer letters are lowercase (`qwertzuiop`, `asdfghjkl`, `yxcvbnm`);
- Shift and caps-like layers produce uppercase letters;
- accented keys such as `ü`, `ö`, `ä`, `é`, `è`, `ê`, `ç` appear directly in the
    matrix rather than being host-side inventions.

This turns the remaining implementation gap into a precise statement:

- the emulator already matches the hardware for the specifically audited normal-layer
    outputs that were switched or checked (`0x06`, `0x08`, `0x09`, `0x0D`, `0x20`,
    `0x1E`, `0x1F`);
- the emulator does **not** yet model the full four-layer S471 matrix per physical
    position;
- the older CLI hypothesis that tied cancel/recall to the top-left key is now even
    weaker, because the dump places `0x04` / `0x05` / `0x07` on a different key
    position near the Q-row right edge.

### Correction keys (doc p.215)

| Oct | Hex    | Name | SDL scancode               | Note                        |
|-----|--------|------|----------------------------|-----------------------------|
| 010 | `0x08` | BS   | `SDL_SCANCODE_BACKSPACE`   | in `KEY_TABLE[]`            |
| 177 | `0x7F` | DEL  | `SDL_SCANCODE_DELETE`      | in `KEY_TABLE[]`            |

Note: `0x7F` also renders as the solid-filled block glyph ▓ in the chargen ROM
(confirmed by direct ROM inspection — see [Chargen glyph reference](#chargen-glyph-reference) below).

### Swiss-French accented characters (Category 3)

Codes `0x0F`–`0x1D` are still the 15 Swiss-French accented characters in the Smaky
code space, as confirmed by the S471 dump. The current runtime does expose them
through the printable-text compatibility path in `keyboard_text_event()`: normal
ASCII text still uses the fresh-text-key gate that suppresses host repeat storms,
while composed non-ASCII `SDL_TEXTINPUT` events fall back to a one-shot direct-code
path when SDL does not provide a matching fresh claimable scancode.

The chargen-code assignments remain:

| UTF-8 codepoint | Smaky code | Char |
|-----------------|------------|------|
| U+00FC / U+00DC | `0x0F` | ü / Ü |
| U+00E0 / U+00C0 | `0x10` | à / À |
| U+00E2 / U+00C2 | `0x11` | â / Â |
| U+00E9 / U+00C9 | `0x12` | é / É |
| U+00E8 / U+00C8 | `0x13` | è / È |
| U+00EB / U+00CB | `0x14` | ë / Ë |
| U+00EA / U+00CA | `0x15` | ê / Ê |
| U+00EF / U+00CF | `0x16` | ï / Ï |
| U+00EE / U+00CE | `0x17` | î / Î |
| U+00F4 / U+00D4 | `0x18` | ô / Ô |
| U+00F9 / U+00D9 | `0x19` | ù / Ù |
| U+00FB / U+00DB | `0x1A` | û / Û |
| U+00E4 / U+00C4 | `0x1B` | ä / Ä |
| U+00F6 / U+00D6 | `0x1C` | ö / Ö |
| U+00E7 / U+00C7 | `0x1D` | ç / Ç |

---

## Character Encoding — Hardware Reference (doc section 10.4, pages 214–215)

The Smaky 6 uses a single-byte encoding in the range `0x00–0x7F`.  Codes `0x00–0x1F`
are **dual-purpose**: the OS, printer driver, and serial handler interpret them as
control/format codes; the chargen ROM renders them as Swiss-French display glyphs.
The emulator must pass the raw byte to the chargen for display **and** let SAMOS
handle the control semantics.

### "Mise en page" (formatting/control) codes — doc p.215

| Oct | Hex    | Name  | Control meaning                  |
|-----|--------|-------|----------------------------------|
| 000 | `0x00` | NUL   | null                             |
| 007 | `0x07` | BEL   | bell — triggers the speaker      |
| 011 | `0x09` | TAB   | horizontal tab                   |
| 012 | `0x0A` | LF    | line feed                        |
| 013 | `0x0B` | VT    | vertical tab                     |
| 014 | `0x0C` | FF    | form feed                        |
| 015 | `0x0D` | CR    | carriage return                  |
| 016 | `0x0E` | Red   | switch to red ink / color        |
| 017 | `0x0F` | Black | switch to black ink              |
| 033 | `0x1B` | ESC   | escape — printer/serial prefix (**not** a keyboard key; chargen renders it as `ä`) |

### DC codes (paper-tape reader/punch controls) — doc p.215

| Oct | Hex    | Name | Meaning     |
|-----|--------|------|-------------|
| 021 | `0x11` | DC1  | Reader on   |
| 022 | `0x12` | DC2  | Aux on      |
| 023 | `0x13` | DC3  | Reader off  |
| 024 | `0x14` | DC4  | Aux off     |

### Communication-request codes — doc p.215

Glyph shapes confirmed by direct inspection of `roms/chargen.rom` (stride 16 bytes/char, 8 rows × 8 cols):

| Oct | Hex    | Name | Standard meaning         | Chargen glyph (from ROM)       |
|-----|--------|------|--------------------------|--------------------------------|
| 001 | `0x01` | SOH  | Start of heading         | empty rectangle (box outline)  |
| 002 | `0x02` | STX  | Start of text            | box outline with dot above     |
| 003 | `0x03` | ETX  | End of text              | decorative cross / diamond     |
| 004 | `0x04` | EOT  | End of transmission      | `<` left-pointing chevron      |
| 005 | `0x05` | ENQ  | Enquiry                  | `>` right-pointing chevron     |
| 006 | `0x06` | ACK  | Acknowledge              | letter-`A` shape               |
| 025 | `0x15` | NAK  | Negative acknowledge     | ê                              |
| 026 | `0x16` | SYN  | Synchronous idle         | ï                              |
| 027 | `0x17` | ETB  | End of transmission block| î                              |
| 030 | `0x18` | CAN  | Cancel                   | ô                              |
| 031 | `0x19` | EM   | End of medium            | ù                              |
| 032 | `0x1A` | SUB  | Substitute               | û                              |

### File-separator codes — doc p.215

| Oct | Hex    | Name | Chargen glyph            |
|-----|--------|------|--------------------------|
| 034 | `0x1C` | FS   | ö                        |
| 035 | `0x1D` | GS   | ç                        |
| 036 | `0x1E` | RS   | « (= MACRO key code)     |
| 037 | `0x1F` | US   | » (= DEFINE key code)    |

---

## Chargen Glyph Reference

The chargen ROM (`roms/chargen.rom`, 2048 bytes) maps 128 7-bit codes to pixel glyphs,
16 bytes per character (rows 0–15; only the top 8–10 rows are typically non-zero).
This is the **Prom 2716** variant documented on page 214.  Do **not** use the
Motorola 6571 or 74S262 tables as a reference — they differ significantly.

#### Swiss-French glyph block `0x0F–0x1F` (confirmed from doc p.214)

| Oct | Hex    | Char | | Oct | Hex    | Char |
|-----|--------|------|-|-----|--------|------|
| 017 | `0x0F` | ü    | | 030 | `0x18` | ô    |
| 020 | `0x10` | à    | | 031 | `0x19` | ù    |
| 021 | `0x11` | â    | | 032 | `0x1A` | û    |
| 022 | `0x12` | é    | | 033 | `0x1B` | ä    |
| 023 | `0x13` | è    | | 034 | `0x1C` | ö    |
| 024 | `0x14` | ë    | | 035 | `0x1D` | ç    |
| 025 | `0x15` | ê    | | 036 | `0x1E` | «    |
| 026 | `0x16` | ï    | | 037 | `0x1F` | »    |
| 027 | `0x17` | î    | | | | |

#### Additional remaps outside standard ASCII printable range (confirmed from doc p.214)

| Oct  | Hex    | Char     | Note                                           |
|------|--------|----------|------------------------------------------------|
| 043  | `0x23` | `#`      | same as ASCII                                  |
| 133  | `0x5B` | `[`      | same as ASCII                                  |
| 134  | `0x5C` | `\`      | same as ASCII                                  |
| 135  | `0x5D` | `]`      | same as ASCII                                  |
| 136  | `0x5E` | `^`      | circumflex accent — only top 3 rows populated (confirmed from ROM) |
| 137  | `0x5F` | `_`      | underscore (shown as `–` in the doc)           |
| 140  | `0x60` | `` ` ``  | backtick                                       |
| 173  | `0x7B` | `{`      | same as ASCII                                  |
| 174  | `0x7C` | `\|`     | same as ASCII                                  |
| 175  | `0x7D` | `}`      | same as ASCII                                  |
| 176  | `0x7E` | `~`      | same as ASCII                                  |
| 177  | `0x7F` | ▓        | solid filled block — cursor glyph (confirmed from ROM) |

#### Selected control-code glyphs confirmed by ROM inspection

| Oct | Hex    | Chargen glyph shape (from ROM bitmap)        |
|-----|--------|----------------------------------------------|
| 000 | `0x00` | mostly blank with two isolated pixels        |
| 001 | `0x01` | empty rectangle (box outline)                |
| 002 | `0x02` | box outline with dot above                   |
| 003 | `0x03` | decorative cross / diamond                   |
| 004 | `0x04` | `<` left-pointing chevron                    |
| 005 | `0x05` | `>` right-pointing chevron                   |
| 006 | `0x06` | letter-`A` shape                             |
| 007 | `0x07` | angular/arrow shape (BEL)                    |
| 010 | `0x08` | crosshair with downward element (BS)         |
| 011 | `0x09` | `←` left-pointing arrow (TAB)               |
| 012 | `0x0A` | `↵` down-and-left arrow (LF)               |
| 013 | `0x0B` | `→` right-pointing arrow (VT)              |
| 014 | `0x0C` | circular / recycled pattern (FF)             |
| 015 | `0x0D` | two isolated pixels (nearly invisible) (CR)  |
| 016 | `0x0E` | snowflake / asterisk-like (Red)              |
| 017 | `0x0F` | two dots above + body = ü (Black)            |
| 036 | `0x1E` | `«` left-pointing double chevrons (MACRO)    |
| 037 | `0x1F` | `»` right-pointing double chevrons (DEFINE)  |

#### SDL mapping for accented characters

On a standard PC keyboard the host OS delivers accented characters as UTF-8
`SDL_TEXTINPUT` events rather than scancodes.  The recommended approach is to add a
`SDL_TEXTINPUT` handler in `keyboard_event()` alongside the existing `SDL_KEYDOWN`
handler: match the Unicode codepoint against a lookup table and push the corresponding
Smaky code into the FIFO.

Example mapping entries needed:

| Unicode | Smaky hex | Char |
|---------|-----------|------|
| U+00FC  | `0x0F`    | ü    |
| U+00E0  | `0x10`    | à    |
| U+00E2  | `0x11`    | â    |
| U+00E9  | `0x12`    | é    |
| U+00E8  | `0x13`    | è    |
| U+00EB  | `0x14`    | ë    |
| U+00EA  | `0x15`    | ê    |
| U+00EF  | `0x16`    | ï    |
| U+00EE  | `0x17`    | î    |
| U+00F4  | `0x18`    | ô    |
| U+00F9  | `0x19`    | ù    |
| U+00FB  | `0x1A`    | û    |
| U+00E4  | `0x1B`    | ä    |
| U+00F6  | `0x1C`    | ö    |
| U+00E7  | `0x1D`    | ç    |
| U+00AB  | `0x1E`    | «    |
| U+00BB  | `0x1F`    | »    |

---

## Boot-time Key Combinations (Phantom ROM)

According to the official user manual (p.24, section "ROM Phantom — Possibilités"):

| Physical key / combo        | Emulator mapping          | Action                                          |
|-----------------------------|---------------------------|-------------------------------------------------|
| **ESC** / **UNDO** (top-left) | `Escape`                | Current working mapping pushes **0x06** to the kbd buffer |
| **SHIFT-BREAK** (top-right) | Shift+Pause / Shift+F11   | Hard reset → boot from **DX0:** (normal boot)   |
| **FUNCTION-SHIFT-BREAK**    | (not yet mapped)          | Hard reset → boot from **DX1:**                 |
| **BREAK** (top-right)       | Pause / F11               | NMI → Phantom ROM monitor / PDP-11 loader (USART 14) |
| **FUNCTION-BREAK**          | (not yet mapped)          | Memory test (POST)                              |

The `FUNCTION` key is the `REP/FNCT` modifier (hardware repeat/function key).
At boot-time the Phantom ROM polls it alongside SHIFT and BREAK to determine the
boot path.  During normal OS operation it is the FNCT modifier that selects alternate
character meanings (e.g., FNCT+key → accented characters on some keys).

### Emulator implementation note

`src/main.c` maps `Shift+Pause` and `Shift+F11` to `machine_reset(m)` (hard reset,
re-reads ROM, restarts the boot sequence from DX0:).  The FUNCTION-SHIFT-BREAK path
(boot from DX1:) is not yet implemented — it would require injecting the FUNCTION bit
into the CLA bitmask alongside SHIFT+BREAK so the Phantom ROM takes the DX1 path.

The `Escape` key on the PC keyboard now maps to the physical **ESC / UNDO**
key (top-left corner) using the current working code **`0x06`**, following the
S471 PROM normal-layer result.

**Important contradiction (2026-05-13):** an external 256-byte S471 keyboard PROM
analysis points the *normal-layer* top-left position at **`0x06`**, not `0x04`.
The same dump also places **`0x1B`** on that physical position only on the FNCT/ALT
layer, while a different normal-layer key candidate on the Q-row right edge yields
`0x04`.  The emulator now follows that `0x06` result as its working mapping, but the
older CLI audit still needs to be reconciled against live runtime.

The current best interpretation is:

- `0x1B` is still not evidence that the top-left key is the normal keyboard ESC key;
    in the dump it appears to be a layer-dependent alternate output, not the default;
- the emulator's previous `0x04` mapping has been retired in favor of the PROM-backed
    `0x06` working mapping;
- the next missing check is to reconcile the PROM position map with live runtime
    behaviour and determine whether the top-left physical key is really `0x06` in
    normal typing while `0x04` belongs to a different key position.

The **BREAK** key (top-right, also labelled NMI or RESET) is a separate physical
key that pulls the Z80 `NMI` pin low; it does not produce any keyboard byte.
