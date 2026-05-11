#!/usr/bin/env python3
"""
tools/gen_smaky6_font.py — Smaky 6 Linux console font generator

Produces tools/Smaky6-8x16.psf: an 8×16 PSF2 font for Linux virtual console
and most terminal emulators (e.g. xterm -fn, konsole, st).
Optionally also produces tools/Smaky6-8x16.bdf and tools/Smaky6-8x16.pcf
for use with X11 (xterm, rxvt, etc.) and any font-manager that reads PCF.

Sources
-------
  • Smaky 6 TMS2716 chargen ROM  (src/chargen_rom.h, embedded array)
  • Terminus Uni2-16 PSF1         (/usr/share/consolefonts/Uni2-Terminus16.psf.gz)
    — fills every codepoint not directly covered by the Smaky ROM.

Glyph cell layout (8 px wide × 16 rows tall)
---------------------------------------------
  Rows  0– 3 : blank top margin
  Rows  4–15 : Smaky chargen rows 0–11  (VIDEO_CHAR_H = 12, bit-reversed LSB→MSB)

  This places the uppercase baseline at output row 11 and descenders (g j p q y)
  at rows 12–13, matching Terminus metrics closely enough for mixed rendering.

Unicode mapping — Smaky chargen index → primary Unicode codepoint
-----------------------------------------------------------------
  0x00–0x0E  decorative control glyphs  → U+E000–U+E00E (private-use area)
  0x0F       ü   → U+00FC               0x10  à   → U+00E0
  0x11       â   → U+00E2               0x12  é   → U+00E9
  0x13       è   → U+00E8               0x14  ë   → U+00EB
  0x15       ê   → U+00EA               0x16  ï   → U+00EF
  0x17       î   → U+00EE               0x18  ô   → U+00F4
  0x19       ù   → U+00F9               0x1A  û   → U+00FB
  0x1B       ä   → U+00E4               0x1C  ö   → U+00F6
  0x1D       ç   → U+00E7               0x1E  «   → U+00AB
  0x1F       »   → U+00BB
  0x20–0x7E  ASCII printable (direct)   0x7F  ▓   → U+2593
  Everything else: Terminus glyph

Usage
-----
  cd /path/to/smemu6
  python3 tools/gen_smaky6_font.py              # generate PSF2 (console)
  python3 tools/gen_smaky6_font.py --pcf         # generate PSF2 + BDF + PCF (X11)
  python3 tools/gen_smaky6_font.py --bdf         # generate PSF2 + BDF only
  python3 tools/gen_smaky6_font.py --preview     # ASCII-art preview of chargen glyphs
  python3 tools/gen_smaky6_font.py --output /tmp/test.psf

  # Activate in current Linux virtual console (requires root or CAP_SYS_TTY_CONFIG):
  sudo setfont tools/Smaky6-8x16.psf

  # Restore default font:
  sudo setfont

  # Install PCF for X11 (once) and use in xterm:
  cp tools/Smaky6-8x16.pcf ~/.fonts/
  fc-cache -f ~/.fonts/
  xterm -fa Smaky6 -fs 16 &
  # or with the legacy bitmap font name:
  xterm -fn "-misc-smaky6-medium-r-normal--16-120-100-100-c-80-iso10646-1"
"""

import argparse
import gzip
import re
import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
ROOT         = Path(__file__).resolve().parent.parent
CHARGEN_H    = ROOT / "src" / "chargen_rom.h"
TERMINUS_GZ  = Path("/usr/share/consolefonts/Uni2-Terminus16.psf.gz")
DEFAULT_OUT     = ROOT / "tools" / "Smaky6-8x16.psf"
DEFAULT_OUT_BDF = ROOT / "tools" / "Smaky6-8x16.bdf"
DEFAULT_OUT_PCF = ROOT / "tools" / "Smaky6-8x16.pcf"

# BDF baseline metrics (8×16 cell, baseline after 12 rows of ascent)
BDF_ASCENT   = 12   # rows above baseline (rows 0–11)
BDF_DESCENT  = 4    # rows below baseline (rows 12–15)

# ---------------------------------------------------------------------------
# Font geometry
CELL_H      = 16   # output glyph height in rows
CELL_W      = 8    # output glyph width in pixels (= 1 byte per row)
SMAKY_TOP   = 4    # first output row for Smaky chargen data
SMAKY_ROWS  = 12   # number of chargen rows used (== VIDEO_CHAR_H)

