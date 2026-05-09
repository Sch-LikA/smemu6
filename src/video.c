/* video.c – Smaky 6 display: alpha + graphic planes, SDL2 render */
#include "machine_internal.h"
#include "video.h"
#include "memory.h"

#include <Z80.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/*
 * Synthetic TI 74S262 character generator fallback.
 * The 74S262 produces a 5×7 dot matrix in an 7×8 cell.
 * Here we store a compact 4-px-wide × 8-px-tall version (Smaky display cell).
 * Each entry is 8 bytes; each byte holds the 4 pixel bits in bits[3:0].
 * Entries 0x00–0x7F cover standard ASCII.
 */
static const uint8_t CHARGEN_SYNTHETIC[128][8] = {
    /* 0x00 – space and control chars (blank cells) */
    [0x00] = {0,0,0,0,0,0,0,0},
    [0x20] = {0,0,0,0,0,0,0,0}, /* SPACE */
    /* A-Z */
    [0x41] = {0x6,0x9,0x9,0xF,0x9,0x9,0x9,0}, /* A */
    [0x42] = {0xE,0x9,0x9,0xE,0x9,0x9,0xE,0}, /* B */
    [0x43] = {0x6,0x9,0x8,0x8,0x8,0x9,0x6,0}, /* C */
    [0x44] = {0xE,0x9,0x9,0x9,0x9,0x9,0xE,0}, /* D */
    [0x45] = {0xF,0x8,0x8,0xE,0x8,0x8,0xF,0}, /* E */
    [0x46] = {0xF,0x8,0x8,0xE,0x8,0x8,0x8,0}, /* F */
    [0x47] = {0x6,0x9,0x8,0xB,0x9,0x9,0x6,0}, /* G */
    [0x48] = {0x9,0x9,0x9,0xF,0x9,0x9,0x9,0}, /* H */
    [0x49] = {0xF,0x2,0x2,0x2,0x2,0x2,0xF,0}, /* I */
    [0x4A] = {0x7,0x1,0x1,0x1,0x1,0x9,0x6,0}, /* J */
    [0x4B] = {0x9,0xA,0xC,0xC,0xA,0x9,0x9,0}, /* K */
    [0x4C] = {0x8,0x8,0x8,0x8,0x8,0x8,0xF,0}, /* L */
    [0x4D] = {0x9,0xF,0xF,0x9,0x9,0x9,0x9,0}, /* M */
    [0x4E] = {0x9,0xD,0xD,0xB,0xB,0x9,0x9,0}, /* N */
    [0x4F] = {0x6,0x9,0x9,0x9,0x9,0x9,0x6,0}, /* O */
    [0x50] = {0xE,0x9,0x9,0xE,0x8,0x8,0x8,0}, /* P */
    [0x51] = {0x6,0x9,0x9,0x9,0xB,0x9,0x7,0}, /* Q */
    [0x52] = {0xE,0x9,0x9,0xE,0xA,0x9,0x9,0}, /* R */
    [0x53] = {0x6,0x9,0x8,0x6,0x1,0x9,0x6,0}, /* S */
    [0x54] = {0xF,0x2,0x2,0x2,0x2,0x2,0x2,0}, /* T */
    [0x55] = {0x9,0x9,0x9,0x9,0x9,0x9,0x6,0}, /* U */
    [0x56] = {0x9,0x9,0x9,0x9,0x9,0x6,0x6,0}, /* V */
    [0x57] = {0x9,0x9,0x9,0x9,0xF,0xF,0x9,0}, /* W */
    [0x58] = {0x9,0x9,0x6,0x6,0x6,0x9,0x9,0}, /* X */
    [0x59] = {0x9,0x9,0x6,0x2,0x2,0x2,0x2,0}, /* Y */
    [0x5A] = {0xF,0x1,0x2,0x4,0x8,0x8,0xF,0}, /* Z */
    /* 0-9 */
    [0x30] = {0x6,0x9,0xB,0xD,0x9,0x9,0x6,0}, /* 0 */
    [0x31] = {0x2,0x6,0x2,0x2,0x2,0x2,0x7,0}, /* 1 */
    [0x32] = {0x6,0x9,0x1,0x2,0x4,0x8,0xF,0}, /* 2 */
    [0x33] = {0xF,0x1,0x2,0x6,0x1,0x9,0x6,0}, /* 3 */
    [0x34] = {0x1,0x3,0x5,0x9,0xF,0x1,0x1,0}, /* 4 */
    [0x35] = {0xF,0x8,0xE,0x1,0x1,0x9,0x6,0}, /* 5 */
    [0x36] = {0x3,0x4,0x8,0xE,0x9,0x9,0x6,0}, /* 6 */
    [0x37] = {0xF,0x1,0x2,0x4,0x4,0x4,0x4,0}, /* 7 */
    [0x38] = {0x6,0x9,0x9,0x6,0x9,0x9,0x6,0}, /* 8 */
    [0x39] = {0x6,0x9,0x9,0x7,0x1,0x2,0xC,0}, /* 9 */
    /* Basic punctuation */
    [0x21] = {0x2,0x2,0x2,0x2,0x2,0x0,0x2,0}, /* ! */
    [0x2E] = {0x0,0x0,0x0,0x0,0x0,0x0,0x2,0}, /* . */
    [0x2C] = {0x0,0x0,0x0,0x0,0x2,0x2,0x4,0}, /* , */
    [0x3F] = {0x6,0x9,0x1,0x2,0x2,0x0,0x2,0}, /* ? */
    [0x3A] = {0x0,0x2,0x0,0x0,0x2,0x0,0x0,0}, /* : */
    [0x2D] = {0x0,0x0,0x0,0xF,0x0,0x0,0x0,0}, /* - */
    [0x2B] = {0x0,0x2,0x2,0xF,0x2,0x2,0x0,0}, /* + */
    [0x3D] = {0x0,0x0,0xF,0x0,0xF,0x0,0x0,0}, /* = */
    [0x28] = {0x1,0x2,0x4,0x4,0x4,0x2,0x1,0}, /* ( */
    [0x29] = {0x8,0x4,0x2,0x2,0x2,0x4,0x8,0}, /* ) */
    /* lowercase — distinct from uppercase (proper descender forms where practical) */
    [0x61]={0x0,0x0,0x7,0x9,0x9,0xF,0x9,0},  /* a */
    [0x62]={0x8,0x8,0xE,0x9,0x9,0x9,0xE,0},  /* b */
    [0x63]={0x0,0x0,0x6,0x9,0x8,0x9,0x6,0},  /* c */
    [0x64]={0x1,0x1,0x7,0x9,0x9,0x9,0x7,0},  /* d */
    [0x65]={0x0,0x0,0x6,0x9,0xF,0x8,0x6,0},  /* e */
    [0x66]={0x3,0x4,0x4,0xE,0x4,0x4,0x4,0},  /* f */
    [0x67]={0x0,0x0,0x7,0x9,0x9,0x7,0x1,0x6},/* g — descender */
    [0x68]={0x8,0x8,0xE,0x9,0x9,0x9,0x9,0},  /* h */
    [0x69]={0x2,0x0,0x6,0x2,0x2,0x2,0x7,0},  /* i */
    [0x6A]={0x1,0x0,0x3,0x1,0x1,0x9,0x6,0},  /* j */
    [0x6B]={0x8,0x8,0x9,0xA,0xC,0xA,0x9,0},  /* k */
    [0x6C]={0x6,0x2,0x2,0x2,0x2,0x2,0x7,0},  /* l */
    [0x6D]={0x0,0x0,0xF,0x9,0x9,0x9,0x9,0},  /* m */
    [0x6E]={0x0,0x0,0xE,0x9,0x9,0x9,0x9,0},  /* n */
    [0x6F]={0x0,0x0,0x6,0x9,0x9,0x9,0x6,0},  /* o */
    [0x70]={0x0,0x0,0xE,0x9,0x9,0xE,0x8,0x8},/* p — descender */
    [0x71]={0x0,0x0,0x7,0x9,0x9,0x7,0x1,0x1},/* q — descender */
    [0x72]={0x0,0x0,0xB,0xC,0x8,0x8,0x8,0},  /* r */
    [0x73]={0x0,0x0,0x6,0x8,0x6,0x1,0xE,0},  /* s */
    [0x74]={0x4,0x4,0xF,0x4,0x4,0x4,0x3,0},  /* t */
    [0x75]={0x0,0x0,0x9,0x9,0x9,0x9,0x6,0},  /* u */
    [0x76]={0x0,0x0,0x9,0x9,0x9,0x6,0x6,0},  /* v */
    [0x77]={0x0,0x0,0x9,0x9,0x9,0xF,0xF,0},  /* w */
    [0x78]={0x0,0x0,0x9,0x6,0x6,0x6,0x9,0},  /* x */
    [0x79]={0x0,0x0,0x9,0x9,0x7,0x1,0x6,0},  /* y */
    [0x7A]={0x0,0x0,0xF,0x2,0x4,0x8,0xF,0},  /* z */
};

