#!/usr/bin/env python3
"""
smaky6_samos.py – Smaky 6 disk image tool

Reads flat sector images from Micropolis hard-sectored 5.25" floppies or
Smaky 6 Winchester (hard disk) images.  Supported image sizes:

  40 tracks × 16 sectors × 256 bytes =    163,840 bytes  (floppy, 40-track)
  77 tracks × 16 sectors × 256 bytes =    315,392 bytes  (floppy, 77-track)
  16,777,216 bytes (16 MB)              = 65,536 sectors  (Winchester HD)

Directory format (reverse-engineered from disk image analysis):

  The first 3 sectors of track 0 (768 bytes) hold up to 32 directory entries
  of 24 bytes each.  An entry with first byte 0x00 or 0xFF is empty/deleted.

  Entry layout:
    [0:8]   name        ASCII, space-padded
    [8:10]  type        2-char code  (SY system, SM SMILE prog, ST standalone,
                                      SR system resource, LS SMILE library,
                                      FH file header, BS binary, IM image,
                                      KS FORTH kernel, DR directory,
                                      HP help text, RF resource file)

  DR entries are containers: their first 3 sectors hold a sub-directory
  of the same 24-byte format.  Sector addresses inside the sub-directory
  are relative to the DR entry's own start sector (not the disk start).
    [10:12] start_sec   uint16 LE – first data sector (0-based absolute)
    [12:14] end_sec     uint16 LE – first sector after the file (exclusive)
                         → size_sectors = end_sec - start_sec
    [14:16] flags       unknown (usually 0x0000)
    [16:18] last_bytes  uint16 LE – bytes used in last sector
                         → exact_size = (size_sectors-1)*256 + last_bytes
                                        if last_bytes > 0
                                        else size_sectors * 256
    [18:19] load_hi/lo  load address, high byte then low byte
                         (e.g. 0x55,0x08 → load at 0x5508)
    [20:21] entry_hi/lo entry point, same encoding
    [22]    month       BCD (0x12 = December)
    [23]    year        BCD (0x82 = 1982); 0x00 = no date

  Note: load/entry addresses are reliable for type SM (SMILE programs).
  Other types may use those bytes for different purposes.

Usage:
  smaky6_samos.py <image> list
  smaky6_samos.py <image> info
  smaky6_samos.py <image> extract <name> [<outfile>]   (--index N if duplicates)
  smaky6_samos.py <image> extract-all [<outdir>]
  smaky6_samos.py <image> hexdump <sector> [<count>]
  smaky6_samos.py <image> cat <name>                   (text dump, --index N)
  smaky6_samos.py <image> image <name> [<outpng>]      (render IM bitmap, needs Pillow)
  smaky6_samos.py <image> add <infile> <name> <type>   (--load 0xADDR --entry 0xADDR)
  smaky6_samos.py <image> delete <name>                (--index N if duplicates)
  smaky6_samos.py <image> compact                      (repack, reclaim deleted gaps)
  smaky6_samos.py create <image>                       (--tracks 40|77, blank formatted image)
  smaky6_samos.py create <image> --winchester           (blank 16 MB Winchester image)
"""

import argparse
import os
import struct
import sys

# ── Constants ─────────────────────────────────────────────────────────────────
SECTOR_SIZE    = 256
SECTORS_TRACK  = 16
DIR_SECTORS    = 3
MAX_ENTRIES    = (DIR_SECTORS * SECTOR_SIZE) // 24  # 32

# Winchester hard disk image size (16 MB flat dump, 65 536 sectors of 256 bytes).
# Represented as 4096 virtual "tracks" of 16 sectors each so that all existing
# code that uses  total_sec = tracks * SECTORS_TRACK  continues to work.
WINCHESTER_SIZE   = 16 * 1024 * 1024
WINCHESTER_TRACKS = WINCHESTER_SIZE // (SECTORS_TRACK * SECTOR_SIZE)  # 4096

GEOMETRIES = {
    40 * SECTORS_TRACK * SECTOR_SIZE: 40,
    77 * SECTORS_TRACK * SECTOR_SIZE: 77,
    WINCHESTER_SIZE:                  WINCHESTER_TRACKS,
}

FILE_TYPES = {
    'SY': 'System',
    'SM': 'SMILE program',
    'ST': 'Standalone',
    'SR': 'System resource',
    'LS': 'SMILE library',
    'FH': 'File/FORTH header',
    'BS': 'Binary/data',
    'IM': 'Image',
    'KS': 'FORTH kernel',
    'DR': 'Directory',
    'HP': 'Help text',
    'RF': 'Resource file',
}

# Container types: entries whose first DIR_SECTORS sectors hold a sub-directory.
# Sector addresses inside the sub-directory are relative to the container's start.
# HP entries (help pages) are plain text files, NOT sub-directories.
CONTAINER_TYPES = frozenset({'DR'})

MONTH_NAMES = ['', 'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
               'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']


# ── Low-level helpers ─────────────────────────────────────────────────────────

def sector_offset(sec: int) -> int:
    return sec * SECTOR_SIZE


