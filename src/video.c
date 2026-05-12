// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* video.c – Smaky 6 display: alpha + graphic planes, SDL2 render */
#include "machine_internal.h"
#include "video.h"
#include "memory.h"
#include "chargen_rom.h"

#include <Z80.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Direct field access: struct Smaky6 fully visible via machine_internal.h */

/* ── Init / fini ────────────────────────────────────────────────────────── */

void video_init(struct Smaky6 *m, SDL_Window *win, SDL_Renderer *ren)
{
    (void)win;
    m->vid.ren        = ren;
    m->vid.mode       = VMODE_ALPHA;
    m->vid.display_on  = 1;
    /* Graphic plane: nibble-interleaved. Each byte high nibble → even scan line,
     * low nibble → odd scan line. Within each nibble bit 3 (MSB) = leftmost pixel.
     * Confirmed from NATHALIE.IM (real hardware image file). */
    m->vid.gfx_msb_first = 1;  /* MSB of nibble = leftmost pixel */

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

    /* Phosphor persistence buffer — heap-allocated to avoid stack pressure.
     * calloc initialises every element to 0.0 (dark screen at startup). */
    m->vid.phosphor_buf   = calloc((size_t)(VIDEO_PX_W * VIDEO_ASPECT_H), sizeof(float));
    m->vid.phosphor_decay = PHOSPHOR_DECAY_DEFAULT;
    if (!m->vid.phosphor_buf)
        fprintf(stderr, "video: phosphor buffer allocation failed; persistence disabled\n");
}

void video_fini(struct Smaky6 *m)
{
    if (m->vid.tex) { SDL_DestroyTexture(m->vid.tex); m->vid.tex = NULL; }
    free(m->vid.phosphor_buf);
    m->vid.phosphor_buf = NULL;
}

