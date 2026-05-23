# SMILE.SM Function-Key Read Path

## Scope

This note documents how the extracted `SMILE.SM` binary reads function keys.
The goal is to anchor the analysis in the application binary itself instead of
inferring behavior from emulator-side assumptions.

Artifacts used:

- `private/floppies/extracted/Sys2-2-SMILE/SMILE.SM`
- `private/floppies/extracted/Sys2-2-SMILE/SMILE.SM.meta.json`
- `private/docs/disasm/SYS.SR`
- `build/generated-st/generated_flo_st_symbols.json`

`SMILE.SM.meta.json` reports:

- load address: `0x5500`
- entry point: `0x5508`

## Result

`SMILE.SM` does **not** read function keys by directly loading `0x4580`,
`0x45BD`, or `0x45BE`, and it does **not** directly `CALL 0x0EE7`.

Instead, the binary uses the SAMOS syscall mechanism:

- `RST 20h`
- followed by inline service byte `0x0E`

In `private/docs/disasm/SYS.SR`, the dispatcher table maps:

- `CODE 0EH = ?GETFO`

So the SMILE binary reads function keys through `RST 20h / 0x0E = ?GETFO`.

## Direct Evidence From SMILE.SM

A direct byte search over `SMILE.SM` found:

- no `LD A,(0x4580)`
- no `LD A,(0x45BD)`
- no `LD A,(0x45BE)`
- no `CALL 0x0EE7`

The only function-key-related read path found in the binary is a single
`RST 20h / 0x0E` site at `0x55D0`.

## Annotated Disassembly Around The Read

Raw bytes around the call site:

```text
55CC: 32 5D 88    LD (0x885D),A
55CF: 47          LD B,A
55D0: E7 0E       RST 20h / 0x0E   ; ?GETFO
55D2: 32 5E 88    LD (0x885E),A
55D5: 3A 5E 88    LD A,(0x885E)
55D8: B7          OR A
55D9: 28 36       JR Z,0x5611
55DB: FE 50       CP 0x50
55DD: C4 E6 59    CALL NZ,0x59E6
55E0: FD CB 05 86 RES 0,(IY+5)
55E4: CD 07 68    CALL 0x6807
```

Interpretation:

1. SMILE preserves the current ordinary-key-related `A` value in `B`.
2. It calls `?GETFO` through `RST 20h / 0x0E`.
3. It stores the returned function-key mask at `0x885E`.
4. If the returned mask is zero, it jumps to `0x5611` and continues the normal
   non-function-key path.
5. If the mask is nonzero, it enters a function-key dispatch path.

So the binary's branch point is explicitly:

- `A == 0`: no function key active
- `A != 0`: function-key-specific handling

## Syscall Table Evidence

In `private/docs/disasm/SYS.SR`:

```text
.W IFCAR        ;CODE 0DH = ?IFCAR
.W GETFO        ;CODE 0EH = ?GETFO
.W ENI50        ;CODE 0FH = ?ENI50
```

And the `GETFO` routine itself is listed there as:

```text
GETFO:
    LOAD A,42576
    OR A,A
    RET
```

The source uses octal notation, so `42576` octal = `0x457E`.

That means the documented syscall body is effectively:

```text
A := (0x457E)
OR A
RET
```

So the static SMILE path is consistent with the already-known
`syscall 0x0E -> 0x457E` family, not with a direct `0x4580` read.

## Function-Key Values Expected By SMILE

The exported `FLO.ST` symbol values give the canonical function-key constants:

| Key | Value |
|---|---:|
| `CHANGE` | `0x01` |
| `SEARCH` | `0x02` |
| `SHOW` | `0x04` |
| `PROGRA` | `0x08` |
| `KILL` | `0x10` |
| `COPY` | `0x20` |
| `CURSOR` | `0x40` |
| `MCLA` | `0x7F` |

These values are important because the current emulator-side function-key
mapping is:

| Key | Current emulator value |
|---|---:|
| `CURSOR` | `0x10` |
| `COPY` | `0x08` |
| `KILL` | `0x40` |
| `PROGRA` | `0x20` |
| `SHOW` | `0x04` |
| `SEARCH` | `0x02` |
| `CHANGE` | `0x01` |

The lower three bits match, but the four high-side function keys are permuted.

## Why PROGRA/F4 Fails In SMILE

This static binary audit gives a concrete explanation for the observed symptom:

- SMILE expects `PROGRA = 0x08`
- the current emulator emits `PROGRA = 0x20`