# ---------------------------------------------------------------------------
# Smaky chargen index → primary Unicode codepoint
SMAKY_UNICODE: dict[int, int] = {
    # Control-code decorative glyphs → private-use area (visible in most UAs)
    0x00: 0xE000,  # ··  two-dot marker
    0x01: 0xE001,  # □   empty-box glyph
    0x02: 0xE002,  # ⊡   box-with-dot glyph
    0x03: 0xE003,  # ✦   decorative cross/diamond
    0x04: 0xE004,  # ❬   left-chevron rendering
    0x05: 0xE005,  # ❭   right-chevron rendering
    0x06: 0xE006,  # ⍙   decorative A-shape (ACK glyph)
    0x07: 0xE007,  # BEL glyph (no standard Unicode rendering)
    0x08: 0xE008,  # BS  arrow glyph
    0x09: 0xE009,  # TAB arrow glyph
    0x0A: 0xE00A,  # LF  down-arrow glyph
    0x0B: 0xE00B,  # VT  left-arrow glyph
    0x0C: 0xE00C,  # FF  glyph
    0x0D: 0xE00D,  # CR  return-arrow glyph
    0x0E: 0xE00E,  # DC1 / "red ink" glyph
    # Swiss-French accented lowercase characters
    0x0F: 0x00FC,  # ü
    0x10: 0x00E0,  # à
    0x11: 0x00E2,  # â
    0x12: 0x00E9,  # é
    0x13: 0x00E8,  # è
    0x14: 0x00EB,  # ë
    0x15: 0x00EA,  # ê
    0x16: 0x00EF,  # ï
    0x17: 0x00EE,  # î
    0x18: 0x00F4,  # ô
    0x19: 0x00F9,  # ù
    0x1A: 0x00FB,  # û
    0x1B: 0x00E4,  # ä
    0x1C: 0x00F6,  # ö
    0x1D: 0x00E7,  # ç
    0x1E: 0x00AB,  # «  (MACRO key chargen code)
    0x1F: 0x00BB,  # »  (DEFINE key chargen code)
    # ASCII printable: 0x20–0x7E mapped 1:1
    **{i: i for i in range(0x20, 0x7F)},
    # Block glyph
    0x7F: 0x2593,  # ▓  DARK SHADE
}

# ---------------------------------------------------------------------------
def revbits(b: int) -> int:
    """Reverse bit order within a byte (Smaky LSB-first → PSF MSB-first)."""
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4)
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2)
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1)
    return b


def smaky_to_cell(raw16: bytes) -> bytes:
    """
    Convert one 16-byte Smaky chargen entry to an 8×16 MSB-first bitmap.

    Hardware uses VIDEO_CHAR_H=12 rows per cell.  We take the first 12 bytes
    of the chargen entry and place them at output rows SMAKY_TOP … SMAKY_TOP+11.
    """
    cell = bytearray(CELL_H)
    for r in range(min(SMAKY_ROWS, CELL_H - SMAKY_TOP)):
        cell[SMAKY_TOP + r] = revbits(raw16[r])
    return bytes(cell)


def parse_chargen_h(path: Path) -> list[bytes]:
    """Extract 128 × 16-byte chargen entries from chargen_rom.h.

    Each data line has the form:
        /* char 0xNN */ 0xAA,0xBB,...  (16 hex bytes)
    The regex must skip the comment-embedded index byte and take only the 16
    payload bytes that follow.
    """
    glyphs: list[bytes] = []
    for line in path.read_text().splitlines():
        # Match lines of the form:  /* char 0xNN */  0xAA, ...
        m = re.match(r"\s*/\*\s*char\s+0x[0-9A-Fa-f]{2}\s*\*/", line)
        if not m:
            continue
        data_part = line[m.end():]
        hex_bytes = re.findall(r"0x([0-9A-Fa-f]{2})", data_part)
        if len(hex_bytes) < 16:
            continue  # skip incomplete lines (e.g. continuation lines don't exist here)
        raw16 = bytes(int(h, 16) for h in hex_bytes[:16])
        glyphs.append(smaky_to_cell(raw16))
    if len(glyphs) != 128:
        sys.exit(f"[gen_smaky6_font] ERROR: expected 128 chargen entries, got {len(glyphs)}")
    return glyphs


