#!/usr/bin/env python3
"""
smaky6_rom_extract.py — Extract ROM binary from Smaky 6 PDF assembly listing.

The PDF listing has two formats:
  SYSMON:  AAAAAA BB [BB [BB [BB]]]          (6-digit octal addr + bytes)
  PHANTOM: NN AAAAAA BB [BB [BB [BB]]]       (line number prefix)

All numbers are octal. Addresses are 6 digits, bytes are 1-3 digits (0-377).
OCR errors are corrected via a substitution table before parsing.

Usage:
  python3 smaky6_rom_extract.py smaky6.txt
  python3 smaky6_rom_extract.py smaky6.txt --start 0 --end 7777 --output sysmon.rom
  python3 smaky6_rom_extract.py smaky6.txt --start 10000 --end 17777 --output samos.rom
  python3 smaky6_rom_extract.py smaky6.txt --all --output full.rom
"""

import re
import sys
import argparse


# ---------------------------------------------------------------------------
# OCR correction table for common single-character substitutions.
# Applied character-by-character to candidate address/byte tokens.
# ---------------------------------------------------------------------------
OCR_CORRECTIONS = str.maketrans({
    'e': '0', 'E': '0', 'c': '0', 'C': '0',
    'o': '0', 'O': '0', 'G': '0', 'Q': '0',
    'D': '0', 'g': '0',
    's': '5', 'S': '5',
    'B': '8', 'b': '6',
    'l': '1', 'I': '1', 'i': '1',
    'Z': '2', 'z': '2',
    'A': '4', 'h': '4',
    'T': '7',
    "'": '',  # stray apostrophes at line start
    '(': '',  # stray parentheses
})

OCTAL_CHARS = set('01234567')


def ocr_fix(token):
    """Apply OCR corrections and return the corrected token, or None if not fixable."""
    fixed = token.translate(OCR_CORRECTIONS)
    if all(c in OCTAL_CHARS for c in fixed) and len(fixed) > 0:
        return fixed
    return None


def is_octal(s):
    return s and all(c in OCTAL_CHARS for c in s)


def try_parse_tokens(tokens):
    """
    Try to extract (address, [bytes]) from a list of string tokens.
    Returns (addr_int, [byte_ints], used_ocr) or None.
    Tries strict parsing first, then OCR-corrected.
    """
    for use_ocr in (False, True):
        if len(tokens) < 2:
            break
        t = tokens[0]
        if use_ocr:
            t = ocr_fix(t) or t
        if not is_octal(t):
            continue
        if len(t) != 6:         # addresses are always 6 digits
            continue
        addr = int(t, 8)
        if addr > 0xFFFF:       # must fit in Z80 address space
            continue

        byte_vals = []
        ok = True
        for raw in tokens[1:]:
            bt = raw
            if use_ocr:
                bt = ocr_fix(bt) or bt
            if not is_octal(bt):
                ok = False
                break
            if len(bt) > 3:
                ok = False
                break
            val = int(bt, 8)
            if val > 0xFF:
                ok = False
                break
            byte_vals.append(val)

        if ok and byte_vals:
            return addr, byte_vals, use_ocr

    return None


# Tokeniser: split on whitespace, strip leading punctuation noise
TOKEN_SPLIT = re.compile(r'\s+')
LEADING_NOISE = re.compile(r"^['\(\)\[\]]+")


def tokenise_line(line):
    """Split a line into clean tokens."""
    raw = TOKEN_SPLIT.split(line.strip())
    tokens = []
    for t in raw:
        t = LEADING_NOISE.sub('', t)   # strip leading punctuation
        t = t.strip("',;:.()[]")       # strip surrounding punctuation
        if t:
            tokens.append(t)
    return tokens


def is_decimal_int(s):
    """True if s looks like a decimal line number (all digits 0-9, reasonable range)."""
    return s.isdigit() and int(s) < 10000


def parse_listing(filename, verbose=False):
    """
    Parse the listing file. Returns:
      memory   : dict {address: byte}
      events   : list of (lineno, kind, message)
    """
    memory = {}
    events = []
    prev_addr = None
    strict_count = 0
    ocr_count = 0
    skip_count = 0

    with open(filename, 'r', encoding='utf-8', errors='replace') as f:
        for lineno, line in enumerate(f, 1):
            tokens = tokenise_line(line)
            if not tokens:
                continue

            # Handle optional leading decimal line number (PHANTOM format)
            work = tokens
            if is_decimal_int(tokens[0]) and len(tokens) >= 3:
                work = tokens[1:]

            result = try_parse_tokens(work)
            if result is None:
                skip_count += 1
                continue

            addr, byte_vals, used_ocr = result

            if used_ocr:
                ocr_count += 1
            else:
                strict_count += 1

            # Check for address continuity
            if prev_addr is not None:
                gap = addr - (prev_addr + 1)
                if addr < prev_addr:
                    events.append((lineno, 'BACK',
                        f'Address went backwards: {prev_addr:06o} -> {addr:06o}'))
                elif gap > 16:
                    events.append((lineno, 'GAP',
                        f'Gap {prev_addr+1:06o}–{addr-1:06o} ({gap} bytes missing)'))

            # Store bytes, detect conflicts
            for i, bval in enumerate(byte_vals):
                a = addr + i
                if a in memory and memory[a] != bval:
                    events.append((lineno, 'CONFLICT',
                        f'Addr {a:06o}: had {memory[a]:03o}, now {bval:03o} (OCR={used_ocr})'))
                memory[a] = bval

            prev_addr = addr + len(byte_vals) - 1

    events.append((0, 'STAT',
        f'Parsed: {strict_count} strict, {ocr_count} OCR-corrected, {skip_count} skipped lines'))
    return memory, events


