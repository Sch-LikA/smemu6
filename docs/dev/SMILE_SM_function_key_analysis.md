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

## Practical Conclusion

For `SMILE.SM`, the relevant read path is:

1. `RST 20h / 0x0E`
2. dispatch to `?GETFO`
3. return a function-key-related byte in `A`
4. branch on exact bitmask values inside SMILE

The strongest static explanation for the current `PROGRA` failure is therefore
**bit assignment mismatch**, not failure of SMILE to poll function keys at all.

## Follow-Up

The next code-side check should be to reconcile the emulator's function-key bit
assignments with the canonical values exported in `FLO.ST` and then revalidate:

- `SMILE` for exact named keys such as `PROGRA`
- `FLIPPER` to confirm that the already-working behavior stays intact