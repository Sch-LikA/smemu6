# Smaky 6 CALM / SMILE syntax notes

This note documents the CALM dialect that appears in Smaky 6 `.SR` sources.
It is based on real source files in `private/floppies/extracted/` and
`private/docs/disasm/`, not on later CALM manuals.

The main consequence is scope: this is an observed syntax note, not a complete
language specification. If a construct is not listed here, it may still exist,
but it has not been confirmed in the currently reviewed corpus.

## What CALM looks like on Smaky 6

Smaky 6 sources are not written as raw Z80 mnemonics only. They use a higher
level assembler vocabulary that maps onto Z80 code generation for `.PROC Z80`.
Common examples are:

- `LOAD` instead of plain `LD`
- `COMP` instead of plain `CP`
- `JUMP,cond`, `CALL,cond`, `RET,cond`
- `DECJ,cond reg,label` for decrement-and-branch forms
- bit-oriented forms such as `TEST A:4`, `SET C:1`, `CLR (HL):7`

The dialect is therefore best described as a CPU-targeted CALM syntax with a
CPU-agnostic style of operands and control verbs, not as handwritten Z80 in
the usual Intel/Zilog notation.

## Reviewed source samples

These notes were extracted from a small but varied sample:

- `private/floppies/extracted/Editeur graphique/DEFINI.SR`
- `private/floppies/extracted/46 CPM Smaky 6 en plus/BIOS.SR`
- `private/floppies/extracted/Desas/DESASYM.SR`
- `private/floppies/extracted/Demo.sr/MOULIN.SR`
- `private/floppies/extracted/Sigma-Laurent/LP84.SR`
- `private/floppies/extracted/Sigma-Laurent/DUMPGRA.SR`
- `private/floppies/extracted/Winchester Tandon/HORLOGE.SR`

## File structure and top-level directives

Observed prologue pattern:

```calm
.TITLE  PROGRAM
.PROC   Z80
.REF    SM6
.LOC    53000
```

Observed directives:

| Directive | Observed role | Notes |
| --- | --- | --- |
| `.TITLE` | Module title | Usually first line. |
| `.SBTTL` | Secondary title / listing subtitle | Seen in `BIOS.SR`. |
| `.PROC Z80` | Select target CPU | Confirms this source is assembled for Z80. |
| `.REF name` | Import symbol set | Examples: `SM6`, `FLO`, `CPM`. Multiple `.REF` lines are allowed. |
| `.LOC expr` | Set assembly location | Used for program load address or table placement. |
| `.HEX` | Switch default radix | Seen together with `.RADIX 16.` in CP/M code. |
| `.RADIX n.` / `.RDX n.` | Set default radix | `16.` is observed. `.RDX` appears in repo searches and should be treated as an alias until proven otherwise. |
| `.IF expr` | Conditional assembly | Often used with feature flags such as `OKI84 = 1`. |
| `.ELSE` / `.ENDIF` | Conditional assembly branches | Standard pairing with `.IF`. |
| `.INS file` | Include another source file | Example: `.INS DX1:DESAFILE`. |
| `.END label` | End of source and entry label | Example: `.END START`. |

## Labels and symbols

Observed symbol forms:

- Global labels: `START:`, `DEBUT:`, `ERREUR:`
- Local numeric labels: `10$:`, `20$:`, with branches such as `JUMP,EQ 10$`
- Equates: `NAME = expression`

Examples:

```calm
OKI84   =   1
PROGR   =   21400
START:
10$:
```

## Literals and expressions

Observed literal syntax:

| Form | Meaning | Example |
| --- | --- | --- |
| `17.` | Decimal literal | Trailing dot is common. |
| `#expr` | Immediate constant / immediate address | `LOAD HL,#53000` |
| `'A` | Character literal | Also `'0`, `'Y`, etc. |
| `O'70` | Octal literal | Seen in CP/M BIOS code. |
| `/text/` | String delimiter for `.ASCII` / `.ASCIZ` | `/.X/`, `/BASIC.SM/` |
| `<CR>` etc. inside strings | Text macros expanded by assembler/runtime | Seen in `.ASCIZ` strings. |

Observed expression style:

- Arithmetic: `+`, `-`, `*`, `/`
- Symbol arithmetic: `ENDGRA-SGRA`, `PROGR+4377`
- Exponent / bit-mask style: `2^7`
- Boolean-ish conditional expressions: `OKI80!OKI82`

The exact precedence rules are not yet documented here. Use parentheses when
transcribing unfamiliar expressions.

## Data definition pseudo-ops

Observed data directives:

| Directive | Observed role | Example |
| --- | --- | --- |
| `.B` | Byte data | `.B 0`, `.B DEP1,DEP2` |
| `.BYTE` | Byte data | `.BYTE CMODE TMINCE` |
| `.W` | Word data or encoded service call list | `.W ?TEXT,TEXT1` |
| `.BW` | Byte + word tuple records | `INTBL: .BW TROIS,TIMTBL+CINQ` |
| `.BBB` | Three-byte tuple records | Used for command tables in `LP84.SR`. |
| `.BLKB` | Reserve bytes | `CH: .BLKB 1` |
| `.BLKW` | Reserve words | `SVSTK: .BLKW 1` |
| `.ASCII` | Inline text without implicit terminator | Used for banners and screen control text. |
| `.ASCIZ` | Zero-terminated text | Used for prompts, filenames, and messages. |

