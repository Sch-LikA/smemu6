# Smaky 6 SDCC Bring-up TODO

Planning document for bringing SDCC-compiled Z80 programs to Smaky 6 under SAMOS.

**Status:** Standalone SDCC scaffold exists and works (`sdcc/examples/hello_alpha`). Programs execute correctly but fail to return to CLI cleanly. Investigation ongoing. Not yet integrated into main CMake/CI.

## Quick Status

| Aspect | Status | Notes |
|--------|--------|-------|
| **Basic compilation** | ✅ Working | SDCC compiles C → Z80 assembly → .SM binary |
| **Program execution** | ✅ Working | Programs run, produce correct output |
| **Return to CLI** | ❌ Broken | Screen corruption, hanging, no prompt return |
| **System calls** | ✅ Partial | RST 20/0x06 string output works; others untested |
| **CMake integration** | ❓ Pending | Waiting for standalone approach to stabilize |

## Key Technical Facts

**CPU & Hardware:**
- Z80 at 2.5 MHz
- 64 KB RAM (Phantom model)
- Load base: 0x6000 (default for SDCC programs)
- Stack: 0xF000 (must be initialized before main)

**Memory Layout:**
- 0x0000–0x07FF: Phantom ROM / SYSMON
- 0x0800–0x22FF: SAMOS kernel
- 0x4000–0x44FF: Alpha screen (20 rows × 64 columns)
- 0x4500–0x45FF: Core workspace (OUTCAR, MAXMEM, BUFLIN)
- 0x6000–0xEFFF: User program area

**Documented Exit Sequence (for native .SM programs):**
```asm
LD A, 0x44              ; Magic value for CLI handoff
LD HL, 0x45C0           ; CLI line buffer (BUFLIN)
JP 0x56AE               ; Jump to SAMOS CLI reprompt sink
```

This exit sequence works for native CALM programs but produces screen corruption when used by SDCC programs.

## Current Scaffold Examples

Located in `sdcc/examples/`:

- **hello_alpha** — Minimal C program that writes to alpha screen
- **sm6peek** — Reads SAMOS workspace variables (OUTCAR, MAXMEM)
- **sm6emitz** — Calls RST 20/0x06 syscall to print text

All exhibit the same exit issue: correct execution, corrupted screen on return.

## Known Issue: SDCC Exit Corruption

**Symptom:**
- Program executes, produces correct output
- When returning from main, screen rows 15–19 become corrupted (repeated underscores)
- CLI prompt never reappears
- Emulator times out

**Investigation Result:**
- Pre-existing issue across multiple sessions
- Affects all SDCC programs identically
- Not caused by stack pointer value (tested 0xF000, 0xF800, 0xFFFF)
- Appears to be architectural incompatibility between SDCC program state and SAMOS exit handler

**Status:** Documented as known limitation. CALM assembly test program created to determine if issue is SDCC-specific or system-level.

## Symbol Tables (SM6.ST, FLO.ST)

Binary format parsed by `sdcc/dump_smaky6_st_symbols.c`:
- 8-byte fixed-width records
- Big-endian 16-bit address + 6-byte 7-bit ASCII symbol name
- Used by programs for system symbol resolution

Useful constants recovered:
- ALPHA = 0x4000
- BUFLIN = 0x45C0
- OUTCAR = 0x4550
- MAXMEM = 0x4560

## Next Steps (Phased Plan)

### Phase 1: Resolve Exit Issue
**Goal:** Programs must exit cleanly to CLI (not corrupt and hang)

- [ ] Test CALM assembly program (hello_calm) to determine if 0x56AE handler is fundamentally broken
- [ ] If CALM exits cleanly: debug why SDCC differs (stack, calling convention, register state)
- [ ] If CALM also fails: issue is system-level; requires deeper SAMOS analysis
- [ ] Document findings and create workaround or fix

### Phase 2: Expand System Call Support
**Goal:** Make more SAMOS functions available to SDCC programs

- [ ] Test RST 08, RST 10, RST 30, RST 38 dispatch vectors
- [ ] Document syscall table (currently only RST 20/0x06 tested)
- [ ] Create C wrappers for common system functions
- [ ] Add examples using file I/O, graphics mode, etc.

### Phase 3: Build Standard Library
**Goal:** Reusable C library for common operations

- [ ] Alpha screen output (already working)
- [ ] File operations (floppy read/write)
- [ ] Graphics modes (MODE G, MODE 2)
- [ ] Keyboard input
- [ ] Timer/RTC operations

### Phase 4: CMake Integration
**Goal:** Add SDCC build targets to main build system

- [ ] Create CMake SDCC compiler configuration
- [ ] Add targets for examples and library
- [ ] Integrate with CI pipeline
- [ ] Document for developers

## Important Design Constraints

