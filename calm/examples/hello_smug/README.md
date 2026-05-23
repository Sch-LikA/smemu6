# hello_smug: Proper CALM Assembly Example

## Overview

A minimal "Hello World" program written in proper CALM assembly syntax. This program demonstrates:
- Correct CALM directives and syntax
- System routine calls for text output
- Proper return to SAMOS CLI
- The "right way" to write simple CALM programs

**Program Flow:**
1. Load address of ?TEXTIM routine
2. Provide the null-terminated string "Hello World"
3. Call ?TEXTIM to output the text
4. Return to CLI via ?RTN

## Technical Details

**CALM Syntax Elements Used:**

```calm
.TITLE HELLO              ; Program name/title
.REF SM6                  ; Reference SM6 symbol table
.LOC 43000               ; Load location (optional - SMILE uses default if omitted)
START:                   ; Entry point label
  .W ?TEXTIM             ; Word: address of text output routine
  .ASCIZ /Hello World/   ; Null-terminated ASCII string
  .W ?RTN                ; Word: address of return-to-CLI routine
.END START               ; End of program, entry point
```

**System Routines:**
- `?TEXTIM` — Text output routine (takes string pointer)
- `?RTN` — Return to SAMOS CLI (clean exit)

**Important Notes:**
- `.LOC` specifies the load address (like `ORG` in standard Z80 assembly)
- `.LOC` is **optional** — SMILE will choose a sensible default if omitted
- `.REF SM6` must be present to access system routine symbols

## Comparison with Manual Approach

**hello.sr (this file - CORRECT):**
- Clean, minimal
- Uses system routines directly
- Proper CALM syntax
- ~10 lines

**hello_calm/hello.SR (our first attempt - INCORRECT):**
- Manual memory addresses
- Attempted manual text output
- Over-complicated
- ~80 lines
- Does NOT follow proper CALM conventions

This version is the **recommended baseline** for all CALM testing.

## Testing Procedure

### Step 1: Prepare Build

```bash
calm/build_smaky6_calm_example.sh hello_smug
```

This copies `hello.sr` to `tmp/calm-hello_smug-build/`.

### Step 2: Get Source on Floppy

The `.SR` file must be on a bootable Sys2-2 floppy to be readable by SMILE.

**Option A: If you have DSK tools**
```bash
# Add hello.SR to Sys2-2.dsk
your-dsk-tool add floppies/Sys2-2.dsk tmp/calm-hello_smug-build/hello.SR
```

**Option B: Manual extraction/repacking**
```bash
python3 tools/extract_samos_image.py floppies/Sys2-2.dsk tmp/sys2-2
cp tmp/calm-hello_smug-build/hello.SR tmp/sys2-2/
# (Repack with your DSK tool)
```

### Step 3: Boot Emulator with Screen Capture

```bash
./smemu6 floppies/Sys2-2.dsk -scrdump -no-display-off > tmp/hello_smug.log 2>&1
```

### Step 4: Inside Emulator, Assemble with SMILE

At the CLI prompt:
```
> SMILE
```

SMILE prompts:
1. "Load which file?" → Type: `HELLO` (for HELLO.SR)
2. Confirm the file
3. Assemble to default (usually HELLO.SM)
4. Confirm to start assembly

### Step 5: Run the Program

After SMILE completes:
```
> HELLO
```

### Step 6: Expected Results

**Success:**
```
> HELLO
Hello World
> 
```
- Program executes
- "Hello World" appears on screen
- CLI prompt immediately returns
- **No corruption**
- **No hanging**

**This is the baseline we're testing for SDCC comparison.**

### Step 7: Check Output Log

```bash
tail -50 /tmp/hello_smug.log
```

Look for:
- "Hello World" in output
- Clean CLI prompt after execution
- No screen corruption
- No repeated characters or garbage

## Key Questions This Tests

1. **Does SMILE work?** Can we successfully assemble .SR files?
2. **Do system routines work?** Can ?TEXTIM output text correctly?
3. **Does ?RTN exit cleanly?** Does return-to-CLI work properly for native CALM?
4. **Baseline for SDCC:** If this works perfectly, we can compare SDCC's failures against it.

## Debugging Tips

- If SMILE can't find the file: Verify hello.SR is on the floppy
- If assembly fails: Check CALM syntax against SMILE documentation
- If "Hello World" doesn't appear: ?TEXTIM may not be working as expected
- If CLI doesn't return: ?RTN may be the wrong address or have wrong expectations

## CALM Programming Notes

This example shows the **correct pattern** for simple CALM programs:

1. Use `.REF SM6` to reference system symbols
2. Call system routines by their ?-prefixed names (e.g., ?TEXTIM, ?RTN)
3. Data (strings, constants) goes inline
4. Let SMILE handle memory management and linking
5. Trust the system routines for I/O and control

Do **NOT**:
- Hardcode memory addresses (0x4000, 0x45C0, etc.)
- Manually manipulate stack or registers
- Write complex initialization code
- Assume memory layout without using .REF

## Related Documentation

- CALM Syntax Reference — [consult SMILE documentation]
- System Routines — `.REF SM6` provides standard entry points
- SDCC Comparison — See [sdcc/SDCC_TODO.md](../../sdcc/SDCC_TODO.md)
- Native Exit Testing — Compare this clean exit with SDCC program behavior

## Next Steps

1. **Verify SMILE can assemble this** — Confirm no syntax errors
2. **Verify clean execution and exit** — This becomes our golden standard
3. **Compare with SDCC output** — Run SDCC programs and observe the difference
4. **Document findings** — Why SDCC fails if this succeeds
5. **Identify root cause** — SDCC-specific or system-level issue?

---

## Program Mechanics (For Reference)

The program structure is actually **data**, not traditional code:

```
Address  Data      Meaning
------   ----      -------
43000    ?TEXTIM   "Jump to text output routine"
         /...../   String "Hello World\0"
         ?RTN      "Jump to return routine"
```

When ?TEXTIM is called, it reads the next bytes as the string pointer and outputs text. Then execution continues to the next data word, which points to ?RTN (return).

This is a **threaded code** pattern common in SAMOS programs.
