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

The source evidence is uneven.

- `private/docs/disasm/SYS.SR` contains a named `RST 20H` dispatch table for
  `SM6`, so those services can be tied to concrete call codes.
- The extracted `.SR` corpus shows many `FLO` and `CPM` calls in real programs,
  but the matching export tables were not yet found in this repository.
- The shipped SMILE symbol tables do show a clear hierarchy: all 251 exported
  `SM6` names also exist in `FLO`, while `FLO` exports 92 additional names.
  `SM6` is therefore a name subset of `FLO`, but not a byte-for-byte alias
  table because at least one shared symbol differs in value: `MINI` is `0001`
  in `SM6` and `0000` in `FLO`. That difference currently looks like symbol-set
  configuration rather than a runtime-service mismatch, because `MINI` is a
  plain constant in the `MON` / `MODI` / `NORM` family and was not found in the
  extracted `.SR` call corpus.

### SM6 versus FLO

Direct comparison of `sdcc/SM6.symbols` and `sdcc/FLO.symbols` gives:

- `SM6`: 251 exported names
- `FLO`: 343 exported names
- shared names: 251
- names present only in `SM6`: 0
- names present only in `FLO`: 92

The extra `FLO` surface is exactly what the extracted corpus suggests: more
file, block, and directory helpers plus many more disk-oriented error/status
symbols. Representative `FLO`-only names include `?RDBLK`, `?WRBLK`, `?OPEBL`,
`?CREBL`, `?LIST`, `?FORMA`, `?GDIR`, `?GHEAD`, `?GNBLK`, `?MODAY`, `?UPDAT`,
`WRPROT`, `RDPROT`, `ONAME`, `ONBLK`, `OATTR`, `ODATE`, and the extended `ER*`
error-code family.

So the practical reading is:

- use `SM6` when a source only needs the smaller common runtime view
- use `FLO` when the source needs the broader storage, directory, or file
  metadata interface
- do not assume the two tables are mechanically identical at the constant
  level, even though the callable runtime surface appears to nest cleanly

### SM6 services with code mapping from `SYS.SR`

These names come from the dispatch table in `private/docs/disasm/SYS.SR`.
The table covers codes `00H..67H`; the list below calls out the services that
already show up repeatedly in extracted programs.

| Code | Service | Observed role |
| --- | --- | --- |
| `00H` | `?DICAR` | display one character |
| `01H` | `?GETCAR` | read one character |
| `05H` | `?GETLINE` | line input |
| `06H` | `?DITEX` | display text from a pointer |
| `0DH` | `?IFCAR` | non-blocking character test/read |
| `0EH` | `?GETFO` | function-key state read |
| `11H` | `?IALPH` | initialize alpha mode |
| `12H` | `?IGRA` | initialize graphics mode/state |
| `18H` | `?CALPH` | clear alpha screen/state |
| `19H` | `?CGRA` | clear graphics screen/state |
| `1AH` | `?BUZZ` | buzzer/beeper helper |
| `20H` | `?SETCU` | set cursor |
| `21H` | `?GETCU` | get cursor |
| `22H` | `?SPACE` | emit a space or spacing action |
| `23H` | `?RETUR` | newline / return helper |
| `2FH` | `?MUL` | multiply helper |
| `31H` | `?JUMPC` | jump through command table |
| `3DH` | `?PLAY` | play note or tune sequence |
| `3EH` | `?BEEP` | short beep helper |
| `47H` | `?PRSTA` | printer status helper |
| `4FH` | `?GETAR` | get argument helper |
| `51H` | `?TAB` | tabulation helper |
| `52H` | `?CLEAR` | clear current line or region |
| `53H` | `?TEXT` | display inline text block |
| `54H` | `?BINBC` | binary to BCD conversion |
| `55H` | `?BCDBI` | BCD to binary conversion |
| `57H` | `?DELAY` | delay helper |
| `59H` | `?AFXHL` | formatted numeric output from `HL` |
| `5EH` | `?TEXTIM` | immediate inline text output |

The table also exposes many less-understood names such as `?JUMPI`, `?COMPH`,
`?LOADB`, `?AMORC`, `?EXECU`, `?MON`, `?TRAPP`, and the `?RDCLK` / `?WRCLK`
family. Those should stay descriptive-only until a call site makes their ABI
clear.

### Source-derived usage patterns by symbol set

The extracted corpus is already enough to group common services by how programs
actually use them.

