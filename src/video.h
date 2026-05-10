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

/* Configure bitmap byte bit order: 0=LSB-left, 1=MSB-left. */
void video_set_gfx_msb_first(struct Smaky6 *m, int on);

/* Render one frame to the SDL2 window */
void video_render(struct Smaky6 *m);

/*
 * Display geometry
 *   Graphic plane : 512 × 240 px (1 bpp), 64 bytes/row
 *   Alpha plane   : 64 cols × 20 rows, each char 8 px wide × 12 px tall
 *                   20 rows × 12 px = 240 raw lines = VIDEO_PX_H exactly.
 *   Chargen ROM   : TMS2716 / 2716 EPROM, 2048 bytes.
 *                   Layout: 16 bytes per character (rows 0–9 = glyph+descenders,
 *                   rows 10–15 = 0). Bit 0 of each byte = leftmost pixel (LSB-first).
 *
 * Pixel aspect ratio:
 *   The pixel buffer is rendered at 512 × VIDEO_ASPECT_H = 512 × 480.
 *   480 = 240 × 2 exactly, so every raw scan line maps to exactly 2 output
 *   lines and every character row maps to exactly 24 output lines — perfectly
 *   uniform with no banding at any integer display_scale factor.
 *   SDL_RenderCopy is a 1:1 blit; SDL_RenderSetLogicalSize handles window scaling.
 *   AR = 512:480 ≈ 1.067:1 (slightly wider than square).
 *   At display_scale=1: physical window = 512 × (480+12) = 512 × 492 px.
 */
#define VIDEO_COLS_CHAR    64
#define VIDEO_ROWS_CHAR    20
#define VIDEO_CHAR_H       12   /* scan lines per character cell: 20×12 = 240 = VIDEO_PX_H */
#define VIDEO_SCAN_LINES   60    /* lores rows in graphic framebuffer; each row × 4 display lines = 240 */
#define VIDEO_PX_W         512
#define VIDEO_PX_H         240
#define VIDEO_ASPECT_H     480                  /* 2× vertical: 240×2=480; every scan line → 2 output lines, every char row → 24 lines */
#define VIDEO_LED_H        14                  /* status-bar height: 1 floppy row */
#define VIDEO_WIN_W        VIDEO_PX_W
#define VIDEO_WIN_H        (VIDEO_ASPECT_H + VIDEO_LED_H)

/* Phosphor colours (ARGB8888).  P31 green phosphor: lit = #00E700, bg = #000800. */
#define VIDEO_COLOR_LIT    0xFF00E700u
#define VIDEO_COLOR_BG     0xFF000800u

/* White phosphor palette (some Smaky 6 units): lit = #E8E8E8, bg = #080808. */
#define VIDEO_COLOR_LIT_WHITE  0xFFE8E8E8u
#define VIDEO_COLOR_BG_WHITE   0xFF080808u

typedef enum { PHOSPHOR_GREEN = 0, PHOSPHOR_WHITE = 1 } PhosphorColour;

#endif /* VIDEO_H */
