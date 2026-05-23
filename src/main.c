// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* main.c – Smaky 6 emulator entry point */
#include "machine_internal.h"  /* For access to cpu_stalled field */
#include "machine.h"
#include "memory.h"
#include "video.h"
#include "keyboard.h"
#include "debug.h"
#include "floppy.h"
#include "icon_data.h"
#include "winchester.h"
#include "sound.h"
#include "launcher.h"

#include <SDL2/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "virtual_floppy.h"

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

/* ── Configuration ──────────────────────────────────────────────────────────*/
/*
 * samos_sys17.rom = Phantom bootloader ROM (2 KB, from SYS17 TMS2716.HEX).
 * Maps to 0x0000–0x07FF.  The rest of the 64 KB address space is RAM.
 * Phantom loads SYS.SY (~8960 bytes) from storage into RAM (~0x5800)
 * and jumps to it.  There is no second pre-loaded ROM.
 */
#define ROM_PHANTOM  "roms/samos_sys17.rom"
#define ROM_CHARGEN  "roms/chargen.rom"
#define FLOPPY_DIR   "floppies/"

static volatile sig_atomic_t g_terminate_requested = 0;
static volatile sig_atomic_t g_dump_ram_requested   = 0;
static volatile sig_atomic_t g_refresh_virtual_floppies_requested = 0;

static void handle_terminate_signal(int sig)
{
    (void)sig;
    g_terminate_requested = 1;
}

static void handle_dump_signal(int sig)
{
    (void)sig;
    g_dump_ram_requested = 1;
}

static void handle_refresh_virtual_floppies_signal(int sig)
{
    (void)sig;
    g_refresh_virtual_floppies_requested = 1;
}

/* ── RAM dump ───────────────────────────────────────────────────────────────*/

static void do_ram_dump(const struct Smaky6 *m)
{
    char path[64];
    static int dump_seq = 0;
    snprintf(path, sizeof(path), "smaky6_ram_%04d_pc%04X.bin",
             dump_seq++, (unsigned)machine_get_pc(m));
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[dump] fopen(%s): %s\n", path, strerror(errno));
        return;
    }
    /* Full 64 KB address space */
    if (fwrite(m->bus, 1, 65536, f) != 65536)
        fprintf(stderr, "[dump] short write to %s\n", path);
    fclose(f);
    fprintf(stderr, "[dump] RAM written to %s  (PC=%04X)\n",
            path, (unsigned)machine_get_pc(m));
}

static void do_virtual_floppy_refresh(struct Smaky6 *m)
{
    int refreshed = 0;

    for (int drive = 0; drive < 2; drive++) {
        int rc = floppy_refresh_virtual(m, drive);
        if (rc < 0) {
            fprintf(stderr, "[main] virtual floppy refresh failed on DX%d\n", drive);
            continue;
        }
        refreshed += rc;
    }

    if (refreshed == 0) {
        fprintf(stderr, "[main] no mounted host-directory virtual floppies to refresh\n");
    }
}

static int dump_virtual_floppy_manifest(const char *hostdir, const char *manifest_path)
{
    char error[256];
    FILE *out = stdout;

    if (!manifest_path || strcmp(manifest_path, "-") != 0) {
        out = fopen(manifest_path, "w");
        if (!out) {
            fprintf(stderr, "[main] cannot open manifest output '%s': %s\n",
                    manifest_path, strerror(errno));
            return -1;
        }
    }

    if (virtual_floppy_dump_manifest(hostdir, out, error, sizeof(error)) != 0) {
        if (out != stdout) {
            fclose(out);
        }
        fprintf(stderr, "[main] cannot dump virtual floppy manifest for '%s': %s\n",
                hostdir, error);
        return -1;
    }

    if (out != stdout) {
        fclose(out);
    }
    return 0;
}

/* ── Helpers ────────────────────────────────────────────────────────────────*/

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -floppy <img>  Mount floppy image on DX0\n"
        "  -floppy-hostdir <dir> Build a writable in-memory DX0 overlay from host files (native only)\n"
        "  -floppy2 <img> Mount floppy image on DX1\n"
        "  -floppy2-hostdir <dir> Build a writable in-memory DX1 overlay from host files (native only)\n"
        "  -dump-vfd-manifest <file>  Dump the selected host-directory virtual floppy layout as JSON ('-' = stdout)\n"
        "  -harddisk <img>  Mount Winchester hard-disk image on drive 0 (SM6WIN0)\n"
        "  -harddisk2 <img> Mount Winchester hard-disk image on drive 1 (SM6WIN1)\n"
        "  -trace         Log Z80 PC at boot milestones to stderr\n"
        "  -break-to-monitor Inject SHIFT+BREAK to enter monitor mode\n"
        "  -inject-str <s> Inject string when CLI prompt appears (use \\n or \\r for Enter/CR, \\f to wait for next prompt)\n"
        "  -inject-keycode <hex> Inject one raw keyboard code via CLA when CLI prompt appears\n"
        "  -inject-chord <combo> Inject one function-key chord such as PROGRA+z or 0x08+0x7A\n"
        "  -inject-chord-delay <f> Frames to wait after the typed command finishes before firing -inject-chord\n"
        "  -inject-at-frame <n> Inject at absolute frame n instead of waiting for CLI prompt\n"
        "  -inject-delay <f> Frames to wait after CLI prompt appears before injection (default 2)\n"
        "  -inject-hold-frames <f> Hold -inject-keycode for this many frames (default 1)\n"
        "  -timeout <s>   Global wall-clock timeout (0=off, default 30s with -trace)\n"

        "  -vmode <m>     Force video mode: alpha|graphic|super\n"
        "  -gfxbits <b>   Bitmap bit order: lsb|msb (default: msb — hardware-verified)\n"
        "  -scale <n>     Integer display scale (1..8, default 2 = 1024x496 window)\n"
        "  -trace08       Trace IN/OUT traffic on port 0x08\n"
        "  -traceflow     Trace focused post-handoff low-RAM control flow\n"
        "  -trace11       Trace port 0x11 reads\n"
        "  -tracecd       Trace port 0xCD reads (Winchester)\n"
        "  -trace19       Trace port 0x19 writes (floppy control)\n"
        "  -tracefdc      Trace focused floppy ID/checksum stream events\n"
        "  -tracekbd      Trace every keyboard CLA / status port read\n"
        "  -tracesnd      Trace every port 0x03 write (buzzer)\n"
        "  -scrdump       Dump changed screen rows to stderr\n"
        "  -no-beeper     Disable the machine buzzer (beeper is on by default)\n"
        "  -drive-sound   Enable floppy drive sounds: motor whir, head steps, sector ticks\n"
        "  -no-display-off  Ignore display-off writes (port 0x00 bit0=0); screen stays on\n"
        "  -verbose-video   Log display on/off and mode changes to stderr\n"
        "  -scanlines     Draw CRT-style scanline overlay (darkens every other output row)\n"
        "  -phosphor <colour>  Screen phosphor: green (default, P31 #00E700) or white (#E8E8E8)\n"
        "  -dump-ram <f>  Dump full 64 KB RAM to file at exit\n"
        "  -no-launcher   Skip the startup configuration dialog\n"
        "  -loadbin <addr> <file>  Load raw binary into RAM at hex address (e.g. -loadbin 0x4600 test.bin)\n"
        "  -freeze        Do not run the CPU; display static RAM contents (use with -loadbin)\n"
        "  -help          Show this help\n"
        "  Pause / F11          BREAK key (top-right, NMI → monitor)\n"
        "  Shift+Pause / Shift+F11  SHIFT+BREAK (hard reset)\n"
        "  Escape               ESC / UNDO key (top-left, current working code 0x06)\n"
        "  Ctrl+D / SIGUSR1  Dump RAM to smaky6_ram_NNNN_pcXXXX.bin at any time\n"
        "  Ctrl+R / SIGUSR2  Refresh mounted host-directory virtual floppies\n",
        argv0);
}

