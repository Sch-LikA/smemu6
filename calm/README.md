# CALM Assembly Development for Smaky 6

This directory contains tools and examples for developing native CALM assembly programs for the Smaky 6 computer.

## What is CALM?

CALM is the **Computer Assembly Language Machine** — the native assembly language for Smaky 6, running on a Zilog Z80 processor. Unlike SDCC (which compiles C to Z80 assembly), CALM programs are written directly in Z80 assembly and assembled using the **SMILE** tool.

## Key Differences from SDCC

| Aspect | SDCC | CALM |
|--------|------|------|
| **Source Format** | C `.c` files | Assembly `.SR` files |
| **Compiler** | `sdcc` (external tool) | SMILE (runs in emulator) |
| **Exit Behavior** | Screen corruption, doesn't return to CLI | To be tested (why we created hello_calm) |
| **Workflow** | Offline: `sdcc` → assembly → binary | Online: write `.SR` → boot emulator → SMILE → execute |
| **Performance** | Optimized by compiler | Direct: every instruction counts |
| **Debugging** | C source symbols available | Assembly addresses only |

## SMILE Assembler

**SMILE** is the interactive assembler that runs **inside the emulated Smaky 6**. It:

- Reads `.SR` (CALM source) files from disk
- Assembles them to `.SM` (executable) binaries
- Produces `.LS` (listing) files with addresses
- Generates `.ST` (symbol table) files
- Is **interactive only** — no batch/headless mode

To use SMILE:
1. Prepare your `.SR` source file
2. Boot the emulator with a floppy containing the `.SR` file
3. At CLI: `> SMILE`
4. Follow SMILE's prompts to load and assemble

## Workflow Overview

### 1. Create Your Program

Write Z80 assembly in a `.SR` file. Example structure:

```asm
; hello.SR - Simple CALM program
            ORG 0x6000          ; Load address

ALPHA       EQU 0x4000          ; Screen base
BUFLIN      EQU 0x45C0          ; CLI buffer
CLI_EXIT    EQU 0x56AE          ; Exit handler

START:      LD SP, 0xF000       ; Initialize stack
            ; ... your program code ...
            
            ; Return to CLI
            LD A, 0x44          ; Magic value
            LD HL, BUFLIN       ; CLI state
            JP CLI_EXIT         ; Jump to exit handler
```

### 2. Prepare the Build

```bash
calm/build_smaky6_calm_example.sh your_program_name
```

This copies your `.SR` file to `tmp/calm-your_program_name-build/` for organization.

### 3. Stage on Floppy

Get your `.SR` file onto a Smaky 6 bootable floppy (e.g., Sys2-2.dsk). This step depends on your DSK handling tools.

### 4. Boot and Assemble

```bash
./smemu6 floppies/Sys2-2.dsk -scrdump -no-display-off > /tmp/test.log
```

Inside the emulator:
```
> SMILE
[Follow SMILE prompts]
> YOUR_PROGRAM_NAME    ; Run after assembly
```

### 5. Analyze Output

Check the screen dump log for:
- Correct output from your program
- Whether the CLI prompt reappears
- Any screen corruption

## Example Programs

### hello_calm

A minimal test program that writes "HELLO CALM!" to the screen and tests whether the native exit handler works correctly.

**Purpose:** Compare native CALM exit behavior against SDCC programs, which exhibit corruption.

**Run:**
```bash
calm/build_smaky6_calm_example.sh hello_calm
```

See [calm/examples/hello_calm/README.md](examples/hello_calm/README.md) for detailed testing instructions.

## Memory Layout Reference

| Address | Size | Purpose |
|---------|------|---------|
| 0x0000-0x07FF | 2 KB | Phantom ROM (SYSMON boot) |
| 0x0800-0x22FF | ~10 KB | SAMOS OS kernel (SYS.SY) |
| 0x4000-0x44FF | 1.25 KB | Alpha screen buffer (20 rows × 64 cols) |
| 0x4500-0x45FF | 256 B | Workspace (OUTCAR, LPSTAT, BUFLIN) |
| 0x5500-0x5FFF | ~3 KB | System program area (SMILE, CLI, etc.) |
| 0x6000-0xEFFF | ~40 KB | User program area (default) |
| 0xF000-0xF300 | 768 B | User stack (recommended: SP=0xF000) |