def bcd_byte(b: int) -> int:
    """Decode a BCD byte to decimal (e.g. 0x82 → 82)."""
    return (b >> 4) * 10 + (b & 0x0F)


def load_image(path: str):
    with open(path, 'rb') as f:
        data = bytearray(f.read())
    size = len(data)
    tracks = GEOMETRIES.get(size)
    if tracks is None:
        known = ', '.join(
            f'{sz // 1024} KB' if sz < 1024 * 1024 else f'{sz // (1024*1024)} MB'
            for sz in sorted(GEOMETRIES)
        )
        print(f"WARNING: {os.path.basename(path)} is {size} bytes "
              f"(known sizes: {known}). Guessing geometry.",
              file=sys.stderr)
        tracks = size // (SECTORS_TRACK * SECTOR_SIZE)
    return data, tracks


def is_winchester(data: bytearray) -> bool:
    """Return True when *data* is a 16 MB Winchester hard-disk image."""
    return len(data) == WINCHESTER_SIZE


# ── Directory parsing ─────────────────────────────────────────────────────────

def parse_dir(data: bytearray) -> list:
    entries = []
    raw = data[:DIR_SECTORS * SECTOR_SIZE]
    for i in range(MAX_ENTRIES):
        e = raw[i * 24: i * 24 + 24]
        if e[0] in (0x00, 0xFF):
            continue

        name = bytes(b & 0x7F for b in e[0:8]).decode('ascii', errors='replace').rstrip()
        ftype = bytes(b & 0x7F for b in e[8:10]).decode('ascii', errors='replace')

        start  = struct.unpack_from('<H', e, 10)[0]
        end    = struct.unpack_from('<H', e, 12)[0]
        flags  = struct.unpack_from('<H', e, 14)[0]
        last_b = struct.unpack_from('<H', e, 16)[0]

        size_sec  = end - start
        exact     = (size_sec - 1) * SECTOR_SIZE + last_b if last_b else size_sec * SECTOR_SIZE

        load  = (e[18] << 8) | e[19]
        entry = (e[20] << 8) | e[21]

        month_bcd = e[22]
        year_bcd  = e[23]
        # 0x00 and 0xFF both mean "no date" (0xFF used by system files)
        month = bcd_byte(month_bcd) if month_bcd and month_bcd != 0xFF else 0
        year  = bcd_byte(year_bcd)  if year_bcd  and year_bcd  != 0xFF else 0

        entries.append({
            'idx':   i,
            '_offset': i * 24,   # byte offset of this entry in the raw directory
            'name':  name,
            'type':  ftype,
            'start': start,
            'end':   end,
            'size_sectors': size_sec,
            'size_exact':   exact,
            'flags': flags,
            'last_bytes': last_b,
            'load':  load,
            'entry': entry,
            'month': month,
            'year':  year,
            'raw':   bytes(e),
        })
    return entries


def parse_subdir(data: bytearray, parent: dict) -> list:
    """Parse the sub-directory embedded in a DR or HP container entry.

    The first DIR_SECTORS sectors of the container hold up to MAX_ENTRIES
    directory entries with the same 24-byte layout as the root directory.
    Sector addresses stored in each sub-entry are *relative* to
    parent['start']; this function converts them to absolute sector numbers
    before returning, so callers can use _read_entry() transparently.

    The '_offset' field of each returned entry is the absolute byte offset
    of the 24-byte entry within the disk image (for potential write-back).
    """
    base = parent['start']
    raw  = data[base * SECTOR_SIZE : (base + DIR_SECTORS) * SECTOR_SIZE]
    entries = []
    for i in range(MAX_ENTRIES):
        e = raw[i * 24 : i * 24 + 24]
        if e[0] in (0x00, 0xFF):
            continue

        name  = bytes(b & 0x7F for b in e[0:8]).decode('ascii', errors='replace').rstrip()
        ftype = bytes(b & 0x7F for b in e[8:10]).decode('ascii', errors='replace')

        rel_start = struct.unpack_from('<H', e, 10)[0]
        rel_end   = struct.unpack_from('<H', e, 12)[0]
        flags     = struct.unpack_from('<H', e, 14)[0]
        last_b    = struct.unpack_from('<H', e, 16)[0]

        abs_start = base + rel_start
        abs_end   = base + rel_end
        size_sec  = rel_end - rel_start
        exact     = (size_sec - 1) * SECTOR_SIZE + last_b if last_b else size_sec * SECTOR_SIZE

        load  = (e[18] << 8) | e[19]
        entry = (e[20] << 8) | e[21]

        month_bcd = e[22]
        year_bcd  = e[23]
        month = bcd_byte(month_bcd) if month_bcd and month_bcd != 0xFF else 0
        year  = bcd_byte(year_bcd)  if year_bcd  and year_bcd  != 0xFF else 0

        entries.append({
            'idx':          i,
            '_offset':      base * SECTOR_SIZE + i * 24,  # absolute byte offset in image
            '_parent':      parent,
            'name':         name,
            'type':         ftype,
            'start':        abs_start,
            'end':          abs_end,
            'size_sectors': size_sec,
            'size_exact':   exact,
            'flags':        flags,
            'last_bytes':   last_b,
            'load':         load,
            'entry':        entry,
            'month':        month,
            'year':         year,
            'raw':          bytes(e),
        })
    return entries


