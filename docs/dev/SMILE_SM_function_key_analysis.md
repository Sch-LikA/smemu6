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

The branch now suppresses `SDL_TEXTINPUT` injection while `PROGRA` is active
and routes those key presses through the S471 matrix path instead, but leaves
the ordinary key on the normal/Shift/Caps matrix layers.

One more emulator-side policy bug remained after those fixes:

- the `?GETFO` compatibility helper at the `0x0519` accessor still preferred the
    staged ordinary byte in `0x457E`
- so with `PROGRA+z`, SMILE could receive `A=0x1A` (`û`) from `?GETFO` instead of
    the held function-bit mask `A=0x08`
- that matches the later interactive symptom exactly: the shortcut no longer
    produced plain `z`, but still failed because `?GETFO` was seeing the ordinary
    key code rather than the function key state

The branch now makes `?GETFO` return the held function bits directly and leaves
ordinary-key staging on the ordinary-key delivery path.

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

## Follow-Up

The next code-side check should be to reconcile the emulator's function-key bit
assignments with the canonical values exported in `FLO.ST` and then revalidate:

- `SMILE` for exact named keys such as `PROGRA`
- `FLIPPER` to confirm that the already-working behavior stays intact