# Smaky 6 Keyboard Hardware & SAMOS ISR Analysis

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
`0x00CD`, **after** the init kbd_wait exits).  FIFO entries queued during the pre-SAMOS
boot phase are discarded at that point — they were destined for the Phantom ROM boot
menu, not the CLI.

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

```
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

```
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
- The precise runtime role of `0x4582` is still unresolved.  It is clearly live ISR
    workspace, not a statically initialised sentinel.

### Stage 3 — Circular buffer management (0x019B–0x01DF)

Reached from the Stage 2 / workspace path, not from the regular-key Stage 1 early
return at `0x016D`.

- Direct disassembly shows Stage 3 starts at `0x019B` with `DE=0x4580` and `HL=0x458A`.
- `0x458A` is initialised to `0x80` at `0x00A1`; `0x4595` and `0x45B6` are guard
    sentinels written by the same init block.
- Stage 3 does more than a simple one-byte enqueue: it scans and compacts an
    internal workspace structure before Stage 4 uses the circular-buffer write pointer
    at `0x457C`.
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

```
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
| 0x0D (blocking char read) | 0x04F6 | Circular buffer at `0x457C`/`0x458A+` |
| 0x0E (non-blocking check) | — | `0x457E` (Stage 1 direct store) |

**The CLI uses syscall 0x0D.**  Direct binary audit confirms `0x0509` loops on the
`0x04F6` circular-buffer routine, while `0x0516` is the separate `0x457E` accessor.
Therefore normal typed input must reach the circular-buffer path somehow.  Exactly
how Stage 1, Stage 2, and Stage 3 cooperate post-boot is still under re-audit.

---

## Why Keys Reach the CLI

The SAMOS ISR fires at 50 Hz.  Each frame:

1. **ISR ACK** (`OUT (0x01), 0x08` at `0x0048`): no keyboard state change in the unified model.
2. **Stage 1** (CLA read #1 at `0x0160`): `keyboard_read_cla()` returns `key_code` if `found=1`, else `0x80 | fonct_bits`.
   - If bit7=0 (regular key): SAMOS stores it to `0x457E` (syscall 0x0E) and returns early — circular buffer NOT touched.
   - If bit7=1: `AND 0x7F` strips bit7 and stores `fonct_bits` to `0x4580` (GETFON register). Falls into Stage 2.
3. **Stage 2** (no-key path, 0x016E): reads `0x4582` at `0x0175` and returns early only if that byte equals `0x80`.  Direct binary audit of `SYS.SY` on 2026-05-13 confirmed the init sentinel write is to `0x458A` at `0x00A1`, not to `0x4582`, so the old "Stage 2 is permanently blocked" claim is not supported by the binary.

**Current emulator path:** physical keys reach the CLI via `keyboard_frame_tick()`,
which writes directly to the SAMOS circular buffer at `0x457C`/`0x4596+`.

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
frames at the live CLI prompt still did not enter the Stage 3/4 path. Runtime trace
showed repeated cycles of:

- status read `pc=0x0044` returning `0x0C`;
- ISR CLA read at `pc=0x0162` returning `0x41`;
- Stage 1 store at `pc=0x0169` rewriting `0x457E`.

During the hold window there was no observed entry into the traced Stage 3/4 PCs
(`0x019B`, `0x01BB`, `0x01C9`, `0x01DF`, `0x01EE`, `0x0200`) and no circular-buffer
pointer advance at `0x457C`.  This rules out the simplest hypothesis that merely
holding a regular CLA key causes `SYS.SY` to promote it from Stage 1 into the
circular-buffer path on later ISR frames.

So the remaining open question is now narrower: how a real hardware-originating key
becomes eligible for the CLI's blocking-read / circular-buffer path, given that the
direct CLA Stage 1 path only updates `0x457E`.

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
> `0x4582=0x80` permanent-block explanation is therefore unsupported.  The current
> emulator still uses the FIFO→circular-buffer path for physical keys, but that
> design should no longer be justified by the withdrawn `0x4582=0x80` claim.

> **Additional audit result (2026-05-13):** direct disassembly of `0x04F6..0x051A`
> shows syscall `0x0D` / the blocking keyboard read loops on the circular-buffer
> routine at `0x04F6`, while syscall `0x0E` is the separate `LD A,(0x457E)` path at
> `0x0516`.  This confirms the CLI does not poll `0x457E` while waiting for input.

### Three delivery paths

| Caller | Path | Destination |
|--------|------|-------------|
| Physical regular key (`SDL_KEYDOWN`) | FIFO → `keyboard_frame_tick()` → direct write to SAMOS circular buffer | Syscall 0x0D (CLI blocking read) |
| Function key F1–F7 (`SDL_KEYDOWN`/`KEYUP`) | Sets/clears `fonct_bits`; `keyboard_frame_tick()` writes `fonct_bits` to `bus[0x4580]` AND `bus[0x457E]` | `0x4580` via GETFON syscall; `0x457E` via syscall 0x0E (non-blocking) — **press-and-hold, clears on release** |
| Power-on / inject (`machine_inject_key()`) | Sets `found=1`, `key_code`, `physically_held=1` for the injected key; the power-on autoboot hold is tracked separately so post-boot injections are not auto-cleared by the boot release logic | Phantom ROM kbd_wait / SAMOS ISR Stage 1 → `0x457E` (syscall 0x0E) |

**Syscall 0x0E and function keys:** Syscall 0x0E reads `0x457E` directly.  Since the physical
regular-key path never touches `0x457E`, it would always return 0 for physical keys.
`keyboard_frame_tick()` therefore mirrors `fonct_bits` into `bus[0x457E]` each frame so that
games reading flippers via syscall 0x0E work correctly.  Writing `fonct_bits` (rather than the
last regular key code) gives true press-and-hold semantics: the latch is `0x00` when no function
key is held, immediately releasing the flipper.

**FLIPPER.SM analysis:** `FLIPPER.SM` detects flippers exclusively via syscall 0x0E followed by
`AND 0xF0` (left flipper: CURSOR, fonct\_bit=`0x10`) and `AND 0x0F` (right flipper: CHANGE,
fonct\_bit=`0x01`).  The fonct\_bit values happen to satisfy both masks perfectly with no
cross-activation.

### Physical keyboard: FIFO-based delivery

`keyboard_event()` pushes one key code per `SDL_KEYDOWN` into a 64-slot software FIFO.
SDL key-repeat events (`ev->repeat != 0`) are **discarded** — Stage 4 auto-repeat is
managed by the emulator (see below).
The CLA fields (`found`, `key_code`, `physically_held`) are **not touched** by
`keyboard_event()` for physical keys.

**Physical-key bit-7 marker:** `keyboard_event()` and `keyboard_text_event()` set **bit 7** in
each FIFO byte to mark it as a physical keystroke.  `keyboard_frame_tick()` strips bit 7 before
writing to the circular buffer, and only arms SAMOS Stage 4 auto-repeat (`0x4558`/`0x4577`)
when bit 7 was set.  Injected keys (from `-inject-str` or ESC-recall re-injection) do not set
bit 7, so they never arm auto-repeat.  This prevents injected sequences from triggering
spurious key repetition at the next prompt.

**Enter auto-repeat exclusion:** Even for physical keystrokes, Enter (`0x0D`) never arms
auto-repeat.  Arming it would cause the SAMOS ISR to re-inject `0x0D` 35 frames later,
making the line editor process an empty command and null-terminate `0x45C0`, erasing any
history content.

```c
void keyboard_event(struct Smaky6 *m, const SDL_KeyboardEvent *ev)
{
    if (ev->type == SDL_KEYUP) return;
    if (ev->type != SDL_KEYDOWN) return;
    if (ev->repeat) return;   /* discard SDL auto-repeat; SAMOS Stage 4 handles it */

    /* Look up key in table; set bit 7 to mark as physical keystroke: */
    m->kbd.fifo[m->kbd.fifo_tail] = code | 0x80u;
    m->kbd.fifo_tail = (m->kbd.fifo_tail + 1) & 63;
}
```

`keyboard_frame_tick()` is called **once per 50 Hz frame, BEFORE `machine_run_frame()`**.
It drains **all** pending FIFO entries into the SAMOS circular buffer each frame (not one per frame),
stopping only when the guard sentinel is hit (buffer region full) or the write pointer is out of range
(SAMOS workspace not yet initialized).

**SDL OS key-repeat suppression (`suppress_text_next`):**  SDL fires `SDL_KEYDOWN` with
`ev->repeat != 0` for held keys at the OS key-repeat rate.  It also fires a
`SDL_TEXTINPUT` event immediately after each such repeat `SDL_KEYDOWN` — but the
`SDL_TEXTINPUT` event has **no repeat flag** of its own.  Without suppression this
causes a duplicate character per OS-repeat cycle (one from the KEYDOWN path, one from
the TEXTINPUT path).  The fix: a `suppress_text_next` flag in `MainLoopCtx` (main.c)
is set whenever a `SDL_KEYDOWN` with `repeat != 0` is received; the very next
`SDL_TEXTINPUT` event is then discarded, and the flag is cleared.  SAMOS Stage 4
provides application-level auto-repeat from within the emulated hardware; OS-level
repeat events are redundant and dropped entirely via the `ev->repeat` check in
`keyboard_event()`.

```c
void keyboard_frame_tick(struct Smaky6 *m)
{
    /* Release power-on virtual key when SAMOS ISR vector is installed.
     * bus[0x4566..7] == 0x003E is written by SAMOS init at 0x00CD,
     * AFTER the init kbd_wait at 0x00B5 exits.  Before this point
     * physically_held=1 keeps FOUND reasserting on every CLA read,
     * passing through all boot-phase kbd_wait loops automatically.
     * FIFO entries from the pre-SAMOS phase are discarded. */
    if (m->kbd.physically_held) {
        uint16_t vec = (uint16_t)m->bus[0x4566u] | ((uint16_t)m->bus[0x4567u] << 8);
        if (vec == 0x003Eu) {
            m->kbd.physically_held = 0;
            m->kbd.found           = 0;
            m->kbd.fifo_head       = m->kbd.fifo_tail;  /* discard pre-SAMOS FIFO */
        }
    }

    if (!m->cpu.iff1) return;   /* inside ISR or pre-SAMOS: leave FIFO for CLA path */
    /* Drain the entire FIFO into the SAMOS circular buffer each frame. */
    while (m->kbd.fifo_head != m->kbd.fifo_tail) {
        uint16_t wr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
        if (wr < 0x4596u || wr > 0x45B6u) break;   /* SAMOS workspace not ready */
        if (m->bus[wr] == 0x80u) break;             /* guard sentinel: buffer full */
        uint8_t raw      = m->kbd.fifo[m->kbd.fifo_head];
        uint8_t physical = raw & 0x80u;             /* set by keyboard_event() for real keys */
        uint8_t code     = raw & 0x7Fu;
        m->kbd.fifo_head = (m->kbd.fifo_head + 1) & 63;
        m->bus[wr] = code;
        wr++;
        m->bus[0x457Cu] = (uint8_t)(wr & 0xFFu);
        m->bus[0x457Du] = (uint8_t)(wr >> 8);
        /* Arm Stage 4 auto-repeat only for physical keys, never for Enter */
        if (physical && code != 0x0Du) {
            m->bus[0x4558u] = 0x23u;
            m->bus[0x4577u] = code;
        }
    }
}
```

**Why guard-sentinel check (not `ptr == base`)?**

Writing stops when `bus[wr] == 0x80` — not when `wr == 0x4596` (base address).  The sentinel at `~0x45B6` marks the end of the writable region.  The SAMOS consume routine decrements the write pointer but leaves the consumed byte in place; a base-address check would stop too early when the pointer has been advanced by a previous write.  The guard-sentinel correctly limits writes to the ~32-slot buffer capacity.

**Why drain all FIFO entries, not just one per frame?**

Draining one entry per frame would impose a minimum 20 ms inter-character floor — i.e., keys typed at normal human speed could be dropped or delayed by a full frame.  Draining all pending entries each frame matches `machine_inject_to_circ_buf()` behaviour and means SAMOS sees the full burst of typed characters without artificial throttling.

### ESC key: emulator-side dual behaviour

The host `Escape` key maps to two distinct behaviours depending on whether the CLI
prompt is currently on an empty line:

| Situation | Emulator action | SAMOS receives |
|-----------|----------------|----------------|
| Non-empty CLI line | Send `0x04` (`<` EOT, EFFACE) | SAMOS clears the current line |
| Empty CLI prompt | Re-inject `kbd.prev_cmd` into FIFO | Characters typed char-by-char |

**Why SAMOS native recall (`0x05`) is not used:**  The SAMOS line editor at `0x5A8A`
writes the cursor character `'-'` (0x2D) directly to `m->bus[0x45C0]` (the first byte
of the line buffer) as part of display initialization at the start of each new input
session.  By the time `0x05` (recall) would run, the previous command's first character
has been overwritten — so recall displays nothing.

**History capture:**  `keyboard_event()` captures the current typed line whenever the
Return key is pressed and the line is non-empty.  The capture reads
`m->bus[0x45C0 .. (0x7014)-1]` (line buffer start up to the cursor pointer) and stores
it in `kbd.prev_cmd[128]` / `kbd.prev_cmd_len`.  This runs *before* the Enter code is
delivered to the FIFO, so the buffer is still intact.

**Recall re-injection:**  When ESC is pressed on an empty CLI prompt (detected by
`machine_cli_prompt_visible()`), `keyboard_event()` pushes each byte of `kbd.prev_cmd`
into the FIFO **without** bit 7 set (treated as injected, not physical).  SAMOS echoes
each character and leaves the cursor at the end of the line, ready for editing.
No Enter is appended — the user can modify the recalled line before pressing Return.

```c
/* On Return press (non-empty line): capture history */
uint16_t cursor = (uint16_t)m->bus[0x7014u] | ((uint16_t)m->bus[0x7015u] << 8);
if (cursor > 0x45C0u && cursor <= 0x45C0u + 127u) {
    int len = (int)(cursor - 0x45C0u);
    memcpy(m->kbd.prev_cmd, &m->bus[0x45C0u], len);
    m->kbd.prev_cmd_len = len;
}

