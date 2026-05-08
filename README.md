# Smaky 6 Emulator

Emulator for the **Smaky 6**, a 1978 Swiss Z80-based personal computer
developed at EPFL (Lausanne) by Jean-Daniel Nicoud.

See [PLAN.md](PLAN.md) for full hardware documentation and implementation plan.

## Building

Dependencies: **CMake ≥ 3.16**, **SDL2**, **git** (for FetchContent).

```bash
git clone https://github.com/your-username/smaky6emu
cd smaky6emu
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Running

```bash
cd build
./smaky6emu                        # text monitor only (requires sysmon.rom)
./smaky6emu -disk ../disks/sys.img # boot from floppy image
```

Floppy images go in `floppies/`. ROM files go in `roms/`:
- `roms/sysmon.rom` — 4 KB SYSMON monitor
- `roms/samos.rom`  — 4 KB SAMOS floppy OS (optional)
- `roms/chargen.rom` — 2 KB character generator PROM (optional; synthetic fallback used)

## Controls

| Key         | Function                  |
|-------------|---------------------------|
| F12         | Toggle single-step mode   |
| F11 / Pause | NMI (BREAK → monitor)     |
| Escape      | Smaky ESC                 |

## ROM Extraction

If you have the PDF documentation with assembly listings:

```bash
pdftotext -layout Smaky6-doc.pdf /tmp/smaky6.txt
python3 tools/smaky6_rom_extract.py /tmp/smaky6.txt \
    --start 0 --end 7777 --output roms/sysmon.rom --report sysmon_report.txt
```

## License

TBD — hardware design by Jean-Daniel Nicoud / EPFL / Epsitec.