/* ── Main-loop context (used to pass state into emscripten_set_main_loop) ───*/

typedef struct {
    /* Long-lived objects */
    struct Smaky6 *m;
    SDL_Window    *win;
    SDL_Renderer  *ren;
    const char    *dump_ram_path;
    int            freeze_cpu;  /* if 1: skip CPU execution, just render */
    /* Config flags (copied from main locals) */
    int  break_to_monitor;
    uint8_t inject_codes[128];
    int  inject_len;
    int  inject_at_prompt;        /* fire inject when prompt_count >= this */
    int  inject_at_frame;         /* if >= 0, fire inject at this absolute frame instead of prompt gating */
    int  inject_delay_frames;     /* wait this many visible-prompt frames before injecting */
    int  inject_hold_frames;      /* hold CLA injection for this many frames */
    int  inject_keycode_enabled;
    uint8_t inject_keycode;
    int  inject_keycode_done;
    int  inject_chord_enabled;
    uint8_t inject_chord_keycode;
    uint8_t inject_chord_fonct_bits;
    int  inject_chord_delay_frames;
    int  inject_chord_release_at;
    int  inject_chord_done;
    int  inject_chord_after_frame;
    int  trace;
    /* Timing */
    Uint64 last_tick;
    Uint64 freq;
    double accum_ms;
    /* Per-frame state */
    int    running;
    int    frame_due;
    int    frame_cnt;
    int    stage1_pressed;
    int    stage1_release_at;
    int    inject_idx;
    int    prompt_was_visible;  /* 1 if prompt was visible last frame */
    int    prompt_count;        /* number of prompt transitions (not→visible) */
    int    prompt_visible_since_frame;
    Uint64 boot_start_ms;
    Uint64 global_timeout_ms;
    int    timeout_reported;
} MainLoopCtx;

static MainLoopCtx *s_loop = NULL;

enum {
    INJECT_WAIT_FOR_NEXT_PROMPT = 0xFF
};

/* Check both timeout watchdogs.  Sets L->running=0 and returns 1 if either
 * threshold was exceeded; otherwise returns 0.  Safe to call multiple times
 * per iteration — the timeout_reported flag prevents duplicate messages. */
static int check_timeouts(MainLoopCtx *L)
{
    if (L->timeout_reported) return 0;

    Uint64 elapsed_ms = SDL_GetTicks64() - L->boot_start_ms;

    if (L->global_timeout_ms > 0 && elapsed_ms >= L->global_timeout_ms) {
        fprintf(stderr, "[main] global timeout after %u.%03u s; exiting cleanly\n",
                (unsigned)(elapsed_ms / 1000ULL), (unsigned)(elapsed_ms % 1000ULL));
        L->timeout_reported = 1;
        L->running = 0;
        return 1;
    }

    return 0;
}

static int prompt_injection_ready(const MainLoopCtx *L, int prompt_now)
{
    if (!prompt_now)
        return 0;
    if (L->prompt_count < L->inject_at_prompt)
        return 0;
    if (L->prompt_visible_since_frame < 0)
        return 0;
    return (L->frame_cnt - L->prompt_visible_since_frame) >= L->inject_delay_frames;
}

static int injection_ready(const MainLoopCtx *L, int prompt_now)
{
    if (L->inject_at_frame >= 0)
        return L->frame_cnt >= L->inject_at_frame;
    return prompt_injection_ready(L, prompt_now);
}

static int parse_inject_chord(const char *arg, uint8_t *fonct_bits_out, uint8_t *keycode_out)
{
    static const struct {
        const char *name;
        uint8_t bit;
    } function_names[] = {
        { "CURSOR", 0x40u },
        { "COPY",   0x20u },
        { "KILL",   0x10u },
        { "PROGRA", 0x08u },
        { "SHOW",   0x04u },
        { "SEARCH", 0x02u },
        { "CHANGE", 0x01u },
    };
    static const struct {
        const char *name;
        uint8_t code;
    } key_names[] = {
        { "END", 0x04u },
        { "ESC", 0x06u },
        { "UNDO", 0x06u },
        { "TAB", 0x09u },
        { "ENTER", 0x0Du },
        { "RETURN", 0x0Du },
        { "SPACE", 0x20u },
    };

    const char *plus = strchr(arg, '+');
    char left[32];
    char right[32];

    if (!plus)
        return 0;
    if ((size_t)(plus - arg) >= sizeof(left))
        return 0;
    if (strlen(plus + 1) >= sizeof(right))
        return 0;

    memcpy(left, arg, (size_t)(plus - arg));
    left[plus - arg] = '\0';
    strcpy(right, plus + 1);

    for (char *p = left; *p; p++) {
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - ('a' - 'A'));
    }
    for (char *p = right; *p; p++) {
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - ('a' - 'A'));
    }

    *fonct_bits_out = 0x00u;
    *keycode_out = 0x00u;

    for (size_t i = 0; i < sizeof(function_names) / sizeof(function_names[0]); i++) {
        if (strcmp(left, function_names[i].name) == 0) {
            *fonct_bits_out = function_names[i].bit;
            break;
        }
    }

    if (*fonct_bits_out == 0x00u) {
        char *end = NULL;
        unsigned long bits = strtoul(left, &end, 0);
        if (!end || *end != '\0' || bits > 0x7Fu)
            return 0;
        *fonct_bits_out = (uint8_t)bits;
    }

    if (strlen(right) == 1) {
        unsigned char c = (unsigned char)right[0];
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c + ('a' - 'A'));
        *keycode_out = (uint8_t)(c & 0x7Fu);
        return 1;
    }

    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
        if (strcmp(right, key_names[i].name) == 0) {
            *keycode_out = key_names[i].code;
            return 1;
        }
    }

    {
        char *end = NULL;
        unsigned long code = strtoul(right, &end, 0);
        if (!end || *end != '\0' || code > 0x7Fu)
            return 0;
        *keycode_out = (uint8_t)code;
    }

    return 1;
}