Important detail: `.W` is overloaded in the reviewed sources.

- In tables, `.W` behaves like ordinary word data.
- In program code, `.W ?NAME,...` is used as the normal way to invoke SMILE or
  SAMOS services and to pass inline operands to them.

## Instruction vocabulary observed in source

Common instruction forms found in the sample set:

| CALM form | Rough Z80 intent |
| --- | --- |
| `LOAD dst,src` | Load / store (`LD`) |
| `COMP A,#x` | Compare (`CP`) |
| `JUMP label` | Unconditional jump (`JP` / `JR`, exact encoding not stated here) |
| `JUMP,cond label` | Conditional jump |
| `CALL label` | Call subroutine |
| `CALL,cond label` | Conditional call |
| `RET` | Return |
| `RET,cond` | Conditional return |
| `INC reg` / `DEC reg` | Increment / decrement |
| `DECJ,cond reg,label` | Decrement and branch while condition holds |
| `ADD dst,src`, `SUB A,#x`, `AND A,#x`, `OR A,A`, `XOR A,A` | Arithmetic / logic |
| `PUSH reg`, `POP reg` | Stack ops |
| `LDIR` | Native Z80 block move mnemonic still appears directly |
| `RRC A`, `CPLC`, `SETC`, `IOF` | Special CPU/control mnemonics seen in corpus |

Two practical rules stand out:

1. CALM does not avoid all native Z80 mnemonics. `LDIR` and register names are
   still used directly.
2. Condition codes are attached after a comma, for example `JUMP,CS ERR1` and
   `RET,EQ`.

Observed condition suffixes include at least:

- `EQ`, `NE`
- `CS`, `CC`
- `LO`, `HS`

Forms with a dot also occur, for example `JUMP.,EQ 10$`. Those should be kept
verbatim until their exact encoding difference is mapped.

## Bit and field syntax

The older sources make heavy use of CALM bit operations that are not standard
Z80 syntax:

```calm
TEST    (HL):0
TEST    A:4
SET     C:1
CLR     (HL):7
```

Observed meaning:

- `TEST X:n` checks bit `n` of operand `X`
- `SET X:n` sets bit `n`
- `CLR X:n` clears bit `n`

These are important because they appear in real device drivers and utility
programs, not just in one isolated source file.

## Runtime service entry points

Smaky 6 CALM code relies heavily on `?NAME` service symbols referenced through
`.REF` files such as `SM6`, `FLO`, and `CPM`.

Commonly observed services include:

| Service | Observed usage |
| --- | --- |
| `?TEXT`, `?TEXTIM` | Display text |
| `?RETURN`, `?RTN` | Return to next line or CLI/runtime |
| `?CALPHA`, `?IALPHA` | Clear/init alpha screen |
| `?IGRA`, `?CGRA` | Graphics mode control |
| `?GETCAR`, `?GETLINE`, `?DICAR`, `?DITEX` | Console input/output |
| `?OPEN`, `?CREATE`, `?DELETE`, `?CLOSE`, `?RDBYTE`, `?WRBYTE` | File I/O |
| `?ERROR`, `?RESET`, `?BUZZ` | Runtime utility / error handling |
| `?PLAY`, `?DELAY`, `?MUL`, `?BINBCD`, `?SETCURS` | Miscellaneous helpers |

Call style is usually one of these:

```calm
.W  ?TEXT,TEXT1
.W  ?TEXTIM
.ASCIZ /Hello/
.W  ?GETCAR,?DICAR,?RETURN
```

This strongly suggests that `.W` emits runtime call vectors plus inline
arguments in the format expected by the Smaky 6 environment.

## Include and multi-file layout

Large programs are often split across several `.SR` units and reassembled by
SMILE with `.INS`:

```calm
.INS DX1:DESAFILE
.INS DX1:DESAIO
.INS DX1:EXCOB.SR
```

Important practical point: included filenames may omit the `.SR` suffix in some
cases and include it in others. Preserve the source spelling when reproducing
or scripting these builds.

## Things that remain unclear

The reviewed sample does not yet fully explain:

- the exact semantic difference between `JUMP` and `JUMP.` forms
- whether `.RDX` is fully identical to `.RADIX`
- the precise binary layout emitted by `.BW` and `.BBB` in every case
- how many non-Z80 CPU backends existed for the same CALM source style in the
  Smaky toolchain era
- the full catalog of `?NAME` services exported by `SM6`, `FLO`, and `CPM`

Until those are confirmed from older manuals or more source files, keep the doc
strictly observational.

## Practical advice for new Smaky 6 CALM examples

- Start from an existing `.SR` file instead of writing raw Z80 from scratch.
- Prefer observed directives: `.TITLE`, `.PROC Z80`, `.REF`, `.LOC`, `.END`.
- Prefer observed runtime calls such as `.W ?TEXTIM` and `.W ?RTN` over hardcoded
  addresses.
- Preserve odd-looking forms like `10$`, `TEST A:4`, and `JUMP.,EQ` exactly.
  They are part of the dialect, not transcription mistakes.
- When a later CALM manual disagrees with the Smaky 6 sources, trust the source
  corpus first and mark the manual-derived claim as unconfirmed.