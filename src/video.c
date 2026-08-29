// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* video.c – Smaky 6 display: alpha + graphic planes, SDL2 render */
#include "machine_internal.h"
#include "video.h"
#include "memory.h"
#include "chargen_rom.h"
#include "platform.h"

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

/* Update the persistence-decay factor used when the phosphor buffer is active. */
void video_set_phosphor_decay(struct Smaky6 *m, float decay)
{
    m->vid.phosphor_decay = decay;
}

/* Load the external chargen ROM when available, otherwise fall back to the
 * built-in synthetic font image. */
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

/* Switch the emulated video mode latched by port 0x00 writes. */
void video_set_mode(struct Smaky6 *m, VideoMode mode)
{
    m->vid.mode = mode;
}

/* Select whether graphics nibble bits are interpreted MSB-first or LSB-first. */
void video_set_gfx_msb_first(struct Smaky6 *m, int on)
{
    m->vid.gfx_msb_first = on ? 1 : 0;
}

/* ── Render ─────────────────────────────────────────────────────────────── */

/* Render one complete video frame, including phosphor persistence and UI bars. */
void video_render(struct Smaky6 *m)
{
    VideoMode     mode = m->vid.mode;
    uint8_t      *cg   = m->vid.chargen;

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
                uint8_t  cell = memory_read(m, addr);
                uint8_t  code = cell & 0x7Fu;
                int      inverse = (cell & 0x80u) != 0;
                int      px0   = col * 8;

                /* Two-level exact mapping for the alpha plane.
                 * ASPECT_H=480=240×2, so every level is exact integer math:
                 * Level 1: char row r → output lines [r*24, (r+1)*24).
                 * Level 2: scan line sl → output lines [y0, y0+2) within row.
                 * No Bresenham rounding needed; all rows and scan lines are uniform. */
                int row_y0 = row * VIDEO_ASPECT_H / VIDEO_ROWS_CHAR;
                int row_y1 = (row + 1) * VIDEO_ASPECT_H / VIDEO_ROWS_CHAR;
                int rh     = row_y1 - row_y0;  /* 19 or 20 */

                if (inverse) {
                    for (int y = row_y0; y < row_y1; y++) {
                        for (int b = 0; b < 8; b++) {
                            int px = px0 + b;

                            if (px < VIDEO_PX_W) {
                                pixels[y * VIDEO_PX_W + px] = LIT;
                            }
                        }
                    }
                }

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
                                pixels[y * VIDEO_PX_W + px] = inverse ? BG : LIT;
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

    /* Hand the rendered framebuffer to the backend: it uploads to the texture,
     * draws the SDL overlays, and presents.  Core owns only the framebuffer. */
    struct smemu6_frame frame = { pixels, VIDEO_PX_W, VIDEO_ASPECT_H, (int)(VIDEO_PX_W * sizeof(uint32_t)) };
    platform_present(m, &frame);

    /* ── stderr screen dump: emit changed lines ──────────────────────────── */
    for (int row = 0; row < VIDEO_ROWS_CHAR; row++) {
        const uint8_t *cur    = &m->bus[MEM_ALPHA_BASE + row * VIDEO_COLS_CHAR];
        uint8_t       *shadow = &m->vid.shadow_alpha[row * VIDEO_COLS_CHAR];

        if (memcmp(cur, shadow, VIDEO_COLS_CHAR) != 0) {
            memcpy(shadow, cur, VIDEO_COLS_CHAR);

            /* Build a printable version of the row, stripping trailing spaces. */
            char line[VIDEO_COLS_CHAR + 1];
            char inverse_mask[VIDEO_COLS_CHAR + 1];
            int  len = 0;
            int  inverse_len = 0;
            for (int col = 0; col < VIDEO_COLS_CHAR; col++) {
                uint8_t cell = cur[col];
                uint8_t c = cell & 0x7Fu;
                line[col] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
                inverse_mask[col] = (cell & 0x80u) ? '^' : ' ';
                if (line[col] != ' ') len = col + 1;
                if (inverse_mask[col] != ' ') inverse_len = col + 1;
            }
            line[len] = '\0';
            inverse_mask[inverse_len] = '\0';

            if (len > 0 && m->dbg.trace_scr)
                fprintf(stderr, "[scr r%02d] %s\n", row, line);
            if (inverse_len > 0 && m->dbg.trace_scr)
                fprintf(stderr, "[scri r%02d] %s\n", row, inverse_mask);

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

}
