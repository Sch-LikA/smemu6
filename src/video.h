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
 *   Alpha plane   : 64 cols × 20 rows, each char 8 px wide × 8 px tall
 *   Chargen ROM   : TMS2716 / 2716 EPROM, 2048 bytes.
 *                   Layout: 16 bytes per character (rows 0–7 = glyph, 8–15 = 0).
 *                   Bit 0 of each byte = leftmost pixel (LSB-first).
 *
 * Pixel aspect ratio:
 *   The real Smaky 6 CRT was ~4:3.  The pixel buffer is rendered directly at
 *   512 × VIDEO_ASPECT_H = 512 × 384 (4:3 frame).  Each raw scan line raw_y
 *   (0–239) is mapped to display lines [raw_y*384/240, (raw_y+1)*384/240)
 *   using integer Bresenham so every row gets either 1 or 2 output lines
 *   with no SDL interpolation artifacts (some rows would otherwise get 6, some
 *   7 output pixels due to the non-integer 1.6× ratio if SDL scaled the
 *   texture).  SDL_RenderCopy is now a 1:1 blit.
 *   At display_scale=2: physical window = 1024 × (384+12)*2 = 1024 × 792 px.
 */
#define VIDEO_COLS_CHAR    64
#define VIDEO_ROWS_CHAR    20
#define VIDEO_SCAN_LINES   60    /* lores rows in graphic framebuffer; each row × 4 display lines = 240 */
#define VIDEO_PX_W         512
#define VIDEO_PX_H         240
#define VIDEO_ASPECT_H     384                  /* 4:3 equivalent: 512*(3/4)=384; 1.6× vertical stretch */
#define VIDEO_LED_H        12                  /* status-bar height (4px pad + 8px chargen glyph) */
#define VIDEO_WIN_W        VIDEO_PX_W
#define VIDEO_WIN_H        (VIDEO_ASPECT_H + VIDEO_LED_H)

/* Phosphor colours (ARGB8888).  P31 green phosphor: lit = #00E700, bg = #000800. */
#define VIDEO_COLOR_LIT    0xFF00E700u
#define VIDEO_COLOR_BG     0xFF000800u

#endif /* VIDEO_H */
