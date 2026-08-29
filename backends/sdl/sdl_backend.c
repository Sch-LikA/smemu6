/* backends/sdl/sdl_backend.c - SDL2 backend for smemu6 (see platform.h).
 *
 * Default backend for the native and Emscripten builds.  It owns every SDL
 * resource (audio device here; window/renderer/texture + input in later steps).
 * The core never references SDL directly: it calls the dispatch functions in
 * platform.c, which forward here through the smemu6_backend vtable.
 *
 * Embedded targets do NOT compile this file (gated in CMake / PlatformIO) and
 * supply their own backend instead.
 */
#include "../src/platform.h"
#include "../src/machine_internal.h"
#include "../src/keyboard.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Accumulates all SDL-owned state for this backend.  Grows as more subsystems
 * (video, keyboard) register their methods onto smemu6_sdl_backend below. */
struct sdl_backend_state {
    SDL_AudioDeviceID audio_dev;   /* 0 == device not open */
    /* Video (Step 4): streaming ARGB8888 texture + the SDL renderer it was
     * created from.  Both are backend-owned; the core never references them. */
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    /* keyboard state added in Step 3 */
};

static struct sdl_backend_state g_sdl_state = {0};

/* ---- audio -------------------------------------------------------------- */

static int SDLCALL sdl_audio_close_thread(void *arg)
{
    SDL_CloseAudioDevice((SDL_AudioDeviceID)(uintptr_t)arg);
    return 0;
}

static int sdl_audio_open(void *ctx, int rate, int channels, int block_frames)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    SDL_AudioSpec want, got;
    (void)block_frames;

    SDL_memset(&want, 0, sizeof(want));
    want.freq     = rate;
    want.format   = AUDIO_S16SYS;
    want.channels = channels;
    /* Emscripten SDL2 requires a power-of-two buffer size; 1024 is the
     * smallest power of two above our 882-sample frame (44100 / 50 Hz). */
    want.samples  = 1024;
    want.callback = NULL;   /* push mode -- no callback thread */

    /* SDL_OpenAudioDevice can transiently fail on PulseAudio/PipeWire if the
     * server is not yet ready.  Retry up to 5 times with a short delay. */
    for (int attempt = 0; attempt < 5 && s->audio_dev == 0; attempt++) {
        if (attempt > 0) SDL_Delay(20);
        s->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    }
    if (!s->audio_dev) {
        fprintf(stderr, "[sound] DISABLED: SDL_OpenAudioDevice failed: %s\n",
                SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(s->audio_dev, 0);

    /* Smoke-test: queue a silent frame and verify the driver accepted it.
     * On a broken PipeWire/PulseAudio session the device opens successfully
     * but silently drops all data (GetQueuedAudioSize stays 0). */
    {
        int16_t silence[1024];
        memset(silence, 0, sizeof(silence));
        SDL_QueueAudio(s->audio_dev, silence, sizeof(silence));
        SDL_Delay(2);
        Uint32 queued = SDL_GetQueuedAudioSize(s->audio_dev);
        if (queued == 0) {
            fprintf(stderr, "[sound] WARNING: audio device opened but queue stays empty "
                    "-- PipeWire/PulseAudio session may be broken; sound will be silent\n");
        } else {
            fprintf(stderr, "[sound] OK: device ready, %u bytes queued (driver: %s)\n",
                    (unsigned)queued, SDL_GetCurrentAudioDriver());
            SDL_ClearQueuedAudio(s->audio_dev);   /* discard the test frame */
        }
    }
    return 0;
}

static int sdl_audio_queued_bytes(void *ctx)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    if (!s->audio_dev) return 0;
    return (int)SDL_GetQueuedAudioSize(s->audio_dev);
}

static void sdl_audio_push(void *ctx, const int16_t *samples, int frames)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    if (!s->audio_dev || !samples || frames <= 0) return;
    SDL_QueueAudio(s->audio_dev, samples, (Uint32)(frames * (int)sizeof(int16_t)));
}

static void sdl_audio_close(void *ctx)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    if (!s->audio_dev) return;
    /* SDL_CloseAudioDevice can block indefinitely on a broken session; run the
     * close in a detached thread so shutdown never hangs. */
    SDL_AudioDeviceID dev = s->audio_dev;
    s->audio_dev = 0;
    SDL_PauseAudioDevice(dev, 1);
    SDL_ClearQueuedAudio(dev);
    SDL_Thread *t = SDL_CreateThread(sdl_audio_close_thread, "audio_close",
                                     (void *)(uintptr_t)dev);
    if (t)
        SDL_DetachThread(t);   /* let it finish on its own; never join */
    else
        SDL_CloseAudioDevice(dev);   /* thread creation failed: risk the block */
}

