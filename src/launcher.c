// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* launcher.c – Smaky 6 SDL2 startup configuration dialog
 *
 * Rendered entirely with SDL2 primitives (no SDL_ttf dependency).
 * Text is drawn using the authentic Smaky 6 chargen ROM, embedded via
 * chargen_rom.h (generated at build time from roms/chargen.rom).
 */

#include "launcher.h"
#include "chargen_rom.h"
#include "icon_data.h"
#ifndef __EMSCRIPTEN__
#  include "logo_data.h"
#endif

#ifndef __EMSCRIPTEN__
#  include "tinyfiledialogs.h"
#endif
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Geometry ────────────────────────────────────────────────────────────── */

#define WIN_W  460
#define WIN_H  505

/* Colours (ARGB) — Smaky 6 palette: cream body, charcoal keys, green phosphor */
#define COL_BG          0xFFCFC6A4   /* cream/beige machine body */
#define COL_SECTION     0xFFC0B898   /* slightly darker beige for section headers */
#define COL_ACCENT      0xFF2A6020   /* dark forest green — readable on beige */
#define COL_TEXT        0xFF2A2520   /* dark charcoal text */
#define COL_TEXT_DIM    0xFF7A7060   /* dimmed warm brown */
#define COL_BTN_BG      0xFFD8D0B0   /* beige button background */
#define COL_BTN_HOVER   0xFFEAE0C0   /* lighter beige on hover */
#define COL_BTN_START   0xFF2A2018   /* dark charcoal Start key */
#define COL_BTN_START_H 0xFF3A3028   /* slightly lighter on hover */
#define COL_BORDER      0xFFA8A080   /* warm beige border */
#define COL_DROPDOWN_BG 0xFFD8D0B0
#define COL_WHITE       0xFFFFFFFF

#define FONT_W  8
#define FONT_H  8    /* cap height used for layout/centering */
#define FONT_ROWS 10 /* rows rendered (includes descenders in rows 8-9) */
#define FONT_SCALE 1   /* render at 1:1 — crisp at launcher size */

static void sdl_set_color(SDL_Renderer *ren, uint32_t argb)
{
    SDL_SetRenderDrawColor(ren,
        (argb >> 16) & 0xFF,
        (argb >>  8) & 0xFF,
        (argb      ) & 0xFF,
        (argb >> 24) & 0xFF);
}

static void draw_rect_filled(SDL_Renderer *ren, int x, int y, int w, int h, uint32_t col)
{
    sdl_set_color(ren, col);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(ren, &r);
}

static void draw_rect_outline(SDL_Renderer *ren, int x, int y, int w, int h, uint32_t col)
{
    sdl_set_color(ren, col);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderDrawRect(ren, &r);
}

/* Draw a single character at pixel (px, py).  Returns advance width. */
static int draw_char(SDL_Renderer *ren, int px, int py, char c, uint32_t col)
{
    unsigned char uc = (unsigned char)c;
    if (uc >= 128) return FONT_W * FONT_SCALE + 1;
    const uint8_t *glyph = &CHARGEN_ROM[uc * 16]; /* 16-byte stride, rows 0-7 */
    sdl_set_color(ren, col);
    for (int row = 0; row < FONT_ROWS; row++) {
        uint8_t bits = glyph[row];
        for (int col_bit = 0; col_bit < FONT_W; col_bit++) {
            if (bits & (1u << col_bit)) {  /* LSB = leftmost pixel */
                SDL_Rect dot = {
                    px + col_bit * FONT_SCALE,
                    py + row    * FONT_SCALE,
                    FONT_SCALE,
                    FONT_SCALE
                };
                SDL_RenderFillRect(ren, &dot);
            }
        }
    }
    return FONT_W * FONT_SCALE + 1;
}

static void draw_text(SDL_Renderer *ren, int x, int y, const char *s, uint32_t col)
{
    while (*s) {
        x += draw_char(ren, x, y, *s, col);
        s++;
    }
}

/* Text width in pixels */
static int text_width(const char *s)
{
    return (int)strlen(s) * (FONT_W * FONT_SCALE + 1);
}

/* ── Widget helpers ──────────────────────────────────────────────────────── */

