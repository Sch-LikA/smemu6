#!/usr/bin/env python3
"""
gen_test_im.py — Generate a diagnostic .IM file for the Smaky 6 emulator.

The graphic framebuffer is 60 lores rows × 64 bytes = 3840 bytes.
Each byte represents 8 horizontal pixels.
This script generates TESTGFX.IM with known geometric patterns to verify:
  1. Bit order within each byte (MSB vs LSB = leftmost pixel)
  2. Byte stride across a row
  3. Row-to-row vertical alignment

Output: tmp/TESTGFX.IM   (load at 0x4600 on the Smaky 6)

Usage:
    python3 tools/gen_test_im.py
    # Then copy tmp/TESTGFX.IM to the floppy or use -loadbin

Expected appearance if rendering is CORRECT (MSB = leftmost):
  Row  0: solid white line (all pixels ON)
  Row  1: 64 isolated dots, one per byte, leftmost pixel of each byte
           → dots at x=0, 8, 16, 24, ... 504   spaced exactly 8px apart
  Row  2: 64 isolated dots, rightmost pixel of each byte
           → dots at x=7, 15, 23, ... 511
  Row  3: alternating 0xAA pattern → pixels at x=0,2,4,6 of each byte group
  Row  4: alternating 0x55 pattern → pixels at x=1,3,5,7 of each byte group
  Row  5: (blank)
  Row  6: solid vertical line at x=0 (all 60 rows, leftmost column pixel)
  Row  7: solid vertical line at x=255 (centre-left)
  Row  8: solid vertical line at x=256 (centre-right)
  Row  9: solid vertical line at x=511 (rightmost)
  Row 10: (blank)
  Rows 11-20: box border (rect)
  Row 30: full horizontal centre line
  Rows 0-59: diagonal from (row=0,x=0) to (row=59,x=59×8=472)
             → steps 8 pixels right per row = exactly 1 byte per row
"""

import os
import sys

ROWS = 60
COLS = 64
PX_W = 512

buf = bytearray(ROWS * COLS)

def byte_addr(row, col):
    return row * COLS + col

def set_px(row, px):
    """Set one pixel. MSB-first: pixel 0 = bit 7, pixel 7 = bit 0."""
    if 0 <= row < ROWS and 0 <= px < PX_W:
        col = px // 8
        bit = 7 - (px % 8)
        buf[byte_addr(row, col)] |= (1 << bit)

def hline(row, x0=0, x1=PX_W-1):
    for px in range(x0, x1+1):
        set_px(row, px)

def vline(px, r0=0, r1=ROWS-1):
    for row in range(r0, r1+1):
        set_px(row, px)

# Row 0: full solid horizontal line
hline(0)

# Row 1: one dot per byte, leftmost pixel of each byte (x=0,8,16,...504)
for col in range(COLS):
    set_px(1, col * 8)

# Row 2: one dot per byte, rightmost pixel of each byte (x=7,15,...511)
for col in range(COLS):
    set_px(2, col * 8 + 7)

# Row 3: 0xAA pattern (10101010) in every byte
for col in range(COLS):
    buf[byte_addr(3, col)] = 0xAA

# Row 4: 0x55 pattern (01010101) in every byte
for col in range(COLS):
    buf[byte_addr(4, col)] = 0x55

# Row 5: blank (separator)

# Rows 6-59: vertical lines at key x positions
vline(0,   6, 59)   # leftmost pixel
vline(255, 6, 59)   # centre-left
vline(256, 6, 59)   # centre-right
vline(511, 6, 59)   # rightmost pixel

# Row 30: full horizontal centre line
hline(30)

# Box: rows 11–19, x 100–400
for row in range(11, 20):
    set_px(row, 100)
    set_px(row, 400)
hline(11, 100, 400)
hline(19, 100, 400)

# Diagonal: one byte (8px) right per row → x = row*8
for row in range(ROWS):
    px = row * 8
    if px < PX_W:
        set_px(row, px)

# Write output
os.makedirs('tmp', exist_ok=True)
out = 'tmp/TESTGFX.IM'
with open(out, 'wb') as f:
    f.write(buf)
print(f"Written {out}  ({len(buf)} bytes, load at 0x4600)")
print()
print("Expected if MSB-first (correct):")
print("  Row 1: dots at x=0, 8, 16, 24 ...  (left edge of each byte group)")
print("  Row 2: dots at x=7, 15, 23, 31 ... (right edge of each byte group)")
print("  Row 3: 0xAA → pixels at even x within each byte group")
print("  Row 4: 0x55 → pixels at odd  x within each byte group")
print("  Diagonal steps 8 px right each row (clean horizontal jumps)")
print()
print("If LSB-first (wrong), rows 1 and 2 swap, 0xAA and 0x55 patterns flip,")
print("and the diagonal steps LEFT by 8 px (wraps around each byte).")

# Optional PNG preview
try:
    from PIL import Image
    VSCALE = 4
    img = Image.new('RGB', (PX_W, ROWS * VSCALE), (0, 20, 0))
    pix = img.load()
    for row in range(ROWS):
        for col in range(COLS):
            byte = buf[byte_addr(row, col)]
            for bit in range(8):
                px = col * 8 + (7 - bit)   # MSB-first
                lit = bool(byte & (1 << bit))
                color = (0, 231, 0) if lit else (0, 20, 0)
                for y in range(row * VSCALE, (row+1) * VSCALE):
                    pix[px, y] = color
    img.save('tmp/TESTGFX_expected.png')
    print("Written tmp/TESTGFX_expected.png (expected MSB-first render)")
except ImportError:
    pass