def parse_psf1(data: bytes) -> tuple[list[bytes], dict[int, int]]:
    """
    Parse a PSF1 font (with Unicode table).

    Returns:
      glyphs    — list of raw glyph bitmaps (each CHARSIZE bytes)
      u2i       — dict mapping Unicode codepoint → glyph index
    """
    if data[:2] != b"\x36\x04":
        sys.exit("[gen_smaky6_font] ERROR: not a PSF1 file")
    mode, charsize = data[2], data[3]
    num = 512 if (mode & 1) else 256
    glyphs = [bytes(data[4 + i * charsize : 4 + (i + 1) * charsize]) for i in range(num)]

    u2i: dict[int, int] = {}
    if mode & 2:  # has Unicode table
        pos = 4 + num * charsize
        for idx in range(num):
            while pos + 1 <= len(data):
                cp = struct.unpack_from("<H", data, pos)[0]
                pos += 2
                if cp == 0xFFFF:
                    break
                if cp != 0xFFFE and cp not in u2i:
                    u2i[cp] = idx
    return glyphs, u2i


def write_psf2(path: Path, entries: list[tuple[bytes, list[int]]]) -> None:
    """
    Write a PSF2 font file.

    entries — list of (glyph_bitmap_bytes, [unicode_codepoints])
    Each glyph is CELL_H bytes (8 pixels wide = 1 byte/row × CELL_H rows).
    """
    n = len(entries)
    bpg = CELL_H  # bytes per glyph (8-wide font: 1 byte/row × CELL_H rows)
    bitmap = b"".join(g for g, _ in entries)

    utable = bytearray()
    for _, cps in entries:
        for cp in cps:
            utable += chr(cp).encode("utf-8")
        utable += b"\xff"  # PSF2 glyph-entry terminator

    # PSF2 header: 32 bytes, 8 × uint32 LE
    header = struct.pack(
        "<IIIIIIII",
        0x864AB572,  # magic
        0,           # version
        32,          # header size
        1,           # flags: HAS_UNICODE_TABLE
        n,           # number of glyphs
        bpg,         # bytes per glyph
        CELL_H,      # height in pixels
        CELL_W,      # width in pixels
    )
    path.write_bytes(header + bitmap + bytes(utable))
    print(f"[gen_smaky6_font] Wrote {n} glyphs to {path}")


def write_bdf(path: Path, entries: list[tuple[bytes, list[int]]]) -> None:
    """
    Write an X11 BDF (Bitmap Distribution Format) font file.

    Font name follows XLFD convention:
      -misc-smaky6-medium-r-normal--16-120-100-100-c-80-iso10646-1

    Baseline: ascent=BDF_ASCENT rows above baseline, descent=BDF_DESCENT below.
    BBX origin yoff = -BDF_DESCENT so glyph rows map to y = CELL_H-1..0
    with y=0 at baseline.
    """
    lines: list[str] = []
    xlfd = (
        "-misc-smaky6-medium-r-normal"
        f"--{CELL_H}-{CELL_H * 10}-100-100-c-{CELL_W * 10}-iso10646-1"
    )
    lines += [
        "STARTFONT 2.1",
        f"FONT {xlfd}",
        f"SIZE {CELL_H} 100 100",
        f"FONTBOUNDINGBOX {CELL_W} {CELL_H} 0 -{BDF_DESCENT}",
        "STARTPROPERTIES 11",
        f"FONT_ASCENT {BDF_ASCENT}",
        f"FONT_DESCENT {BDF_DESCENT}",
        f"DEFAULT_CHAR 32",
        f"POINT_SIZE {CELL_H * 10}",
        "RESOLUTION_X 100",
        "RESOLUTION_Y 100",
        f"AVERAGE_WIDTH {CELL_W * 10}",
        "CHARSET_REGISTRY \"ISO10646\"",
        "CHARSET_ENCODING \"1\"",
        f"FAMILY_NAME \"Smaky6\"",
        "WEIGHT_NAME \"Medium\"",
        "ENDPROPERTIES",
        f"CHARS {len(entries)}",
    ]

    for bitmap, cps in entries:
        cp = cps[0]
        char_name = f"U{cp:04X}"
        # BDF BITMAP rows: one hex byte per row (8 px wide), MSB=leftmost
        hex_rows = [f"{b:02X}" for b in bitmap]
        lines += [
            f"STARTCHAR {char_name}",
            f"ENCODING {cp}",
            f"SWIDTH 500 0",
            f"DWIDTH {CELL_W} 0",
            f"BBX {CELL_W} {CELL_H} 0 -{BDF_DESCENT}",
            "BITMAP",
            *hex_rows,
            "ENDCHAR",
        ]

    lines.append("ENDFONT")
    path.write_text("\n".join(lines) + "\n", encoding="latin-1")
    print(f"[gen_smaky6_font] Wrote {len(entries)} glyphs to {path}")


