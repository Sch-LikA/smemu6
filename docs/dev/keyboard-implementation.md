# Keyboard Implementation From Scratch

This document replaces the old mixed keyboard assumptions with a fresh
implementation plan based only on the findings that are now strong enough to be
treated as implementation inputs.

Goal: define the keyboard model we should build next, without inheriting the
current shortcut-heavy SDL-to-SAMOS path as the architectural baseline.

Primary objective: stay as close as possible to the original hardware.  When a
choice exists between convenience and hardware fidelity, this design document
prefers hardware fidelity by default and treats convenience behaviour as a
secondary compatibility layer.

## Scope

This document covers:

- host input normalization;
- S471 physical matrix decoding;
- layer selection;
- boot-time and post-boot regular-key delivery;
- function-key handling;
- what should be removed from the current implementation;
- a step-by-step refactor TODO.

This document does **not** treat older emulator behaviour as authoritative.
Anything not re-validated by the recent ROM dump or recent runtime probes is a
hint only.

This document also does **not** assume that the current emulator user experience
is the correct target.  The target is the original keyboard hardware behaviour
first; host-friendly shortcuts are acceptable only when they are explicitly
separated from the strict model.

## Validated Inputs

The following are the inputs we can now safely design around.

### 1. The S471 lookup PROM is authoritative for keycode generation

What we have here is not the 2 KB 2716 chargen EPROM.  It is the keyboard-side
**256x8 PROM** used as a keycode lookup device, very likely in the 74S471 /
74LS471 class.  In practice it behaves as **4 x 64 lookup entries**:

- layer 0: normal;
- layer 1: Shift;
- layer 2: FNCT/ALT-like;
- layer 3: caps-like.

This distinction matters:

- the **S471 PROM** maps physical key position + layer to an emitted 7-bit keyboard code;
- the **2716 chargen ROM** maps that keyboard/display code to glyph pixels.

This means the keyboard implementation should stop inventing keycodes from host
characters and instead derive them from:

- a physical key position;
- the active layer/modifier state;
- the S471 lookup table.

That is the key architectural direction: derive behaviour from the original
hardware lookup path, not from host-side convenience interpretations.

### 2. Top-left ESC / UNDO is `0x06` in normal use

Confirmed from the S471 PROM dump:

- top-left ESC / UNDO = `0x06 / 0x06 / 0x1B / 0x06` across the four layers.

This is now a validated hardware fact, not a compatibility guess.

### 3. The older `0x04` / `0x05` assumption belongs elsewhere

The S471 table places:

- Q-row right-edge candidate = `0x04 / 0x05 / 0x07 / 0x04`.

Therefore the previous top-left `0x04` assumption was based on the wrong
physical position.

### 4. The full 64-position matrix now has a checked position map

We now have a full 64-position physical-position table derived from the S471
PROM dump and matched against the front-panel key legends.  The checked-in
reference table is:

- `docs/dev/smaky6_full_matrix_map.tsv`

This upgrades the keyboard work from a handful of individually validated keys to
an explicit 64-position matrix model.

### 5. Directly validated matrix positions now include ordinary letter rows and RETURN

From the S471 PROM dump:

- Backspace position = `0x08 / 0x7F / 0x01 / 0x08`;
- Tab position = `0x09 / 0x0B / 0x03 / 0x09`;
- Space position = `0x20 / 0x20 / 0x02 / 0x20`;
- CTRL position = `0x1E` on all four layers;
- RETURN position = `0x0D / 0x0C / 0x0A / 0x0D`;
- DEFINE position = `0x1F` on all four layers.

The real-keyboard photo also confirms that the visible front-panel legends do
not always match the literal byte values emitted by the PROM.  For example, the
three keys after `L` and the three keys after `M` are best identified from their
keycap legend clusters first, and only then associated with the PROM outputs.
Future host bindings should therefore keep two separate notions:

- front-panel key identity (what the real keycap says);
- emitted S471 code for the active layer.

### 6. The alphanumeric matrix is real Swiss-German QWERTZ hardware