void video_set_phosphor_decay(struct Smaky6 *m, float decay)
{
    m->vid.phosphor_decay = decay;
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

    const uint32_t LIT = (m->vid.phosphor == PHOSPHOR_WHITE) ? VIDEO_COLOR_LIT_WHITE : VIDEO_COLOR_LIT;
    const uint32_t BG  = (m->vid.phosphor == PHOSPHOR_WHITE) ? VIDEO_COLOR_BG_WHITE  : VIDEO_COLOR_BG;

    /* Always fill with the phosphor background colour.
     * When display_off, pixels stays all-BG; the phosphor block below decays
     * the persistence buffer without adding any new lit pixels. */
    for (int i = 0; i < VIDEO_PX_W * VIDEO_ASPECT_H; i++)
        pixels[i] = BG;

    /* Render alpha / graphic planes only while the display is actually on.
     * When blanked (display_off=0), the all-BG fill above feeds the decay. */
    if (m->vid.display_on) {

    /* Respect the selected display mode. Unconditionally compositing the
     * graphic plane (0x4600–0x54FF) in alpha mode shows uninitialised RAM
     * as pixel noise while the machine is in text-only operation. */

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
         * Nibble-interleaved layout (confirmed from NATHALIE.IM + hardware hint):
         *   Each byte: high nibble → 4 pixels on even scan line (pair*2)
         *              low  nibble → 4 pixels on odd  scan line (pair*2+1)
         * Native resolution: 256×120 (64 bytes × 4 px/nibble, 60 pairs × 2 lines)
         * Output: 512×480 — each native pixel 2× wide, each scan line 4× tall. */
        for (int pair = 0; pair < VIDEO_SCAN_LINES; pair++) {
            int y_even0 = pair * 8;       /* output rows for even scan line */
            int y_odd0  = pair * 8 + 4;   /* output rows for odd  scan line */
            for (int col = 0; col < 64; col++) {
                uint16_t addr = (uint16_t)(MEM_GFX_BASE + pair * 64 + col);
                uint8_t  byte = memory_read(m, addr);
                uint8_t  hi   = byte >> 4;
                uint8_t  lo   = byte & 0x0Fu;
                for (int bit = 0; bit < 4; bit++) {
                    int px_nat = col * 4 + (m->vid.gfx_msb_first ? (3 - bit) : bit);
                    int ox0    = px_nat * 2;   /* 2× horizontal stretch */
                    if (ox0 + 1 >= VIDEO_PX_W) continue;
                    if ((hi >> bit) & 1u) {
                        for (int y = y_even0; y < y_even0 + 4; y++) {
                            pixels[y * VIDEO_PX_W + ox0]     = LIT;
                            pixels[y * VIDEO_PX_W + ox0 + 1] = LIT;
                        }
                    }
                    if ((lo >> bit) & 1u) {
                        for (int y = y_odd0; y < y_odd0 + 4; y++) {
                            pixels[y * VIDEO_PX_W + ox0]     = LIT;
                            pixels[y * VIDEO_PX_W + ox0 + 1] = LIT;
                        }
                    }
                }
            }
        }
    }
    } /* if (m->vid.display_on) */

    /* ── Phosphor persistence ─────────────────────────────────────────────────
     * Emulates P31 green phosphor remanence.  Lit pixels snap to full brightness
     * (1.0); dark or blanked pixels decay by phosphor_decay each 20 ms frame.
     *
     * With the default decay of 0.70, a pixel that was lit on frame N still
     * glows at 70% on frame N+1 even if the display is fully blanked for that
     * frame.  This eliminates the 25 Hz flash produced by the SAMOS two-stage
     * ISR: Stage 1 (frame N) blanks the display while updating the framebuffer;
     * Stage 2 (frame N+1) unblanks it.  On real hardware the P31 phosphor
     * (~25 ms to 10% decay) makes this entirely imperceptible to the viewer.
     *
     * Skipped when no_display_off is active (display is always on). */
    if (m->vid.phosphor_buf && !m->vid.no_display_off) {
        float       *pb    = m->vid.phosphor_buf;
        const float  decay = m->vid.phosphor_decay;
        const float  inv   = 1.0f / 256.0f;  /* clamp sub-LSB values to 0 */

        if (m->vid.phosphor == PHOSPHOR_WHITE) {
            /* White: LIT R=G=B=0xE8=232, BG R=G=B=0x08=8, delta=224 */
            for (int i = 0; i < VIDEO_PX_W * VIDEO_ASPECT_H; i++) {
                float p = (pixels[i] == VIDEO_COLOR_LIT_WHITE) ? 1.0f
                        : pb[i] * decay;
                if (p < inv) p = 0.0f;
                pb[i] = p;
                uint8_t v = (uint8_t)(8.0f + 224.0f * p);
                pixels[i] = 0xFF000000u
                           | ((uint32_t)v << 16) | ((uint32_t)v << 8) | v;
            }
        } else {
            /* Green P31: LIT G=0xE7=231, BG G=0x08=8, delta=223; R=B=0 */
            for (int i = 0; i < VIDEO_PX_W * VIDEO_ASPECT_H; i++) {
                float p = (pixels[i] == VIDEO_COLOR_LIT) ? 1.0f
                        : pb[i] * decay;
                if (p < inv) p = 0.0f;
                pb[i] = p;
                uint8_t g = (uint8_t)(8.0f + 223.0f * p);
                pixels[i] = 0xFF000000u | ((uint32_t)g << 8);
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
    /* Background for the LED strip */
    SDL_SetRenderDrawColor(ren, 72, 68, 64, 255);
    SDL_Rect bar = { 0, VIDEO_ASPECT_H, VIDEO_WIN_W, VIDEO_LED_H };
    SDL_RenderFillRect(ren, &bar);
    /* Separator line */
    SDL_SetRenderDrawColor(ren, 130, 125, 115, 255);
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
        /* DX0 starts at x=4; DX1 starts at the midpoint of the space left of the BREAK button */
        int lx = (d == 0) ? 4 : ((4 + VIDEO_SYS_NMI_X) / 2);

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

    /* ── RESET / BREAK buttons (right end of disk status bar) ────────────── */
    {
        /* Get logical mouse position for hover highlight */
        float mx_f = -1, my_f = -1;
        { int wx, wy; SDL_GetMouseState(&wx, &wy);
          SDL_RenderWindowToLogical(ren, wx, wy, &mx_f, &my_f); }
        int smx = (int)mx_f, smy = (int)my_f;

        static const struct { const char *label; int x; } SYSBTNS[2] = {
            { "BREAK", VIDEO_SYS_NMI_X },
            { "RESET", VIDEO_SYS_RST_X },
        };
        /* Auto-expire the reset_armed state after 3 seconds */
        if (m->vid.reset_armed && SDL_GetTicks() - m->vid.reset_armed_at > 3000)
            m->vid.reset_armed = 0;

        for (int i = 0; i < 2; i++) {
            int bx = SYSBTNS[i].x;
            int by = VIDEO_SYS_BTN_Y;
            int bw = (i == 1) ? VIDEO_SYS_RST_W : VIDEO_SYS_BTN_W;
            int bh = VIDEO_SYS_BTN_H;
            int hover = (smx >= bx && smx < bx + bw && smy >= by && smy < by + bh);
            /* RESET button (index 1) blinks orange when armed */
            int armed = (i == 1) && m->vid.reset_armed;
            int blink_on = armed && ((SDL_GetTicks() / 200) & 1);

            /* Fill: orange blinking when armed (RESET), brown for BREAK, red for RESET */
            if (armed) {
                if (blink_on)
                    SDL_SetRenderDrawColor(ren, 255, 160,   0, 255);
                else
                    SDL_SetRenderDrawColor(ren, 180,  80,   0, 255);
            } else if (i == 0) {
                /* BREAK button — brown base */
                SDL_SetRenderDrawColor(ren, hover ? 180 : 140, hover ? 100 : 70, hover ? 40 : 20, 255);
            } else if (hover) {
                SDL_SetRenderDrawColor(ren, 255,  60,  60, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 200,  20,  20, 255);
            }
            SDL_Rect btn = { bx, by, bw, bh };
            SDL_RenderFillRect(ren, &btn);

            /* Border: bright yellow when armed, tan for BREAK, pink for RESET */
            if (armed)
                SDL_SetRenderDrawColor(ren, 255, 220,  80, 255);
            else if (i == 0)
                SDL_SetRenderDrawColor(ren, 200, 150,  80, 255);
            else
                SDL_SetRenderDrawColor(ren, 255, 120, 120, 255);
            SDL_RenderDrawRect(ren, &btn);

            /* Label — centred */
            int label_len = (int)strlen(SYSBTNS[i].label);
            int tx = bx + (bw - label_len * 9) / 2;
            int ty = by + (bh - 8) / 2;
            SDL_SetRenderDrawColor(ren, 220, 190, 175, 255);
            for (int ci = 0; SYSBTNS[i].label[ci]; ci++) {
                uint8_t gc = (uint8_t)SYSBTNS[i].label[ci];
                for (int sl = 0; sl < 8; sl++) {
                    uint8_t bits = m->vid.chargen[gc * 16 + sl];
                    for (int b = 0; b < 8; b++) {
                        if (bits & (1u << b))
                            SDL_RenderDrawPoint(ren, tx + ci * 9 + b, ty + sl);
                    }
                }
            }
        }
    }

    /* ── Function-key button bar ────────────────────────────────────────── */
    {
        static const struct { const char *label; uint8_t bit; } FKEYS[7] = {
            { "CURSOR", 0x10 }, { "COPY",   0x08 }, { "KILL",   0x40 },
            { "PROGRA", 0x20 }, { "SHOW",   0x04 }, { "SEARCH", 0x02 },
            { "CHANGE", 0x01 },
        };

        /* Get logical mouse position for hover highlight */
        float mx_f = -1, my_f = -1;
        { int wx, wy; SDL_GetMouseState(&wx, &wy);
          SDL_RenderWindowToLogical(ren, wx, wy, &mx_f, &my_f); }
        int fmx = (int)mx_f, fmy = (int)my_f;

        /* Bar background */
        SDL_SetRenderDrawColor(ren, 62, 48, 44, 255);
        SDL_Rect fbar = { 0, VIDEO_FKEY_Y, VIDEO_WIN_W, VIDEO_FKEY_H };
        SDL_RenderFillRect(ren, &fbar);
        /* Top separator */
        SDL_SetRenderDrawColor(ren, 120, 90, 80, 255);
        SDL_RenderDrawLine(ren, 0, VIDEO_FKEY_Y, VIDEO_WIN_W - 1, VIDEO_FKEY_Y);

        for (int i = 0; i < 7; i++) {
            int bx = VIDEO_FKEY_BTN_X0 + i * (VIDEO_FKEY_BTN_W + VIDEO_FKEY_BTN_GAP);
            int by = VIDEO_FKEY_Y + 1;
            int bw = VIDEO_FKEY_BTN_W;
            int bh = VIDEO_FKEY_BTN_H;

            int active  = (m->kbd.fonct_bits    & FKEYS[i].bit) != 0;
            int latched = (m->kbd.fonct_latched & FKEYS[i].bit) != 0;
            int hover   = (fmx >= bx && fmx < bx + bw && fmy >= by && fmy < by + bh);

            /* Button fill: yellow=latched, red=held, dim=idle */
            if (latched)
                SDL_SetRenderDrawColor(ren, 160, 130,   0, 255);
            else if (active)
                SDL_SetRenderDrawColor(ren, 200,  30,  30, 255);
            else if (hover)
                SDL_SetRenderDrawColor(ren, 130,  55,  45, 255);
            else
                SDL_SetRenderDrawColor(ren, 100,  38,  30, 255);
            SDL_Rect btn = { bx, by, bw, bh };
            SDL_RenderFillRect(ren, &btn);

            /* Button border: bright yellow=latched, bright red=active, dim=idle */
            if (latched)
                SDL_SetRenderDrawColor(ren, 255, 220,  60, 255);
            else if (active)
                SDL_SetRenderDrawColor(ren, 255,  80,  80, 255);
            else
                SDL_SetRenderDrawColor(ren, 130,  50,  50, 255);
            SDL_RenderDrawRect(ren, &btn);

            /* Label colour */
            int label_len = (int)strlen(FKEYS[i].label);
            int tx = bx + (bw - label_len * 9) / 2;
            int ty = by + (bh - 8) / 2;
            if (latched)
                SDL_SetRenderDrawColor(ren, 255, 255, 180, 255);
            else if (active)
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            else if (hover)
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            else
                SDL_SetRenderDrawColor(ren, 220, 190, 175, 255);
            for (int ci = 0; FKEYS[i].label[ci]; ci++) {
                uint8_t gc = (uint8_t)FKEYS[i].label[ci];
                for (int sl = 0; sl < 8; sl++) {
                    uint8_t row_bits = m->vid.chargen[gc * 16 + sl];
                    for (int b = 0; b < 8; b++) {
                        if (row_bits & (1u << b))
                            SDL_RenderDrawPoint(ren, tx + ci * 9 + b, ty + sl);
                    }
                }
            }
        }
    }

    SDL_RenderPresent(ren);
}