/* Draw a button-style box.  Returns 1 if the mouse is hovering. */
static int draw_button(SDL_Renderer *ren, int x, int y, int w, int h,
                        const char *label, int mx, int my, uint32_t bg_col, uint32_t hover_col)
{
    int hover = (mx >= x && mx < x + w && my >= y && my < y + h);
    draw_rect_filled(ren, x, y, w, h, hover ? hover_col : bg_col);
    draw_rect_outline(ren, x, y, w, h, COL_BORDER);
    int tx = x + (w - text_width(label)) / 2;
    int ty = y + (h - FONT_H * FONT_SCALE) / 2;
    draw_text(ren, tx, ty, label, COL_WHITE);
    return hover;
}

/* Draw a dropdown-style selector showing the current choice.
 * Clicking cycles through the options. */
static void draw_dropdown(SDL_Renderer *ren, int x, int y, int w, int h,
                           const char *label, int mx, int my)
{
    int hover = (mx >= x && mx < x + w && my >= y && my < y + h);
    draw_rect_filled(ren, x, y, w, h, hover ? COL_BTN_HOVER : COL_DROPDOWN_BG);
    draw_rect_outline(ren, x, y, w, h, COL_BORDER);
    int tx = x + 6;
    int ty = y + (h - FONT_H * FONT_SCALE) / 2;
    draw_text(ren, tx, ty, label, COL_WHITE);
    /* down-arrow indicator on right edge */
    draw_text(ren, x + w - 14, ty, "v", COL_TEXT_DIM);
}

/* ── File picker (threaded, non-blocking) ───────────────────────────────── */

/*
 * Running zenity via popen() in the main thread blocks the SDL event loop,
 * causing the window manager to report the app as "not responding".  We run
 * zenity in a detached SDL thread and poll the result from the event loop.
 */
typedef enum { PICK_IDLE = 0, PICK_RUNNING, PICK_DONE } PickState;

typedef struct PickCtx {
    volatile PickState state;   /* written by thread, read by main loop */
    char               result[1024]; /* heap-free: fixed buffer is enough */
} PickCtx;

#ifndef __EMSCRIPTEN__
/* Thread entry: runs the native file dialog and deposits the result. */
static int pick_thread(void *data)
{
    PickCtx *ctx = (PickCtx *)data;
    ctx->result[0] = '\0';
    static const char *patterns[] = { "*.dsk", "*.DSK" };
    const char *p = tinyfd_openFileDialog(
        "Select disk image", "", 2, patterns, "Disk images", 0);
    if (p) {
        strncpy(ctx->result, p, sizeof(ctx->result) - 1);
        ctx->result[sizeof(ctx->result) - 1] = '\0';
    }
    ctx->state = PICK_DONE;
    return 0;
}

/* Launch an async pick.  Returns SDL_Thread* (detached) or NULL. */
static SDL_Thread *pick_file_async(PickCtx *ctx)
{
    ctx->result[0] = '\0';
    ctx->state = PICK_RUNNING;
    SDL_Thread *t = SDL_CreateThread(pick_thread, "FilePicker", ctx);
    if (!t) {
        ctx->state = PICK_IDLE;
        return NULL;
    }
    SDL_DetachThread(t);   /* we poll ctx->state; no need to join */
    return t;
}
#else /* __EMSCRIPTEN__ */
/*
 * Emscripten file picker.
 *
 * We trigger a hidden <input type="file"> element defined in shell.html.
 * A JS FileReader onchange handler reads the chosen file into the Emscripten
 * virtual FS under /tmp/picked.dsk and then calls back into C via
 * smemu6_pick_done() (exported with EMSCRIPTEN_KEEPALIVE).
 *
 * No SDL thread needed — the JS callback sets pick_ctx->state directly.
 */

/* Global pointer to the currently-active pick context, set before triggering
 * the JS picker.  Only one pick can be in flight at a time (enforced by the
 * PICK_IDLE guard in the event loop). */
static PickCtx *g_active_pick_ctx = NULL;

/* Called from JS (FileReader onload) once the file has been written to /tmp. */
EMSCRIPTEN_KEEPALIVE
void smemu6_pick_done(const char *virtual_path)
{
    if (!g_active_pick_ctx) return;
    if (virtual_path && virtual_path[0]) {
        strncpy(g_active_pick_ctx->result, virtual_path,
                sizeof(g_active_pick_ctx->result) - 1);
        g_active_pick_ctx->result[sizeof(g_active_pick_ctx->result) - 1] = '\0';
    }
    g_active_pick_ctx->state = PICK_DONE;
    g_active_pick_ctx = NULL;
}