/* On ESC press (empty line): re-inject history */
for (int i = 0; i < m->kbd.prev_cmd_len; i++) {
    m->kbd.fifo[m->kbd.fifo_tail] = m->kbd.prev_cmd[i];  /* no bit 7 → no auto-repeat */
    m->kbd.fifo_tail = (m->kbd.fifo_tail + 1) & 63;
}
```

**Empty-line detection** uses `machine_cli_prompt_visible()` (in `machine.c`), which
scans all 20 video rows for the pattern `*` ` ` `-` (with bit 7 stripped).  A row
matching this pattern indicates the prompt is at the start of a new empty line.

---

### Autoboot / inject: CLA-based delivery

`machine_inject_key()` (used by `-break-to-monitor` and `-inject-str`) sets:

```c
m->kbd.key_code        = code;
m->kbd.found           = 1;
m->kbd.physically_held = 1;
```

While `physically_held=1`, every CLA read returns `key_code` and immediately
re-asserts `found=1` — exactly as on real hardware with a held key.  `machine_release_key()`
clears both flags to end the inject.

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

**Write** (by `keyboard_frame_tick()` or `machine_inject_to_circ_buf()`):
```
[ptr] = key_code;   ptr++
```
After: `[0x4596] = key`, `ptr = 0x4597`.

**Peek** (`0x04F6`):
```
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
```
dec ptr; store       ; ptr = 0x4596
HL = DE = 0x4596
A = [0x4596]         ; read character
INC HL               ; HL = 0x4597
LDIR (BC = ptr-base = 0)  ; copies 0 bytes
```
After consume: `ptr = 0x4596`, `[0x4596]` retains the consumed character (LDIR did nothing).
`keyboard_frame_tick()` uses the **guard-sentinel check** (`[wr] != 0x80`), not `ptr == 0x4596`.

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