static void main_loop_cleanup(void)
{
    if (!s_loop) return;
    if (s_loop->dump_ram_path) {
        FILE *f = fopen(s_loop->dump_ram_path, "wb");
        if (!f) {
            fprintf(stderr, "[dump] fopen(%s): %s\n",
                    s_loop->dump_ram_path, strerror(errno));
        } else {
            if (fwrite(s_loop->m->bus, 1, 65536, f) != 65536)
                fprintf(stderr, "[dump] short write to %s\n",
                        s_loop->dump_ram_path);
            else
                fprintf(stderr, "[dump] RAM written to %s  (PC=%04X)\n",
                        s_loop->dump_ram_path,
                        (unsigned)machine_get_pc(s_loop->m));
            fclose(f);
        }
    }
    machine_destroy(s_loop->m);
    SDL_DestroyRenderer(s_loop->ren);
    SDL_DestroyWindow(s_loop->win);
    /* Quit video+timer subsystems explicitly.  Do NOT call SDL_Quit() here:
     * that would attempt to close the audio device on the main thread, which
     * can block indefinitely on a broken PipeWire/PulseAudio session.
     * sound_fini() already launched a detached thread for audio close; the
     * audio subsystem (and that thread) will be reaped when the process exits. */
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER);
}

static void main_loop_iter(void)
{
    MainLoopCtx *L = s_loop;

    if (g_terminate_requested) {
        fprintf(stderr, "[main] termination signal received; exiting cleanly\n");
        L->running = 0;
    }

    if (g_dump_ram_requested) {
        g_dump_ram_requested = 0;
        do_ram_dump(L->m);
    }

    if (g_refresh_virtual_floppies_requested) {
        g_refresh_virtual_floppies_requested = 0;
        do_virtual_floppy_refresh(L->m);
    }

    check_timeouts(L);

    /* ── Events ─────────────────────────────────────────────────────── */
    SDL_PumpEvents();   /* answer WM pings (_NET_WM_PING) every iteration */
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            L->running = 0;
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
                L->running = 0;
            else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                keyboard_clear_all_function_bits(L->m);
                L->m->kbd.host_text_down_count = 0;
                memset(L->m->kbd.host_text_down, 0, sizeof(L->m->kbd.host_text_down));
            }
            break;

        case SDL_KEYDOWN:
            if (ev.key.keysym.scancode == SDL_SCANCODE_F12) {
                debug_toggle(L->m);
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_PAUSE ||
                       ev.key.keysym.scancode == SDL_SCANCODE_F11) {
                if (ev.key.keysym.mod & KMOD_SHIFT)
                    machine_reset(L->m);   /* SHIFT+BREAK → hard reset */
                else
                    machine_nmi(L->m);     /* BREAK alone → NMI / monitor */
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_Q &&
                       (ev.key.keysym.mod & KMOD_CTRL)) {
                L->running = 0;
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_D &&
                       (ev.key.keysym.mod & KMOD_CTRL)) {
                do_ram_dump(L->m);
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_R &&
                       (ev.key.keysym.mod & KMOD_CTRL)) {
                do_virtual_floppy_refresh(L->m);
            } else {
                keyboard_event(L->m, &ev.key);
            }
            break;

        case SDL_KEYUP:
            keyboard_event(L->m, &ev.key);
            break;

        case SDL_TEXTINPUT:
            keyboard_text_event(L->m, &ev.text);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT ||
                ev.button.button == SDL_BUTTON_RIGHT) {
                /* Convert window coords → logical renderer coords */
                float lx_f, ly_f;
                SDL_RenderWindowToLogical(L->ren, ev.button.x, ev.button.y,
                                          &lx_f, &ly_f);
                int lx = (int)lx_f, ly = (int)ly_f;

                /* RESET / BREAK buttons (status bar, first row) — fire on left-click only */
                if (ev.type == SDL_MOUSEBUTTONDOWN &&
                    ev.button.button == SDL_BUTTON_LEFT) {
                    if (lx >= VIDEO_SYS_NMI_X && lx < VIDEO_SYS_NMI_X + VIDEO_SYS_BTN_W &&
                        ly >= VIDEO_SYS_BTN_Y  && ly < VIDEO_SYS_BTN_Y  + VIDEO_SYS_BTN_H) {
                        L->m->vid.reset_armed = 0;  /* cancel any pending reset */
                        machine_nmi(L->m);
                        break;
                    }
                    if (lx >= VIDEO_SYS_RST_X && lx < VIDEO_SYS_RST_X + VIDEO_SYS_RST_W &&
                        ly >= VIDEO_SYS_BTN_Y  && ly < VIDEO_SYS_BTN_Y  + VIDEO_SYS_BTN_H) {
                        if (L->m->vid.reset_armed) {
                            /* Second click — confirmed: do the reset */
                            L->m->vid.reset_armed = 0;
                            machine_reset(L->m);
                        } else {
                            /* First click — arm the button */
                            L->m->vid.reset_armed    = 1;
                            L->m->vid.reset_armed_at = SDL_GetTicks();
                        }
                        break;
                    }
                    /* Clicked somewhere else — cancel armed reset */
                    L->m->vid.reset_armed = 0;
                }

                /* Hit-test each function-key button.
                 * Bit order must match FKEYS[] in video.c:
                 * CURSOR=0x40, COPY=0x20, KILL=0x10, PROGRA=0x08,
                 * SHOW=0x04, SEARCH=0x02, CHANGE=0x01
                 *
                 * Left-click: held while button is down, released on mouse-up
                 *   (but stays set if latched).
                 * Right-click (DOWN only): toggle persistent latch for this bit. */
                static const uint8_t FKEY_BITS[7] = {
                    0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
                };
                if (ev.button.button == SDL_BUTTON_LEFT &&
                    ev.type == SDL_MOUSEBUTTONUP &&
                    L->m->kbd.fonct_mouse_bits != 0) {
                    keyboard_set_mouse_function_bits(L->m, 0);
                }
                for (int i = 0; i < 7; i++) {
                    int bx = VIDEO_FKEY_BTN_X0 + i * (VIDEO_FKEY_BTN_W + VIDEO_FKEY_BTN_GAP);
                    int by = VIDEO_FKEY_Y + 1;
                    if (lx >= bx && lx < bx + VIDEO_FKEY_BTN_W &&
                        ly >= by && ly < by + VIDEO_FKEY_BTN_H) {
                        if (ev.button.button == SDL_BUTTON_LEFT) {
                            if (ev.type == SDL_MOUSEBUTTONDOWN) {
                                keyboard_set_mouse_function_bits(L->m, FKEY_BITS[i]);
                            }
                        }
                        break;
                    }
                }
            }
            break;

        default:
            break;
        }
    }

    /* ── Timing ─────────────────────────────────────────────────────── */
    Uint64 now     = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - L->last_tick) * 1000.0 / (double)L->freq;
    L->last_tick   = now;
    L->accum_ms   += elapsed;

    /* Cap accumulator to one frame to prevent catch-up storms that
     * starve the event loop and make the WM consider the app frozen. */
    if (L->accum_ms > 40.0)
        L->accum_ms = 40.0;

    /* Enforce watchdog even if no frame is due yet. */
    check_timeouts(L);

    /* Run as many 50 Hz frames as the accumulated time allows */
    while (L->accum_ms >= 20.0) {
        if (check_timeouts(L)) break;
        if (!L->running) break;   /* honour quit requests between frames */

        if (!L->freeze_cpu) {
            if (L->m->dbg.trace_kbd && L->m->kbd.found) {
                fprintf(stderr,
                        "[kbd] pre-frame %d found=%d code=%02X held=%d boot=%d reassert=%d\n",
                        L->frame_cnt + 1,
                        L->m->kbd.found,
                        (unsigned)L->m->kbd.key_code,
                        L->m->kbd.physically_held,
                        L->m->kbd.boot_key_held,
                        L->m->kbd.reassert_pending);
            }
            keyboard_frame_tick(L->m);
            machine_int(L->m);
            machine_run_frame(L->m);
            if (L->m->cpu_stalled) {
                fprintf(stderr, "[main] CPU stall detected; exiting cleanly\n");
                L->running = 0;
                break;
            }
        } /* !freeze_cpu */
        L->accum_ms -= 20.0;
        L->frame_due = 1;
        L->frame_cnt++;

        if (L->break_to_monitor && !L->stage1_pressed && L->frame_cnt == 100) {
            machine_inject_shift_break(L->m);
            L->stage1_pressed = 1;
            L->stage1_release_at = L->frame_cnt + 1;
            if (L->trace)
                fprintf(stderr,
                        "[break-to-monitor] injected SHIFT+BREAK at frame %d\n",
                        L->frame_cnt);
        }

        if (L->stage1_release_at == L->frame_cnt) {
            machine_release_key(L->m);
            L->stage1_release_at = -1;
        }

        if (L->inject_chord_release_at == L->frame_cnt) {
            machine_release_key(L->m);
            L->inject_chord_release_at = -1;
        }

        /* Track prompt transitions to distinguish init prompt from post-command prompt */
        int prompt_now = machine_cli_prompt_visible(L->m);
        if (prompt_now != L->prompt_was_visible) {
            if (L->trace)
                fprintf(stderr, "[prompt] frame %d: %s (count=%d)\n",
                        L->frame_cnt,
                        prompt_now ? "appeared" : "gone",
                        L->prompt_count + (prompt_now ? 1 : 0));
            if (prompt_now) {
                L->prompt_count++;
                L->prompt_visible_since_frame = L->frame_cnt;
            } else {
                L->prompt_visible_since_frame = -1;
            }
        }
        L->prompt_was_visible = prompt_now;

        if (L->inject_keycode_enabled && !L->inject_keycode_done &&
            injection_ready(L, prompt_now)) {
            machine_inject_key(L->m, L->inject_keycode);
            L->inject_keycode_done = 1;
            L->stage1_release_at = L->frame_cnt + L->inject_hold_frames;
            if (L->trace || L->m->dbg.trace_kbd) {
                if (L->inject_at_frame >= 0)
                    fprintf(stderr,
                            "[inject] CLA keycode 0x%02X armed at frame %d via -inject-at-frame; hold=%d release at %d\n",
                            (unsigned)L->inject_keycode,
                            L->frame_cnt,
                            L->inject_hold_frames,
                            L->stage1_release_at);
                else
                    fprintf(stderr,
                            "[inject] CLA keycode 0x%02X armed at frame %d after %d prompt frame(s); hold=%d release at %d\n",
                            (unsigned)L->inject_keycode,
                            L->frame_cnt,
                            L->frame_cnt - L->prompt_visible_since_frame,
                            L->inject_hold_frames,
                            L->stage1_release_at);
            }
        }

        if (L->inject_len > 0 && L->inject_idx < L->inject_len &&
            injection_ready(L, prompt_now)) {
            int start = L->inject_idx;
            int end = start;

            while (end < L->inject_len &&
                   L->inject_codes[end] != INJECT_WAIT_FOR_NEXT_PROMPT) {
                end++;
            }

            if (L->trace) {
                if (L->inject_at_frame >= 0)
                    fprintf(stderr,
                            "[inject] frame %d reached; writing %d char(s) via circular buffer\n",
                            L->frame_cnt, end - start);
                else
                    fprintf(stderr,
                            "[inject] CLI prompt stable at frame %d; writing %d char(s) via circular buffer\n",
                            L->frame_cnt, end - start);
            }
            for (int i = start; i < end; i++) {
                uint8_t kc = L->inject_codes[i];
                machine_inject_to_circ_buf(L->m, kc);
                if (L->trace)
                    fprintf(stderr,
                            "[inject] circ_buf <- 0x%02X ('%c') (char %d/%d)\n",
                            (unsigned)kc,
                            (kc >= 0x20 && kc < 0x7F) ? (char)kc : '?',
                            (i - start) + 1, end - start);
            }
            L->inject_idx = end;
            if (L->inject_idx < L->inject_len &&
                L->inject_codes[L->inject_idx] == INJECT_WAIT_FOR_NEXT_PROMPT) {
                L->inject_idx++;
                L->inject_at_prompt++;
            } else if (L->inject_idx >= L->inject_len &&
                       L->inject_chord_enabled &&
                       !L->inject_chord_done &&
                       L->inject_chord_after_frame < 0) {
                L->inject_chord_after_frame = L->frame_cnt + L->inject_chord_delay_frames;
                if (L->trace) {
                    fprintf(stderr,
                            "[inject] chord armed after typed command; frame=%d target=%d\n",
                            L->frame_cnt,
                            L->inject_chord_after_frame);
                }
            }
        }

        if (L->inject_chord_enabled && !L->inject_chord_done) {
            int chord_ready;

            if (L->inject_len == 0 && L->inject_chord_after_frame < 0 && injection_ready(L, prompt_now)) {
                L->inject_chord_after_frame = L->frame_cnt + L->inject_chord_delay_frames;
            }

            chord_ready = (L->inject_chord_after_frame >= 0 &&
                           L->frame_cnt >= L->inject_chord_after_frame);
            if (chord_ready) {
                machine_inject_key_chord(L->m,
                                         L->inject_chord_keycode,
                                         L->inject_chord_fonct_bits);
                L->inject_chord_done = 1;
                L->inject_chord_release_at = L->frame_cnt + L->inject_hold_frames;
                if (L->trace || L->m->dbg.trace_kbd) {
                    fprintf(stderr,
                            "[inject] chord f=%02X key=%02X fired at frame %d; hold=%d release at %d\n",
                            (unsigned)L->inject_chord_fonct_bits,
                            (unsigned)L->inject_chord_keycode,
                            L->frame_cnt,
                            L->inject_hold_frames,
                            L->inject_chord_release_at);
                }
            }
        }
    }

    /* ── Render ─────────────────────────────────────────────────────── */
    if (L->frame_due) {
        video_render(L->m);
        L->frame_due = 0;
    }
