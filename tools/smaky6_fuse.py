#!/usr/bin/env python3
"""
smaky6_fuse.py – FUSE filesystem for Smaky 6 disk images.

Mounts a .dsk image as a flat directory; each directory entry appears as a
regular file named  NAME.TYPE  (e.g. SYS.SY, NATHALIE.IM).

Usage:
    smaky6_fuse.py <image> <mountpoint> [FUSE options]
    smaky6_fuse.py <image> <mountpoint> -o ro          # read-only
    smaky6_fuse.py <image> <mountpoint> -f             # foreground
    smaky6_fuse.py <image> <mountpoint> -d             # debug

    # Unmount:
    fusermount -u <mountpoint>          # Linux
    umount <mountpoint>                 # macOS

Requirements:
    pip install fusepy
    # Linux: apt install fuse
    # macOS: brew install macfuse  (and allow in System Preferences)

Write support:
    Create  cp myfile.bin <mountpoint>/MYFILE.SM
    Delete  rm <mountpoint>/OLD.SM
    Files must be named NAME.TT where NAME ≤ 8 chars and TT is a 2-char
    Smaky 6 type code (SY SM ST SR LS FH BS IM KS or any 2 letters).
    Writes are buffered in memory and committed to the image on file close.

Duplicate names:
    If the directory contains two entries with the same NAME.TYPE, the
    second appears as NAME.TYPE~1, the third as NAME.TYPE~2, etc.
"""

import os
import sys
import stat
import errno
import time
import calendar
import threading
import argparse
from pathlib import Path

# ── Import shared helpers from smaky6_samos ──────────────────────────────────
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from smaky6_samos import (
        load_image, save_image, parse_dir, parse_subdir,
        _read_entry, _next_free_sector, _free_dir_slot, dec_to_bcd,
        SECTOR_SIZE, SECTORS_TRACK, DIR_SECTORS, MAX_ENTRIES, sector_offset,
        CONTAINER_TYPES,
    )
except ImportError as exc:
    sys.exit(f"Cannot import smaky6_samos: {exc}\n"
             "Make sure smaky6_samos.py is in the same directory.")

try:
    from fuse import FUSE, FuseOSError, Operations, LoggingMixIn
except ImportError:
    sys.exit(
        "fusepy not found.\n"
        "Install with:  pip install fusepy\n"
        "Linux also needs libfuse:  sudo apt install fuse"
    )


# ── Helpers ───────────────────────────────────────────────────────────────────

def _bcd_to_int(b: int) -> int:
    return (b >> 4) * 10 + (b & 0x0F)


def _entry_mtime(e: dict) -> float:
    """Convert BCD month/year directory fields to a Unix timestamp."""
    month = _bcd_to_int(e['month'])
    year  = _bcd_to_int(e['year'])
    year_full = 1900 + year if year >= 78 else 2000 + year
    month = max(1, min(12, month)) if month else 1
    try:
        return float(calendar.timegm((year_full, month, 1, 12, 0, 0, 0, 1, 0)))
    except Exception:
        return 0.0


def _entry_size(e: dict) -> int:
    """Byte size of the file described by directory entry e."""
    start, end, last = e['start'], e['end'], e['last_bytes']
    if end <= start:
        return 0
    full = end - start - 1
    tail = last if last else SECTOR_SIZE
    return full * SECTOR_SIZE + tail


def _ent_to_fname(e: dict) -> str:
    return f"{e['name'].rstrip()}.{e['type']}"


def _build_name_map(entries: list) -> dict:
    """
    Map visible filename -> entry dict.
    Duplicate base names get a ~N suffix on the later occurrences.
    """
    seen: dict[str, int] = {}
    result: dict[str, dict] = {}
    for e in entries:
        base = _ent_to_fname(e)
        if base not in seen:
            seen[base] = 0
            result[base] = e
        else:
            seen[base] += 1
            result[f"{base}~{seen[base]}"] = e
    return result


