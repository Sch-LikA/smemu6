/* video.c – Smaky 6 display: alpha + graphic planes, SDL2 render */
#include "machine_internal.h"
#include "video.h"
#include "memory.h"
#include "chargen_rom.h"

#include <Z80.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* Direct field access: struct Smaky6 fully visible via machine_internal.h */

/* ── Init / fini ────────────────────────────────────────────────────────── */

void video_init(struct Smaky6 *m, SDL_Window *win, SDL_Renderer *ren)
{
    (void)win;
    m->vid.ren        = ren;
    m->vid.mode       = VMODE_ALPHA;
    m->vid.display_on  = 1;
    m->vid.gfx_msb_first = 0;

    SDL_Texture *tex = SDL_CreateTexture(ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        VIDEO_PX_W, VIDEO_ASPECT_H);
    if (!tex) {
        fprintf(stderr, "video: SDL_CreateTexture: %s\n", SDL_GetError());
    }
    m->vid.tex = tex;

    memset(m->vid.chargen,      0,    2048);
    memset(m->vid.shadow_alpha, 0x20, sizeof(m->vid.shadow_alpha));
}

void video_fini(struct Smaky6 *m)
{
    if (m->vid.tex) { SDL_DestroyTexture(m->vid.tex); m->vid.tex = NULL; }
}

void video_load_chargen(struct Smaky6 *m, const char *path)
{
    uint8_t *cg = m->vid.chargen;

    if (path) {
        FILE *f = fopen(path, "rb");
        if (f) {
            /* TMS2716 / 2716 EPROM: 2048 bytes, 16 bytes per character.
             * Rows 0–7 hold the glyph; rows 8–15 are unused (zero).
             * Bit 0 of each data byte = leftmost pixel (LSB-first). */
            size_t n = fread(cg, 1, 2048, f);
            fclose(f);
            if (n == 2048) {
                fprintf(stderr, "video: loaded chargen from '%s'\n", path);
                return;
            }
            fprintf(stderr, "video: short chargen read from '%s', using synthetic\n", path);
        }
    }

    for (int i = 0; i < 2048; i++)
        cg[i] = CHARGEN_ROM[i];
    fprintf(stderr, "video: using embedded chargen ROM\n");
}

void video_set_mode(struct Smaky6 *m, VideoMode mode)
{
    m->vid.mode = mode;
}

void video_set_gfx_msb_first(struct Smaky6 *m, int on)
{
    m->vid.gfx_msb_first = on ? 1 : 0;
}

/* ── Render ─────────────────────────────────────────────────────────────── */

