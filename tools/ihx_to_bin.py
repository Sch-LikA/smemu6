#!/usr/bin/env python3

import argparse
from pathlib import Path


def parse_intel_hex(path: Path) -> dict[int, int]:
    data: dict[int, int] = {}
    upper = 0

    with path.open("r", encoding="ascii") as handle:
        for line_no, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"{path}:{line_no}: missing ':' record prefix")

            payload = bytes.fromhex(line[1:])
            if len(payload) < 5:
                raise ValueError(f"{path}:{line_no}: truncated record")

            count = payload[0]
            address = (payload[1] << 8) | payload[2]
            record_type = payload[3]
            record_data = payload[4:4 + count]
            checksum = payload[4 + count]

            if len(record_data) != count:
                raise ValueError(f"{path}:{line_no}: byte count mismatch")

            total = sum(payload[:-1]) + checksum
            if (total & 0xFF) != 0:
                raise ValueError(f"{path}:{line_no}: checksum mismatch")

            if record_type == 0x00:
                base = upper + address
                for index, value in enumerate(record_data):
                    data[base + index] = value
            elif record_type == 0x01:
                break
            elif record_type == 0x02:
                if count != 2:
                    raise ValueError(f"{path}:{line_no}: bad type 02 record length")
                upper = (((record_data[0] << 8) | record_data[1]) << 4)
            elif record_type == 0x04:
                if count != 2:
                    raise ValueError(f"{path}:{line_no}: bad type 04 record length")
                upper = (((record_data[0] << 8) | record_data[1]) << 16)
            else:
                continue

    if not data:
        raise ValueError(f"{path}: no data records found")

    return data


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert Intel HEX to a flat binary")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--base", type=lambda value: int(value, 0), default=None)
    parser.add_argument("--fill", type=lambda value: int(value, 0), default=0x00)
    args = parser.parse_args()

    if not 0 <= args.fill <= 0xFF:
        raise ValueError("fill byte must be in range 0..255")

    records = parse_intel_hex(args.input)
    start = min(records) if args.base is None else args.base
    end = max(records)
    if start > end:
        raise ValueError("base address is above last data byte")

    image = bytearray([args.fill] * (end - start + 1))
    for address, value in records.items():
        if address < start:
            continue
        image[address - start] = value

    args.output.write_bytes(image)
    print(f"wrote {args.output} ({len(image)} bytes from 0x{start:04X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())