/* ── Video resource hand-off (Step 4) ────────────────────────────────── *
 * The front-end created the SDL window+renderer; it hands us the renderer so
 * we create our own streaming texture and own the upload target.  Kept in the
 * backend so the core never references SDL.  The window itself stays owned by
 * the front-end (created/destroyed there). */
static void sdl_video_setup(void *ctx, void *renderer)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    SDL_Renderer *ren = (SDL_Renderer *)(uintptr_t)renderer;
    if (!ren) return;
    s->ren = ren;
    if (s->tex) { SDL_DestroyTexture(s->tex); s->tex = NULL; }
    s->tex = SDL_CreateTexture(ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        VIDEO_PX_W, VIDEO_ASPECT_H);
    if (!s->tex)
        fprintf(stderr, "video: SDL_CreateTexture (backend): %s\n", SDL_GetError());
}

static void sdl_video_teardown(void *ctx)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    if (s->tex) { SDL_DestroyTexture(s->tex); s->tex = NULL; }
    s->ren = NULL;
}

/* ── Input: keyboard ──────────────────────────────────────────────────── *
 * The core is SDL-free, so this backend forwards the ALREADY-portable scancode
 * straight into keyboard_event().  Any SDL->portable mapping lives in the
 * front-end (main.c); for SDL the portable scancodes are already identical to
 * SDL's, so this handler is a pure pass-through.  A non-SDL backend could map
 * its own device codes onto the same portable space here instead. */
static void sdl_key(void *ctx, struct Smaky6 *m, smemu6_scancode scan, int down, int repeat)
{
    (void)ctx;
    keyboard_event(m, scan, down, repeat);
}

/* One decoded unicode codepoint from an SDL_TEXTINPUT event straight into the
 * core ordinary-key path. */
static void sdl_text(void *ctx, struct Smaky6 *m, uint32_t codepoint)
{
    (void)ctx;
    keyboard_text(m, codepoint);
}

/* ── Present (desktop front-end) ─────────────────────────────────────── *
 * Upload one core-rendered framebuffer to the SDL texture, draw the desktop
 * overlays (CRT scanlines, disk-status LED bar, function-key bar, RESET /
 * BREAK buttons), and flip.  This is SDL-specific desktop UI: it lives in
 * the backend, invisible to the core (which only builds the framebuffer).
 * The machine pointer lets the overlays reflect live state (drives, keyboard). */
static void sdl_present(void *ctx, struct Smaky6 *m, const struct smemu6_frame *frame)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    SDL_Renderer *ren = s->ren;
    SDL_Texture  *tex = s->tex;
    if (!ren || !tex) return;

    /* Upload the core-rendered framebuffer to the streaming texture. */
    SDL_UpdateTexture(tex, NULL, frame->pixels, frame->stride_bytes);

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
        int mounted   = is_hd ? 1 : (m->fdc.media[d].kind != FLOPPY_MEDIA_NONE);
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
            { "CURSOR", 0x40 }, { "COPY",   0x20 }, { "KILL",   0x10 },
            { "PROGRA", 0x08 }, { "SHOW",   0x04 }, { "SEARCH", 0x02 },
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
            int hover   = (fmx >= bx && fmx < bx + bw && fmy >= by && fmy < by + bh);

            /* Strict baseline: function keys are direct held inputs only. */
            if (active)
                SDL_SetRenderDrawColor(ren, 200,  30,  30, 255);
            else if (hover)
                SDL_SetRenderDrawColor(ren, 130,  55,  45, 255);
            else
                SDL_SetRenderDrawColor(ren, 100,  38,  30, 255);
            SDL_Rect btn = { bx, by, bw, bh };
            SDL_RenderFillRect(ren, &btn);

            if (active)
                SDL_SetRenderDrawColor(ren, 255,  80,  80, 255);
            else
                SDL_SetRenderDrawColor(ren, 130,  50,  50, 255);
            SDL_RenderDrawRect(ren, &btn);

            /* Label colour */
            int label_len = (int)strlen(FKEYS[i].label);
            int tx = bx + (bw - label_len * 9) / 2;
            int ty = by + (bh - 8) / 2;
            if (active)
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

/* The compiled-in default backend.  Audio and present are wired in Steps 1-2;
 * keyboard input (key/text) in Step 3.  Any method a target does not need
 * stays NULL, and the core NULL-checks before calling. */
struct smemu6_backend smemu6_sdl_backend = {
    .ctx                = &g_sdl_state,
    .audio_open         = sdl_audio_open,
    .audio_queued_bytes = sdl_audio_queued_bytes,
    .audio_push         = sdl_audio_push,
    .audio_close        = sdl_audio_close,
    .present            = sdl_present,
    .video_setup        = sdl_video_setup,
    .video_teardown     = sdl_video_teardown,
    .key                = sdl_key,
    .text               = sdl_text,
};
