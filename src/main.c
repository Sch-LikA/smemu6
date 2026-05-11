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

/* ── Helpers ────────────────────────────────────────────────────────────────*/

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -floppy <img>  Mount floppy image on DX0\n"
        "  -floppy2 <img> Mount floppy image on DX1\n"
        "  -harddisk <img>  Mount Winchester hard-disk image on drive 0 (SM6WIN0)\n"
        "  -harddisk2 <img> Mount Winchester hard-disk image on drive 1 (SM6WIN1)\n"
        "  -trace         Log Z80 PC at boot milestones to stderr\n"
        "  -autoboot      Inject Enter key after 3 s to auto-select floppy boot\n"
        "  -break-to-monitor Inject SHIFT+BREAK to enter monitor mode (0x1B = Escape)\n"
        "  -autoboot2 <n> Stage2 key code (decimal or 0xHH), default 0x20\n"
        "  -autoboot3 <n> Optional stage3 key code (decimal or 0xHH), default disabled\n"
        "  -inject-str <s> Inject string when CLI prompt appears (use \\n for Enter/CR)\n"
        "  -inject-delay <f> Frames after CLI prompt detected before injection (default 2)\n"
        "  -timeout <s>   Global wall-clock timeout (0=off, default 30s with -trace)\n"
        "  -autoboot-timeout <s> Wall-clock timeout in autoboot mode (0=off)\n"
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
        "  -scanlines     Draw CRT-style scanline overlay (darkens every other output row)\n"
        "  -phosphor <colour>  Screen phosphor: green (default, P31 #00E700) or white (#E8E8E8)\n"
        "  -dump-ram <f>  Dump full 64 KB RAM to file at exit\n"
        "  -inject-via-fifo  Route -inject-str through keyboard FIFO (tests physical kbd path)\n"
        "  -no-launcher   Skip the startup configuration dialog\n"
        "  -loadbin <addr> <file>  Load raw binary into RAM at hex address (e.g. -loadbin 0x4600 test.bin)\n"
        "  -freeze        Do not run the CPU; display static RAM contents (use with -loadbin)\n"
        "  -help          Show this help\n"
        "  Pause / F11          BREAK key (NMI → monitor)\n"
        "  Shift+Pause / Shift+F11  SHIFT+BREAK (hard reset)\n"
        "  Ctrl+D / SIGUSR1  Dump RAM to smaky6_ram_NNNN_pcXXXX.bin at any time\n",
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
    int  autoboot;
    int  break_to_monitor;
    int  autoboot2_code;
    int  autoboot3_code;
    uint8_t inject_codes[128];
    int  inject_len;
    int  inject_via_fifo;
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
    int    stage2_pressed;
    int    stage2_release_at;
    int    stage3_pressed;
    int    stage3_release_at;
    int    inject_idx;
    Uint64 boot_start_ms;
    Uint64 global_timeout_ms;
    Uint64 autoboot_timeout_ms;
    int    timeout_reported;
} MainLoopCtx;

static MainLoopCtx *s_loop = NULL;

/* Check both timeout watchdogs.  Sets L->running=0 and returns 1 if either
 * threshold was exceeded; otherwise returns 0.  Safe to call multiple times
 * per iteration — the timeout_reported flag prevents duplicate messages. */
