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
 *   Window        : 512 × 240 (native; SDL logical size)
 */
#define VIDEO_COLS_CHAR    64
#define VIDEO_ROWS_CHAR    20
#define VIDEO_SCAN_LINES   240
#define VIDEO_PX_W         512
#define VIDEO_PX_H         240
#define VIDEO_LED_H        12          /* status-bar height for disk LEDs */
#define VIDEO_WIN_W        VIDEO_PX_W
#define VIDEO_WIN_H        (VIDEO_PX_H + VIDEO_LED_H)

#endif /* VIDEO_H */