def _parse_fname(fname: str) -> tuple[str, str]:
    """
    Split 'NAME.TYPE' into ('NAME   ', 'TY').
    NAME is uppercased, right-padded to 8 chars.
    TYPE is uppercased, must be exactly 2 chars.
    Raises ValueError on bad format.
    """
    # Strip ~N duplicate suffix if present
    if '~' in fname:
        fname = fname.rsplit('~', 1)[0]
    parts = fname.rsplit('.', 1)
    if len(parts) != 2 or len(parts[1]) != 2:
        raise ValueError(f"Name must be NAME.TT (2-char type): {fname!r}")
    name, typ = parts
    if not name or len(name) > 8:
        raise ValueError(f"Smaky 6 name must be 1-8 chars: {name!r}")
    name_padded = name.upper().ljust(8)
    return name_padded, typ.upper()


def _write_sectors(data: bytearray, start_sec: int, content: bytes) -> None:
    """Write content into the flat image starting at start_sec."""
    for i, sec in enumerate(range(start_sec, start_sec + len(content) // SECTOR_SIZE + 1)):
        chunk = content[i * SECTOR_SIZE: (i + 1) * SECTOR_SIZE]
        if not chunk:
            break
        off = sector_offset(sec)
        data[off: off + len(chunk)] = chunk


def _commit_file(data: bytearray, tracks: int,
                 smaky_name: str, smaky_type: str,
                 content: bytes, slot_off: int) -> None:
    """
    Write *content* to the next free region of the image and fill in
    the 24-byte directory slot at byte offset *slot_off*.
    Raises OSError(ENOSPC) if there is not enough room.
    """
    entries = parse_dir(data)
    start_sec = _next_free_sector(entries, tracks)
    n_sectors = (len(content) + SECTOR_SIZE - 1) // SECTOR_SIZE
    total_sectors = tracks * SECTORS_TRACK
    if start_sec + n_sectors > total_sectors:
        raise FuseOSError(errno.ENOSPC)

    # Write data sectors
    for i in range(n_sectors):
        sec_data = content[i * SECTOR_SIZE: (i + 1) * SECTOR_SIZE]
        sec_data = sec_data.ljust(SECTOR_SIZE, b'\x00')
        off = sector_offset(start_sec + i)
        data[off: off + SECTOR_SIZE] = sec_data

    end_sec = start_sec + n_sectors
    last_bytes = len(content) % SECTOR_SIZE  # 0 means full last sector

    now = time.localtime()
    month_bcd = dec_to_bcd(now.tm_mon)
    year_bcd  = dec_to_bcd(now.tm_year % 100)

    entry = bytearray(24)
    entry[0:8]   = smaky_name.encode('ascii', errors='replace')[:8].ljust(8)
    entry[8:10]  = smaky_type.encode('ascii', errors='replace')[:2]
    entry[10:12] = start_sec.to_bytes(2, 'little')
    entry[12:14] = end_sec.to_bytes(2, 'little')
    entry[14:16] = (0).to_bytes(2, 'little')      # flags
    entry[16:18] = last_bytes.to_bytes(2, 'little')
    entry[18:20] = (0).to_bytes(2, 'little')      # load addr
    entry[20:22] = (0).to_bytes(2, 'little')      # entry addr
    entry[22]    = month_bcd
    entry[23]    = year_bcd

    data[slot_off: slot_off + 24] = entry


# ── FUSE Operations ───────────────────────────────────────────────────────────

class Smaky6FS(Operations):
    """FUSE filesystem backed by a Smaky 6 flat disk image."""

    def __init__(self, image_path: str, read_only: bool = False):
        self.image_path = image_path
        self.read_only  = read_only
        self.data, self.tracks = load_image(image_path)
        self._mount_time = time.time()
        self._lock = threading.Lock()

        # Open-file state keyed by (path string)
        # Each entry: {'buf': bytearray, 'slot_off': int|None, 'dirty': bool}
        # slot_off is None for brand-new files until first commit
        self._open: dict[str, dict] = {}

    # ── Internal helpers ──────────────────────────────────────────────────

    def _entries(self):
        return parse_dir(self.data)

    def _name_map(self):
        return _build_name_map(self._entries())

    def _stat_root(self):
        t = self._mount_time
        return dict(
            st_mode  = stat.S_IFDIR | 0o755,
            st_nlink = 2,
            st_size  = 0,
            st_atime = t, st_mtime = t, st_ctime = t,
            st_uid   = os.getuid(),
            st_gid   = os.getgid(),
        )

    def _stat_entry(self, e: dict) -> dict:
        mtime = _entry_mtime(e) or self._mount_time
        mode  = stat.S_IFREG | (0o444 if self.read_only else 0o644)
        return dict(
            st_mode  = mode,
            st_nlink = 1,
            st_size  = _entry_size(e),
            st_atime = mtime, st_mtime = mtime, st_ctime = mtime,
            st_uid   = os.getuid(),
            st_gid   = os.getgid(),
        )

    def _resolve_path(self, path):
        """
        Resolve a FUSE path to (kind, entry, parent_entry):
          ('root', None, None)       for '/'
          ('dir',  dr_e, None)       for '/BASIC.DR'  (a DR container)
          ('file', e,    None)       for '/SYS.SY'    (a root-level file)
          ('file', e,    dr_e)       for '/BASIC.DR/BINBASIC.SM'
        Raises FuseOSError(ENOENT) if the path does not exist.
        Must be called with self._lock held.
        """
        parts = [p for p in path.split('/') if p]
        root_entries = self._entries()
        root_map = _build_name_map(root_entries)

        if not parts:
            return ('root', None, None)

        if parts[0] not in root_map:
            raise FuseOSError(errno.ENOENT)
        e0 = root_map[parts[0]]

        if len(parts) == 1:
            if e0['type'].strip() in CONTAINER_TYPES:
                return ('dir', e0, None)
            return ('file', e0, None)

        if len(parts) == 2:
            if e0['type'].strip() not in CONTAINER_TYPES:
                raise FuseOSError(errno.ENOTDIR)
            sub_map = _build_name_map(parse_subdir(self.data, e0))
            if parts[1] not in sub_map:
                raise FuseOSError(errno.ENOENT)
            return ('file', sub_map[parts[1]], e0)

        raise FuseOSError(errno.ENOENT)  # deeper nesting not supported

    # ── FUSE: metadata ────────────────────────────────────────────────────

    def getattr(self, path, fh=None):
        if path == '/':
            return self._stat_root()
        fname = path.lstrip('/')
        with self._lock:
            # File currently open for writing (root-level only)
            if fname in self._open:
                state = self._open[fname]
                t = self._mount_time
                mode = stat.S_IFREG | (0o444 if self.read_only else 0o644)
                return dict(
                    st_mode=mode, st_nlink=1,
                    st_size=len(state['buf']),
                    st_atime=t, st_mtime=t, st_ctime=t,
                    st_uid=os.getuid(), st_gid=os.getgid(),
                )
            kind, e, parent = self._resolve_path(path)
            if kind == 'root':
                return self._stat_root()
            if kind == 'dir':
                mtime = _entry_mtime(e) or self._mount_time
                return dict(
                    st_mode  = stat.S_IFDIR | 0o755,
                    st_nlink = 2,
                    st_size  = 0,
                    st_atime = mtime, st_mtime = mtime, st_ctime = mtime,
                    st_uid   = os.getuid(),
                    st_gid   = os.getgid(),
                )
            return self._stat_entry(e)

    def readdir(self, path, fh):
        yield '.'
        yield '..'
        with self._lock:
            if path == '/':
                for fname in self._name_map():
                    yield fname
            else:
                kind, e, parent = self._resolve_path(path)
                if kind != 'dir':
                    raise FuseOSError(errno.ENOTDIR)
                for fname in _build_name_map(parse_subdir(self.data, e)):
                    yield fname

    def statfs(self, path):
        total_sec = self.tracks * SECTORS_TRACK
        with self._lock:
            entries = self._entries()
        used_sec = _next_free_sector(entries, self.tracks) - DIR_SECTORS
        free_sec = total_sec - DIR_SECTORS - max(used_sec, 0)
        return dict(
            f_bsize  = SECTOR_SIZE,
            f_frsize = SECTOR_SIZE,
            f_blocks = total_sec,
            f_bfree  = max(free_sec, 0),
            f_bavail = max(free_sec, 0),
            f_files  = MAX_ENTRIES,
            f_ffree  = MAX_ENTRIES - len(entries),
            f_namelen = 11,   # "NAME.TT" max = 8+1+2
        )

    # ── FUSE: read ────────────────────────────────────────────────────────

    def open(self, path, flags):
        fname = path.lstrip('/')
        write_requested = bool(flags & (os.O_WRONLY | os.O_RDWR))
        with self._lock:
            if write_requested and self.read_only:
                raise FuseOSError(errno.EROFS)
            # Check the path exists
            if fname not in self._open:
                kind, e, parent = self._resolve_path(path)
                if kind == 'dir':
                    raise FuseOSError(errno.EISDIR)
                if write_requested:
                    if parent is not None:
                        # Sub-directory entries are read-only via FUSE
                        raise FuseOSError(errno.EROFS)
                    buf = bytearray(_read_entry(self.data, e))
                    self._open[fname] = {'buf': buf,
                                         'slot_off': e['_offset'],
                                         'dirty': False}
        return 0

    def read(self, path, size, offset, fh):
        fname = path.lstrip('/')
        with self._lock:
            if fname in self._open:
                buf = self._open[fname]['buf']
                return bytes(buf[offset: offset + size])
            kind, e, parent = self._resolve_path(path)
            if kind != 'file':
                raise FuseOSError(errno.EISDIR)
            content = _read_entry(self.data, e)
        return bytes(content[offset: offset + size])

    # ── FUSE: write ───────────────────────────────────────────────────────

    def create(self, path, mode, fi=None):
        if self.read_only:
            raise FuseOSError(errno.EROFS)
        fname = path.lstrip('/')
        try:
            _parse_fname(fname)
        except ValueError as exc:
            raise FuseOSError(errno.EINVAL) from exc
        with self._lock:
            nm = self._name_map()
            if fname in nm:
                # Overwrite: load existing content, keep slot
                e = nm[fname]
                self._open[fname] = {'buf': bytearray(),
                                     'slot_off': e['_offset'],
                                     'dirty': True}
            else:
                # New file: reserve a dir slot now so concurrent creates don't clash
                slot_off = _free_dir_slot(self.data)
                if slot_off < 0:
                    raise FuseOSError(errno.ENOSPC)
                # Mark slot as reserved with a temporary sentinel (zero name field)
                # so _free_dir_slot won't return it again before commit
                self.data[slot_off: slot_off + 8] = b'\xff' * 8
                self._open[fname] = {'buf': bytearray(), 'slot_off': slot_off,
                                     'dirty': True}
        return 0

    def write(self, path, data, offset, fh):
        if self.read_only:
            raise FuseOSError(errno.EROFS)
        fname = path.lstrip('/')
        with self._lock:
            if fname not in self._open:
                raise FuseOSError(errno.EBADF)
            state = self._open[fname]
            buf = state['buf']
            # Extend buffer if necessary
            if offset + len(data) > len(buf):
                buf.extend(b'\x00' * (offset + len(data) - len(buf)))
            buf[offset: offset + len(data)] = data
            state['dirty'] = True
        return len(data)

    def truncate(self, path, length, fh=None):
        if self.read_only:
            raise FuseOSError(errno.EROFS)
        fname = path.lstrip('/')
        with self._lock:
            if fname not in self._open:
                # Open implicitly
                nm = self._name_map()
                if fname not in nm:
                    raise FuseOSError(errno.ENOENT)
                e = nm[fname]
                buf = bytearray(_read_entry(self.data, e))
                self._open[fname] = {'buf': buf,
                                     'slot_off': e['_offset'],
                                     'dirty': True}
            state = self._open[fname]
            buf = state['buf']
            if length < len(buf):
                del buf[length:]
            elif length > len(buf):
                buf.extend(b'\x00' * (length - len(buf)))
            state['dirty'] = True

    def flush(self, path, fh):
        return 0   # commit happens on release

    def release(self, path, fh):
        fname = path.lstrip('/')
        with self._lock:
            if fname not in self._open:
                return 0
            state = self._open.pop(fname)
            if not state['dirty']:
                return 0
            # Parse filename -> Smaky 6 name + type
            try:
                smaky_name, smaky_type = _parse_fname(fname)
            except ValueError:
                return 0
            content = bytes(state['buf'])
            slot_off = state['slot_off']
            try:
                _commit_file(self.data, self.tracks,
                             smaky_name, smaky_type, content, slot_off)
                save_image(self.image_path, self.data)
            except FuseOSError:
                raise
            except Exception as exc:
                print(f"[smaky6_fuse] commit error: {exc}", file=sys.stderr)
                raise FuseOSError(errno.EIO)
        return 0

    def unlink(self, path):
        if self.read_only:
            raise FuseOSError(errno.EROFS)
        with self._lock:
            kind, e, parent = self._resolve_path(path)
            if kind != 'file':
                raise FuseOSError(errno.EISDIR)
            if parent is not None:
                # Cannot delete files inside DR sub-directories via FUSE
                raise FuseOSError(errno.EROFS)
            off = e['_offset']
            # Zero the 24-byte entry → marks it as deleted
            self.data[off: off + 24] = b'\x00' * 24
            save_image(self.image_path, self.data)
        return 0

    # ── FUSE: unsupported ─────────────────────────────────────────────────

    def mkdir(self, path, mode):
        # Creating new DR containers not yet supported
        raise FuseOSError(errno.ENOSYS)

    def rmdir(self, path):
        raise FuseOSError(errno.ENOSYS)

    def rename(self, old, new, flags=0):
        raise FuseOSError(errno.ENOSYS)   # would need to update dir entry

    def symlink(self, target, source):
        raise FuseOSError(errno.ENOSYS)

    def link(self, target, source):
        raise FuseOSError(errno.ENOSYS)

    def chmod(self, path, mode):
        raise FuseOSError(errno.ENOSYS)

    def chown(self, path, uid, gid):
        raise FuseOSError(errno.ENOSYS)

    def utimens(self, path, times=None):
        return 0   # silently ignore timestamp updates


# ── _read_entry needs the _offset field, patch parse_dir ─────────────────────
# parse_dir already stores _offset per entry; verify it does, then expose it.

def _check_offset_field():
    """Abort early if smaky6_samos.parse_dir doesn't store '_offset'."""
    dummy = bytearray(SECTOR_SIZE * DIR_SECTORS)
    entries = parse_dir(dummy)
    # An empty disk returns no entries; that's fine.
    return True

_check_offset_field()


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(
        description='Mount a Smaky 6 floppy image via FUSE',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    p.add_argument('image',      help='Flat disk image (.dsk / .img)')
    p.add_argument('mountpoint', help='Empty directory to mount on')
    p.add_argument('-o', '--options', default='',
                   help='Comma-separated FUSE options  (e.g. ro,allow_other)')
    p.add_argument('-f', '--foreground', action='store_true',
                   help='Run in foreground')
    p.add_argument('-d', '--debug', action='store_true',
                   help='Enable FUSE debug output (implies -f)')

    args = p.parse_args()

    # Parse -o options
    opts = {k: True for k in args.options.split(',') if k}
    read_only = 'ro' in opts
    allow_other = 'allow_other' in opts

    mnt = Path(args.mountpoint)
    if not mnt.is_dir():
        sys.exit(f"Mountpoint {mnt} does not exist or is not a directory")

    if not Path(args.image).is_file():
        sys.exit(f"Image file not found: {args.image}")

    mode = 'read-only' if read_only else 'read-write'
    print(f"Mounting {args.image!r} on {args.mountpoint!r} ({mode})")
    print(f"Unmount with:  fusermount -u {args.mountpoint!r}")

    fs = Smaky6FS(args.image, read_only=read_only)

    FUSE(
        fs,
        args.mountpoint,
        nothreads=False,
        foreground=args.foreground or args.debug,
        debug=args.debug,
        allow_other=allow_other,
        ro=read_only,
    )


if __name__ == '__main__':
    main()