This is why `-inject-str "A"` visibly changes the prompt line while a one-shot
CLA key injected through `machine_inject_key()` does not.

---

## Notes on `SDL_VIDEODRIVER=dummy`

When `SDL_VIDEODRIVER=dummy` is set (CI / automated test mode), SDL delivers **no real keyboard events**.  Only the key injection path (`machine_inject_key()`) works.  Interactive keyboard testing requires a real display:

```sh
DISPLAY=:0 ./build/smemu6 -floppy floppies/Sys1-H.dsk
```

---

## Memory Map Summary

| Address | Purpose |
|---------|---------|
| `0x457E` | Last key code from Stage 1 (used by syscall 0x0E) |
| `0x4558` | Debounce / key-repeat countdown (0x23 = 35 frames) |
| `0x4580` | **Function-key bitmask (GETFON register)** — written by SAMOS ISR Stage 1 (`AND 0x7F; LD (0x4580),A`) from the CLA return value each frame.  Since `keyboard_read_cla()` returns `0x80 | fonct_bits` when no regular key is held, Stage 1 stores exactly `fonct_bits` here automatically.  No extra write from `main.c` needed. |
| `0x4566` | SAMOS 50 Hz ISR vector (written to `0x003E` by SYS.SY at `0x00CD`, **after** boot-menu kbd_wait exits; used as the power-on virtual key release trigger in `keyboard_frame_tick()`) |
| `0x458A+` | Circular buffer storage (slots marked with bit 7 when filled) |
| `0x45BF` | Timer countdown register (unrelated to keyboard) |
| `0x45C0` | **CLI line buffer start** — typed characters stored here; overwritten by `-` cursor at start of each new input session (used for ESC history capture) |
| `0x7014:0x7015` | **Cursor pointer** (little-endian) — points one past the last typed character in the line buffer; used by ESC history capture to determine line length |