So when the user presses `F4` for PROGRA, SMILE will not recognize the bit it
expects.

This also explains why a different application can still appear correct:

- an app that only checks coarse groups or masks may still work
- an app that distinguishes named function keys by exact bit value will not

## Concurrent PROGRA + Matrix-Key Issue

Later interactive validation narrowed a second, separate issue:

- pressing `PROGRA+z` while assembling `HELLO.SR` produced plain `z`

That symptom is not explained by the bit-order mismatch alone. An early
emulator hypothesis was that `PROGRA` should also select the S471 `FNCT` layer:

- `keyboard.c` already defined `S471_LAYER_FNCT`
- but `current_layer()` only considered Shift and Caps Lock
- so even with `PROGRA` held, a concurrent matrix key still resolved through the
    normal S471 layer

That hypothesis turned out to be wrong for SMILE. Later user validation showed
that changing the ordinary key into an FNCT-layer byte (`û`, then `ù`) was
itself the bug: SMILE preserves the ordinary key separately and reads `PROGRA`
through `?GETFO`.

There was also a second implementation bug in the host-input path:

- text-capable keys such as `z` were treated specially through `SDL_TEXTINPUT`
- before the fix, `keyboard_event()` skipped those scancodes entirely and let
    `keyboard_text_event()` inject plain direct text
- so even after the FNCT layer selector was corrected, a `PROGRA+z` press could
    still be overwritten by a plain text `z`

The branch now suppresses `SDL_TEXTINPUT` injection while any function key is
active and routes those key presses through the S471 matrix path instead, but
leaves the ordinary key on the normal/Shift/Caps matrix layers.

One more emulator-side policy bug remained after those fixes:

- the `?GETFO` compatibility helper at the `0x0519` accessor still preferred the
    staged ordinary byte in `0x457E`
- so with `PROGRA+z`, SMILE could receive `A=0x1A` (`û`) from `?GETFO` instead of
    the held function-bit mask `A=0x08`
- that matches the later interactive symptom exactly: the shortcut no longer
    produced plain `z`, but still failed because `?GETFO` was seeing the ordinary
    key code rather than the function key state

The branch now makes `?GETFO` return the held function bits directly and leaves
ordinary-key staging on the ordinary-key delivery path. One more concrete bug
was found in that compatibility hook itself: the emulator was still treating the
special read as `pc == 0x0519`, while existing traces showed the actual
`LD A,(0x457E)` access at `pc == 0x0516`. The current code now recognizes the
full `GETFO` routine window `0x0516..0x0519` instead.

Another low-level mismatch was corrected afterwards: when no ordinary key was
latched, the emulator had drifted to returning bare `fonct_bits` from the CLA
port. The hardware audit notes and `SYS.SY` Stage 1/2 model instead require the
no-key/function path to be `0x80 | fonct_bits`, so the current code now restores
that bit-7-set idle/function behavior.

The next timing correction narrows the ordinary-key compatibility prefix itself:
plain post-boot typing still keeps the first `0x80 | key_code` ordinary CLA
delivery, but simultaneous `PROGRA+ordinary` input no longer uses that prefix.
The rationale is that SMILE expects the ordinary key to remain plain while the
function state is read separately; pushing the ordinary key through the same
Stage 2 workspace path during a held function key appears to be the remaining
delivery mismatch.

That timing change was later falsified by user retest because `PROGRA+z` and
`PROGRA+END` then had no visible effect at all. The next hardware-oriented step
instead removes the unconditional guest-workspace mirrors of `fonct_bits` at
`0x4580`, `0x45BD`, and `0x45BE`, so the function state is no longer published
outside the real CLA / `?GETFO` timing path.

An automated headless repro now exists for this scenario as well. With the
prepared hostdir media on DX0 (`-floppy-hostdir private/floppies/extracted/Sys2-2-SMILE`),
the emulator can now inject `SMILE HELLO.SR` at the CLI prompt, wait a
configurable number of frames for the editor to load `HELLO.SR`, then inject a
simultaneous chord such as `PROGRA+z`.

That scripted repro still shows the same underlying bug. Once `HELLO.SR` is
visible in SMILE, the automated `PROGRA+z` chord degrades into visible `z`
input on the source line instead of invoking the expected PROGRA behavior.
So the new injector is useful as a stable repro harness, but it does not alter
the remaining simultaneous-key mismatch.

