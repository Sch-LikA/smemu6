#!/usr/bin/env python3
"""
gen_test_im.py -- Generate a diagnostic .IM file for the Smaky 6 emulator.

Confirmed hardware layout (nibble-interleaved):
  Each byte: high nibble (bits 7-4) -> 4 pixels on EVEN scan line (pair*2)
             low  nibble (bits 3-0) -> 4 pixels on ODD  scan line (pair*2+1)
  Within each nibble: bit 3 (MSB) = leftmost pixel.
  Native resolution: 256x120  (64 bytes x 4 px/nibble = 256 wide,
                                60 byte-pairs x 2 scan lines = 120 tall)
  Displayed: 512x480 with 2x stretch in both axes.

Output: tmp/TESTGFX.IM   (load at 0x4600 on the Smaky 6 graphic plane)
        tmp/TESTGFX_expected.png  (reference render, requires Pillow)

Usage:
    python3 tools/gen_test_im.py
    # Copy tmp/TESTGFX.IM to a disk image via smaky6_samos.py, then:
    #   TESTGFX  (at the SAMOS prompt) then  G  (graphic mode)
    # Or: ./build/smemu6 -loadbin 4600 tmp/TESTGFX.IM -vmode graphic
    #          -no-launcher -no-display-off -freeze
"""

import os

PAIRS = 60    # byte-pairs (row groups)
COLS  = 64    # bytes per pair
PX_W  = 256   # native pixels wide  (4 px per nibble x 64 bytes)
LINES = 120   # native scan lines   (2 per pair x 60 pairs)

buf = bytearray(PAIRS * COLS)   # 3840 bytes

def set_px(scan_line, px):
    if not (0 <= scan_line < LINES and 0 <= px < PX_W):
        return
    pair          = scan_line // 2
    is_odd        = scan_line & 1
    col           = px // 4
    bit_in_nibble = 3 - (px % 4)       # bit 3 (MSB of nibble) = leftmost pixel
    bit_in_byte   = bit_in_nibble if is_odd else bit_in_nibble + 4
    buf[pair * COLS + col] |= (1 << bit_in_byte)

def hline(scan_line, x0=0, x1=PX_W-1):
    for px in range(x0, x1 + 1):
        set_px(scan_line, px)

def vline(px, sl0=0, sl1=LINES-1):
    for sl in range(sl0, sl1 + 1):
        set_px(sl, px)

def rect(sl0, sl1, x0, x1):
    hline(sl0, x0, x1);  hline(sl1, x0, x1)
    for sl in range(sl0, sl1 + 1):
        set_px(sl, x0);  set_px(sl, x1)

# Scan line 0: solid full horizontal line
hline(0)
# Scan line 1: leftmost pixel of each nibble group (px=0,4,8,...252)
for col in range(COLS):
    set_px(1, col * 4)
# Scan line 2: rightmost pixel of each nibble group (px=3,7,11,...255)
for col in range(COLS):
    set_px(2, col * 4 + 3)
# Scan line 3: alternating 1010 within each nibble
for col in range(COLS):
    set_px(3, col * 4 + 1);  set_px(3, col * 4 + 3)
# Scan line 4: alternating 0101 within each nibble
for col in range(COLS):
    set_px(4, col * 4 + 0);  set_px(4, col * 4 + 2)
# Vertical lines at key x positions (scan lines 6+)
vline(0,   6, LINES-1)
vline(127, 6, LINES-1)
vline(128, 6, LINES-1)
vline(255, 6, LINES-1)
# Horizontal centre line
hline(60)
# Box
rect(22, 38, 50, 200)
# Diagonal: 4px right per scan line
for sl in range(LINES):
    px = sl * 4
    if px < PX_W:
        set_px(sl, px)

os.makedirs("tmp", exist_ok=True)
out_bin = "tmp/TESTGFX.IM"
with open(out_bin, "wb") as f:
    f.write(buf)
print(f"Written {out_bin}  ({len(buf)} bytes, load at 0x4600)")

try:
    from PIL import Image
    OUT_W, OUT_H = PX_W * 2, LINES * 4   # 512 x 480
    img = Image.new("RGB", (OUT_W, OUT_H), (0, 20, 0))
    pix = img.load()
    for pair in range(PAIRS):
        for col in range(COLS):
            byte = buf[pair * COLS + col]
            hi = byte >> 4;  lo = byte & 0xF
            for bit in range(4):
                px_nat = col * 4 + (3 - bit)
                ox0 = px_nat * 2
                ey0 = pair * 8
                color = (0, 231, 0) if (hi >> bit) & 1 else (0, 20, 0)
                for y in range(ey0, ey0 + 4):
                    pix[ox0, y] = color;  pix[ox0 + 1, y] = color
                oy0 = pair * 8 + 4
                color = (0, 231, 0) if (lo >> bit) & 1 else (0, 20, 0)
                for y in range(oy0, oy0 + 4):
                    pix[ox0, y] = color;  pix[ox0 + 1, y] = color
    img.save("tmp/TESTGFX_expected.png")
    print("Written tmp/TESTGFX_expected.png")
except ImportError:
    print("(Pillow not installed -- skipping PNG preview)")