---

## Key Codes — Hardware Reference (doc section 10.4, pages 213–215)

### Category 1 — "Touches de fonction" (7 function keys, bitmask)

These 7 keys are **not** sent through the key FIFO.  When no regular key is pressed
(FOUND=0), the CLA port returns a 7-bit bitmask — one bit per function key held.
The SAMOS `?GETFON` / `GETFON` system calls read this bitmask.

Codes confirmed from doc section 10.4, page 213 (octal):

| Key     | Octal | Hex    | Bit | Primary SDL scancode      | Alt SDL scancode           |
|---------|-------|--------|-----|---------------------------|----------------------------|
| CURSOR  | `020` | `0x10` | 4   | `SDL_SCANCODE_F1`         | `SDL_SCANCODE_LCTRL`       |
| COPY    | `010` | `0x08` | 3   | `SDL_SCANCODE_F2`         | `SDL_SCANCODE_LALT`        |
| KILL    | `100` | `0x40` | 6   | `SDL_SCANCODE_F3`         | `SDL_SCANCODE_RALT`        |
| PROGRA  | `040` | `0x20` | 5   | `SDL_SCANCODE_F4`         | `SDL_SCANCODE_LGUI`        |
| SHOW    | `004` | `0x04` | 2   | `SDL_SCANCODE_F5`         | `SDL_SCANCODE_INSERT`      |
| SEARCH  | `002` | `0x02` | 1   | `SDL_SCANCODE_F6`         | `SDL_SCANCODE_HOME`        |
| CHANGE  | `001` | `0x01` | 0   | `SDL_SCANCODE_F7`         | `SDL_SCANCODE_END`         |

