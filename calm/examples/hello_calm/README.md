# hello_calm: CALM Assembly Example

## Purpose

A minimal "Hello World" program written directly in CALM assembly that serves as a baseline reference for understanding how native programs can exit cleanly.

**Research Goal:** Test whether native SAMOS/CLI programs exit cleanly (and why SDCC programs don't).

- Writes "HELLO CALM!" to the alpha screen
- Implements the documented exit sequence for native .SM programs
- Provides a comparison point against SDCC-compiled programs that exhibit screen corruption on exit

## Files

- `hello.SR` — CALM source file (Z80 assembly syntax)
- This README

## Technical Details

**Memory Layout:**
- Program load address: 0x6000
- Alpha screen base: 0x4000 (row 0, 20 rows × 64 columns)
- Output location: Row 10 = 0x4000 + (10 × 64) = 0x4280
- CLI buffer: 0x45C0
- CLI reprompt sink: 0x56AE (target of documented exit sequence)

**Exit Sequence (from SAMOS documentation):**
```asm
LD A, 0x44          ; Magic value for CLI handoff
LD HL, 0x45C0       ; CLI line buffer address  
JP 0x56AE           ; Jump to CLI reprompt sink
```

This exact sequence is what SDCC programs attempt but which produces screen corruption.

## How to Test

### Step 1: Prepare Files

```bash
calm/build_smaky6_calm_example.sh hello_calm
```

This copies `hello.SR` to `tmp/calm-hello_calm-build/`.

### Step 2: Get Source on Floppy

SMILE (the assembler) must be run inside the emulator and reads from the floppy. You need to get `hello.SR` onto a bootable Sys2-2 floppy.

**Option A: Using dsk-tools (if available)**
```bash
# If you have a DSK manipulation tool:
your-dsk-tool add floppies/Sys2-2.dsk tmp/calm-hello_calm-build/hello.SR
```

**Option B: Manual extraction/repacking (more involved)**
```bash
# Extract the floppy contents
python3 tools/extract_samos_image.py floppies/Sys2-2.dsk /tmp/sys2-2-extract

# Copy source file
cp tmp/calm-hello_calm-build/hello.SR /tmp/sys2-2-extract/

# Repack (requires DSK creation tool — consult your system)
# Your tool here
```

**Option C: Load from second floppy (if emulator supports)**
```bash
# Create a simple DSK with just hello.SR (if possible on your system)
# Then boot with: ./smemu6 floppies/Sys2-2.dsk -floppy2 custom-floppy.dsk
```

### Step 3: Boot Emulator with Screen Capture

```bash
./smemu6 floppies/Sys2-2.dsk -scrdump -no-display-off > /tmp/calm-test.log 2>&1
```

This will:
- Boot to Sys2-2 CLI prompt
- Display screen output in the terminal
- Log everything to `/tmp/calm-test.log`

### Step 4: Inside the Emulator, Assemble with SMILE

At the CLI prompt:
```
> SMILE
```

SMILE is interactive. It will prompt you:
1. **Load which file?** → Type: `HELLO` (SMILE finds `HELLO.SR`)
2. **Source file is HELLO.SR. OK?** → Respond as prompted
3. **Assemble to?** → Default is usually fine (creates `HELLO.SM`)
4. **OK to start?** → Confirm

SMILE will assemble your program and display status messages.

### Step 5: Run the Program

After SMILE completes and returns to CLI:
```
> HELLO
```

The program should:
- Display "HELLO CALM!" somewhere on the screen
- Execute without error
- Return to the CLI prompt

**Important:** Watch carefully for screen corruption or hanging. This is what we're testing.

### Step 6: Analyze Output

If running with screen capture:
```bash
# See the final screen state
tail -50 /tmp/calm-test.log

# Look for:
# - Does "HELLO CALM!" appear?
# - Does the CLI prompt reappear?
# - Is there screen corruption (garbage characters, repeated patterns)?
# - Any error messages?
```

## Expected Results

### Success Case
```
> HELLO
HELLO CALM!
> 
```
Program executes, shows output, cleanly returns to CLI. No corruption.

### Failure Case (What SDCC Exhibits)
```
> HELLO
HELLO CALM!
[screen corruption: rows 15-19 filled with repeated underscores]
[no prompt appears]
[emulator times out]
```

## How to Debug If Tests Fail

1. **Check SMILE assembly output** — Did SMILE report errors?
2. **Verify .SR format** — Make sure the file is readable by SMILE
3. **Check file on floppy** — Confirm hello.SR actually exists on the boot floppy
4. **Use simpler test** — Try assembling an existing .SR file first to verify SMILE works
5. **Screen dump manually** — Take a manual screenshot of the emulator window if the log doesn't capture it

## Comparison with SDCC Version

To compare against SDCC:

```bash
# Build and test SDCC version
sdcc/build_smaky6_sdcc_example.sh hello_alpha
sdcc/run_smaky6_sdcc_example.sh hello_alpha

# Compare outputs
diff <(tail -20 tmp/calm-hello_calm-build/calm-test.log) \
     <(tail -20 tmp/sdcc-hello_alpha-build/out.txt)
```

## Investigation Questions

This test is designed to answer:

1. **Does the native exit handler work?** If CALM exits cleanly but SDCC doesn't, it's SDCC-specific.
2. **Is 0x56AE broken for all programs?** If both fail identically, it's a system-level issue.
3. **Is it a stack state problem?** Comparing stack setup might show differences.
4. **Is it an architectural incompatibility?** CALM vs SDCC calling conventions might differ.

## References

- SAMOS memory layout: `docs/dev/HARDWARE.md`
- CLI implementation: `docs/dev/CLI_SY_analysis.md`  
- SDCC exit investigation: `TODO.md` (search for "Experimental SDCC scaffold")
- Z80 instruction set: Standard Z80 assembly documentation