The dump confirms:

- normal layer letters are lowercase;
- Shift and caps-like layers produce uppercase letters;
- accented characters are present directly in the S471 matrix;
- the current emulator's generic printable ASCII passthrough is a usability
  shortcut, not a faithful hardware model.

### 7. The 7 bottom-row function keys are out of matrix

The 7 bottom-row function keys are out of matrix and must be modeled as a
function-bitmask (`fonct_bits`) visible only through CLA when `FOUND=0`; they
should not be generated through the S471 table.

This should be treated as a validated architectural input, so future work does
not waste effort trying to hunt those keys inside the 64-position scan table.

### 8. Photo evidence confirms CAPS/LOCK and NMI/BREAK are separate non-matrix keys

The real-keyboard photo confirms two additional hardware facts:

- `CAPS/LOCK` is a separate latching key and is not part of the 64-position
   ordinary matrix;
- `NMI` / `BREAK` is a separate key and is neither part of the ordinary matrix
   nor one of the 7 bottom-row function keys.

Therefore:

- the caps-like S471 layer should be modeled as a separate latched modifier
   input, not as an ordinary matrix position;
- BREAK-family handling remains outside ordinary keycode delivery and outside
   `fonct_bits`.

### 9. Post-boot regular-key delivery should be CLA-driven, not FIFO-shortcut-driven

This is now the implemented baseline.

Validated runtime and code result:

- ordinary host scancodes are resolved by physical position through the S471
   lookup table in `src/keyboard.c`;
- the resulting ordinary key is latched into the CLA-visible `found/key_code`
   state instead of being written directly into the SAMOS circular buffer;
- overlapping SDL taps are queued as pending ordinary keys and promoted when the
   current latch becomes idle;
- once `SYS.SY` commits a still-held ordinary key into the circular buffer via
   the `0x457C` advance hook, the emulator arms `0x4558` / `0x4577` there and
   then drops the CLA-visible hold so later Stage 1 helper polls do not zero the
   countdown before Stage 4 can repeat it;
- released promoted keys are allowed one synthetic reassert so `SYS.SY` can see
   them, then they are dropped again;
- once `SYS.SY` commits such a released promoted key into the circular buffer,
   the emulator also clears SAMOS repeat state (`0x4558`, `0x4577`) so the key is
   not re-injected endlessly by Stage 4.

What remains as active design work is narrower:

- finish the remaining host-position coverage in `HOST_MATRIX_KEYS[]`;
- keep re-auditing the exact hardware explanation for the bit-7-prefixed first
   ordinary CLA read, which is still validated as behaviour but not yet as a
   pure electrical claim.

So the required direction is not "make the current emulator trick work better";
it is "replace emulator shortcuts with a model that is as close as possible to
the original keyboard hardware."

### 10. The current implementation is still partly hybrid

The current codebase still combines multiple host-side input classes:

- ordinary matrix positions via `HOST_MATRIX_KEYS[]` and S471 lookup;
- function keys as a separate `fonct_bits` bitmask;
- some convenience host aliases for BREAK / reset / function access.

That split is acceptable as a description of the current code, but it should not
be the architecture we preserve.

## Hint-Only Inputs

The following are still useful, but they should not be treated as design axioms.

### 1. Old CLI interpretation of `0x04` / `0x05`

The older emulator logic treated these as top-left ESC/UNDO cancel/recall codes.
The ROM dump shows that physical assumption was wrong.  Any remaining runtime
truth in that older analysis must now be reinterpreted against the correct key
position.

### 2. Current host convenience aliases

Mappings such as:

- `F1..F7`;
- `End`, `Home`, `Insert`;
- `Left Alt`, `Left Ctrl`, `Left Windows`, `AltGr`;
- `F8`, `F9`.

These are useful for usability, but they are not evidence of original physical
key placement.

### 3. Compatibility text-entry may still be useful later

If accented input or host-layout-friendly text entry returns, it should be
documented as a compatibility layer on top of the strict S471 baseline, not as
the hardware-faithful producer model.