void video_render(struct Smaky6 *m)
{
    SDL_Renderer *ren  = m->vid.ren;
    SDL_Texture  *tex  = m->vid.tex;
    VideoMode     mode = m->vid.mode;
    uint8_t      *cg   = m->vid.chargen;

    if (!ren || !tex) return;

    uint32_t pixels[VIDEO_PX_W * VIDEO_ASPECT_H];
    memset(pixels, 0, sizeof(pixels));

    /* Display-off: blank the machine area (status bar still renders below) */
    if (!m->vid.display_on) {
        SDL_Rect machine_dst = { 0, 0, VIDEO_PX_W, VIDEO_ASPECT_H };
        SDL_UpdateTexture(tex, NULL, pixels, VIDEO_PX_W * (int)sizeof(uint32_t));
        SDL_RenderCopy(ren, tex, NULL, &machine_dst);
        /* fall through to status bar rendering */
        goto render_status_bar;
    }

    const uint32_t LIT = (m->vid.phosphor == PHOSPHOR_WHITE) ? VIDEO_COLOR_LIT_WHITE : VIDEO_COLOR_LIT;
    const uint32_t BG  = (m->vid.phosphor == PHOSPHOR_WHITE) ? VIDEO_COLOR_BG_WHITE  : VIDEO_COLOR_BG;

    /* Fill with phosphor background colour */
    for (int i = 0; i < VIDEO_PX_W * VIDEO_ASPECT_H; i++)
        pixels[i] = BG;

    /* Respect the selected display mode. Unconditionally compositing the
     * bitmap plane makes the OS workspace at 0x4500 look like random screen
     * corruption while the machine is in text mode. */

    if (mode != VMODE_GRAPHIC) {
        /* ── Alpha plane ─────────────────────────────────────────────────── */
        for (int row = 0; row < VIDEO_ROWS_CHAR; row++) {
            for (int col = 0; col < VIDEO_COLS_CHAR; col++) {
                uint16_t addr = (uint16_t)(MEM_ALPHA_BASE + row * VIDEO_COLS_CHAR + col);
                uint8_t  code = memory_read(m, addr) & 0x7Fu;
                int      px0   = col * 8;

                /* Two-level exact mapping for the alpha plane.
                 * ASPECT_H=480=240×2, so every level is exact integer math:
                 * Level 1: char row r → output lines [r*24, (r+1)*24).
                 * Level 2: scan line sl → output lines [y0, y0+2) within row.
                 * No Bresenham rounding needed; all rows and scan lines are uniform. */
                int row_y0 = row * VIDEO_ASPECT_H / VIDEO_ROWS_CHAR;
                int row_y1 = (row + 1) * VIDEO_ASPECT_H / VIDEO_ROWS_CHAR;
                int rh     = row_y1 - row_y0;  /* 19 or 20 */

                for (int sl = 0; sl < VIDEO_CHAR_H; sl++) {
                    /* TMS2716 layout: 16 bytes/char; rows 0-9 hold glyph+descenders,
                     * rows 10-11 are blank spacing (always 0 in ROM).
                     * Bit 0 = leftmost pixel (LSB-first serial output). */
                    uint8_t bits = cg[code * 16 + sl];
                    int y0 = row_y0 + sl * rh / VIDEO_CHAR_H;
                    int y1 = row_y0 + (sl + 1) * rh / VIDEO_CHAR_H;
                    for (int b = 0; b < 8; b++) {
                        int px = px0 + b;
                        if (px < VIDEO_PX_W && (bits & (1u << b))) {
                            for (int y = y0; y < y1; y++)
                                pixels[y * VIDEO_PX_W + px] = LIT;
                        }
                    }
                }
            }
        }
    }

    if (mode != VMODE_ALPHA) {
        /* ── Graphic plane ───────────────────────────────────────────────── *
         * 60 lores rows × 4 raw scan lines = 240 raw lines → 480 output lines.
         * Each lores row maps to exactly 8 output lines (480/60=8, exact). */
        for (int row = 0; row < VIDEO_SCAN_LINES; row++) {
            /* Each lores row spans 4 raw scan lines → exactly 8 output lines. */
            int y0 = row * 4 * VIDEO_ASPECT_H / VIDEO_PX_H;
            int y1 = (row * 4 + 4) * VIDEO_ASPECT_H / VIDEO_PX_H;
            for (int col = 0; col < 64; col++) {
                uint16_t addr = (uint16_t)(MEM_GFX_BASE + row * 64 + col);
                uint8_t  byte = memory_read(m, addr);
                for (int bit = 0; bit < 8; bit++) {
                    int px = col * 8 + (m->vid.gfx_msb_first ? (7 - bit) : bit);
                    if ((byte & (1u << bit)) && px < VIDEO_PX_W) {
                        for (int y = y0; y < y1; y++)
                            pixels[y * VIDEO_PX_W + px] = LIT;
                    }
                }
            }
        }
    }

    SDL_UpdateTexture(tex, NULL, pixels, VIDEO_PX_W * (int)sizeof(uint32_t));

    /* ── stderr screen dump: emit changed lines ──────────────────────────── */
    for (int row = 0; row < VIDEO_ROWS_CHAR; row++) {
        const uint8_t *cur    = &m->bus[MEM_ALPHA_BASE + row * VIDEO_COLS_CHAR];
        uint8_t       *shadow = &m->vid.shadow_alpha[row * VIDEO_COLS_CHAR];

        if (memcmp(cur, shadow, VIDEO_COLS_CHAR) != 0) {
            memcpy(shadow, cur, VIDEO_COLS_CHAR);

            /* Build a printable version of the row, stripping trailing spaces */
            char line[VIDEO_COLS_CHAR + 1];
            int  len = 0;
            for (int col = 0; col < VIDEO_COLS_CHAR; col++) {
                uint8_t c = cur[col] & 0x7Fu;
                line[col] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
                if (line[col] != ' ') len = col + 1;
            }
            line[len] = '\0';

            if (len > 0 && m->dbg.trace_scr)
                fprintf(stderr, "[scr r%02d] %s\n", row, line);

            if (row <= 3 && strncmp(line, "ERROR", 5) == 0) {
                fprintf(stderr,
                        "[screrr] pc=%04X af=%04X bc=%04X de=%04X hl=%04X "
                        "k=%02X/%d w54=%02X w55=%02X w56=%02X w57=%02X\n",
                        (unsigned)Z80_PC(m->cpu),
                        (unsigned)Z80_AF(m->cpu),
                        (unsigned)Z80_BC(m->cpu),
                        (unsigned)Z80_DE(m->cpu),
                        (unsigned)Z80_HL(m->cpu),
                        m->kbd.key_code,
                        m->kbd.found,
                        m->bus[0x4554],
                        m->bus[0x4555],
                        m->bus[0x4556],
                        m->bus[0x4557]);
            }
        }
    }

    SDL_RenderClear(ren);

    /* Render machine content (512×240) at 1:1 into the logical window.
     * The physical window is VIDEO_WIN_H * display_scale pixels tall, giving
     * integer-scaled output.  No explicit vertical stretch is applied here —
     * the scale factor already enlarges the output proportionally. */
    SDL_Rect machine_dst = { 0, 0, VIDEO_PX_W, VIDEO_ASPECT_H };
    SDL_RenderCopy(ren, tex, NULL, &machine_dst);

    /* ── CRT scanline overlay ────────────────────────────────────────────── *
     * Draw a 50%-transparent black rectangle 1 logical pixel tall over every
     * other output row, mimicking the dark gaps between phosphor scan lines.
     * The SDL logical size is VIDEO_PX_W × VIDEO_ASPECT_H; integer rows here
     * correspond directly to the output pixels before display_scale is applied. */
    if (m->vid.scanlines && m->vid.display_on) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 128);
        SDL_Rect line = { 0, 0, VIDEO_PX_W, 1 };
        for (int y = 1; y < VIDEO_ASPECT_H; y += 2) {
            line.y = y;
            SDL_RenderFillRect(ren, &line);
        }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    }

    /* ── Status bar: disk activity + track/sector ───────────────────────── */