#ifndef __EMSCRIPTEN__
    SDL_Delay(1);
#endif

#ifdef __EMSCRIPTEN__
    if (!L->running)
        emscripten_cancel_main_loop();
#endif
}

/* ── Emscripten JS-callable exports ─────────────────────────────────────────*/
#ifdef __EMSCRIPTEN__

/* Returns the build version string (e.g. "v1.0.4-37-gabcdef"). */
EMSCRIPTEN_KEEPALIVE
const char *smemu6_get_version(void)
{
#ifdef SMEMU6_VERSION
    return SMEMU6_VERSION;
#else
    return "dev";
#endif
}

/* Called from JS to reset the machine (without reloading the page). */
EMSCRIPTEN_KEEPALIVE
void smemu6_reset(void)
{
    if (!s_loop || !s_loop->m) return;
    machine_reset(s_loop->m);
    /* Reset state so key injection fires again on next CLI prompt */
    s_loop->frame_cnt          = 0;
    s_loop->stage1_pressed     = 0;
    s_loop->stage1_release_at  = -1;
    s_loop->inject_idx         = 0;
    s_loop->prompt_was_visible = 0;
    s_loop->prompt_count       = 0;
    s_loop->timeout_reported   = 0;
    s_loop->boot_start_ms      = SDL_GetTicks64();
    s_loop->running            = 1;
    /* Cancel any existing main loop before setting a new one — Emscripten
     * aborts if emscripten_set_main_loop is called while one is active. */
    emscripten_cancel_main_loop();
    emscripten_set_main_loop(main_loop_iter, 0, 0);
}