/* Called from JS if the user cancels the file picker. */
EMSCRIPTEN_KEEPALIVE
void smemu6_pick_cancel(void)
{
    if (!g_active_pick_ctx) return;
    g_active_pick_ctx->result[0] = '\0';
    g_active_pick_ctx->state = PICK_DONE;  /* DONE with empty result = cancelled */
    g_active_pick_ctx = NULL;
}

/* Launch an async pick via the browser <input type="file"> element. */
static SDL_Thread *pick_file_async(PickCtx *ctx)
{
    ctx->result[0] = '\0';
    ctx->state = PICK_RUNNING;
    g_active_pick_ctx = ctx;
    /* Trigger the hidden file-input element defined in web/shell.html. */
    EM_ASM({
        var inp = document.getElementById('smemu6-file-input');
        if (inp) {
            inp.onchange = function(e) {
                var file = e.target.files[0];
                if (!file) { Module._smemu6_pick_cancel(); return; }
                var path = '/tmp/' + file.name;
                var reader = new FileReader();
                reader.onload = function(ev) {
                    var buf = new Uint8Array(ev.target.result);
                    try {
                        FS.unlink(path);
                    } catch(ex) {}
                    FS.writeFile(path, buf);
                    var pathPtr = allocate(intArrayFromString(path), ALLOC_NORMAL);
                    Module._smemu6_pick_done(pathPtr);
                    _free(pathPtr);
                };
                reader.onerror = function() { Module._smemu6_pick_cancel(); };
                reader.readAsArrayBuffer(file);
                /* reset so the same file can be picked again */
                inp.value = '';
            };
            inp.click();
        } else {
            console.warn('smemu6: #smemu6-file-input element not found in shell.html');
            Module._smemu6_pick_cancel();
        }
    });
    return NULL;  /* no thread; result arrives via JS callback */
}
#endif /* __EMSCRIPTEN__ */

/* Truncate a path to at most 38 visible chars, prepending "..." if needed. */
static void truncate_path(const char *path, char *out, int out_len)
{
    if (!path) { out[0] = '\0'; return; }
    size_t len = strlen(path);
    if (len <= 46) {
        snprintf(out, (size_t)out_len, "%s", path);
    } else {
        snprintf(out, (size_t)out_len, "...%s", path + len - 46);
    }
}

/* ── Section header ──────────────────────────────────────────────────────── */

static int draw_section(SDL_Renderer *ren, int y, const char *title)
{
    draw_rect_filled(ren, 0, y, WIN_W, 18, COL_SECTION);
    draw_text(ren, 10, y + 5, title, COL_ACCENT);
    return y + 20;
}

/* ── Row layout constants ─────────────────────────────────────────────────── */

#define ROW_H       24
#define LABEL_X     12
#define CTRL_X     175
#define CTRL_W      90
#define CTRL_H      18
#define BROWSE_X   273
#define BROWSE_W    60
#define CLEAR_X    339
#define CLEAR_W     18
#define PATH_ROW_H  14   /* extra height below disk rows for path sub-row */

/* ── State ───────────────────────────────────────────────────────────────── */

typedef struct {
    /* Storage */
    int   dx0_is_harddisk;   /* 0=floppy, 1=harddisk */
    char *dx0_path;
    char *dx1_path;
    int   autoboot;          /* 0/1 */

    /* Screen */
    int   scale;             /* 0..3 → 1×..4× */
    int   phosphor;          /* 0=green, 1=white */
    int   scanlines;
    int   no_display_off;

    /* Sound */
    int   beeper;
} State;

/* Widget hit areas (filled during draw, used during event handling) */
typedef struct {
    SDL_Rect dx0_type;
    SDL_Rect dx0_browse;
    SDL_Rect dx0_clear;
    SDL_Rect dx1_browse;
    SDL_Rect dx1_clear;
    SDL_Rect autoboot_dd;
    SDL_Rect scale_dd;
    SDL_Rect phosphor_dd;
    SDL_Rect scanlines_dd;
    SDL_Rect no_blank_dd;
    SDL_Rect beeper_dd;
    SDL_Rect btn_help;
    SDL_Rect btn_start;
} HitAreas;