### 4. The `0x80 | key_code` first-read behaviour

This must be treated as an **implementation heuristic / compatibility path**, not
as a hardware axiom.

Why:

- the documented CLA semantics are still the clean electrical rule: if FOUND=1,
   return a regular keycode with bit 7 clear; if FOUND=0, return
   `0x80 | fonct_bits`;
- returning `0x80 | key_code` for a regular key mixes the "no regular key found"
   encoding in bit 7 with regular-key data on bits 0-6;
- that combination currently has probe value because it drives `SYS.SY` correctly,
   but it is not yet justified as a real hardware state.

Therefore, the strict model must not depend on it as a proven electrical fact.
At most, it may remain as a compatibility behaviour until a cleaner hardware
explanation is found.

### 5. Out-of-matrix keys and unresolved printed legends

Some important keys are likely not represented cleanly as ordinary matrix
positions in the currently decoded S471 table, or at least not in a way that is
safe to identify from printed legends alone.

Probable out-of-matrix or separately handled cases include:

- CAPS / LOCK;
- RESET / NMI / BREAK-family keys;
- the 7 function keys.

Therefore the PROM positions should not yet be treated as a one-to-one mapping to
front-panel printed legends unless a position map confirms that identification.

For the 7 function keys, the current best interpretation is stronger than a mere
warning: they should not be modeled as ordinary S471 matrix entries that produce
a normal latched keycode.  They should be handled as separate inputs that update
the function-bitmask state.

## New Implementation Model

The new implementation should be built around a strict physical-key core.

Design priority order:

1. original hardware behaviour;
2. electrical and firmware consistency;
3. only then host convenience and fallback compatibility.

### A. Separate physical position from emitted code

The keyboard core should work with a model like:

- `physical_position` = one of 64 ordinary matrix positions;
- `layer` = normal / Shift / FNCT / caps-like;
- `resolved_code` = `s471_layer[layer][physical_position]` for ordinary matrix keys;
- `fonct_bits` = separate state contributed by the 7 function keys;
- `pressed` / `released` state;
- optional metadata for host convenience bindings.

The core should not start from ASCII bytes or SDL text strings.

The important split is:

- ordinary matrix keys resolve through the 64-position S471 lookup;
- the 7 function keys do **not** resolve through that table and instead update
   the separate function-key state.

### A1. Clean code representation

The cleanest code shape is to represent the keyboard core with three separate
concepts:

- scanned ordinary-matrix key position (`0..63`), used with the S471 lookup PROM;
- layer / modifier state, used to choose which of the 4 S471 lookup layers applies;
- function-key bitmask (`fonct_bits`), used for the 7 direct function keys.

One reasonable C-level representation is:

```c
typedef enum {
   S471_LAYER_NORMAL = 0,
   S471_LAYER_SHIFT  = 1,
   S471_LAYER_FNCT   = 2,
   S471_LAYER_CAPS   = 3,
} SmakyKeyLayer;

typedef struct {
   uint8_t found;
   uint8_t key_code;
   uint8_t physically_held;
   uint8_t reassert_pending;
   uint32_t reassert_cycles;
} SmakyClaKeyState;

typedef struct {
   uint8_t shift_pressed;
   uint8_t fnct_pressed;
   uint8_t caps_like_active;
   uint8_t fonct_bits;
} SmakyModifierState;

typedef struct {
   int active;
   uint8_t position; /* 0..63 */
} SmakyMatrixKey;
```

And one clean separation of responsibilities is:

- `SmakyMatrixKey` identifies the currently scanned ordinary key position;
- `SmakyModifierState` decides which S471 layer is active and tracks the 7 direct
  function-key inputs in `fonct_bits`;
- `SmakyClaKeyState` models the low-level CLA-visible latch / FOUND state.

The ordinary key path should then look like:

1. host input -> matrix position;
2. matrix position + layer -> `s471_layer[layer][position]`;
3. resolved code latched into `SmakyClaKeyState`;
4. CLA reads expose that state through the strict hardware rule.