/* Called from JS after writing a .dsk file into the virtual FS. */
EMSCRIPTEN_KEEPALIVE
void smemu6_mount_dx0(const char *path)
{
    if (s_loop && s_loop->m)
        floppy_mount(s_loop->m, 0, path);
}

EMSCRIPTEN_KEEPALIVE
void smemu6_mount_dx1(const char *path)
{
    if (s_loop && s_loop->m)
        floppy_mount(s_loop->m, 1, path);
}

#endif /* __EMSCRIPTEN__ */

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int main(int argc, char *argv[])
{
    const char *disk_path  = NULL;
    const char *disk2_path = NULL;
    const char *harddisk_path  = NULL;
    const char *harddisk2_path = NULL;
    int trace    = 0;
    int break_to_monitor = 0;  /* SHIFT+BREAK for monitor entry */
    uint8_t inject_codes[128];  /* key sequence to inject at OS prompt */
    int inject_len = 0;
    int forced_vmode = -1;
    int gfx_msb_first = -1;  /* -1 = use video_init() default (msb) */
    const char *loadbin_file = NULL;   /* -loadbin <addr> <file> */
    uint16_t    loadbin_addr = 0;
    int freeze_cpu = 0;  /* -freeze: don't execute CPU; just render static RAM */
    int trace08 = 0;
    int traceflow = 0;
    int trace11 = 0;
    int tracecd = 0;
    int trace19 = 0;
    int tracefdc = 0;
    int tracekbd = 0;
    int tracesnd = 0;
    int tracewin = 0;
    int scrdump = 0;
    int enable_beeper      = 1;  /* -no-beeper: disable machine buzzer (on by default) */
    int enable_drive_sound = 0;  /* -drive-sound: enable floppy drive sounds (off by default) */
    int no_display_off     = 0;  /* -no-display-off: ignore port 0x00 display-blank writes */
    int verbose_video      = 0;  /* -verbose-video: log display on/off/mode changes to stderr */
    float phosphor_decay   = PHOSPHOR_DECAY_DEFAULT; /* -phosphor-decay <v>: persistence per frame */
    int scanlines          = 0;  /* -scanlines: draw CRT scanline overlay */
    int phosphor_white     = 0;  /* -phosphor white: use white phosphor palette */
    int no_launcher        = 0;  /* -no-launcher: skip startup dialog */
    const char *dump_ram_path = NULL;  /* -dump-ram: write RAM to this file at exit */
    int inject_at_prompt = 1;          /* -inject-at-prompt N: fire inject when prompt_count >= N */
    int inject_at_frame = -1;          /* -inject-at-frame N: fire inject at frame N instead of prompt gating */
    int inject_delay_frames = 2;       /* -inject-delay N: wait N frames after prompt appears */
    int inject_hold_frames = 1;        /* -inject-hold-frames N: hold CLA key for N frames */
    int inject_keycode_enabled = 0;    /* -inject-keycode: inject one key via CLA path */
    uint8_t inject_keycode = 0;
    int inject_chord_enabled = 0;      /* -inject-chord: inject function-key + ordinary-key combo */
    uint8_t inject_chord_keycode = 0;
    uint8_t inject_chord_fonct_bits = 0;
    int inject_chord_delay_frames = 0; /* -inject-chord-delay N: wait N frames after typed command */
    int display_scale = 1;             /* -scale N: integer pixel scale factor */
    int global_timeout_sec = -1;  /* -1 = auto policy */
    const char *disk_hostdir = NULL;
    const char *disk2_hostdir = NULL;
    const char *vfd_manifest_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-floppy") == 0 && i + 1 < argc) {
            disk_path = argv[++i];
            disk_hostdir = NULL;
        } else if (strcmp(argv[i], "-floppy-hostdir") == 0 && i + 1 < argc) {
#ifdef __EMSCRIPTEN__
            fprintf(stderr, "-floppy-hostdir is not supported in the web build\n");
            return 1;
#else
            disk_hostdir = argv[++i];
            disk_path = NULL;
#endif
        } else if (strcmp(argv[i], "-floppy2") == 0 && i + 1 < argc) {
            disk2_path = argv[++i];
            disk2_hostdir = NULL;
        } else if (strcmp(argv[i], "-floppy2-hostdir") == 0 && i + 1 < argc) {
#ifdef __EMSCRIPTEN__
            fprintf(stderr, "-floppy2-hostdir is not supported in the web build\n");
            return 1;
#else
            disk2_hostdir = argv[++i];
            disk2_path = NULL;
#endif
        } else if (strcmp(argv[i], "-dump-vfd-manifest") == 0 && i + 1 < argc) {
            vfd_manifest_path = argv[++i];
        } else if (strcmp(argv[i], "-harddisk") == 0 && i + 1 < argc) {
            harddisk_path = argv[++i];
        } else if (strcmp(argv[i], "-harddisk2") == 0 && i + 1 < argc) {
            harddisk2_path = argv[++i];
        } else if (strcmp(argv[i], "-trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "-break-to-monitor") == 0) {
            break_to_monitor = 1;
        } else if (strcmp(argv[i], "-inject-str") == 0 && i + 1 < argc) {
            const char *s = argv[++i];
            inject_len = 0;
            for (int j = 0; s[j] && inject_len < 126; j++) {
                unsigned char c = (unsigned char)s[j];
                if (c == '\\' && (s[j + 1] == 'n' || s[j + 1] == 'r')) {
                    inject_codes[inject_len++] = 0x0D; /* Enter (CR, 0x0D) */
                    j++;
                } else if (c == '\\' && s[j + 1] == 'f') {
                    inject_codes[inject_len++] = INJECT_WAIT_FOR_NEXT_PROMPT;
                    j++;
                } else if (c >= 'a' && c <= 'z') {
                    inject_codes[inject_len++] = (uint8_t)(c - 0x20); /* uppercase */
                } else if (c >= 'A' && c <= 'Z') {
                    inject_codes[inject_len++] = (uint8_t)c;
                } else if (c >= '0' && c <= '9') {
                    inject_codes[inject_len++] = (uint8_t)c;
                } else if (c == ' ') {
                    inject_codes[inject_len++] = 0x20;
                } else if (c == '.' || c == ':' || c == '-' || c == '_' || c == '/') {
                    inject_codes[inject_len++] = (uint8_t)c;
                } else if (c == '\n' || c == '\r') {
                    inject_codes[inject_len++] = 0x0D;
                } else if (c < 0x20 || c >= 0x80) {
                    inject_codes[inject_len++] = c; /* raw control/high byte */
                }
                /* unknown printable: skip */
            }
        } else if (strcmp(argv[i], "-inject-delay") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 100000) {
                fprintf(stderr, "Invalid -inject-delay value: %s\n", argv[i]);
                return 1;
            }
            inject_delay_frames = (int)v;
        } else if (strcmp(argv[i], "-inject-at-frame") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 1000000) {
                fprintf(stderr, "Invalid -inject-at-frame value: %s\n", argv[i]);
                return 1;
            }
            inject_at_frame = (int)v;
        } else if (strcmp(argv[i], "-inject-keycode") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long v = strtoul(argv[++i], &end, 0);
            if (!end || *end != '\0' || v > 0x7Fu) {
                fprintf(stderr, "Invalid -inject-keycode value: %s (expected 0..0x7F)\n", argv[i]);
                return 1;
            }
            inject_keycode_enabled = 1;
            inject_keycode = (uint8_t)v;
        } else if (strcmp(argv[i], "-inject-chord") == 0 && i + 1 < argc) {
            if (!parse_inject_chord(argv[++i], &inject_chord_fonct_bits, &inject_chord_keycode)) {
                fprintf(stderr,
                        "Invalid -inject-chord value: %s (expected PROGRA+z or 0x08+0x7A)\n",
                        argv[i]);
                return 1;
            }
            inject_chord_enabled = 1;
        } else if (strcmp(argv[i], "-inject-chord-delay") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 100000) {
                fprintf(stderr, "Invalid -inject-chord-delay value: %s\n", argv[i]);
                return 1;
            }
            inject_chord_delay_frames = (int)v;
        } else if (strcmp(argv[i], "-inject-hold-frames") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 1 || v > 100000) {
                fprintf(stderr, "Invalid -inject-hold-frames value: %s\n", argv[i]);
                return 1;
            }
            inject_hold_frames = (int)v;
        } else if (strcmp(argv[i], "-timeout") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 24 * 60 * 60) {
                fprintf(stderr, "Invalid -timeout value: %s (expected 0..86400)\n", argv[i]);
                return 1;
            }
            global_timeout_sec = (int)v;
        } else if (strcmp(argv[i], "-vmode") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "alpha") == 0) {
                forced_vmode = VMODE_ALPHA;
            } else if (strcmp(m, "graphic") == 0) {
                forced_vmode = VMODE_GRAPHIC;
            } else if (strcmp(m, "super") == 0) {
                forced_vmode = VMODE_SUPER;
            } else {
                fprintf(stderr, "Invalid -vmode value: %s (expected alpha|graphic|super)\n", m);
                return 1;
            }
        } else if (strcmp(argv[i], "-gfxbits") == 0 && i + 1 < argc) {
            const char *b = argv[++i];
            if (strcmp(b, "lsb") == 0) {
                gfx_msb_first = 0;
            } else if (strcmp(b, "msb") == 0) {
                gfx_msb_first = 1;
            } else {
                fprintf(stderr, "Invalid -gfxbits value: %s (expected lsb|msb)\n", b);
                return 1;
            }
        } else if (strcmp(argv[i], "-scale") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 1 || v > 8) {
                fprintf(stderr, "Invalid -scale value: %s (expected 1..8)\n", argv[i]);
                return 1;
            }
            display_scale = (int)v;
        } else if (strcmp(argv[i], "-trace08") == 0) {
            trace08 = 1;
        } else if (strcmp(argv[i], "-traceflow") == 0) {
            traceflow = 1;
        } else if (strcmp(argv[i], "-trace11") == 0) {
            trace11 = 1;
        } else if (strcmp(argv[i], "-tracecd") == 0) {
            tracecd = 1;
        } else if (strcmp(argv[i], "-trace19") == 0) {
            trace19 = 1;
        } else if (strcmp(argv[i], "-tracefdc") == 0) {
            tracefdc = 1;
        } else if (strcmp(argv[i], "-tracekbd") == 0) {
            tracekbd = 1;
        } else if (strcmp(argv[i], "-tracesnd") == 0) {
            tracesnd = 1;
        } else if (strcmp(argv[i], "-trace-win") == 0) {
            tracewin = 1;
        } else if (strcmp(argv[i], "-scrdump") == 0) {
            scrdump = 1;
        } else if (strcmp(argv[i], "-no-beeper") == 0) {
            enable_beeper = 0;
        } else if (strcmp(argv[i], "-drive-sound") == 0) {
            enable_drive_sound = 1;
        } else if (strcmp(argv[i], "-no-display-off") == 0) {
            no_display_off = 1;
        } else if (strcmp(argv[i], "-verbose-video") == 0) {
            verbose_video = 1;
        } else if (strcmp(argv[i], "-no-phosphor") == 0) {
            phosphor_decay = 0.0f;
        } else if (strcmp(argv[i], "-phosphor-decay") == 0 && i + 1 < argc) {
            char *end = NULL;
            float v = strtof(argv[++i], &end);
            if (!end || *end != '\0' || v < 0.0f || v > 0.99f) {
                fprintf(stderr, "-phosphor-decay requires a value 0.0..0.99\n"); return 1;
            }
            phosphor_decay = v;
        } else if (strcmp(argv[i], "-scanlines") == 0) {
            scanlines = 1;
        } else if (strcmp(argv[i], "-phosphor") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "-phosphor requires an argument (green|white)\n"); return 1; }
            i++;
            if (strcmp(argv[i], "white") == 0) {
                phosphor_white = 1;
            } else if (strcmp(argv[i], "green") != 0) {
                fprintf(stderr, "Unknown phosphor colour '%s' (use green or white)\n", argv[i]); return 1;
            }
        } else if (strcmp(argv[i], "-dump-ram") == 0 && i + 1 < argc) {
            dump_ram_path = argv[++i];
        } else if (strcmp(argv[i], "-inject-at-prompt") == 0 && i + 1 < argc) {
            char *end = NULL;
            long n = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || n < 1) {
                fprintf(stderr, "-inject-at-prompt requires an integer >= 1\n"); return 1;
            }
            inject_at_prompt = (int)n;
        } else if (strcmp(argv[i], "-no-launcher") == 0) {
            no_launcher = 1;
        } else if (strcmp(argv[i], "-loadbin") == 0 && i + 2 < argc) {
            char *end = NULL;
            unsigned long addr = strtoul(argv[i + 1], &end, 16);
            if (!end || *end != '\0' || addr > 0xFFFF) {
                fprintf(stderr, "Invalid -loadbin address: %s\n", argv[i + 1]);
                return 1;
            }
            loadbin_addr = (uint16_t)addr;
            loadbin_file = argv[i + 2];
            i += 2;
        } else if (strcmp(argv[i], "-freeze") == 0) {
            freeze_cpu = 1;
        } else if (strcmp(argv[i], "-help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (global_timeout_sec < 0) {
        global_timeout_sec = trace ? 45 : 0;
    }

    if (vfd_manifest_path && !disk_hostdir && !disk2_hostdir) {
        fprintf(stderr, "-dump-vfd-manifest requires -floppy-hostdir or -floppy2-hostdir\n");
        return 1;
    }

#ifdef __EMSCRIPTEN__
    /* Emscripten: always skip the launcher (it uses blocking threads and
     * tinyfiledialogs which don't work in the browser).  The web shell
     * provides equivalent controls via HTML. */
    no_launcher = 1;
#endif

#ifndef __EMSCRIPTEN__
    signal(SIGINT,  handle_terminate_signal);
    signal(SIGTERM, handle_terminate_signal);
#ifdef SIGUSR1
    signal(SIGUSR1, handle_dump_signal);
    fprintf(stderr, "[main] PID %d \xe2\x80\x94 send SIGUSR1 to dump RAM\n", (int)getpid());
#endif
#ifdef SIGUSR2
    signal(SIGUSR2, handle_refresh_virtual_floppies_signal);
    fprintf(stderr, "[main] PID %d \xe2\x80\x94 send SIGUSR2 to refresh virtual floppies\n", (int)getpid());
#endif
#else
    (void)handle_terminate_signal;
    (void)handle_dump_signal;
    (void)handle_refresh_virtual_floppies_signal;
    (void)main_loop_cleanup;
#endif

    sound_set_beeper_enabled(enable_beeper);
    sound_set_drive_sound_enabled(enable_drive_sound);

    /* ── SDL2 init ─────────────────────────────────────────────────────────────────────── */
    /* Disable X11 _NET_WM_PING so the WM never marks the window as
     * unresponsive (e.g. while a file dialog thread is running). */
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_PING, "0");

    if (SDL_Init(SDL_INIT_VIDEO | ((enable_beeper || enable_drive_sound) ? SDL_INIT_AUDIO : 0) | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    /* macOS launches .app bundles with '/' as the working directory, so
     * relative paths like "roms/samos_sys17.rom" would not be found.
     * SDL_GetBasePath() returns the bundle's Contents/Resources/ folder;
     * chdir() there makes all relative asset lookups work correctly. */
    {
        char *base = SDL_GetBasePath();
        if (base) {
            if (chdir(base) != 0)
                fprintf(stderr, "[main] chdir(%s): %s\n", base, strerror(errno));
            SDL_free(base);
        }
    }
#endif

    /* ── Launcher ───────────────────────────────────────────────────────── */
#ifndef __EMSCRIPTEN__
    if (!no_launcher) {
        /* Build hints from CLI so the launcher pre-populates its controls */
        LauncherHints hints = {
            .dx0_mode        = harddisk_path ? LAUNCHER_STORAGE_HARDDISK :
                               (disk_hostdir ? LAUNCHER_STORAGE_HOSTDIR :
                                (disk_path ? LAUNCHER_STORAGE_FLOPPY : -1)),
            .dx0_path        = harddisk_path ? harddisk_path :
                               (disk_hostdir ? disk_hostdir : disk_path),
            .dx1_mode        = disk2_hostdir ? LAUNCHER_STORAGE_HOSTDIR :
                               (disk2_path ? LAUNCHER_STORAGE_FLOPPY : -1),
            .dx1_path        = disk2_hostdir ? disk2_hostdir : disk2_path,
            .scale           = display_scale,        /* always pass; launcher uses it as-is */
            .phosphor_white  = phosphor_white ? 1 : -1,
            .scanlines       = scanlines ? 1 : -1,
            .no_display_off  = no_display_off ? 1 : -1,
            .beeper          = enable_beeper ? -1 : 0,  /* -1=default(on), 0=off */
        };
        LauncherConfig lc;
        int lresult = launcher_run(&lc, &hints);
        if (lresult != 0) {
            /* User closed the launcher without clicking Start */
            SDL_Quit();
            return 0;
        }
        /* Apply launcher config — launcher result is authoritative (it was
         * pre-populated from CLI, so the user saw and confirmed every value) */
        if (lc.dx0_mode >= 0) {
            disk_path = NULL;
            disk_hostdir = NULL;
            harddisk_path = NULL;
            if (lc.dx0_path) {
                if (lc.dx0_mode == LAUNCHER_STORAGE_HARDDISK) {
                    harddisk_path = lc.dx0_path;
                } else if (lc.dx0_mode == LAUNCHER_STORAGE_HOSTDIR) {
                    disk_hostdir = lc.dx0_path;
                } else {
                    disk_path = lc.dx0_path;
                }
            }
        }
        if (lc.dx1_mode >= 0) {
            disk2_path = NULL;
            disk2_hostdir = NULL;
            if (lc.dx1_path) {
                if (lc.dx1_mode == LAUNCHER_STORAGE_HOSTDIR) {
                    disk2_hostdir = lc.dx1_path;
                } else {
                    disk2_path = lc.dx1_path;
                }
            }
        }
        if (lc.scale >= 1)      display_scale  = lc.scale;
        if (lc.phosphor_white >= 0) phosphor_white = lc.phosphor_white;
        if (lc.scanlines >= 0)  scanlines      = lc.scanlines;
        if (lc.no_display_off >= 0) no_display_off = lc.no_display_off;
        if (lc.beeper >= 0)     enable_beeper  = lc.beeper;
        /* Re-apply beeper setting now that launcher may have changed it */
        sound_set_beeper_enabled(enable_beeper);
    }
#endif /* __EMSCRIPTEN__ */

    SDL_Window *win = SDL_CreateWindow(
        "Smemu6 - Smaky6 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VIDEO_WIN_W * display_scale, VIDEO_WIN_H * display_scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Set window icon from embedded RGBA pixel data */
    {
        /* cast away const: SDL_CreateRGBSurfaceFrom takes void*, data is read-only */
        SDL_Surface *icon = SDL_CreateRGBSurfaceFrom(
            (void *)smaky6_icon_rgba,
            SMAKY6_ICON_W, SMAKY6_ICON_H,
            32, SMAKY6_ICON_W * 4,
            0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
        if (icon) { SDL_SetWindowIcon(win, icon); SDL_FreeSurface(icon); }
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED);
    if (!ren) {
        /* Fall back to software rendering if hardware acceleration is unavailable. */
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(ren, VIDEO_WIN_W, VIDEO_WIN_H);
    SDL_StartTextInput();

    /* ── Machine init ───────────────────────────────────────────────────── */
    struct Smaky6 *m = machine_create();
    if (!m) {
        fprintf(stderr, "machine_create: out of memory\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* Load Phantom bootloader ROM (2 KB at 0x0000–0x07FF).  Everything else
     * in the 64 KB address space is RAM — Phantom loads SYS.SY from disk. */
    if (machine_load_rom(m, ROM_PHANTOM, MEM_SYSMON_BASE) != 0) {
        fprintf(stderr, "WARNING: Phantom ROM (%s) not found; CPU will execute garbage\n",
                ROM_PHANTOM);
    }

    /* Init video (after machine so chargen ROM path is available) */
    video_init(m, win, ren);
    video_load_chargen(m, ROM_CHARGEN);
    if (gfx_msb_first >= 0)
        video_set_gfx_msb_first(m, gfx_msb_first);
    if (forced_vmode >= 0)
        video_set_mode(m, (VideoMode)forced_vmode);

    /* Pre-load raw binary into RAM (e.g. for graphic plane test patterns) */
    if (loadbin_file) {
        FILE *f = fopen(loadbin_file, "rb");
        if (!f) {
            fprintf(stderr, "WARNING: -loadbin: cannot open '%s': %s\n",
                    loadbin_file, strerror(errno));
        } else {
            uint16_t addr = loadbin_addr;
            int c;
            size_t n = 0;
            while ((c = fgetc(f)) != EOF && addr + n <= 0xFFFF) {
                m->bus[addr + n] = (uint8_t)c;
                n++;
            }
            fclose(f);
            fprintf(stderr, "[loadbin] loaded %zu bytes from '%s' at 0x%04X\n",
                    n, loadbin_file, (unsigned)addr);
        }
    }

    /* Mount floppies if specified */
    if (disk_hostdir) {
        if (floppy_mount_hostdir(m, 0, disk_hostdir) != 0) {
            fprintf(stderr, "WARNING: could not mount virtual host directory '%s' on DX0\n",
                    disk_hostdir);
        } else if (vfd_manifest_path) {
            if (dump_virtual_floppy_manifest(disk_hostdir, vfd_manifest_path) != 0) {
                machine_destroy(m);
                SDL_DestroyRenderer(ren);
                SDL_DestroyWindow(win);
                SDL_Quit();
                return 1;
            }
        }
    } else if (disk_path) {
        if (floppy_mount(m, 0, disk_path) != 0) {
            fprintf(stderr, "WARNING: could not mount '%s'\n", disk_path);
        }
    }
    if (disk2_hostdir) {
        if (floppy_mount_hostdir(m, 1, disk2_hostdir) != 0) {
            fprintf(stderr, "WARNING: could not mount virtual host directory '%s' on DX1\n",
                    disk2_hostdir);
        } else if (vfd_manifest_path) {
            if (dump_virtual_floppy_manifest(disk2_hostdir, vfd_manifest_path) != 0) {
                machine_destroy(m);
                SDL_DestroyRenderer(ren);
                SDL_DestroyWindow(win);
                SDL_Quit();
                return 1;
            }
        }
    } else if (disk2_path) {
        if (floppy_mount(m, 1, disk2_path) != 0) {
            fprintf(stderr, "WARNING: could not mount '%s' on DX1\n", disk2_path);
        }
    }

    /* Mount hard-disk images if specified */
    if (harddisk_path) {
        if (winchester_load(&m->win, 0, harddisk_path) != 0)
            fprintf(stderr, "WARNING: could not mount hard-disk '%s' on drive 0\n", harddisk_path);
    }
    if (harddisk2_path) {
        if (winchester_load(&m->win, 1, harddisk2_path) != 0)
            fprintf(stderr, "WARNING: could not mount hard-disk '%s' on drive 1\n", harddisk2_path);
    }

    machine_reset(m);
    if (m->fdc.media[0].kind == FLOPPY_MEDIA_NONE && m->win.image[0] == NULL) {
        machine_release_key(m);
    }
    if (trace)
        machine_set_trace(m, 1);
    if (trace08)
        machine_set_trace_port08(m, 1);
    if (traceflow)
        machine_set_trace_flow(m, 1);
    if (trace11)
        machine_set_trace_port11(m, 1);
    if (tracecd)
        machine_set_trace_port_cd(m, 1);
    if (trace19)
        machine_set_trace_port19(m, 1);
    if (tracefdc)
        machine_set_trace_fdc(m, 1);
    if (tracesnd)
        machine_set_trace_snd(m, 1);
    if (tracewin)
        m->win.trace = 1;
    if (scrdump)
        machine_set_trace_scr(m, 1);
    if (tracekbd)
        machine_set_trace_kbd(m, 1);
    if (no_display_off)
        machine_set_no_display_off(m, 1);
    if (verbose_video)
        machine_set_verbose_video(m, 1);
    if (phosphor_decay != PHOSPHOR_DECAY_DEFAULT)
        machine_set_phosphor_decay(m, phosphor_decay);
    if (scanlines)
        machine_set_scanlines(m, 1);
    if (phosphor_white)
        machine_set_phosphor(m, 1);

    /* ── Main loop ──────────────────────────────────────────────────────────────────────── */
    /*
     * Target: 50 Hz frame rate.  We track elapsed time manually and run
     * machine frames as needed.
     */

    /* Simpler approach: track elapsed time manually */
    static MainLoopCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.m                  = m;
    ctx.win                = win;
    ctx.ren                = ren;
    ctx.dump_ram_path      = dump_ram_path;
    ctx.freeze_cpu         = freeze_cpu;
    ctx.break_to_monitor   = break_to_monitor;
    memcpy(ctx.inject_codes, inject_codes, sizeof(inject_codes));
    ctx.inject_len         = inject_len;
    ctx.inject_at_prompt   = inject_at_prompt;
    ctx.inject_at_frame    = inject_at_frame;
    ctx.inject_delay_frames = inject_delay_frames;
    ctx.inject_hold_frames = inject_hold_frames;
    ctx.inject_keycode_enabled = inject_keycode_enabled;
    ctx.inject_keycode     = inject_keycode;
    ctx.inject_keycode_done = 0;
    ctx.inject_chord_enabled = inject_chord_enabled;
    ctx.inject_chord_keycode = inject_chord_keycode;
    ctx.inject_chord_fonct_bits = inject_chord_fonct_bits;
    ctx.inject_chord_delay_frames = inject_chord_delay_frames;
    ctx.inject_chord_release_at = -1;
    ctx.inject_chord_done = 0;
    ctx.inject_chord_after_frame = -1;
    ctx.trace              = trace;
    ctx.last_tick          = SDL_GetPerformanceCounter();
    ctx.freq               = SDL_GetPerformanceFrequency();
    ctx.accum_ms           = 0.0;
    ctx.running            = 1;
    ctx.frame_due          = 0;
    ctx.frame_cnt          = 0;
    ctx.stage1_pressed     = 0;
    ctx.stage1_release_at  = -1;
    ctx.inject_idx         = 0;
    ctx.prompt_was_visible = 0;
    ctx.prompt_count       = 0;
    ctx.prompt_visible_since_frame = -1;
    ctx.boot_start_ms      = SDL_GetTicks64();
    ctx.global_timeout_ms  = (Uint64)global_timeout_sec * 1000ULL;
    ctx.timeout_reported   = 0;

    s_loop = &ctx;

#ifdef __EMSCRIPTEN__
    fprintf(stderr, "Smemu6 WEB starting ... (floppy: %s)\n",
            disk_path ? disk_path : "none");
    emscripten_set_main_loop(main_loop_iter, 0, 1);
    /* Never reached with simulate_infinite_loop=1 */
#else
    while (ctx.running)
        main_loop_iter();

    /* ── Cleanup ────────────────────────────────────────────────────────── */
    main_loop_cleanup();
#endif
    return 0;
}
