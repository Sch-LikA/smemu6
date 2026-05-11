#!/usr/bin/env python3
"""
gen_test_gfx.py — Generate a Smaky 6 graphic framebuffer test pattern.

The Smaky 6 graphic plane is 60 lores rows × 64 bytes = 3840 bytes at 0x4600.
Each lores row is displayed 4× vertically → 240 scan lines total.
Pixel layout within each byte is determined by the shift register bit order.

Outputs:
  tmp/test_gfx.bin          raw 3840-byte framebuffer (load at 0x4600)
  tmp/test_gfx_msb.png      expected render if MSB of byte = leftmost pixel
  tmp/test_gfx_lsb.png      expected render if LSB of byte = leftmost pixel

Load into emulator (requires -loadbin support):
  ./build/smemu6 -loadbin 0x4600 tmp/test_gfx.bin -no-launcher

Usage:
  python3 tools/gen_test_gfx.py
"""

import os
import sys

ROWS = 60     # lores rows
COLS = 64     # bytes per row
PX_W = 512    # pixels wide
PX_H = 240    # native height (each lores row = 4 scan lines)

buf = bytearray(ROWS * COLS)

def set_pixel(row, px):
    """Set pixel at lores row (0..59), pixel column (0..511).
    Stored MSB-first: leftmost pixel = bit 7 of first byte."""
    if 0 <= row < ROWS and 0 <= px < PX_W:
        byte_idx = row * COLS + px // 8
        bit = 7 - (px % 8)          # MSB-first: leftmost px → bit 7
        buf[byte_idx] |= (1 << bit)

def hline(row, x0=0, x1=PX_W-1):
    for px in range(x0, x1 + 1):
        set_pixel(row, px)

def vline(px, r0=0, r1=ROWS-1):
    for row in range(r0, r1 + 1):
        set_pixel(row, px)

def rect(r0, r1, x0, x1):
    hline(r0, x0, x1)
    hline(r1, x0, x1)
    for row in range(r0, r1 + 1):
        set_pixel(row, x0)
        set_pixel(row, x1)

# ── Pattern 1: frame ────────────────────────────────────────────────────────
hline(0)           # top full line
hline(59)          # bottom full line
vline(0)           # left edge
vline(511)         # right edge

# ── Pattern 2: cross-hairs at exact centre ──────────────────────────────────
hline(30)          # horizontal midline
vline(256)         # vertical midline

# ── Pattern 3: byte-boundary markers at row 2 ───────────────────────────────
# One pixel at the START of each byte group → should give 64 evenly spaced dots
# spaced exactly 8 pixels apart.  With wrong bit order the dots shift by 7 px.
for col in range(COLS):
    set_pixel(2, col * 8)      # leftmost pixel of each byte (px 0,8,16,…504)

# ── Pattern 4: second byte-boundary row, rightmost pixel of each byte ───────
for col in range(COLS):
    set_pixel(4, col * 8 + 7)  # rightmost pixel of each byte (px 7,15,23,…511)

# ── Pattern 5: 45° diagonal (1 px right per lores row, starting at left) ────
# Spans 60 px across for 60 rows → very steep, clearly staircased at 1px/row.
# Used to verify row-to-row pixel alignment.
for row in range(ROWS):
    set_pixel(row, row)

# ── Pattern 6: shallow diagonal (fills most of the width) ───────────────────
# ~8 px per lores row — steps should be exactly 1 byte wide if rendering correct
for row in range(ROWS):
    px = row * 8
    if px < PX_W:
        set_pixel(row, px)

# ── Pattern 7: small reference box (rows 10–20, px 100–200) ─────────────────
rect(10, 20, 100, 200)

# ── Pattern 8: checkerboard in top-right quadrant (rows 0–29, px 257–511) ───
for row in range(0, 30):
    for px in range(257, 512):
        if (row + px) % 2 == 0:
            set_pixel(row, px)

# ── Write binary ────────────────────────────────────────────────────────────
os.makedirs('tmp', exist_ok=True)
out_bin = 'tmp/test_gfx.bin'
with open(out_bin, 'wb') as f:
    f.write(buf)
print(f"Written {out_bin}  ({len(buf)} bytes)")
print(f"  Load at 0x4600 with:  ./build/smemu6 -loadbin 0x4600 {out_bin} -no-launcher")

# ── PNG reference renders ────────────────────────────────────────────────────
try:
    from PIL import Image
except ImportError:
    print("\nPIL not available — skipping PNG output (pip install pillow)")
    sys.exit(0)

SCALE = 2   # 2× vertical stretch matching VIDEO_ASPECT_H = 480

for msb_first in (True, False):
    img = Image.new('RGB', (PX_W, PX_H * SCALE), (0, 20, 0))
    pix = img.load()
    for row in range(ROWS):
        for col in range(COLS):
            byte = buf[row * COLS + col]
            for bit in range(8):
                if msb_first:
                    px = col * 8 + (7 - bit)
                else:
                    px = col * 8 + bit
                lit = bool(byte & (1 << bit))
                color = (0, 231, 0) if lit else (0, 20, 0)
                y_base = row * 4 * SCALE // (PX_H // ROWS)  # = row * SCALE * 4 / 4
                y_base = row * SCALE * (PX_H // ROWS) // (PX_H // ROWS)
                # Each lores row = 4 native scan lines = 8 output lines (2× stretch)
                y_base = row * 4 * SCALE  # row * 8
                for y in range(y_base, min(y_base + 4 * SCALE, PX_H * SCALE)):
                    pix[px, y] = color

    label = 'msb' if msb_first else 'lsb'
    path = f'tmp/test_gfx_{label}.png'
    img.save(path)
    print(f"Written {path}  (bit order: {'bit7=leftmost' if msb_first else 'bit0=leftmost'})")