static SDL_Rect make_rect(int x, int y, int w, int h)
{
    SDL_Rect r = { x, y, w, h };
    return r;
}

static int rect_hit(const SDL_Rect *r, int mx, int my)
{
    return mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h;
}

/* ── Help overlay ─────────────────────────────────────────────────────────── */

static void show_help(SDL_Window *parent)
{
    (void)parent;
    const char *msg =
        "SAMOS usage\n"
        "  LIST        — list files on disk\n"
        "  COPY A B    — copy file A to B\n"
        "  INIT dx0:   — initialise disk\n"
        "  TAB         — switch between drives\n"
        "\n"
        "Keyboard mapping\n"
        "  Right Ctrl   = CHANGE     Left Ctrl  = CURSOR\n"
        "  Menu/App     = SEARCH     AltGr      = PROGRA\n"
        "  F10          = SHOW       Win/Super  = KILL\n"
        "  Left Alt     = COPY\n"
        "  F8           = MACRO      F9         = DEFINE\n"
        "  Backspace    = BS         Delete     = DEL\n"
        "  Pause / F11  = BREAK (NMI)\n"
        "  Shift+Pause  = Hard reset\n"
        "  Ctrl+Q       = Quit\n";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
        "Smemu6 Help", msg, parent);
}

/* ── Draw full launcher frame ─────────────────────────────────────────────── */