def find_entries(entries: list, name: str) -> list:
    name = name.upper().strip()
    return [e for e in entries if e['name'].upper() == name]


def date_str(e: dict) -> str:
    m, y = e['month'], e['year']
    if not m and not y:
        return ''
    mon = MONTH_NAMES[m] if 1 <= m <= 12 else f'?{m}'
    return f'{mon} 19{y:02d}'


def type_desc(t: str) -> str:
    return FILE_TYPES.get(t.strip(), '?')


# ── Commands ──────────────────────────────────────────────────────────────────

def cmd_list(args, data: bytearray, tracks: int):
    entries = parse_dir(data)
    total_sec = tracks * SECTORS_TRACK

    if is_winchester(data):
        print(f"Smaky 6 Winchester: {total_sec} sectors, "
              f"{total_sec * SECTOR_SIZE // 1024} KB total")
    else:
        print(f"Smaky 6 floppy: {tracks} tracks, {total_sec} sectors, "
              f"{total_sec * SECTOR_SIZE // 1024} KB total")
    print()
    print(f"{'#':>2}  {'Name':<10} {'Tp':<2}  {'Description':<20} "
          f"{'Start':>5} {'End':>5} {'Sectors':>7} {'Bytes':>8}  "
          f"{'Load':>6} {'Entry':>6}  Date")
    print('─' * 98)

    def _print_entry(e, prefix=''):
        load_s  = f"0x{e['load']:04X}"  if e['load']  else '      '
        entry_s = f"0x{e['entry']:04X}" if e['entry'] else '      '
        idx_s   = f"{e['idx']:>2}" if not prefix else '  '
        name_col = (prefix + e['name'])[:10].ljust(10)
        print(f"{idx_s}  {name_col} {e['type']:<2}  "
              f"{type_desc(e['type']):<20} "
              f"{e['start']:>5} {e['end']:>5} {e['size_sectors']:>7} "
              f"{e['size_exact']:>8}  "
              f"{load_s}  {entry_s}  {date_str(e)}")
        if e['type'].strip() in CONTAINER_TYPES:
            sub = parse_subdir(data, e)
            for se in sub:
                _print_entry(se, prefix='> ')

    used_end = DIR_SECTORS
    for e in entries:
        # Show gaps (deleted/free space between files)
        if e['start'] > used_end:
            gap = e['start'] - used_end
            print(f"    {'(free/deleted)':<10}                              "
                  f"{used_end:>5} {e['start']:>5} {gap:>7} {gap*SECTOR_SIZE:>8}")
        used_end = e['end']
        _print_entry(e)

    # Trailing free space
    if entries and entries[-1]['end'] < total_sec:
        free = total_sec - entries[-1]['end']
        print(f"    {'(free)':<10}                              "
              f"{entries[-1]['end']:>5} {total_sec:>5} {free:>7} {free*SECTOR_SIZE:>8}")

    print('─' * 98)
    used_kb  = (entries[-1]['end'] if entries else DIR_SECTORS) * SECTOR_SIZE // 1024
    free_kb  = (total_sec - (entries[-1]['end'] if entries else DIR_SECTORS)) * SECTOR_SIZE // 1024
    print(f"  {len(entries)} top-level entries    Used: {used_kb} KB    Free: {free_kb} KB")


def cmd_info(args, data: bytearray, tracks: int):
    entries = parse_dir(data)
    total_sec = tracks * SECTORS_TRACK

    print(f"Image:    {args.image}")
    print(f"Size:     {len(data):,} bytes")
    if is_winchester(data):
        print(f"Type:     Winchester hard disk  (16 MB flat sector dump)")
        print(f"Geometry: {total_sec} sectors × {SECTOR_SIZE} bytes  "
              f"(virtual: {tracks} tracks × {SECTORS_TRACK} sectors)")
    else:
        print(f"Type:     Micropolis floppy  ({tracks}-track)")
        print(f"Geometry: {tracks} tracks × {SECTORS_TRACK} sectors × {SECTOR_SIZE} bytes")
    print(f"Capacity: {total_sec * SECTOR_SIZE // 1024} KB  ({total_sec} sectors)")
    print(f"Files:    {len(entries)}")
    if entries:
        used = entries[-1]['end']
        print(f"Used:     {used * SECTOR_SIZE // 1024} KB  ({used} sectors)")
        print(f"Free:     {(total_sec - used) * SECTOR_SIZE // 1024} KB  ({total_sec - used} sectors)")

    print()
    print("Directory field layout (24 bytes/entry):")
    print("  [ 0: 8] name        space-padded ASCII")
    print("  [ 8:10] type        2-char code")
    print("  [10:12] start_sec   uint16 LE  first data sector")
    print("  [12:14] end_sec     uint16 LE  exclusive end (size = end-start sectors)")
    print("  [14:16] flags       uint16 LE  (purpose unknown)")
    print("  [16:18] last_bytes  uint16 LE  bytes used in last sector (0 = full)")
    print("  [18:19] load_addr   high, low  load address (reliable for type SM)")
    print("  [20:21] entry_addr  high, low  entry point")
    print("  [22]    month       BCD (0x07 = July)")
    print("  [23]    year        BCD (0x82 = 1982); 0x00 = no date")

    print()
    print("Raw directory entries:")
    raw = data[:DIR_SECTORS * SECTOR_SIZE]
    for i in range(MAX_ENTRIES):
        e = raw[i * 24: i * 24 + 24]
        if e[0] in (0x00, 0xFF):
            continue
        name = bytes(b & 0x7F for b in e[0:8]).decode('ascii', errors='replace').rstrip()
        print(f"  [{i:02d}] {name:<10}  {e.hex()}")