/* Direct field access: struct Smaky6 fully visible via machine_internal.h */

/* ── Init / fini ────────────────────────────────────────────────────────── */

void video_init(struct Smaky6 *m, SDL_Window *win, SDL_Renderer *ren)
{
    (void)win;
    m->vid.ren  = ren;
    m->vid.mode = VMODE_ALPHA;
    m->vid.gfx_msb_first = 0;

    SDL_Texture *tex = SDL_CreateTexture(ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        VIDEO_PX_W, VIDEO_PX_H);
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

    for (int ch = 0; ch < 128; ch++) {
        for (int row = 0; row < 8; row++) {
            /* Store at 16-byte stride (rows 0-7 = glyph, 8-15 = 0) to match
             * the real TMS2716 layout used by the rendering code. */
            cg[ch * 16 + row] = CHARGEN_SYNTHETIC[ch][row];
        }
    }
    fprintf(stderr, "video: using synthetic 74S262 character generator\n");
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

    uint32_t pixels[VIDEO_PX_W * VIDEO_PX_H];
    memset(pixels, 0, sizeof(pixels));

    const uint32_t LIT = VIDEO_COLOR_LIT;
    const uint32_t BG  = VIDEO_COLOR_BG;

    /* Fill with phosphor background colour */
    for (int i = 0; i < VIDEO_PX_W * VIDEO_PX_H; i++)
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
                int      px0  = col * 8;
                int      py0  = row * 8;

                for (int sl = 0; sl < 8; sl++) {
                    /* TMS2716 layout: 16 bytes/char; rows 0-7 hold the glyph.
                     * Bit 0 = leftmost pixel (LSB-first serial output). */
                    uint8_t bits = cg[code * 16 + sl];
                    for (int b = 0; b < 8; b++) {
                        int px = px0 + b;
                        int py = py0 + sl;
                        if (px < VIDEO_PX_W && py < VIDEO_PX_H) {
                            if (bits & (1u << b))
                                pixels[py * VIDEO_PX_W + px] = LIT;
                        }
                    }
                }
            }
        }
    }

    if (mode != VMODE_ALPHA) {
        /* ── Graphic plane ───────────────────────────────────────────────── */
        for (int row = 0; row < VIDEO_SCAN_LINES; row++) {
            for (int col = 0; col < 64; col++) {
                uint16_t addr = (uint16_t)(MEM_GFX_BASE + row * 64 + col);
                uint8_t  byte = memory_read(m, addr);
                for (int bit = 0; bit < 8; bit++) {
                    int px = col * 8 + (m->vid.gfx_msb_first ? (7 - bit) : bit);
                    if (px < VIDEO_PX_W && row < VIDEO_PX_H) {
                        if (byte & (1u << bit))
                            pixels[row * VIDEO_PX_W + px] = LIT;
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

            if (len > 0)
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

    /* ── Status bar: disk activity + track/sector ───────────────────────── */
    /* Dark background for the LED strip */
    SDL_SetRenderDrawColor(ren, 10, 18, 10, 255);
    SDL_Rect bar = { 0, VIDEO_ASPECT_H, VIDEO_WIN_W, VIDEO_LED_H };
    SDL_RenderFillRect(ren, &bar);
    /* Separator line */
    SDL_SetRenderDrawColor(ren, 0, 80, 0, 255);
    SDL_RenderDrawLine(ren, 0, VIDEO_ASPECT_H, VIDEO_WIN_W - 1, VIDEO_ASPECT_H);

    /* Two drive slots: LED + label + track number.
     * Glyphs are rendered 1:1 from chargen (8×8 logical px); the display_scale
     * factor applied to the SDL window makes them crisp at any scale value.
     * Slot width = 256 logical px (two equal halves of 512-wide window). */
    static const char *drive_label[2] = { "A", "B" };
    const int gh  = 8;   /* chargen glyph height in logical pixels */
    const int ly  = VIDEO_ASPECT_H + (VIDEO_LED_H - gh) / 2;  /* vertically centred */
    for (int d = 0; d < 2; d++) {
        int mounted = m->fdc.image[d] != NULL;
        int active  = m->fdc.disk_active[d] > 0;
        int lx = 4 + d * 256;   /* slot left edge; two 256-px halves */

        /* LED body: 8×8 square */
        SDL_Rect led = { lx, ly, 8, 8 };
        if (active) {
            SDL_SetRenderDrawColor(ren, 255, 140, 0, 255);  /* amber (active) */
        } else if (mounted) {
            SDL_SetRenderDrawColor(ren, 55, 30, 0, 255);    /* dim amber (idle) */
        } else {
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);   /* off */
        }
        SDL_RenderFillRect(ren, &led);
        SDL_SetRenderDrawColor(ren, 70, 70, 70, 255);
        SDL_RenderDrawRect(ren, &led);

        /* Drive letter glyph (1px per bit) */
        uint8_t code = (uint8_t)drive_label[d][0];
        int tx = lx + 12;
        SDL_SetRenderDrawColor(ren, 0, 200, 0, 255);
        for (int sl = 0; sl < 8; sl++) {
            uint8_t bits = m->vid.chargen[code * 16 + sl];
            for (int b = 0; b < 8; b++) {
                if (bits & (1u << b))
                    SDL_RenderDrawPoint(ren, tx + b, ly + sl);
            }
        }

        /* Track / sector info — only when mounted */
        if (mounted) {
            char info[12];
            int trk = m->fdc.track[d];
            int sec = m->fdc.phased_sector[d];
            snprintf(info, sizeof(info), "T:%02d S:%02d", trk, sec);
            int ix = tx + 10;   /* after drive letter + 2px gap */
            SDL_SetRenderDrawColor(ren, 0, 170, 0, 255);
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