static int check_timeouts(MainLoopCtx *L)
{
    if (L->timeout_reported) return 0;

    Uint64 elapsed_ms = SDL_GetTicks64() - L->boot_start_ms;

    if (L->autoboot_timeout_ms > 0 && elapsed_ms >= L->autoboot_timeout_ms) {
        fprintf(stderr, "[main] autoboot timeout after %u.%03u s; exiting cleanly\n",
                (unsigned)(elapsed_ms / 1000ULL), (unsigned)(elapsed_ms % 1000ULL));
        L->timeout_reported = 1;
        L->running = 0;
        return 1;
    }

    if (L->global_timeout_ms > 0 && elapsed_ms >= L->global_timeout_ms) {
        fprintf(stderr, "[main] global timeout after %u.%03u s; exiting cleanly\n",
                (unsigned)(elapsed_ms / 1000ULL), (unsigned)(elapsed_ms % 1000ULL));
        L->timeout_reported = 1;
        L->running = 0;
        return 1;
    }

    return 0;
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
            if (ev.button.button == SDL_BUTTON_LEFT) {
                /* Convert window coords → logical renderer coords */
                float lx_f, ly_f;
                SDL_RenderWindowToLogical(L->ren, ev.button.x, ev.button.y,
                                          &lx_f, &ly_f);
                int lx = (int)lx_f, ly = (int)ly_f;
                /* Hit-test each function-key button */
                static const uint8_t FKEY_BITS[7] = {
                    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40
                };
                for (int i = 0; i < 7; i++) {
                    int bx = VIDEO_FKEY_BTN_X0 + i * (VIDEO_FKEY_BTN_W + VIDEO_FKEY_BTN_GAP);
                    int by = VIDEO_FKEY_Y + 1;
                    if (lx >= bx && lx < bx + VIDEO_FKEY_BTN_W &&
                        ly >= by && ly < by + VIDEO_FKEY_BTN_H) {
                        if (ev.type == SDL_MOUSEBUTTONDOWN)
                            L->m->kbd.fonct_bits |=  FKEY_BITS[i];
                        else
                            L->m->kbd.fonct_bits &= (uint8_t)~FKEY_BITS[i];
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
        keyboard_frame_tick(L->m);
        machine_int(L->m);
        machine_run_frame(L->m);
        if (L->m->dbg.trace_kbd) {
            uint16_t ptr = (uint16_t)L->m->bus[0x457Cu] | ((uint16_t)L->m->bus[0x457Du] << 8);
            if (ptr != 0x4596u) {
                fprintf(stderr, "[frame] end: circ ptr=0x%04X [ptr]=0x%02X"
                        "  fifo head=%d tail=%d\n",
                        ptr, (unsigned)L->m->bus[ptr],
                        L->m->kbd.fifo_head, L->m->kbd.fifo_tail);
            }
        }
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
                        "[autoboot] injected SHIFT+BREAK at frame %d (monitor entry)\n",
                        L->frame_cnt);
        }

        if (L->autoboot && !L->stage1_pressed && L->frame_cnt == 150) {
            machine_inject_key(L->m, 0x00);
            L->stage1_pressed = 1;
            L->stage1_release_at = L->frame_cnt + 1;
            if (L->trace)
                fprintf(stderr, "[autoboot] injected Enter key at frame %d\n",
                        L->frame_cnt);
        }

        if ((L->autoboot || L->break_to_monitor) && L->stage1_release_at == L->frame_cnt)
            machine_release_key(L->m);

        if (L->autoboot && !L->stage2_pressed && machine_in_posthandoff_keywait(L->m)) {
            machine_inject_key(L->m, (uint8_t)L->autoboot2_code);
            L->stage2_pressed = 1;
            L->stage2_release_at = L->frame_cnt + 1;
            if (L->trace)
                fprintf(stderr,
                        "[autoboot] injected stage2 key 0x%02X at frame %d (pc=%04X)\n",
                        (unsigned)L->autoboot2_code, L->frame_cnt,
                        machine_get_pc(L->m));
        }

        if (L->autoboot && L->stage2_release_at == L->frame_cnt)
            machine_release_key(L->m);

        if (L->autoboot && L->autoboot3_code >= 0 && L->stage2_pressed &&
            !L->stage3_pressed && L->frame_cnt >= 360) {
            machine_inject_key(L->m, (uint8_t)L->autoboot3_code);
            L->stage3_pressed = 1;
            L->stage3_release_at = L->frame_cnt + 5;
            if (L->trace)
                fprintf(stderr,
                        "[autoboot] injected stage3 key 0x%02X at frame %d\n",
                        (unsigned)L->autoboot3_code, L->frame_cnt);
        }

        if (L->autoboot && L->stage3_release_at == L->frame_cnt)
            machine_release_key(L->m);

        if (L->inject_len > 0 && L->inject_idx == 0 &&
            L->stage2_pressed && machine_cli_prompt_visible(L->m)) {
            if (L->trace)
                fprintf(stderr,
                        "[inject] CLI prompt detected at frame %d; "
                        "writing %d char(s) via %s\n",
                        L->frame_cnt, L->inject_len,
                        L->inject_via_fifo ? "FIFO" : "circular buffer");
            if (!L->inject_via_fifo) {
                for (int i = 0; i < L->inject_len; i++) {
                    uint8_t kc = L->inject_codes[i];
                    machine_inject_to_circ_buf(L->m, kc);
                    if (L->trace)
                        fprintf(stderr,
                                "[inject] circ_buf <- 0x%02X ('%c') (char %d/%d)\n",
                                (unsigned)kc,
                                (kc >= 0x20 && kc < 0x7F) ? (char)kc : '?',
                                i + 1, L->inject_len);
                }
                L->inject_idx = L->inject_len;
            } else {
                L->inject_idx = 1;
            }
        }
        if (L->inject_via_fifo && L->inject_idx > 0 &&
            L->inject_idx <= L->inject_len) {
            uint8_t kc = L->inject_codes[L->inject_idx - 1];
            int next = (L->m->kbd.fifo_tail + 1) & 63;
            if (next != L->m->kbd.fifo_head) {
                L->m->kbd.fifo[L->m->kbd.fifo_tail] = kc;
                L->m->kbd.fifo_tail = next;
                if (L->trace)
                    fprintf(stderr,
                            "[inject-fifo] FIFO <- 0x%02X ('%c') "
                            "(char %d/%d)\n",
                            (unsigned)kc,
                            (kc >= 0x20 && kc < 0x7F) ? (char)kc : '?',
                            L->inject_idx, L->inject_len);
            }
            L->inject_idx++;
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

/* Called from JS to reset the machine (without reloading the page). */
EMSCRIPTEN_KEEPALIVE
void smemu6_reset(void)
{
    if (!s_loop || !s_loop->m) return;
    machine_reset(s_loop->m);
    /* Reset autoboot state so key injection fires again after frame 150 */
    s_loop->frame_cnt          = 0;
    s_loop->stage1_pressed     = 0;
    s_loop->stage1_release_at  = -1;
    s_loop->stage2_pressed     = 0;
    s_loop->stage2_release_at  = -1;
    s_loop->stage3_pressed     = 0;
    s_loop->stage3_release_at  = -1;
    s_loop->inject_idx         = 0;
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
    int autoboot = 0;
    int break_to_monitor = 0;  /* SHIFT+BREAK for monitor entry */
    int autoboot2_code = 0x20;
    int autoboot3_code = -1;   /* disabled by default */
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
    float phosphor_decay   = PHOSPHOR_DECAY_DEFAULT; /* -phosphor-decay <v>: persistence per frame */
    int scanlines          = 0;  /* -scanlines: draw CRT scanline overlay */
    int phosphor_white     = 0;  /* -phosphor white: use white phosphor palette */
    int no_launcher        = 0;  /* -no-launcher: skip startup dialog */
    const char *dump_ram_path = NULL;  /* -dump-ram: write RAM to this file at exit */
    int inject_via_fifo = 0;           /* -inject-via-fifo: push inject-str through kbd FIFO */
    int display_scale = 1;             /* -scale N: integer pixel scale factor */
    int global_timeout_sec = -1;  /* -1 = auto policy */
    int autoboot_timeout_sec = -1;  /* -1 = default when autoboot is enabled */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-floppy") == 0 && i + 1 < argc) {
            disk_path = argv[++i];
        } else if (strcmp(argv[i], "-floppy2") == 0 && i + 1 < argc) {
            disk2_path = argv[++i];
        } else if (strcmp(argv[i], "-harddisk") == 0 && i + 1 < argc) {
            harddisk_path = argv[++i];
        } else if (strcmp(argv[i], "-harddisk2") == 0 && i + 1 < argc) {
            harddisk2_path = argv[++i];
        } else if (strcmp(argv[i], "-trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "-autoboot") == 0) {
            autoboot = 1;
        } else if (strcmp(argv[i], "-break-to-monitor") == 0) {
            break_to_monitor = 1;
        } else if (strcmp(argv[i], "-autoboot2") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 0x7F) {
                fprintf(stderr, "Invalid -autoboot2 value: %s (expected 0..127 or 0x00..0x7F)\n", argv[i]);
                return 1;
            }
            autoboot2_code = (int)v;
        } else if (strcmp(argv[i], "-autoboot3") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 0x7F) {
                fprintf(stderr, "Invalid -autoboot3 value: %s (expected 0..127 or 0x00..0x7F)\n", argv[i]);
                return 1;
            }
            autoboot3_code = (int)v;
        } else if (strcmp(argv[i], "-inject-str") == 0 && i + 1 < argc) {
            const char *s = argv[++i];
            inject_len = 0;
            for (int j = 0; s[j] && inject_len < 126; j++) {
                unsigned char c = (unsigned char)s[j];
                if (c == '\\' && s[j + 1] == 'n') {
                    inject_codes[inject_len++] = 0x0D; /* Enter (CR, 0x0D) */
                    j++;
                } else if (c >= 'a' && c <= 'z') {
                    inject_codes[inject_len++] = (uint8_t)(c - 0x20); /* uppercase */
                } else if (c >= 'A' && c <= 'Z') {
                    inject_codes[inject_len++] = (uint8_t)c;
                } else if (c >= '0' && c <= '9') {
                    inject_codes[inject_len++] = (uint8_t)c;
                } else if (c == ' ') {
                    inject_codes[inject_len++] = 0x20;
                } else if (c == '\n' || c == '\r') {
                    inject_codes[inject_len++] = 0x0D;
                }
                /* unknown chars: skip */
            }
        } else if (strcmp(argv[i], "-inject-delay") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 100000) {
                fprintf(stderr, "Invalid -inject-delay value: %s\n", argv[i]);
                return 1;
            }
            (void)v; /* option accepted for backward compatibility; no longer used */
        } else if (strcmp(argv[i], "-timeout") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 24 * 60 * 60) {
                fprintf(stderr, "Invalid -timeout value: %s (expected 0..86400)\n", argv[i]);
                return 1;
            }
            global_timeout_sec = (int)v;
        } else if (strcmp(argv[i], "-autoboot-timeout") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 0);
            if (!end || *end != '\0' || v < 0 || v > 24 * 60 * 60) {
                fprintf(stderr, "Invalid -autoboot-timeout value: %s (expected 0..86400)\n", argv[i]);
                return 1;
            }
            autoboot_timeout_sec = (int)v;
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
        } else if (strcmp(argv[i], "-inject-via-fifo") == 0) {
            inject_via_fifo = 1;
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
#else
    (void)handle_terminate_signal;
    (void)handle_dump_signal;
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
            .dx0_is_harddisk = harddisk_path ? 1 : (disk_path ? 0 : -1),
            .dx0_path        = harddisk_path ? harddisk_path : disk_path,
            .dx1_path        = disk2_path,
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
        if (lc.dx0_path) {
            if (lc.dx0_is_harddisk == 1) { harddisk_path = lc.dx0_path; disk_path = NULL; }
            else                          { disk_path = lc.dx0_path;     harddisk_path = NULL; }
        } else if (lc.dx0_is_harddisk >= 0 && lc.dx0_is_harddisk != (harddisk_path ? 1 : 0)) {
            /* Type changed but no new path — clear old path of wrong type */
            if (lc.dx0_is_harddisk == 1) { harddisk_path = NULL; }
            else                          { disk_path = NULL; }
        }
        if (lc.dx1_path)        disk2_path     = lc.dx1_path;
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
    if (disk_path) {
        if (floppy_mount(m, 0, disk_path) != 0) {
            fprintf(stderr, "WARNING: could not mount '%s'\n", disk_path);
        }
    }
    if (disk2_path) {
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
    ctx.autoboot           = autoboot;
    ctx.break_to_monitor   = break_to_monitor;
    ctx.autoboot2_code     = autoboot2_code;
    ctx.autoboot3_code     = autoboot3_code;
    memcpy(ctx.inject_codes, inject_codes, sizeof(inject_codes));
    ctx.inject_len         = inject_len;
    ctx.inject_via_fifo    = inject_via_fifo;
    ctx.trace              = trace;
    ctx.last_tick          = SDL_GetPerformanceCounter();
    ctx.freq               = SDL_GetPerformanceFrequency();
    ctx.accum_ms           = 0.0;
    ctx.running            = 1;
    ctx.frame_due          = 0;
    ctx.frame_cnt          = 0;
    ctx.stage1_pressed     = 0;
    ctx.stage1_release_at  = -1;
    ctx.stage2_pressed     = 0;
    ctx.stage2_release_at  = -1;
    ctx.stage3_pressed     = 0;
    ctx.stage3_release_at  = -1;
    ctx.inject_idx         = 0;
    ctx.boot_start_ms      = SDL_GetTicks64();
    ctx.global_timeout_ms  = (Uint64)global_timeout_sec * 1000ULL;
    ctx.autoboot_timeout_ms = 0;
    ctx.timeout_reported   = 0;

    if (autoboot && autoboot_timeout_sec > 0)
        ctx.autoboot_timeout_ms = (Uint64)autoboot_timeout_sec * 1000ULL;
    if (break_to_monitor && autoboot_timeout_sec > 0)
        ctx.autoboot_timeout_ms = (Uint64)autoboot_timeout_sec * 1000ULL;

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