def _select_entry(entries_found, name, index_arg):
    """Resolve a (possibly duplicate) name to a single entry."""
    if not entries_found:
        return None
    if len(entries_found) == 1:
        return entries_found[0]
    if index_arg is not None:
        if 0 <= index_arg < len(entries_found):
            return entries_found[index_arg]
        print(f"Index {index_arg} out of range (0-{len(entries_found)-1})", file=sys.stderr)
        sys.exit(1)
    # Prompt
    print(f"Multiple files named '{name}':", file=sys.stderr)
    for i, e in enumerate(entries_found):
        print(f"  [{i}] type={e['type']}  start={e['start']}  "
              f"size={e['size_exact']} B  {date_str(e)}", file=sys.stderr)
    try:
        idx = int(input("Choose index: "))
    except (EOFError, ValueError):
        sys.exit(1)
    return entries_found[idx]


def _read_entry(data: bytearray, e: dict) -> bytes:
    off = sector_offset(e['start'])
    return bytes(data[off: off + e['size_exact']])


def cmd_extract(args, data: bytearray, tracks: int):
    entries = parse_dir(data)
    found = find_entries(entries, args.name)
    if not found:
        avail = ', '.join(f"{e['name']}({e['type']})" for e in entries)
        print(f"File '{args.name}' not found.\nAvailable: {avail}", file=sys.stderr)
        sys.exit(1)

    e = _select_entry(found, args.name, getattr(args, 'index', None))
    raw = _read_entry(data, e)

    if args.output and args.output != '-':
        with open(args.output, 'wb') as f:
            f.write(raw)
        print(f"Extracted {e['name']} ({e['type']}) → {args.output}  "
              f"({len(raw)} bytes)")
    else:
        sys.stdout.buffer.write(raw)


def cmd_extract_all(args, data: bytearray, tracks: int):
    entries = parse_dir(data)
    outdir = getattr(args, 'outdir', 'extracted')
    os.makedirs(outdir, exist_ok=True)

    total = [0]

    def _extract_entries(ents, dest):
        seen: dict = {}
        for e in ents:
            base = e['name'].replace('/', '_').replace(' ', '_')
            typ  = e['type'].strip()
            key  = (base, typ)
            if key in seen:
                seen[key] += 1
                base = f"{base}_{seen[key]}"
            else:
                seen[key] = 0

            if typ in CONTAINER_TYPES:
                # Create a subdirectory and recurse into the sub-directory
                subdir = os.path.join(dest, f"{base}.{typ}")
                os.makedirs(subdir, exist_ok=True)
                print(f"  {e['name']:<10} {typ}  (directory)  →  {subdir}/")
                _extract_entries(parse_subdir(data, e), subdir)
            else:
                fname = os.path.join(dest, f"{base}.{typ}")
                raw   = _read_entry(data, e)
                with open(fname, 'wb') as f:
                    f.write(raw)
                total[0] += 1
                print(f"  {e['name']:<10} {typ}  {len(raw):>8} B  →  {fname}")

    _extract_entries(entries, outdir)
    print(f"\n{total[0]} files extracted to {outdir}/")