static void draw_frame(SDL_Renderer *ren, const State *s, int mx, int my, HitAreas *ha)
{
    /* Clear */
    draw_rect_filled(ren, 0, 0, WIN_W, WIN_H, COL_BG);

    /* ── Header: logo image ─────────────────────────────────────────────── */
    int y = 8;
#ifndef __EMSCRIPTEN__
    {
        SDL_Surface *logo_surf = SDL_CreateRGBSurfaceFrom(
            (void *)smaky6_logo_rgba,
            SMAKY6_LOGO_W, SMAKY6_LOGO_H, 32, SMAKY6_LOGO_W * 4,
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
        if (logo_surf) {
            SDL_Texture *logo_tex = SDL_CreateTextureFromSurface(ren, logo_surf);
            SDL_FreeSurface(logo_surf);
            if (logo_tex) {
                SDL_SetTextureBlendMode(logo_tex, SDL_BLENDMODE_BLEND);
                SDL_Rect dst = {
                    (WIN_W - SMAKY6_LOGO_W) / 2, y,
                    SMAKY6_LOGO_W, SMAKY6_LOGO_H
                };
                SDL_RenderCopy(ren, logo_tex, NULL, &dst);
                SDL_DestroyTexture(logo_tex);
            }
        }
    }
    y += SMAKY6_LOGO_H + 8;
#endif /* __EMSCRIPTEN__ */

    /* ── Storage ─────────────────────────────────────────────────────────── */
    y = draw_section(ren, y, "Storage");
    y += 4;

    /* DX0 row */
    draw_text(ren, LABEL_X, y + 5, "DX0:", COL_TEXT);
    {
        const char *lbl = s->dx0_is_harddisk ? "Harddisk" : "Floppy";
        draw_dropdown(ren, CTRL_X, y, CTRL_W, CTRL_H, lbl, mx, my);
        ha->dx0_type = make_rect(CTRL_X, y, CTRL_W, CTRL_H);
    }
    draw_button(ren, BROWSE_X, y, BROWSE_W, CTRL_H, "Browse", mx, my, COL_BTN_BG, COL_BTN_HOVER);
    ha->dx0_browse = make_rect(BROWSE_X, y, BROWSE_W, CTRL_H);
    draw_button(ren, CLEAR_X, y, CLEAR_W, CTRL_H, "x", mx, my, COL_BTN_BG, COL_BTN_HOVER);
    ha->dx0_clear = make_rect(CLEAR_X, y, CLEAR_W, CTRL_H);
    {
        char trunc[52];
        truncate_path(s->dx0_path, trunc, (int)sizeof(trunc));
        draw_text(ren, LABEL_X, y + CTRL_H + 3, trunc, COL_TEXT_DIM);
    }
    y += ROW_H + PATH_ROW_H;

    /* DX1 row */
    draw_text(ren, LABEL_X, y + 5, "DX1:", COL_TEXT);
    draw_text(ren, CTRL_X, y + 5, "Floppy", COL_TEXT_DIM);
    draw_button(ren, BROWSE_X, y, BROWSE_W, CTRL_H, "Browse", mx, my, COL_BTN_BG, COL_BTN_HOVER);
    ha->dx1_browse = make_rect(BROWSE_X, y, BROWSE_W, CTRL_H);
    draw_button(ren, CLEAR_X, y, CLEAR_W, CTRL_H, "x", mx, my, COL_BTN_BG, COL_BTN_HOVER);
    ha->dx1_clear = make_rect(CLEAR_X, y, CLEAR_W, CTRL_H);
    {
        char trunc[52];
        truncate_path(s->dx1_path, trunc, (int)sizeof(trunc));
        draw_text(ren, LABEL_X, y + CTRL_H + 3, trunc, COL_TEXT_DIM);
    }
    y += ROW_H + PATH_ROW_H;

    /* Autoboot row */
    draw_text(ren, LABEL_X, y + 5, "Autoboot:", COL_TEXT);
    {
        const char *lbl = s->autoboot ? "On" : "Off";
        draw_dropdown(ren, CTRL_X, y, CTRL_W, CTRL_H, lbl, mx, my);
        ha->autoboot_dd = make_rect(CTRL_X, y, CTRL_W, CTRL_H);
    }
    y += ROW_H + 6;

    /* ── Screen ──────────────────────────────────────────────────────────── */
    y = draw_section(ren, y, "Screen");
    y += 4;

    static const char *scale_labels[] = { "1x", "2x", "3x", "4x" };
    draw_text(ren, LABEL_X, y + 5, "Scale:", COL_TEXT);
    draw_dropdown(ren, CTRL_X, y, CTRL_W, CTRL_H,
                  scale_labels[s->scale < 4 ? s->scale : 1], mx, my);
    ha->scale_dd = make_rect(CTRL_X, y, CTRL_W, CTRL_H);
    y += ROW_H;

    draw_text(ren, LABEL_X, y + 5, "Phosphor:", COL_TEXT);
    {
        const char *lbl = s->phosphor ? "White" : "Green";
        draw_dropdown(ren, CTRL_X, y, CTRL_W, CTRL_H, lbl, mx, my);
        ha->phosphor_dd = make_rect(CTRL_X, y, CTRL_W, CTRL_H);
    }
    y += ROW_H;

    draw_text(ren, LABEL_X, y + 5, "Scanlines:", COL_TEXT);
    {
        const char *lbl = s->scanlines ? "On" : "Off";
        draw_dropdown(ren, CTRL_X, y, CTRL_W, CTRL_H, lbl, mx, my);
        ha->scanlines_dd = make_rect(CTRL_X, y, CTRL_W, CTRL_H);
    }
    y += ROW_H;

    draw_text(ren, LABEL_X, y + 5, "No screen blank:", COL_TEXT);
    {
        const char *lbl = s->no_display_off ? "On" : "Off";
        draw_dropdown(ren, CTRL_X, y, CTRL_W, CTRL_H, lbl, mx, my);
        ha->no_blank_dd = make_rect(CTRL_X, y, CTRL_W, CTRL_H);
    }
    y += ROW_H + 6;

    /* ── Sound ───────────────────────────────────────────────────────────── */
    y = draw_section(ren, y, "Sound");
    y += 4;

    draw_text(ren, LABEL_X, y + 5, "Beeper:", COL_TEXT);
    {
        const char *lbl = s->beeper ? "On" : "Off";
        draw_dropdown(ren, CTRL_X, y, CTRL_W, CTRL_H, lbl, mx, my);
        ha->beeper_dd = make_rect(CTRL_X, y, CTRL_W, CTRL_H);
    }
    y += ROW_H;

    draw_text(ren, LABEL_X, y + 5, "Drive sounds:", COL_TEXT);
    draw_text(ren, CTRL_X, y + 5, "Off (coming soon)", COL_TEXT_DIM);
    y += ROW_H + 10;

    /* ── ROM warning ─────────────────────────────────────────────────────── */
    draw_text(ren, LABEL_X, y, "ROM: roms/samos_sys17.rom", COL_TEXT_DIM);
    y += FONT_H * FONT_SCALE + 12;

    /* ── Buttons ─────────────────────────────────────────────────────────── */
    y = draw_section(ren, y, "");
    y += 8;
    int btn_w = 90, btn_h = 26;
    int btn_y = y;
    draw_button(ren, 70,  btn_y, btn_w, btn_h, "Help",  mx, my, COL_BTN_BG, COL_BTN_HOVER);
    ha->btn_help  = make_rect(70,  btn_y, btn_w, btn_h);
    draw_button(ren, 300, btn_y, btn_w, btn_h, "Start", mx, my, COL_BTN_START, COL_BTN_START_H);
    ha->btn_start = make_rect(300, btn_y, btn_w, btn_h);
}

/* ── launcher_run ────────────────────────────────────────────────────────── */

int launcher_run(LauncherConfig *cfg, const LauncherHints *hints)
{
    /* Sentinel defaults — means "not set, let main() keep CLI/default" */
    cfg->dx0_is_harddisk = -1;
    cfg->dx0_path        = NULL;
    cfg->dx1_path        = NULL;
    cfg->autoboot        = -1;
    cfg->scale           = -1;
    cfg->phosphor_white  = -1;
    cfg->scanlines       = -1;
    cfg->no_display_off  = -1;
    cfg->beeper          = -1;

    /* Detect headless / dummy video driver */
    const char *driver = SDL_GetCurrentVideoDriver();
    /* If SDL not yet initialised, getenv gives us the hint */
    if (!driver) {
        const char *env = SDL_getenv("SDL_VIDEODRIVER");
        if (env && strcmp(env, "dummy") == 0)
            driver = "dummy";
    }
    if (driver && strcmp(driver, "dummy") == 0) {
        /* Headless: fill in sensible defaults and return immediately */
        cfg->dx0_is_harddisk = 0;
        cfg->autoboot        = 0;
        cfg->scale           = 1;   /* 2× */
        cfg->phosphor_white  = 0;
        cfg->scanlines       = 1;
        cfg->no_display_off  = 1;
        cfg->beeper          = 1;
        return 0;
    }

    /* Init SDL if not already done */
    int sdl_was_init = SDL_WasInit(SDL_INIT_VIDEO);
    if (!sdl_was_init) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "[launcher] SDL_Init: %s\n", SDL_GetError());
            return 0; /* skip launcher, proceed with defaults */
        }
    }

    SDL_Window *win = SDL_CreateWindow(
        "Smemu6 - Smaky6 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "[launcher] SDL_CreateWindow: %s\n", SDL_GetError());
        return 0;
    }

    /* Set window icon from embedded RGBA pixel data */
    {
        SDL_Surface *icon = SDL_CreateRGBSurfaceFrom(
            (void *)smaky6_icon_rgba,
            SMAKY6_ICON_W, SMAKY6_ICON_H,
            32, SMAKY6_ICON_W * 4,
            0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
        if (icon) { SDL_SetWindowIcon(win, icon); SDL_FreeSurface(icon); }
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        SDL_DestroyWindow(win);
        return 0;
    }

    /* Default state (matches README defaults) */
    State s = {
        .dx0_is_harddisk = 0,
        .dx0_path        = NULL,
        .dx1_path        = NULL,
        .autoboot        = 1,
        .scale           = 0,   /* index 0 → 1× */
        .phosphor        = 0,   /* green */
        .scanlines       = 1,   /* on */
        .no_display_off  = 1,   /* on */
        .beeper          = 1,   /* on */
    };

    /* Pre-populate from CLI hints */
    if (hints) {
        if (hints->dx0_is_harddisk >= 0)
            s.dx0_is_harddisk = hints->dx0_is_harddisk;
        if (hints->dx0_path)
            s.dx0_path = SDL_strdup(hints->dx0_path);
        if (hints->dx1_path)
            s.dx1_path = SDL_strdup(hints->dx1_path);
        if (hints->autoboot >= 0)
            s.autoboot = hints->autoboot;
        if (hints->scale >= 1 && hints->scale <= 4)
            s.scale = hints->scale - 1;   /* factor → index */
        if (hints->phosphor_white >= 0)
            s.phosphor = hints->phosphor_white;
        if (hints->scanlines >= 0)
            s.scanlines = hints->scanlines;
        if (hints->no_display_off >= 0)
            s.no_display_off = hints->no_display_off;
        if (hints->beeper >= 0)
            s.beeper = hints->beeper;
    }

    HitAreas ha;
    memset(&ha, 0, sizeof(ha));

    int mx = 0, my = 0;
    int result = -1;   /* -1 = window closed without Start */

    /* Async file-picker state */
    PickCtx  pick_ctx = { PICK_IDLE, "" };
    int      pick_target = 0;   /* 0 = dx0, 1 = dx1 */

    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
                    running = 0;
                break;
            case SDL_MOUSEMOTION:
                mx = ev.motion.x;
                my = ev.motion.y;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button != SDL_BUTTON_LEFT) break;
                mx = ev.button.x;
                my = ev.button.y;

                if (rect_hit(&ha.dx0_type,    mx, my)) { s.dx0_is_harddisk ^= 1; break; }
                if (rect_hit(&ha.dx0_browse,  mx, my)) {
                    if (pick_ctx.state == PICK_IDLE) {
                        pick_target = 0;
                        pick_file_async(&pick_ctx);
                    }
                    break;
                }
                if (rect_hit(&ha.dx0_clear,   mx, my)) { free(s.dx0_path); s.dx0_path = NULL; break; }
                if (rect_hit(&ha.dx1_browse,  mx, my)) {
                    if (pick_ctx.state == PICK_IDLE) {
                        pick_target = 1;
                        pick_file_async(&pick_ctx);
                    }
                    break;
                }
                if (rect_hit(&ha.dx1_clear,   mx, my)) { free(s.dx1_path); s.dx1_path = NULL; break; }
                if (rect_hit(&ha.autoboot_dd, mx, my)) { s.autoboot ^= 1; break; }
                if (rect_hit(&ha.scale_dd,    mx, my)) { s.scale = (s.scale + 1) % 4; break; }
                if (rect_hit(&ha.phosphor_dd, mx, my)) { s.phosphor ^= 1; break; }
                if (rect_hit(&ha.scanlines_dd,mx, my)) { s.scanlines ^= 1; break; }
                if (rect_hit(&ha.no_blank_dd, mx, my)) { s.no_display_off ^= 1; break; }
                if (rect_hit(&ha.beeper_dd,   mx, my)) { s.beeper ^= 1; break; }
                if (rect_hit(&ha.btn_help,    mx, my)) { show_help(win); break; }
                if (rect_hit(&ha.btn_start,   mx, my)) {
                    result = 0;
                    running = 0;
                    break;
                }
                break;
            default:
                break;
            }
        }

        /* Collect async file-picker result when ready */
        if (pick_ctx.state == PICK_DONE) {
            if (pick_ctx.result[0] != '\0') {
                char *p = malloc(strlen(pick_ctx.result) + 1);
                if (p) {
                    memcpy(p, pick_ctx.result, strlen(pick_ctx.result) + 1);
                    if (pick_target == 0) { free(s.dx0_path); s.dx0_path = p; }
                    else                  { free(s.dx1_path); s.dx1_path = p; }
                }
            }
            pick_ctx.state = PICK_IDLE;
        }

        draw_frame(ren, &s, mx, my, &ha);
        SDL_RenderPresent(ren);
        SDL_Delay(16);  /* ~60 fps redraws */
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);

    if (result == 0) {
        /* Transfer state → cfg */
        cfg->dx0_is_harddisk = s.dx0_is_harddisk;
        cfg->dx0_path        = s.dx0_path;   /* transfer ownership */
        cfg->dx1_path        = s.dx1_path;
        cfg->autoboot        = s.autoboot;
        cfg->scale           = s.scale + 1;  /* index → actual scale factor (1..4) */
        cfg->phosphor_white  = s.phosphor;
        cfg->scanlines       = s.scanlines;
        cfg->no_display_off  = s.no_display_off;
        cfg->beeper          = s.beeper;
    } else {
        free(s.dx0_path);
        free(s.dx1_path);
    }

    return result;
}
