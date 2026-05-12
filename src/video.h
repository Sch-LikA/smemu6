// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* video.h – Smaky 6 display */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <SDL2/SDL.h>

struct Smaky6;

typedef enum {
    VMODE_ALPHA    = 0,  /* Text only  (?IALPHA) */
    VMODE_GRAPHIC  = 1,  /* Bitmap only (?IGRA)  */
    VMODE_SUPER    = 2   /* Superimposed (?IAGRA) */
} VideoMode;

void video_init(struct Smaky6 *m, SDL_Window *win, SDL_Renderer *ren);
void video_fini(struct Smaky6 *m);

/* Load character generator ROM (2716, 2 KB).
 * Falls back to built-in synthetic 74S262 table if path is NULL or missing. */
void video_load_chargen(struct Smaky6 *m, const char *path);

/* Set display mode from emulated I/O port write */
void video_set_mode(struct Smaky6 *m, VideoMode mode);

/* Override graphic nibble bit order: 0=LSB-left, 1=MSB-left (default: MSB). */
void video_set_gfx_msb_first(struct Smaky6 *m, int on);

/* Render one frame to the SDL2 window */
void video_render(struct Smaky6 *m);

/*
 * Display geometry
 *   Graphic plane : nibble-interleaved, native 256×120 px (1 bpp).
 *     Each byte: high nibble (bits 7–4) → 4 pixels on even scan line (pair*2)
 *                low  nibble (bits 3–0) → 4 pixels on odd  scan line (pair*2+1)
 *     Within each nibble: bit 3 (MSB) = leftmost pixel.
 *     64 bytes × 4 px/nibble = 256 native px wide; 60 pairs × 2 = 120 lines.
 *     Rendered 2× wide, 4× tall → 512 × 480 output.
 *   Alpha plane   : 64 cols × 20 rows, each char 8 px wide × 12 px tall
 *                   20 rows × 12 px = 240 raw lines = VIDEO_PX_H exactly.
 *   Chargen ROM   : TMS2716 / 2716 EPROM, 2048 bytes.
 *                   Layout: 16 bytes per character (rows 0–9 = glyph+descenders,
 *                   rows 10–15 = 0). Bit 0 of each byte = leftmost pixel (LSB-first).
 *
 * Pixel aspect ratio:
 *   The pixel buffer is rendered at 512 × VIDEO_ASPECT_H = 512 × 480.
 *   Each native gfx pixel is 2 output px wide × 4 output px tall.
 *   480 = 120 × 4 exactly; every character row maps to 24 output lines.
 *   SDL_RenderCopy is a 1:1 blit; SDL_RenderSetLogicalSize handles window scaling.
 *   AR = 512:480 ≈ 1.067:1 (slightly wider than square).
 *   At display_scale=1: physical window = 512 × (480+12) = 512 × 492 px.
 */
#define VIDEO_COLS_CHAR    64
#define VIDEO_ROWS_CHAR    20
#define VIDEO_CHAR_H       12   /* scan lines per character cell: 20×12 = 240 = VIDEO_PX_H */
#define VIDEO_SCAN_LINES   60    /* byte-pairs in graphic framebuffer (60 pairs × 2 scan lines = 120 native lines) */
#define VIDEO_PX_W         512
#define VIDEO_PX_H         240
#define VIDEO_ASPECT_H     480                  /* native 120 lines × 4 = 480 output lines; each char row → 24 output lines */
#define VIDEO_LED_H        14                  /* disk status-bar height */
#define VIDEO_FKEY_H       14                  /* function-key button bar height */
#define VIDEO_WIN_W        VIDEO_PX_W
#define VIDEO_WIN_H        (VIDEO_ASPECT_H + VIDEO_LED_H + VIDEO_FKEY_H)

/* Function-key button bar geometry (logical pixels) */
#define VIDEO_FKEY_Y       (VIDEO_ASPECT_H + VIDEO_LED_H)   /* y of fkey bar */
#define VIDEO_FKEY_BTN_W   70   /* button width */
#define VIDEO_FKEY_BTN_GAP  3   /* gap between buttons */
#define VIDEO_FKEY_BTN_X0   2   /* left margin */
#define VIDEO_FKEY_BTN_H   12   /* button height inside bar (1px top/bottom pad) */

/* RESET / NMI buttons in the disk status bar (first row).
 * Two buttons placed flush at the right edge of the 512-px bar. */
#define VIDEO_SYS_BTN_W    38   /* NMI button width */
#define VIDEO_SYS_RST_W    50   /* RESET button width (wider — 5 chars) */
#define VIDEO_SYS_BTN_GAP   3   /* gap between the two buttons */
#define VIDEO_SYS_BTN_H    10   /* button height (1px top/bottom pad within LED_H=14) */
#define VIDEO_SYS_BTN_Y    (VIDEO_ASPECT_H + 2)  /* y inside status bar */
/* NMI is leftmost of the pair; RESET is rightmost (far right edge) */
#define VIDEO_SYS_NMI_X    (VIDEO_WIN_W - VIDEO_SYS_BTN_W - VIDEO_SYS_RST_W - VIDEO_SYS_BTN_GAP - 2)
#define VIDEO_SYS_RST_X    (VIDEO_WIN_W - VIDEO_SYS_RST_W - 2)

/* Phosphor colours (ARGB8888).  P31 green phosphor: lit = #00E700, bg = #000800. */
#define VIDEO_COLOR_LIT    0xFF00E700u
#define VIDEO_COLOR_BG     0xFF000800u

/* White phosphor palette (some Smaky 6 units): lit = #E8E8E8, bg = #080808. */
#define VIDEO_COLOR_LIT_WHITE  0xFFE8E8E8u
#define VIDEO_COLOR_BG_WHITE   0xFF080808u

typedef enum { PHOSPHOR_GREEN = 0, PHOSPHOR_WHITE = 1 } PhosphorColour;

/* Default per-frame phosphor decay factor.
 * Approximates P31 medium-persistence green phosphor (~25 ms to 10% decay):
 * after one blank 20 ms frame, a previously-lit pixel retains ~70% brightness.
 * Adjust with -phosphor-decay; set to 0.0 to disable persistence entirely. */
#define PHOSPHOR_DECAY_DEFAULT  0.70f

/* Override the per-frame phosphor decay at runtime (0.0 = instant, 1.0 = infinite). */
void video_set_phosphor_decay(struct Smaky6 *m, float decay);

#endif /* VIDEO_H */