1. **Keep SDCC examples standalone** — Self-contained directories with minimal CMake coupling
2. **Metadata preservation** — .SM files must include proper load/entry/flags in directory
3. **Symbol table consumption** — Generated headers from SM6.ST / FLO.ST for consistency
4. **No libc startup** — Minimal crt0; no full Newlib support needed yet
5. **Screen output first** — Alpha screen the primary I/O mechanism for now

## Build Artifacts

Standard SDCC → .SM pipeline:

```
C source (.c)
    ↓ (sdcc + sdasz80)
Assembly (.asm)
    ↓ (sdasz80)
Relocatable object (.rel)
    ↓ (sdldz80 linking crt0.rel + user.rel)
Intel HEX (.ihx)
    ↓ (tools/ihx_to_bin.py)
Binary (.SM)
    ↓ (tools/extract_samos_image.py wrapper / smaky6_samos.py extract-all --metadata --clear)
Floppy-ready package (with .meta.json sidecar)
```

## Testing Methodology

1. **Build locally:** `sdcc/build_smaky6_sdcc_example.sh example_name`
2. **Run in emulator:** `sdcc/run_smaky6_sdcc_example.sh example_name`
3. **Capture output:** `-scrdump -no-display-off` saves screen state to log
4. **Analyze:** Check for correct execution and clean exit

Example:
```bash
sdcc/build_smaky6_sdcc_example.sh hello_alpha
sdcc/run_smaky6_sdcc_example.sh hello_alpha
tail -50 tmp/sdcc-hello_alpha-build/out.txt
```

## Related Documentation

- [SAMOS Memory Layout](../docs/dev/HARDWARE.md)
- [CLI Implementation](../docs/dev/CLI_SY_analysis.md)
- [CALM Assembly Guide](../calm/README.md) — Native CALM development as comparison
- [Emulator Guide](../docs/EMULATOR_GUIDE_EN.md)

## For Developers Adding New Examples

1. Create directory: `sdcc/examples/my_program/`
2. Write C code: `sdcc/examples/my_program/main.c`
3. Optional config: `sdcc/examples/my_program/layout.conf` (to override load/entry)
4. Build: `sdcc/build_smaky6_sdcc_example.sh my_program`
5. Test: `sdcc/run_smaky6_sdcc_example.sh my_program`
6. Check output: `tail -20 tmp/sdcc-my_program-build/out.txt`

See `sdcc/examples/hello_alpha/` for minimal complete example.

## Blocking Issues

1. **SDCC programs don't exit cleanly** → Blocks practical development workflow
2. **Symbol table format only partially understood** → Limits system integration
3. **Loader contract not fully documented** → Affects reliability of load/entry

## Success Criteria for Phase 1

- [ ] CALM hello_calm program successfully exits to clean CLI (if possible)
- [ ] Root cause of SDCC corruption identified
- [ ] Workaround or fix implemented
- [ ] Updated crt0.s works for all example programs
- [ ] New examples can be created and executed without exit issues

---

## Investigation Archive

This section preserves detailed investigation notes and test results. For active development, use the sections above.

### Key Discoveries

- **Symbol tables:** SM6.ST (2008 bytes) and FLO.ST (2744 bytes) are binary formats containing machine constants
- **Loader contract:** Undocumented; .SM `load` and `entry` fields do not directly correspond to literal PC values
- **Exit path:** SDCC uses 0x56AE (same as native programs) but produces corruption; root cause unknown
- **Stack critical:** SP must be 0xF000 before calling main; other values tested also corrupt
- **Syscalls working:** RST 20/0x06 (string output) confirmed functional

### Test Coverage

| Program | Execution | Output | Exit | Rows Output |
|---------|-----------|--------|------|-------------|
| hello_alpha | ✅ | ✅ | ❌ Corrupt | ~341 |
| sm6emitz | ✅ | ✅ | ❌ Corrupt | ~70 |
| sm6peek | ✅ | ✅ | ❌ Corrupt | ~80 |
| hello_calm (pending) | ? | ? | ? | ? |

### Historical Investigation Paths

1. **Stack address investigation** — Tested SP=0xF000, 0xF800, 0xFFFF; all corrupt identically
2. **Exit sequence variants** — Tried direct JP, stacked return, call 0x56AE; all fail
3. **Relocation testing** — Moved programs to different load addresses; corruption follows
4. **Calling convention analysis** — Confirmed SDCC uses HL for pointers; matches SAMOS expectations
5. **CALM comparison** — Created hello_calm test program to isolate SDCC-specific issues

### Next Investigation Steps

1. Test CALM hello_calm in emulator using SMILE assembler
2. Compare native .SM exit behavior with SDCC corruption
3. Trace CPU state at exit for both cases
4. If CALM works: reverse-engineer what's different about SDCC program state
5. If CALM also fails: issue is SAMOS-level; need deeper analysis

### Related Issue Tracking

- See `TODO.md` (main project) for SDCC exit issue documentation
- See `calm/README.md` for CALM test program setup
- See `calm/examples/hello_calm/README.md` for CALM testing procedure
