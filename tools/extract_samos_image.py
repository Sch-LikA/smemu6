#!/usr/bin/env python3

import json
import pathlib
import shutil
import sys

SECTOR_SIZE = 256
DIR_SECTORS = 3
ENTRY_SIZE = 24
ENTRY_COUNT = (DIR_SECTORS * SECTOR_SIZE) // ENTRY_SIZE


def usage() -> int:
    print("usage: extract_samos_image.py <image.dsk> <outdir>", file=sys.stderr)
    return 2


def decode_name(raw: bytes) -> str:
    return raw.decode("ascii", errors="strict").rstrip(" \x00")


def decode_type(raw: bytes) -> str:
    return raw.decode("ascii", errors="strict").rstrip(" \x00")


def bcd_to_int(value: int) -> int:
    return ((value >> 4) * 10) + (value & 0x0F)


def parse_entries(image: bytes, dir_sector: int, container_start_sector: int):
    entries = []
    base = dir_sector * SECTOR_SIZE
    for index in range(ENTRY_COUNT):
        entry = image[base + index * ENTRY_SIZE: base + (index + 1) * ENTRY_SIZE]
        if entry == b"\x00" * ENTRY_SIZE:
            continue
        name = decode_name(entry[0:8])
        file_type = decode_type(entry[8:10])
        if not name or not file_type.strip():
            continue
        start_sector = int.from_bytes(entry[10:12], "little")
        end_sector = int.from_bytes(entry[12:14], "little")
        sectors = max(0, end_sector - start_sector)
        last_bytes = int.from_bytes(entry[16:18], "little")
        size = sectors * SECTOR_SIZE
        if sectors > 0 and last_bytes:
            size = (sectors - 1) * SECTOR_SIZE + last_bytes
        entries.append({
            "name": name,
            "type": file_type,
            "start_sector": start_sector,
            "end_sector": end_sector,
            "absolute_start_sector": container_start_sector + start_sector,
            "flags": int.from_bytes(entry[14:16], "little"),
            "load": (entry[18] << 8) | entry[19],
            "entry": (entry[20] << 8) | entry[21],
            "date_month": bcd_to_int(entry[22]),
            "date_year": bcd_to_int(entry[23]),
            "size": size,
        })
    return entries


def write_sidecar(path: pathlib.Path, metadata: dict) -> None:
    with open(path, "w", encoding="ascii") as handle:
        json.dump(metadata, handle, indent=2, sort_keys=True)
        handle.write("\n")


def extract_dir(image: bytes, outdir: pathlib.Path, entries):
    outdir.mkdir(parents=True, exist_ok=True)
    for entry in entries:
        file_name = f"{entry['name']}.{entry['type']}"
        metadata = {
            "type": entry["type"],
            "flags": entry["flags"],
            "load": entry["load"],
            "entry": entry["entry"],
            "date_month": entry["date_month"],
            "date_year": entry["date_year"],
            "start_sector": entry["start_sector"],
        }
        if entry["type"] == "DR":
            child_dir = outdir / file_name
            child_entries = parse_entries(image, entry["absolute_start_sector"], entry["absolute_start_sector"])
            extract_dir(image, child_dir, child_entries)
            write_sidecar(outdir / f"{file_name}.meta.json", metadata)
            continue

        start = entry["absolute_start_sector"] * SECTOR_SIZE
        with open(outdir / file_name, "wb") as handle:
            handle.write(image[start:start + entry["size"]])
        write_sidecar(outdir / f"{file_name}.meta.json", metadata)


def main() -> int:
    if len(sys.argv) != 3:
        return usage()

    image_path = pathlib.Path(sys.argv[1])
    outdir = pathlib.Path(sys.argv[2])
    image = image_path.read_bytes()
    if len(image) not in (40 * 16 * SECTOR_SIZE, 77 * 16 * SECTOR_SIZE):
        print(f"unsupported image size: {len(image)}", file=sys.stderr)
        return 1

    if outdir.exists():
        shutil.rmtree(outdir)
    root_entries = parse_entries(image, 0, 0)
    extract_dir(image, outdir, root_entries)
    print(f"extracted {len(root_entries)} top-level entries to {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())