The next traced refinement narrowed one more compatibility boundary. In the
repeated-`z` scripted repro, the later function-read path was not using the
already-audited `0x0516..0x0519` `0x457E` window. The active read in that SMILE
path appears at `pc=0x0524`, and the previous code treated that access as a raw
ordinary-byte read.

The current branch now widens the compatibility hook to include `pc=0x0524` as
well. In the scripted `PROGRA+z` hostdir repro, that change removes the visible
`z` flood from the source line: the post-chord screen stays on the loaded
`HELLO.SR` text instead of repeatedly writing `z`. This does not yet prove that
the final PROGRA action is fully hardware-faithful, but it does show that the
active SMILE path was still missing one real function-read window.

One more repeat-shaped bug then remained on `PROGRA+END`. Tracing that path
showed two separate sources of repeat pressure:

- the first chord delivery still armed the ordinary `END` CLA reassert path
    (`code=04`, `reassert=1`, normal scan-delay countdown)
- even after disabling that ordinary reassert, the later no-key CLA path could
    still keep re-exposing the held `PROGRA` bit after the first successful
    `?GETFO` read, which matches the confirmation-line flicker report more closely

The current branch now uses a narrower one-shot rule for that second part:

- `?GETFO` still returns the current held function state, so `PROGRA+z` keeps
    seeing `08` on the later `pc=0x0524` read
- once that function read has happened, the later no-key CLA path stops
    re-exposing the same held function bit until release

In the traced hostdir repros, that leaves the working `PROGRA+z` path intact
while changing the `PROGRA+END` follow-up CLA reads from repeated `0x88`-style
function/no-key exposures to plain `0x80` idle/no-key reads.

The next remaining compatibility layer is the `?GETFO` override itself. The
current branch now uses a narrower staged-byte-first policy: inside the audited
`GETFO` routine window (`0x0516..0x0519`), it returns the staged `0x457E` byte
when that byte is nonzero and only synthesizes held function bits when `0x457E`
is otherwise empty. A first attempt also consumed `0x457E` on each `?GETFO`
read, but that caused stray prompt characters during the normal headless boot
check and was therefore removed again.

That staged-byte-first policy was then falsified by user retest because it made
the old repeat bug reappear and made function keys echo regular characters
again. So the branch has been rolled back to the previous always-function
`?GETFO` override while the remaining simultaneous-key mismatch is investigated
elsewhere.

One last host-side mismatch appeared while the wrong FNCT-layer model was still
in place:

- on a QWERTZ host keyboard, pressing the key labeled `z` reaches SDL as
    `scancode=Y` with logical symbol `z`
- the FNCT shortcut path was still using raw scancode positions, so it resolved
    the Smaky `y` key and produced `0x19` (`ù`) instead of the Smaky `z` key
- that explains the next observed symptom exactly: `PROGRA+z` no longer yielded
    `z` or `û`, but still echoed `ù`

That host-layout workaround is no longer the controlling fix for SMILE, because
the final correction is to stop remapping the ordinary key through the FNCT
layer in the first place.

## Practical Conclusion

For `SMILE.SM`, the relevant read path is:

1. `RST 20h / 0x0E`
2. dispatch to `?GETFO`
3. return a function-key-related byte in `A`
4. branch on exact bitmask values inside SMILE

The strongest explanation is therefore: the bit assignment had to be corrected,
the `?GETFO` accessor had to return function bits, and the ordinary key must
remain plain while `PROGRA` is read separately. SMILE is not failing to poll
function keys; the emulator was conflating the function-bit path with matrix
byte remapping.

One more delivery bug then appeared to show up with non-letter combinations such as
`PROGRA+END`:

- even after the matrix/FNCT remap was removed, the first ordinary CLA read was
    still being prefixed as `0x80 | key_code`
- that made SAMOS treat a normal ordinary key as a function/no-key payload source
    for the live workspace path
- for `END`, the ordinary code `0x04` therefore surfaced as the visible chevron
    glyph instead of leaving the function state intact for SMILE's command logic

That specific hypothesis was later falsified by immediate user testing, because
removing the bit-7-first ordinary CLA delivery broke normal keyboard input at the
CLI prompt. So the current model still keeps the first ordinary CLA read as
`0x80 | key_code`, and the remaining simultaneous-key issue must lie elsewhere.

## Follow-Up

The next code-side check should be to reconcile the emulator's function-key bit
assignments with the canonical values exported in `FLO.ST` and then revalidate:

- `SMILE` for exact named keys such as `PROGRA`
- `FLIPPER` to confirm that the already-working behavior stays intact