| Symbol set | Common services | Representative observed use |
| --- | --- | --- |
| `SM6` | `?TEXT`, `?TEXTIM`, `?GETCAR`, `?DICAR`, `?RETURN`, `?RTN` | console UI and CLI-style interaction |
| `SM6` | `?IALPHA`, `?CALPHA`, `?IGRA`, `?CGRA`, `?SETCURS` | alpha/graphics setup and cursor control |
| `SM6` | `?BEEP`, `?BUZZ`, `?PLAY`, `?MUL`, `?BINBCD`, `?AFXHL`, `?TAB` | utility, sound, formatting, arithmetic |
| `FLO` | `?OPEN`, `?CREATE`, `?DELETE`, `?CLOSE`, `?RDBYTE`, `?WRBYTE`, `?RDBLK`, `?OPEBLK`, `?RENAME`, `?LGO` | file, block, loader, and driver-management calls |
| `CPM` | `?DIR`, `?LGO` | CP/M launcher and directory-related helpers observed in CP/M bridge code |

Concrete usage seen in the corpus:

- `ECHO.SR` uses `?GETCAR`, `?DICAR`, `?WMOD`, `?RMOD`, `?BEEP`, and `?SPACE`
  as a tight echo-and-modem test loop.
- `DUMPGRA.SR` uses `?CREATE`, `?WRBYTE`, and `?CLOSE` to write a tiny control
  file after prompting through `?TEXTIM` and `?GETCAR`.
- `FPRINT.SR` uses `?RENAME`, `?OPEBLK`, `?RDBLK`, and `?CLOSE` in a
  foreground printer driver workflow.
- `SELINTER.SR` and `SELVAL.SR` use `?OPEN`, `?RDBYTE`, `?CREATE`, `?WRBYTE`,
  `?DELETE`, and `?CLOSE` for day-data persistence.
- `BASDEMO.SR` and `EXCPM.SR` use `?LGO` to hand off control to another `.SM`
  program.

Call style is usually one of these:

```calm
.W  ?TEXT,TEXT1
.W  ?TEXTIM
.ASCIZ /Hello/
.W  ?GETCAR,?DICAR,?RETURN
```

This strongly suggests that `.W` emits runtime call vectors plus inline
arguments in the format expected by the Smaky 6 environment.

## Observed mismatches versus later manual-derived assumptions

The later CALM manual itself is not currently checked into this repository.
The comparison below therefore uses the later-manual assumptions that had
already leaked into repo notes and examples, and contrasts them with the Smaky
6 source corpus.

| Later/manual-derived assumption | What the Smaky 6 corpus shows |
| --- | --- |
| CALM examples are mostly straightforward Z80 plus a few directives. | Real Smaky 6 sources use a distinct CALM layer heavily: `LOAD`, `COMP`, `JUMP,cond`, `DECJ`, `TEST A:4`, `.BW`, `.BBB`, `.INS`, and threaded `.W ?NAME` forms are normal, not exceptional. |
| `.REF SM6` is the central runtime binding pattern. | Real programs use multiple symbol spaces. `SM6` is common for console/runtime work, printer and storage code use `.REF FLO`, and CP/M bridge code uses `.REF CPM`. The shipped symbol tables confirm that `SM6` is a name subset of `FLO`; the only mismatch found so far is a non-service constant (`MINI`). |
| `.LOC` and a tiny `.W ?TEXTIM ... .W ?RTN` skeleton are enough to describe the language. | That skeleton is valid for a toy sample, but it hides the more typical structure of the corpus: command tables, inline threaded service calls, conditional assembly, and multi-file builds via `.INS`. |
| Later guidance can be imported as syntax unless it looks obviously incompatible. | For Smaky 6 work the rule must be stricter: if a form is not observed in the local `.SR` corpus, it is unconfirmed even if a later manual documents it. |
| Odd forms such as `JUMP.,EQ`, `CALL.`, `TEST X:n`, or `.BBB` are probably transcription noise. | These forms recur across independent sources and should be preserved verbatim until their exact assembler semantics are recovered. |
| Runtime services can be documented generically without distinguishing symbol origins. | The corpus still needs per-symbol-set evidence tracking. `SM6` services now have a code-mapped dispatch table from `SYS.SR`; `FLO` and `CPM` currently have usage evidence only, even if some of those symbol sets later prove to be layered rather than independent. |

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
- the full export tables for `FLO` and `CPM` symbol sets
- whether any callable `?NAME` services differ between the `SM6` and `FLO`
  tables, or whether the observed mismatch is limited to non-service constants

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
