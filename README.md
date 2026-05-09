# Smaky 6 Emulator

Emulator for the **Smaky 6**, a 1978 Swiss Z80-based personal computer
developed at EPFL (Lausanne) by Jean-Daniel Nicoud.

See [docs/dev/HARDWARE.md](docs/dev/HARDWARE.md) for full hardware documentation.

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
./smaky6emu                                        # Phantom ROM only (requires roms/samos_sys17.rom)
./smaky6emu -disk ../floppies/sys.img              # boot from floppy image on DX0
./smaky6emu -disk ../floppies/sys.img \
            -disk2 ../floppies/other.img           # DX0 + DX1
./smaky6emu -harddisk ../harddisks/SM6WIN0.DSK     # boot with Winchester drive 0
./smaky6emu -disk ../floppies/sys.img \
            -harddisk ../harddisks/SM6WIN0.DSK \
            -harddisk2 ../harddisks/SM6WIN1.DSK    # floppy + both Winchester drives
```

Floppy images go in `floppies/`. Hard-disk images go in `harddisks/`. ROM files go in `roms/`:
- `roms/samos_sys17.rom` — 2 KB Phantom bootstrap ROM (TMS2716 "SYS17")
- `roms/chargen.rom` — 2 KB character generator PROM (optional; synthetic fallback used)

## Controls

| Key                     | Function                                  |
|-------------------------|-------------------------------------------|
| F12                     | Toggle single-step mode                   |
| F11 / Pause             | BREAK — NMI (drops into monitor)          |
| Shift+F11 / Shift+Pause | SHIFT+BREAK — hard reset (reboots)        |
| Escape                  | Smaky ESC                                 |

## ROM Extraction

If you have the PDF documentation with assembly listings:

```bash
pdftotext -layout Smaky6-doc.pdf /tmp/smaky6.txt
python3 tools/smaky6_rom_extract.py /tmp/smaky6.txt \
    --start 0 --end 7777 --output roms/sysmon.rom --report sysmon_report.txt
```

## License

TBD — hardware design by Jean-Daniel Nicoud / EPFL / Epsitec.