def build_binary(memory, start, end, fill=0xFF):
    """Build a bytearray for address range [start, end] inclusive."""
    size = end - start + 1
    rom = bytearray([fill] * size)
    covered = 0
    for addr, val in memory.items():
        if start <= addr <= end:
            rom[addr - start] = val
            covered += 1
    return rom, covered


def find_gaps(memory, start, end):
    """Return list of (gap_start, gap_end) for uncovered addresses in range."""
    gaps = []
    in_gap = False
    gap_start = None
    for addr in range(start, end + 1):
        if addr not in memory:
            if not in_gap:
                in_gap = True
                gap_start = addr
        else:
            if in_gap:
                gaps.append((gap_start, addr - 1))
                in_gap = False
    if in_gap:
        gaps.append((gap_start, end))
    return gaps


def hex_dump(data, start_addr, width=16):
    lines = []
    for i in range(0, len(data), width):
        chunk = data[i:i+width]
        addr = start_addr + i
        hex_part = ' '.join(f'{b:02X}' for b in chunk)
        asc_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        lines.append(f'{addr:04X}  {hex_part:<{width*3}}  {asc_part}')
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description='Extract Smaky 6 ROM from PDF listing text')
    ap.add_argument('input', help='pdftotext output file')
    ap.add_argument('--start',  default='0',    help='Start address octal (default: 0)')
    ap.add_argument('--end',    default='7777',  help='End address octal (default: 7777 = SYSMON)')
    ap.add_argument('--output', default=None,    help='Output binary file')
    ap.add_argument('--all',    action='store_true', help='Dump all found addresses')
    ap.add_argument('--fill',   default='377',   help='Fill byte for gaps, octal (default: 377=0xFF)')
    ap.add_argument('--hexdump',action='store_true', help='Print hex dump to stdout')
    ap.add_argument('--report', default=None,    help='Write report to file (default: stdout)')
    args = ap.parse_args()

    start = int(args.start, 8)
    end   = int(args.end,   8)
    fill  = int(args.fill,  8)

    # Default output filenames
    if args.output is None:
        if start == 0 and end == 0o7777:
            args.output = 'sysmon.rom'
        elif start == 0o10000 and end == 0o17777:
            args.output = 'samos.rom'
        else:
            args.output = f'smaky6_{start:06o}_{end:06o}.rom'

    print(f'Parsing {args.input} ...', file=sys.stderr)
    memory, events = parse_listing(args.input)
    print(f'Total unique addresses found: {len(memory)}', file=sys.stderr)

    # Determine output range
    if args.all and memory:
        start = min(memory.keys())
        end   = max(memory.keys())

    rom, covered = build_binary(memory, start, end, fill)
    size = end - start + 1
    gaps = find_gaps(memory, start, end)
    pct  = 100.0 * covered / size if size else 0.0

    # Build report
    R = []
    R.append('=== Smaky 6 ROM Extraction Report ===')
    R.append(f'Source : {args.input}')
    R.append(f'Range  : {start:06o}–{end:06o}  (0x{start:04X}–0x{end:04X})')
    R.append(f'Size   : {size} bytes ({size//1024} KB)')
    R.append(f'Covered: {covered}/{size} bytes ({pct:.1f}%)')
    R.append(f'Output : {args.output}')
    R.append('')

    # Parse stats
    stat = next((e for e in events if e[1] == 'STAT'), None)
    if stat:
        R.append(f'Parse  : {stat[2]}')
        R.append('')

    # Gaps
    if not gaps:
        R.append('Coverage: COMPLETE — no gaps in range.')
    else:
        R.append(f'Gaps ({len(gaps)} regions):')
        for gs, ge in gaps:
            R.append(f'  {gs:06o}–{ge:06o}  (0x{gs:04X}–0x{ge:04X}, {ge-gs+1} bytes)')

    R.append('')

    # Conflicts (likely OCR errors)
    conflicts = [e for e in events if e[1] == 'CONFLICT']
    if conflicts:
        R.append(f'Conflicts (same address, different bytes — OCR errors): {len(conflicts)}')
        for lineno, _, msg in conflicts[:40]:
            R.append(f'  Line {lineno:6d}: {msg}')
        if len(conflicts) > 40:
            R.append(f'  ... and {len(conflicts)-40} more')
        R.append('')

    # Address jumps
    backs = [e for e in events if e[1] == 'BACK']
    if backs:
        R.append(f'Backwards address jumps (OCR address corruption): {len(backs)}')
        for lineno, _, msg in backs[:20]:
            R.append(f'  Line {lineno:6d}: {msg}')
        R.append('')

    report_text = '\n'.join(R)

    if args.report:
        with open(args.report, 'w') as f:
            f.write(report_text + '\n')
    else:
        print(report_text)

    # Write ROM binary
    with open(args.output, 'wb') as f:
        f.write(rom)
    print(f'Written: {args.output} ({len(rom)} bytes)', file=sys.stderr)

    # Optional hex dump
    if args.hexdump:
        print(hex_dump(rom, start))


if __name__ == '__main__':
    main()