**Implemented:** `uint8_t fonct_bits` field in `struct kbd`.  `keyboard_event()`
sets/clears the corresponding bit on `SDL_KEYDOWN`/`SDL_KEYUP` for **both** the
F1–F7 primary scancodes and the alternative modifier/nav scancodes (End, Home,
Insert, LAlt, LCtrl, LGui, RAlt).  Mouse clicks on the function-key status bar
also set/clear bits (left-click = momentary; right-click = latched toggle).

The SAMOS ISR Stage 2 (`AND 0x7F; LD (0x4580), A`) stores `fonct_bits` to the
GETFON register automatically when `keyboard_read_cla()` returns `0x80 | fonct_bits`
(FOUND=0).  No post-frame bus write is needed.

### Category 2 — Special keys in the normal FIFO (codes confirmed from doc p.213)

| Key      | Octal | Hex    | Chargen glyph | SDL scancode              |
|----------|-------|--------|---------------|---------------------------|
| TAB      | `011` | `0x09` | (tab)         | `SDL_SCANCODE_TAB`        |
| MACRO    | `036` | `0x1E` | `«`           | `SDL_SCANCODE_F8`         |
| DEF(INE) | `037` | `0x1F` | `»`           | `SDL_SCANCODE_F9`         |

`REP / FNCT` (code `0` in the doc) is the hardware repeat/function modifier; it
generates no independent code and can be ignored for now.