## Exit Sequence (Documented)

To return cleanly to the CLI from a `.SM` program:

```asm
LD A, 0x44          ; Magic register value for CLI handoff
LD HL, 0x45C0       ; Point to CLI line buffer
JP 0x56AE           ; Jump to SAMOS CLI reprompt sink
```

**Critical:** Use `JP` (jump), not `RET` (return). Returning would cause issues because the stack may not have a valid return address in mapped memory.

## Z80 Instruction Set

CALM uses standard Zilog Z80 assembly mnemonics. Common instructions:

```asm
LD A, 0x44          ; Load value into register A
LD (HL), A          ; Store A to memory address in HL
LD HL, 0x4000       ; Load 16-bit address into HL pair
LD SP, 0xF000       ; Load stack pointer
INC HL              ; Increment 16-bit register pair
ADD HL, DE          ; Add DE to HL
JP 0x56AE           ; Unconditional jump
CALL 0x0020         ; Call subroutine (push PC to stack)
RET                 ; Return (pop PC from stack)
JR HALT             ; Relative jump (short form)
```

For complete Z80 documentation, consult standard references.

## Creating a New Program

1. **Create directory:**
   ```bash
   mkdir -p calm/examples/my_program
   ```

2. **Write source file:**
   ```bash
   cat > calm/examples/my_program/my_program.SR << 'EOF'
   ; Your program here
   ORG 0x6000
   ; ... assembly code ...
   EOF
   ```

3. **Create README:**
   ```bash
   cat > calm/examples/my_program/README.md << 'EOF'
   # My Program
   
   Description of what it does.
   
   ## Testing
   
   calm/build_smaky6_calm_example.sh my_program
   [Boot emulator and assemble with SMILE]
   EOF
   ```

4. **Prepare build:**
   ```bash
   calm/build_smaky6_calm_example.sh my_program
   ```

5. **Follow the hello_calm workflow** for testing.

## Troubleshooting

### SMILE Can't Find File

- Verify `.SR` file is on the floppy (not in subdirectory)
- Check filename matches what you're typing (case-sensitive)
- SMILE may use uppercase only; try `> SMILE` then `MYFILE` not `myfile.sr`

### Program Doesn't Load

- Check `ORG 0x6000` matches the expected load address
- Verify no syntax errors in assembly
- Try assembling an existing example first

### Program Runs but Doesn't Exit

- This is the exact problem SDCC exhibits
- Check your exit sequence: `LD A,#0x44` + `LD HL,#0x45C0` + `JP 0x56AE`
- Try jumping to different addresses (e.g., `0x5602` for shorter setup)
- This is under investigation (see hello_calm testing)

### Screen Corruption on Exit

- Indicates control flow is entering wrong memory region
- Could be stack issue, wrong exit address, or architectural problem
- This is the main issue we're investigating with hello_calm

## Development Tips

1. **Keep programs small** — Smaller → faster to test → easier to debug
2. **Use `.SR` comments liberally** — Assembly is hard to read
3. **Test incrementally** — Add one feature at a time
4. **Capture output** — Always use `-scrdump -no-display-off` to save screen state
5. **Compare against hello_calm** — A working reference is invaluable

## Related Documentation

- [SAMOS Memory Layout](../docs/dev/HARDWARE.md)
- [CLI Implementation Analysis](../docs/dev/CLI_SY_analysis.md)
- [SDCC Exit Investigation](../TODO.md#experimental-sdcc-scaffold)
- [.SM File Format](../docs/dev/SDCC_TODO.md)

## Investigation Goals

The CALM examples serve a research purpose: **understanding why SDCC programs fail to exit cleanly while native programs (potentially) can.**

Current hypothesis: The SAMOS exit handler at 0x56AE expects a specific register/stack state that SDCC programs don't provide due to different calling conventions or program structure.

By testing native CALM programs, we can:
1. Verify if 0x56AE actually works for native programs
2. Compare CPU state at exit (native vs SDCC)
3. Determine if it's an architectural incompatibility
4. Find a workaround or proper fix for SDCC

Results will be documented in `README.md` files as they're discovered.