def cmd_image(args, data: bytearray, tracks: int):
    """Render a Smaky 6 IM (1bpp graphic bitmap) to a PNG file."""
    try:
        from PIL import Image as PILImage
    except ImportError:
        print("Pillow is required for image rendering:  pip install Pillow",
              file=sys.stderr)
        sys.exit(1)

    entries = parse_dir(data)
    found = find_entries(entries, args.name)
    if not found:
        print(f"File '{args.name}' not found.", file=sys.stderr)
        sys.exit(1)
    e = _select_entry(found, args.name, getattr(args, 'index', None))

    if e['type'].strip() != 'IM':
        print(f"WARNING: '{e['name']}' has type '{e['type']}', not 'IM'. "
              "Attempting render anyway.", file=sys.stderr)

    raw = _read_entry(data, e)

    # Smaky 6 graphic plane: 512 px wide, 64 bytes/row, 1 bpp
    # Height inferred from file size.
    BYTES_PER_ROW = 64
    PX_W = 512
    n_bytes = len(raw)
    n_rows  = n_bytes // BYTES_PER_ROW
    if n_bytes % BYTES_PER_ROW:
        print(f"WARNING: {n_bytes} bytes is not a multiple of {BYTES_PER_ROW}; "
              f"last partial row ignored.", file=sys.stderr)

    # Build greyscale image (0=black, 255=white)
    img = PILImage.new('L', (PX_W, n_rows), 0)
    pix = img.load()
    for row in range(n_rows):
        for col in range(BYTES_PER_ROW):
            byte = raw[row * BYTES_PER_ROW + col]
            for bit in range(8):
                px = col * 8 + (7 - bit)
                pix[px, row] = 255 if (byte >> bit) & 1 else 0

    # Determine output path
    outpath = getattr(args, 'outpng', None)
    if not outpath:
        outpath = e['name'].strip() + '.png'

    # Save raw 1:1 pixel image
    img.save(outpath)
    print(f"Rendered {e['name']} ({n_rows} scan lines, {PX_W} px wide) → {outpath}")

    # Save aspect-ratio-corrected version (scan lines are 4× taller on real CRT)
    AR_SCALE = 4
    ar_rows  = n_rows * AR_SCALE
    img_ar   = img.resize((PX_W, ar_rows), PILImage.NEAREST)
    base, ext = outpath.rsplit('.', 1) if '.' in outpath else (outpath, 'png')
    ar_path  = f"{base}_ar.{ext}"
    img_ar.save(ar_path)
    print(f"Aspect-ratio corrected ({PX_W}×{ar_rows}) → {ar_path}")


def cmd_cat(args, data: bytearray, tracks: int):
    entries = parse_dir(data)
    found = find_entries(entries, args.name)
    if not found:
        print(f"File '{args.name}' not found.", file=sys.stderr)
        sys.exit(1)
    e = _select_entry(found, args.name, getattr(args, 'index', None))
    raw = _read_entry(data, e)
    # Print as Latin-1 text, replacing non-printable bytes
    text = raw.decode('latin-1', errors='replace')
    for ch in text:
        if ch == '\r':
            sys.stdout.write('\n')
        elif ch == '\x00':
            break   # stop at first NUL (common end-of-file marker)
        else:
            sys.stdout.write(ch)
    sys.stdout.write('\n')


def cmd_hexdump(args, data: bytearray, tracks: int):
    sec   = args.sector
    count = getattr(args, 'count', 1)
    total = tracks * SECTORS_TRACK

    if sec < 0 or sec >= total:
        print(f"Sector {sec} out of range (0–{total-1})", file=sys.stderr)
        sys.exit(1)
    count = min(count, total - sec)

    for s in range(sec, sec + count):
        off   = sector_offset(s)
        chunk = data[off: off + SECTOR_SIZE]
        trk, soff = divmod(s, SECTORS_TRACK)
        print(f"\n── Sector {s}  (track {trk}, sec-in-track {soff})"
              f"  offset 0x{off:06X} ──")
        for row in range(0, SECTOR_SIZE, 16):
            line = chunk[row: row + 16]
            hex_part = ' '.join(f'{b:02X}' for b in line)
            asc_part = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in line)
            print(f"  {off+row:06X}  {hex_part:<48}  {asc_part}")


# ── Write helpers ─────────────────────────────────────────────────────────────