**Tab note:** SAMOS uses code `0x09` at the CLI prompt to insert the string `DX1:`,
expanding the tab as a drive prefix shortcut.  SDL fires `SDL_KEYDOWN` (not
`SDL_TEXTINPUT`) for Tab, so it must be in `KEY_TABLE[]` rather than handled via
`keyboard_text_event()`.

### Correction keys (doc p.215)

| Oct | Hex    | Name | SDL scancode               | Note                        |
|-----|--------|------|----------------------------|-----------------------------|
| 010 | `0x08` | BS   | `SDL_SCANCODE_BACKSPACE`   | in `KEY_TABLE[]`            |
| 177 | `0x7F` | DEL  | `SDL_SCANCODE_DELETE`      | in `KEY_TABLE[]`            |

Note: `0x7F` also renders as the solid-filled block glyph ▓ in the chargen ROM
(confirmed by direct ROM inspection — see [Chargen glyph reference](#chargen-glyph-reference) below).

### Swiss-French accented characters (Category 3)

Codes `0x0F`–`0x1D` are the 15 Swiss-French accented characters.  These are delivered
to the emulator via `SDL_TEXTINPUT` events as 2-byte UTF-8 sequences (the SDL event
system converts all text to UTF-8 regardless of host locale).  `keyboard_text_event()`
in `src/keyboard.c` handles both `SDL_TEXTINPUT` and `SDL_KEYDOWN` events:

- **Single-byte (0x20–0x7E):** pushed to the FIFO directly as printable ASCII.
- **Two-byte UTF-8 (`0xC2`–`0xDF` leading byte):** codepoint decoded, looked up in
  `ACCENT_TABLE[]`; the Smaky chargen code is pushed to the FIFO if found.

`ACCENT_TABLE[]` covers all 30 entries (15 lowercase + 15 uppercase):

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
| **ESC** / **UNDO** (top-left) | `Escape`                | Push **0x04** to kbd buffer → SAMOS CLI cancel/undo |
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

The `Escape` key on the PC keyboard maps to the physical **ESC / UNDO** key
(top-left corner).  The S471 keyboard encoder EPROM assigns this key hardware
code **`0x04`**, which the SAMOS CLI dispatches on to cancel/undo the current
command line (confirmed by CLI.SY disassembly: `CP 0x04` / `JP Z, cancel`).

`0x1B` is **not** the keyboard code for ESC.  It is the Smaky control-code
table entry for the printer/serial escape prefix, and it is also the chargen
display index for the `ä` glyph — both completely separate namespaces.

The **BREAK** key (top-right, also labelled NMI or RESET) is a separate physical
key that pulls the Z80 `NMI` pin low; it does not produce any keyboard byte.