render_status_bar:
    /* Grey background for the LED strip */
    SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
    SDL_Rect bar = { 0, VIDEO_ASPECT_H, VIDEO_WIN_W, VIDEO_LED_H };
    SDL_RenderFillRect(ren, &bar);
    /* Separator line */
    SDL_SetRenderDrawColor(ren, 90, 90, 90, 255);
    SDL_RenderDrawLine(ren, 0, VIDEO_ASPECT_H, VIDEO_WIN_W - 1, VIDEO_ASPECT_H);

    /* Two drive slots: LED + label + track number.
     * Glyphs are rendered 1:1 from chargen (8×8 logical px); the display_scale
     * factor applied to the SDL window makes them crisp at any scale value.
     * Slot width = 256 logical px (two equal halves of 512-wide window).
     *
     * Row 1 (y=VIDEO_ASPECT_H+2):  floppy drives DX0 / DX1
     * Row 2 (y=VIDEO_ASPECT_H+16): Winchester drives HD0 / HD1 */
    static const char *dx_label[2] = { "DX0:", "DX1:" };
    const int ly = VIDEO_ASPECT_H + 2;   /* glyph y */
    for (int d = 0; d < 2; d++) {
        int lx = 4 + d * 256;   /* slot left edge; two 256-px halves */

        int is_hd     = m->win.image[d] != NULL;
        int mounted   = is_hd ? 1 : (m->fdc.image[d] != NULL);
        int active    = is_hd ? (m->win.disk_active[d] > 0)
                               : (m->fdc.disk_active[d] > 0);

        /* LED */
        SDL_Rect led = { lx, ly, 8, 8 };
        if (is_hd) {
            if (active)       SDL_SetRenderDrawColor(ren, 255,  80,   0, 255); /* orange-red */
            else if (mounted) SDL_SetRenderDrawColor(ren,  60,  20,   0, 255); /* dim red */
            else              SDL_SetRenderDrawColor(ren,  20,  20,  20, 255); /* off */
        } else {
            if (active)       SDL_SetRenderDrawColor(ren, 255, 140,   0, 255); /* amber */
            else if (mounted) SDL_SetRenderDrawColor(ren,  55,  30,   0, 255); /* dim amber */
            else              SDL_SetRenderDrawColor(ren,  20,  20,  20, 255); /* off */
        }
        SDL_RenderFillRect(ren, &led);
        SDL_SetRenderDrawColor(ren, 70, 70, 70, 255);
        SDL_RenderDrawRect(ren, &led);

        /* Label (DX0: / DX1:) */
        int tx = lx + 12;
        SDL_SetRenderDrawColor(ren, is_hd ? 0 : 0, is_hd ? 180 : 200, is_hd ? 180 : 0, 255);
        for (int ci = 0; dx_label[d][ci]; ci++) {
            uint8_t code = (uint8_t)dx_label[d][ci];
            for (int sl = 0; sl < 8; sl++) {
                uint8_t bits = m->vid.chargen[code * 16 + sl];
                for (int b = 0; b < 8; b++) {
                    if (bits & (1u << b))
                        SDL_RenderDrawPoint(ren, tx + ci * 9 + b, ly + sl);
                }
            }
        }

        /* Info text */
        if (mounted) {
            char info[20];
            if (is_hd) {
                snprintf(info, sizeof(info), "C:%u H:%u S:%02u",
                         (unsigned)m->win.last_cyl[d],
                         (unsigned)m->win.last_head[d],
                         (unsigned)(m->win.sector_num & 0x1Fu));
                SDL_SetRenderDrawColor(ren, 0, 140, 140, 255);
            } else {
                snprintf(info, sizeof(info), "T:%02d S:%02d",
                         m->fdc.track[d], m->fdc.phased_sector[d]);
                SDL_SetRenderDrawColor(ren, 0, 170, 0, 255);
            }
            int ix = tx + 38;
            for (int ci = 0; info[ci]; ci++) {
                uint8_t gc = (uint8_t)info[ci];
                for (int sl = 0; sl < 8; sl++) {
                    uint8_t bits = m->vid.chargen[gc * 16 + sl];
                    for (int b = 0; b < 8; b++) {
                        if (bits & (1u << b))
                            SDL_RenderDrawPoint(ren, ix + ci * 9 + b, ly + sl);
                    }
                }
            }
        }
    }

    SDL_RenderPresent(ren);
}