def dec_to_bcd(n: int) -> int:
    """Encode a decimal integer as a BCD byte (e.g. 82 → 0x82)."""
    return ((n // 10) << 4) | (n % 10)


def save_image(path: str, data: bytearray) -> None:
    with open(path, 'r+b') as f:
        f.write(data)


def _free_dir_slot(data: bytearray) -> int:
    """Return the byte offset of the first empty directory slot, or -1 if full."""
    raw = data[:DIR_SECTORS * SECTOR_SIZE]
    for i in range(MAX_ENTRIES):
        off = i * 24
        if raw[off] in (0x00, 0xFF):
            return off
    return -1


def _next_free_sector(entries: list, tracks: int) -> int:
    """Return the first sector not occupied by any directory entry."""
    if not entries:
        return DIR_SECTORS
    last_end = max(e['end'] for e in entries)
    return max(last_end, DIR_SECTORS)


def cmd_add(args, data: bytearray, tracks: int):
    import datetime

    # ── Validate name / type ────────────────────────────────────────────────
    name = args.name.upper().strip()
    ftype = args.ftype.upper().strip()
    if len(name) > 8:
        print(f"Name '{name}' is too long (max 8 chars).", file=sys.stderr)
        sys.exit(1)
    if len(ftype) > 2:
        print(f"Type '{ftype}' is too long (max 2 chars).", file=sys.stderr)
        sys.exit(1)

    # ── Read input file ─────────────────────────────────────────────────────
    with open(args.infile, 'rb') as f:
        file_data = f.read()
    file_size = len(file_data)
    if file_size == 0:
        print("Input file is empty.", file=sys.stderr)
        sys.exit(1)

    # ── Check space ─────────────────────────────────────────────────────────
    entries    = parse_dir(data)
    total_sec  = tracks * SECTORS_TRACK
    start_sec  = _next_free_sector(entries, tracks)

    # Number of sectors needed
    size_sec   = (file_size + SECTOR_SIZE - 1) // SECTOR_SIZE
    end_sec    = start_sec + size_sec
    last_bytes = file_size % SECTOR_SIZE  # 0 means last sector is full

    if end_sec > total_sec:
        free = total_sec - start_sec
        print(f"Not enough space: need {size_sec} sectors, "
              f"only {free} free (starting at sector {start_sec}).",
              file=sys.stderr)
        sys.exit(1)

    dir_slot = _free_dir_slot(data)
    if dir_slot < 0:
        print(f"Directory is full ({MAX_ENTRIES} entries).", file=sys.stderr)
        sys.exit(1)

    # ── Warn on duplicate name ──────────────────────────────────────────────
    dupes = find_entries(entries, name)
    if dupes:
        same_type = [d for d in dupes if d['type'].strip() == ftype]
        if same_type:
            print(f"WARNING: a file named '{name}' of type '{ftype}' already exists "
                  f"(starting at sector {same_type[0]['start']}). "
                  "Adding anyway.", file=sys.stderr)

    # ── Parse optional addresses ────────────────────────────────────────────
    def parse_addr(s):
        if s is None:
            return 0
        try:
            return int(s, 0) & 0xFFFF
        except ValueError:
            print(f"Invalid address '{s}' (use e.g. 0x5508 or 21768).",
                  file=sys.stderr)
            sys.exit(1)

    load_addr  = parse_addr(getattr(args, 'load',  None))
    entry_addr = parse_addr(getattr(args, 'entry', None))

    # ── Date ────────────────────────────────────────────────────────────────
    now        = datetime.date.today()
    month_bcd  = dec_to_bcd(now.month)
    year_bcd   = dec_to_bcd(now.year % 100)

    # ── Write file data into image ──────────────────────────────────────────
    img_off = sector_offset(start_sec)
    # Pad to full sector boundary with zeros
    padded  = file_data + b'\x00' * (size_sec * SECTOR_SIZE - file_size)
    data[img_off: img_off + len(padded)] = padded

    # ── Write directory entry ───────────────────────────────────────────────
    entry = bytearray(24)
    entry[0:8]  = name.ljust(8).encode('ascii')
    entry[8:10] = ftype.ljust(2).encode('ascii')
    struct.pack_into('<H', entry, 10, start_sec)
    struct.pack_into('<H', entry, 12, end_sec)
    struct.pack_into('<H', entry, 14, 0)           # flags
    struct.pack_into('<H', entry, 16, last_bytes)
    entry[18]   = (load_addr  >> 8) & 0xFF
    entry[19]   =  load_addr        & 0xFF
    entry[20]   = (entry_addr >> 8) & 0xFF
    entry[21]   =  entry_addr       & 0xFF
    entry[22]   = month_bcd
    entry[23]   = year_bcd

    data[dir_slot: dir_slot + 24] = entry

    # ── Save image ──────────────────────────────────────────────────────────
    save_image(args.image, data)

    load_s  = f"0x{load_addr:04X}"  if load_addr  else ''
    entry_s = f"0x{entry_addr:04X}" if entry_addr else ''
    print(f"Added {name:<10} {ftype:<2}  sectors {start_sec}–{end_sec-1} "
          f"({size_sec} sectors, {file_size} bytes)"
          + (f"  load={load_s}" if load_s else '')
          + (f"  entry={entry_s}" if entry_s else ''))


def cmd_delete(args, data: bytearray, tracks: int):
    entries = parse_dir(data)
    found   = find_entries(entries, args.name)
    if not found:
        print(f"File '{args.name}' not found.", file=sys.stderr)
        sys.exit(1)

    e = _select_entry(found, args.name, getattr(args, 'index', None))

    # Zero out the directory entry (marks it deleted; first byte → 0x00)
    slot_off = e['idx'] * 24
    data[slot_off: slot_off + 24] = b'\x00' * 24

    # Note: sector data is intentionally left in place (as on real Smaky 6).
    # The space will appear as "free/deleted" in the listing but is NOT
    # reclaimed automatically — the Smaky 6 OS uses contiguous allocation
    # and does not compact the disk.
    save_image(args.image, data)
    print(f"Deleted {e['name']} ({e['type']})  "
          f"sectors {e['start']}–{e['end']-1}  ({e['size_exact']} bytes). "
          f"Sector data left in place (not zeroed).")


def cmd_create(args):
    """Create a blank, formatted Smaky 6 floppy or Winchester image."""
    import datetime

    winchester = getattr(args, 'winchester', False)
    tracks     = WINCHESTER_TRACKS if winchester else args.tracks
    label      = (args.label or '').upper().strip()[:8]
    outpath    = args.image

    if os.path.exists(outpath) and not args.force:
        print(f"'{outpath}' already exists. Use --force to overwrite.",
              file=sys.stderr)
        sys.exit(1)

    total_sec  = tracks * SECTORS_TRACK
    image_size = total_sec * SECTOR_SIZE

    # The Smaky 6 formats track 0 sectors 0–2 as the directory.
    # All directory entries are zeroed (empty).
    # Data area starts at sector 3.
    # Uninitialised sectors are filled with 0xE5 (standard floppy fill byte).
    data = bytearray([0xE5] * image_size)

    # Zero out the directory sectors
    data[0: DIR_SECTORS * SECTOR_SIZE] = b'\x00' * (DIR_SECTORS * SECTOR_SIZE)

    # Optionally write a volume label into the first 8 bytes of sector 0.
    # This is not part of the documented Smaky 6 format but does no harm —
    # the OS treats the directory as a flat array of 24-byte entries and
    # a zero first byte means empty, so a label starting with a space or
    # a printable char would be misread as an entry.  We therefore only
    # write it as a comment in the reserved area after the 32 entries
    # (bytes 768+ within the 768-byte directory are unused padding).
    if label:
        label_off = MAX_ENTRIES * 24   # first byte after last dir entry
        label_bytes = label.encode('ascii').ljust(8, b'\x00')
        data[label_off: label_off + 8] = label_bytes

    with open(outpath, 'wb') as f:
        f.write(data)

    now       = datetime.date.today()
    kb        = image_size // 1024
    free_sec  = total_sec - DIR_SECTORS
    print(f"Created {outpath}")
    if winchester:
        print(f"  Type     : Winchester hard disk (16 MB)")
        print(f"  Geometry : {total_sec} sectors × {SECTOR_SIZE} bytes")
    else:
        print(f"  Type     : Micropolis floppy ({tracks}-track)")
        print(f"  Geometry : {tracks} tracks × {SECTORS_TRACK} sectors × {SECTOR_SIZE} bytes")
    print(f"  Size     : {image_size:,} bytes  ({kb} KB)")
    print(f"  Directory: {DIR_SECTORS} sectors, max {MAX_ENTRIES} entries")
    print(f"  Free     : {free_sec} sectors  ({free_sec * SECTOR_SIZE // 1024} KB)")
    if label:
        print(f"  Label    : {label}  (stored at byte {MAX_ENTRIES * 24})")
    print(f"  Created  : {now.strftime('%d %b %Y')}")


def cmd_compact(args, data: bytearray, tracks: int):
    """Repack all live files contiguously, reclaiming gaps from deleted entries."""
    entries = parse_dir(data)
    if not entries:
        print("Disk has no files; nothing to compact.")
        return

    total_sec = tracks * SECTORS_TRACK

    # Check whether compaction is actually needed
    gaps = 0
    cursor = DIR_SECTORS
    for e in entries:
        if e['start'] > cursor:
            gaps += e['start'] - cursor
        cursor = e['end']
    if gaps == 0:
        print(f"Disk is already compact ({len(entries)} files, no gaps).")
        return

    print(f"Compacting: {gaps} gap sector(s) to reclaim "
          f"({gaps * SECTOR_SIZE // 1024} KB)…")

    # Build a fresh 64 KB zero-filled sector area (preserve directory sectors)
    new_data = bytearray(len(data))
    new_data[0: DIR_SECTORS * SECTOR_SIZE] = data[0: DIR_SECTORS * SECTOR_SIZE]

    # Clear directory in new image — we will rewrite it
    new_data[0: DIR_SECTORS * SECTOR_SIZE] = b'\x00' * (DIR_SECTORS * SECTOR_SIZE)

    write_cursor = DIR_SECTORS   # next free sector in new layout
    new_entries  = []

    for e in entries:
        old_off  = sector_offset(e['start'])
        size_sec = e['size_sectors']
        # Copy file sectors to new position
        new_off  = sector_offset(write_cursor)
        new_data[new_off: new_off + size_sec * SECTOR_SIZE] = \
            data[old_off: old_off + size_sec * SECTOR_SIZE]

        # Build updated entry
        updated = dict(e)
        updated['start'] = write_cursor
        updated['end']   = write_cursor + size_sec
        new_entries.append(updated)

        if e['start'] != write_cursor:
            print(f"  {e['name']:<10} {e['type']}  "
                  f"sector {e['start']} → {write_cursor}")

        write_cursor += size_sec

    # Rewrite directory entries in order
    for i, e in enumerate(new_entries):
        slot = i * 24
        raw  = bytearray(24)
        raw[0:8]  = e['name'].ljust(8).encode('ascii')
        raw[8:10] = e['type'].ljust(2).encode('ascii')
        struct.pack_into('<H', raw, 10, e['start'])
        struct.pack_into('<H', raw, 12, e['end'])
        struct.pack_into('<H', raw, 14, e['flags'])
        struct.pack_into('<H', raw, 16, e['last_bytes'])
        raw[18]   = (e['load']  >> 8) & 0xFF
        raw[19]   =  e['load']        & 0xFF
        raw[20]   = (e['entry'] >> 8) & 0xFF
        raw[21]   =  e['entry']       & 0xFF
        raw[22]   = dec_to_bcd(e['month']) if e['month'] else 0
        raw[23]   = dec_to_bcd(e['year'])  if e['year']  else 0
        new_data[slot: slot + 24] = raw

    save_image(args.image, new_data)

    free_after = total_sec - write_cursor
    print(f"Done. {len(new_entries)} files, "
          f"{write_cursor} sectors used, "
          f"{free_after} sectors free "
          f"({free_after * SECTOR_SIZE // 1024} KB).")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(
        description='Smaky 6 floppy disk image tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split('Usage:')[1] if 'Usage:' in __doc__ else '')

    sub_top = p.add_subparsers(dest='top_command', required=True)

    # ── create (top-level, no existing image needed) ──────────────────────
    pcr = sub_top.add_parser('create',
                             help='Create a blank formatted floppy image')
    pcr.add_argument('image',  help='Output image path')
    pcr.add_argument('--tracks', type=int, choices=[40, 77], default=77,
                     help='Number of tracks for floppy  (default: 77 = 315 KB)')
    pcr.add_argument('--winchester', action='store_true',
                     help='Create a 16 MB Winchester hard-disk image instead of a floppy')
    pcr.add_argument('--label', default='',
                     help='Optional volume label (max 8 chars)')
    pcr.add_argument('--force', action='store_true',
                     help='Overwrite existing file')

    # ── disk commands (require an existing image) ─────────────────────────
    pd = sub_top.add_parser('disk',
                            help='Operate on an existing disk image')
    pd.add_argument('image', help='Flat disk image (.dsk / .img)')
    sub = pd.add_subparsers(dest='command', required=True)

    # list
    sub.add_parser('list', aliases=['ls'],
                   help='List directory with file metadata')

    # info
    sub.add_parser('info',
                   help='Show disk geometry, free space, raw directory bytes')

    # extract
    px = sub.add_parser('extract', aliases=['x'],
                        help='Extract one file (stdout if no outfile given)')
    px.add_argument('name', help='Filename (case-insensitive)')
    px.add_argument('output', nargs='?', default='-',
                    help='Output path  (- or omit = stdout)')
    px.add_argument('--index', '-i', type=int, default=None,
                    help='Which copy to extract when name is not unique')

    # extract-all
    pa = sub.add_parser('extract-all', aliases=['xa'],
                        help='Extract every file into a directory')
    pa.add_argument('outdir', nargs='?', default='extracted',
                    help='Output directory  (default: extracted/)')

    # cat
    pc = sub.add_parser('cat',
                        help='Print file as text (stops at NUL, CR→LF)')
    pc.add_argument('name', help='Filename (case-insensitive)')
    pc.add_argument('--index', '-i', type=int, default=None,
                    help='Which copy when name is not unique')

    # add
    padd = sub.add_parser('add',
                          help='Add a file to the disk image')
    padd.add_argument('infile', help='Local file to add')
    padd.add_argument('name',   help='Smaky 6 filename (max 8 chars, upper-cased)')
    padd.add_argument('ftype',  help='2-char type code  (e.g. SM, SY, IM, BS)')
    padd.add_argument('--load',  default=None,
                      help='Load address in hex, e.g. 0x5500')
    padd.add_argument('--entry', default=None,
                      help='Entry point in hex, e.g. 0x5508')

    # compact
    sub.add_parser('compact',
                   help='Repack files contiguously, reclaiming deleted gaps')

    # delete
    pdel = sub.add_parser('delete', aliases=['del', 'rm'],
                          help='Mark a file as deleted in the directory')
    pdel.add_argument('name', help='Filename (case-insensitive)')
    pdel.add_argument('--index', '-i', type=int, default=None,
                      help='Which copy when name is not unique')

    # image
    pi = sub.add_parser('image', aliases=['img'],
                        help='Render an IM bitmap file to PNG (requires Pillow)')
    pi.add_argument('name', help='Filename (case-insensitive, type IM)')
    pi.add_argument('outpng', nargs='?', default=None,
                    help='Output PNG path  (default: <name>.png)')
    pi.add_argument('--index', '-i', type=int, default=None,
                    help='Which copy when name is not unique')

    # hexdump
    ph = sub.add_parser('hexdump', aliases=['hd'],
                        help='Hex dump one or more raw sectors')
    ph.add_argument('sector', type=int,
                    help='Starting absolute sector number')
    ph.add_argument('count',  type=int, nargs='?', default=1,
                    help='Number of sectors to dump  (default: 1)')

    args = p.parse_args()

    # ── Dispatch ──────────────────────────────────────────────────────────
    if args.top_command == 'create':
        cmd_create(args)
        return

    # All other commands need an existing image
    data, tracks = load_image(args.image)

    disk_dispatch = {
        'list':        cmd_list,
        'ls':          cmd_list,
        'info':        cmd_info,
        'extract':     cmd_extract,
        'x':           cmd_extract,
        'extract-all': cmd_extract_all,
        'xa':          cmd_extract_all,
        'image':       cmd_image,
        'img':         cmd_image,
        'add':         cmd_add,
        'compact':     cmd_compact,
        'delete':      cmd_delete,
        'del':         cmd_delete,
        'rm':          cmd_delete,
        'cat':         cmd_cat,
        'hexdump':     cmd_hexdump,
        'hd':          cmd_hexdump,
    }
    disk_dispatch[args.command](args, data, tracks)


if __name__ == '__main__':
    main()