The function-key path should look like:

1. host input -> one of the 7 direct function keys;
2. set/clear the corresponding bit in `fonct_bits`;
3. do **not** synthesize a normal latched keycode from the S471 matrix for that path.

### B. Resolve keycodes through the S471 table

For every host keypress that represents a real **ordinary matrix** key:

1. map host input to a physical key position;
2. determine the active layer;
3. read the emitted code from the S471 matrix;
4. feed that code into the CLA-side keyboard state.

This should become the default path for real ordinary-key use.

The 7 function keys are a separate path:

1. map host input to one of the 7 function inputs;
2. update `fonct_bits` directly on press/release;
3. let the strict CLA rule expose that state only through `0x80 | fonct_bits`
    when FOUND=0.

SDL2 already gives explicit scancodes for `F1..F7`, so the host-convenience
layer can stay simple here without weakening the strict model.  A readable shape
is:

```c
static int fn_index_from_scancode(SDL_Scancode sc)
{
      switch (sc) {
      case SDL_SCANCODE_F1: return 0; /* CURSOR */
      case SDL_SCANCODE_F2: return 1; /* COPY   */
      case SDL_SCANCODE_F3: return 2; /* KILL   */
      case SDL_SCANCODE_F4: return 3; /* PROGRA */
      case SDL_SCANCODE_F5: return 4; /* SHOW   */
      case SDL_SCANCODE_F6: return 5; /* SEARCH */
      case SDL_SCANCODE_F7: return 6; /* CHANGE */
      default: return -1;
      }
}
```

Then the SDL key handler can do:

- on `SDL_KEYDOWN`, set the corresponding `fonct_bits` bit;
- on `SDL_KEYUP`, clear the corresponding `fonct_bits` bit;
- leave CLA unchanged except that `keyboard_read_cla()` returns
   `0x80 | fonct_bits` whenever `found == 0`.

This keeps the separation clean:

- SDL `F1..F7` are just a host-facing way to drive the 7 direct function inputs;
- they do not create ordinary latched keycodes;
- they do not go through the S471 64-position key matrix.

Integration note with redcode/Z80 + Zeta:

- the 7 function keys do not need any fake FIFO -> SAMOS circular-buffer path to
   become visible to software;
- `SYS.SY` sees them naturally through `IN (0x00)` when `FOUND == 0`, because CLA
   returns `0x80 | fonct_bits` and Stage 1 copies that into `0x4580`;
- direct helper consumers such as syscall `0x0E` still read through the `0x457E`
   accessor, so the emulator synthesizes held `fonct_bits` there only when no staged
   ordinary key byte is pending;
- therefore the strict function-key path remains a hardware-visible read-state
   change, not a synthetic text/buffer injection path.

If a convenience mapping cannot be reconciled with this hardware-first path, the
convenience mapping should lose, not the hardware model.

### C. Retire direct physical-key writes to the SAMOS circular buffer

The current shortcut path:

- `SDL_KEYDOWN` / `SDL_TEXTINPUT`
- pending ordinary-key queue for overlapping taps
- `keyboard_frame_tick()`
- direct write to circular buffer

should no longer be the primary implementation for physical keys.

Instead:

- physical regular keys should enter through the low-level CLA-facing state;
- `SYS.SY` should remain responsible for promotion into its own workspace and
  finally into the circular buffer;
- the emulator should model scanner/FOUND behaviour rather than post-facto text
  insertion.

### D. Keep the strict CLA electrical rule authoritative

The strict model should keep the CLA contract as:

- if FOUND=1, CLA returns `key_code & 0x7F` and the read clears the latch;
- if FOUND=0, CLA returns `0x80 | fonct_bits`.

That rule is consistent with the validated port semantics and should remain the
authoritative hardware model until disproved.

This is also why the 7 function keys should not be treated as ordinary S471
matrix entries that generate a latched keycode: their strict-model role is to
contribute to `fonct_bits`, which is then exposed on the CLA port only through
the FOUND=0 path.