def glyph_preview(bitmap: bytes, label: str = "") -> str:
    """Return a multi-line ASCII-art string visualising an 8×16 glyph."""
    lines = [f"  {label}"]
    for r, byte in enumerate(bitmap):
        bar = "".join("#" if (byte >> (7 - c)) & 1 else "." for c in range(8))
        marker = " ←" if r == SMAKY_TOP else ("   " if r < SMAKY_TOP else "")
        lines.append(f"  {r:2d}│{bar}│{marker}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(
        description="Generate Smaky6-8x16.psf Linux console font",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--preview",
        action="store_true",
        help="Print ASCII-art preview of all 128 Smaky chargen glyphs and exit",
    )
    ap.add_argument(
        "--preview-index",
        type=lambda s: int(s, 0),
        default=None,
        metavar="N",
        help="Preview only glyph at chargen index N (e.g. 0x41 for 'A')",
    )
    ap.add_argument(
        "--output",
        default=str(DEFAULT_OUT),
        metavar="PATH",
        help=f"Output PSF2 path (default: {DEFAULT_OUT})",
    )
    ap.add_argument(
        "--bdf",
        action="store_true",
        help="Also write a BDF file alongside the PSF (for X11 / PCF conversion)",
    )
    ap.add_argument(
        "--pcf",
        action="store_true",
        help="Also write BDF and compile it to PCF with bdftopcf (requires bdftopcf)",
    )
    ap.add_argument(
        "--bdf-output",
        default=str(DEFAULT_OUT_BDF),
        metavar="PATH",
        help=f"BDF output path (default: {DEFAULT_OUT_BDF})",
    )
    ap.add_argument(
        "--pcf-output",
        default=str(DEFAULT_OUT_PCF),
        metavar="PATH",
        help=f"PCF output path (default: {DEFAULT_OUT_PCF})",
    )
    args = ap.parse_args()
    if args.pcf:
        args.bdf = True  # PCF requires BDF as intermediate

    # ── 1. Load Smaky chargen ──────────────────────────────────────────────
    smaky = parse_chargen_h(CHARGEN_H)
    print(f"[gen_smaky6_font] Loaded {len(smaky)} Smaky chargen glyphs from {CHARGEN_H.name}")

    if args.preview or args.preview_index is not None:
        indices = (
            [args.preview_index]
            if args.preview_index is not None
            else range(128)
        )
        for idx in indices:
            cp = SMAKY_UNICODE.get(idx)
            ch = chr(cp) if cp is not None and 0x20 <= cp <= 0xFFFF else ""
            label = (
                f"Smaky 0x{idx:02X}  →  U+{cp:04X}  {ch}"
                if cp is not None
                else f"Smaky 0x{idx:02X}  (unmapped)"
            )
            print(glyph_preview(smaky[idx], label))
            print()
        return

    # ── 2. Load Terminus ──────────────────────────────────────────────────
    if not TERMINUS_GZ.exists():
        sys.exit(
            f"[gen_smaky6_font] ERROR: Terminus PSF not found at {TERMINUS_GZ}\n"
            "  Install with: sudo apt-get install fonts-terminus  (or check path)"
        )
    term_data = gzip.decompress(TERMINUS_GZ.read_bytes())
    term_glyphs, term_u2i = parse_psf1(term_data)
    print(
        f"[gen_smaky6_font] Loaded {len(term_glyphs)} Terminus glyphs, "
        f"{len(term_u2i)} Unicode mappings"
    )

    # ── 3. Detect and report uppercase duplicates in Smaky a–z slots ──────
    # The Smaky keyboard was uppercase-only for regular input, but the chargen
    # has proper lowercase letters.  Verify this and report if any are dupes.
    raw_cg: dict[int, bytes] = {}
    for line in CHARGEN_H.read_text().splitlines():
        m = re.match(r"\s*/\*\s*char\s+0x([0-9A-Fa-f]{2})\s*\*/", line)
        if not m:
            continue
        idx = int(m.group(1), 16)
        data_part = line[m.end():]
        hb = re.findall(r"0x([0-9A-Fa-f]{2})", data_part)
        if len(hb) >= SMAKY_ROWS:
            raw_cg[idx] = bytes(int(h, 16) for h in hb[:SMAKY_ROWS])
    dupes = [
        lc
        for lc in range(0x61, 0x7B)
        if raw_cg.get(lc) == raw_cg.get(lc - 0x20)
    ]
    if dupes:
        dupe_chars = "".join(chr(c) for c in dupes)
        print(
            f"[gen_smaky6_font] WARNING: {len(dupes)} lowercase chargen slots are "
            f"uppercase duplicates ({dupe_chars!r}) — Terminus glyphs used for those"
        )
    else:
        print(
            "[gen_smaky6_font] All 26 Smaky lowercase slots have distinct glyphs"
        )
    skip_smaky = set(dupes)  # smaky indices to skip (will use Terminus instead)

    # ── 4. Merge: Terminus baseline, Smaky overrides ─────────────────────
    u2bm: dict[int, bytes] = {}

    # Terminus fills the baseline Unicode coverage (resize to CELL_H if needed)
    for cp, idx in term_u2i.items():
        g = term_glyphs[idx]
        if len(g) != CELL_H:
            padded = bytearray(CELL_H)
            padded[: min(len(g), CELL_H)] = g[:CELL_H]
            g = bytes(padded)
        u2bm[cp] = g

    # Smaky glyphs override Terminus (except uppercase-duplicate slots)
    overridden = 0
    for smaky_idx, cp in SMAKY_UNICODE.items():
        if smaky_idx in skip_smaky:
            continue
        u2bm[cp] = smaky[smaky_idx]
        overridden += 1
    print(f"[gen_smaky6_font] Smaky glyphs override {overridden} codepoints")

    # ── 5. Build sorted glyph list and write PSF2 ─────────────────────────
    sorted_cps = sorted(u2bm)
    entries = [(u2bm[cp], [cp]) for cp in sorted_cps]
    total = len(entries)
    print(f"[gen_smaky6_font] Total codepoints in output font: {total}")

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    write_psf2(out, entries)

    # ── 6. Optional BDF output ─────────────────────────────────────────────
    bdf_path = Path(args.bdf_output)
    pcf_path = Path(args.pcf_output)
    if args.bdf:
        bdf_path.parent.mkdir(parents=True, exist_ok=True)
        write_bdf(bdf_path, entries)

    # ── 7. Optional PCF compilation ───────────────────────────────────────
    if args.pcf:
        import shutil, subprocess
        if not shutil.which("bdftopcf"):
            print(
                "[gen_smaky6_font] WARNING: bdftopcf not found — skipping PCF.\n"
                "  Install with: sudo apt-get install xfonts-utils"
            )
        else:
            result = subprocess.run(
                ["bdftopcf", "-t", "-o", str(pcf_path), str(bdf_path)],
                capture_output=True, text=True,
            )
            if result.returncode == 0:
                print(f"[gen_smaky6_font] Compiled PCF to {pcf_path}")
            else:
                print(
                    f"[gen_smaky6_font] ERROR: bdftopcf failed:\n{result.stderr}"
                )

    # ── 8. Usage hint ──────────────────────────────────────────────────────────
    print()
    print("To activate in the current virtual console:")
    print(f"  sudo setfont {out}")
    print("To restore default font:")
    print("  sudo setfont")
    if args.pcf:
        print()
        print("To install the PCF for X11:")
        print(f"  cp {pcf_path} ~/.fonts/")
        print("  fc-cache -f ~/.fonts/")
        print("  xterm -fn '-misc-smaky6-medium-r-normal--16-120-100-100-c-80-iso10646-1'")
    elif args.bdf:
        print()
        print(f"BDF written to {bdf_path}")
        print(f"  Compile to PCF: bdftopcf -t -o {pcf_path} {bdf_path}")


if __name__ == "__main__":
    main()