### D1. CLA / STATUS code shape

The strict port behaviour can be represented directly in code.

For `IN (0x00)` / CLA:

```c
uint8_t keyboard_read_cla(struct Smaky6 *m)
{
      if (m->kbd.found) {
            uint8_t value = m->kbd.key_code & 0x7Fu;

            /* Strobe side effect: reading CLA clears the latched key state. */
            m->kbd.found = 0;
            /* Any additional latch-clear / reassert scheduling stays here. */

            return value;
      }

      return 0x80u | (m->kbd.fonct_bits & 0x7Fu);
}
```

For `IN (0x01)` / STATUS:

```c
uint8_t keyboard_read_status(struct Smaky6 *m)
{
      uint8_t st = 0;

      if (m->kbd.found)
            st |= 0x04u; /* bit 2 = FOUND */

      st |= 0x08u;     /* bit 3 = board-dependent; keep high if the current board wiring does so */
      return st;
}
```

That keeps the model clean:

- ordinary matrix keys affect `found` + `key_code`;
- the 7 direct function keys affect `fonct_bits`;
- CLA returns either a latched ordinary key or `0x80 | fonct_bits`, never a blend
   of both in the strict model;
- STATUS exposes FOUND on bit 2, while bit 3 may remain tied high depending on
   the board wiring being modeled.

### E. Keep the first-read compatibility behaviour separate from the strict model

For post-boot held regular keys, the current compatibility behaviour is:

- first relevant CLA read returns `0x80 | key_code`;
- subsequent handling follows the validated SYS.SY path.

This should be treated as:

- a compatibility path that currently reproduces the observed `SYS.SY` promotion
   behaviour in the emulator;
- useful for injection and transitional refactoring work;
- **not** part of the strict electrical keyboard model unless real hardware
   evidence eventually justifies it.

### F. Keep function-key handling, but stop conflating it with physical matrix fidelity

The function-key model should be split conceptually into two layers:

- **hardware-faithful layer**: actual Smaky function-key state as a separate
   function-bitmask input, not as ordinary S471 matrix keycodes;
- **host convenience layer**: alternative PC bindings that are not physical-key
  claims.

In practice this means:

- keep the currently working `fonct_bits` behaviour as the basis of the strict
   function-key path;
- document convenience bindings as convenience bindings;
- do not let them distort the physical matrix model.

The emulator may expose convenience bindings, but they must never redefine what
the original hardware is assumed to do.

### G. Demote `SDL_TEXTINPUT` to compatibility mode

`SDL_TEXTINPUT` should remain available for:

- accented host input convenience;
- non-US/non-Swiss host layouts;
- optional relaxed typing mode.

But it should not remain the primary model for the default hardware-faithful path.

Current direction:

- default runtime: strict S471 physical-key emulation for non-text keys, plus `SDL_TEXTINPUT` for printable host text;
- future optional mode split: keep the current hybrid runtime as the host-friendly default until a fully strict printable path is good enough to stand on its own.

The strict mode is not an optional afterthought.  It is the reference model we
should be aiming to make correct first.

What this means concretely:

- **Strict mode**: ordinary matrix keys follow `host position -> S471 table -> regular keycode -> CLA latch path`; function keys update `fonct_bits`, feed GETFON through the normal CLA path, and are synthesized for syscall `0x0E` only when no staged ordinary direct byte is pending; space bar remains a normal key unless later hardware evidence shows it is separately wired, in which case it can become a specific special case without changing the overall architecture.

- **Compatibility mode**: keep `SDL_TEXTINPUT` and other host-friendly typing paths for ordinary printable input; in the current runtime this is active for printable keys by default so international host layouts produce the intended character while Backspace, Tab, Return, function keys, BREAK-family handling, and other non-text keys still follow the strict keydown path; document any extra host aliases explicitly as non-physical host bindings, not as claims about original keyboard wiring.

### H. Model layer selection explicitly

The implementation should represent at least these states explicitly:

- Shift pressed;
- FNCT pressed;
- caps-like state if it is a persistent hardware mode rather than plain Shift;
- BREAK / RESET / NMI related combinations separately from ordinary keycodes.

The lookup layer must be chosen before the final code is emitted.

### I. Treat BREAK-family behaviour as a separate subsystem boundary

BREAK, SHIFT-BREAK, FUNCTION-BREAK, and FUNCTION-SHIFT-BREAK should not be
mixed into the ordinary printable-key path.

They should be handled as dedicated machine-control events that may still depend
on keyboard-side modifier state.

## Proposed Refactor Boundaries

### Replace

- `KEY_TABLE[]` as the main source of physical keycodes;
- printable ASCII passthrough as the default source of printable keys;
- direct physical-key FIFO-to-circular-buffer delivery as the primary runtime path.

### Keep temporarily

- the current `fonct_bits` path if it continues to match observed behaviour;
- the current host convenience aliases, but clearly marked as compatibility input;
- `SDL_TEXTINPUT` for compatibility mode and accented fallback.

### Introduce

- one checked-in S471 matrix table in repo code, not just in ad-hoc notes;
- a host-to-physical-position map;
- explicit layer resolution;
- a single physical-key producer path for post-boot regular keys;
- a mode boundary between strict hardware emulation and host-friendly compatibility input.

## Step-By-Step TODO

1. Add a checked-in S471 matrix source file to the emulator codebase.
   Use the full four-layer 64-entry table as the only keycode authority.

2. Introduce a `physical_position` abstraction.
   Map host inputs to physical Smaky positions rather than directly to emitted bytes.

3. Introduce explicit layer resolution.
   Resolve normal / Shift / FNCT / caps-like before generating a keycode.

4. Split host input into two modes.
   Add a strict S471 physical-key path and preserve the current text-input path only as compatibility mode.

5. Keep physical regular keys on the strict CLA-facing path.
   Route them through the CLA-facing key state instead.

6. Expand the strict CLA model to the remaining audited host positions.
   Keep `FOUND=1 -> key_code & 0x7F` and `FOUND=0 -> 0x80 | fonct_bits` as the authoritative contract.

7. Re-test the top-left ESC / UNDO path with `0x06` as the real hardware code.
   Re-audit CLI behaviour without assuming the old `0x04` story.

8. Add an explicit mapping for the Q-row right-edge candidate.
   Treat `0x04 / 0x05 / 0x07 / 0x04` as a separate physical key position and test what SAMOS actually does with it.

9. Keep the `0x80 | key_code` path only as an optional compatibility experiment.
   Use it for controlled probes or transitional testing, not as the strict default model.

10. Rework Backspace, Tab, Return, and Space around physical-position semantics.
   Stop treating their current normal-layer behaviour as sufficient coverage.

11. Decide how FNCT-layer printable outputs should be exposed on host keyboards.
    Choose between strict positional bindings, optional compatibility shortcuts, or both.

12. Separate BREAK-family events from ordinary keycode delivery.
    Keep NMI/reset/boot-path handling outside the ordinary printable key pipeline.

13. Reduce or remove `KEY_TABLE[]` once strict mode is in place.
    Keep only host compatibility bindings that are still intentionally non-physical.

14. Revalidate auto-repeat after each queue / promotion change.
   Released promoted keys must clear `0x4558` and `0x4577` once committed so Stage 4 does not re-inject them endlessly.

15. Add focused runtime traces for physical position, resolved layer, resolved code, CLA return, and promotion outcome.
    Make the strict implementation debuggable without reintroducing shortcut logic.

16. Only after the strict path is validated, decide whether it should replace the current default behaviour.

## Immediate Recommendation

Do **not** start by patching more individual keycodes into the current hybrid
implementation.  The next work should start by creating the strict S471-backed
producer path and treating the current FIFO/text path as compatibility scaffolding.

The core discipline for the refactor should be: every new decision must move the
keyboard implementation closer to the original hardware, not merely closer to the
current emulator's observable behaviour.
