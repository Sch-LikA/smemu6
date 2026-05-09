/* main.c – Smaky 6 emulator entry point */
#include "machine_internal.h"  /* For access to cpu_stalled field */
#include "machine.h"
#include "memory.h"
#include "video.h"
#include "keyboard.h"
#include "debug.h"
#include "floppy.h"

#include <SDL2/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

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
        "  -disk <img>    Mount floppy image on DX0\n"
        "  -disk2 <img>   Mount floppy image on DX1\n"
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
        "  -gfxbits <b>   Bitmap bit order: lsb|msb\n"
        "  -scale <n>     Integer display scale (1..8, default 2 = 1024x496 window)\n"
        "  -trace08       Trace IN/OUT traffic on port 0x08\n"
        "  -traceflow     Trace focused post-handoff low-RAM control flow\n"
        "  -trace11       Trace port 0x11 reads\n"
        "  -tracecd       Trace port 0xCD reads (Winchester)\n"
        "  -trace19       Trace port 0x19 writes (floppy control)\n"
        "  -tracefdc      Trace focused floppy ID/checksum stream events\n"
        "  -tracekbd      Trace every keyboard CLA / status port read\n"
        "  -dump-ram <f>  Dump full 64 KB RAM to file at exit\n"
        "  -inject-via-fifo  Route -inject-str through keyboard FIFO (tests physical kbd path)\n"
        "  -help          Show this help\n"
        "  Pause / F11          BREAK key (NMI → monitor)\n"
        "  Shift+Pause / Shift+F11  SHIFT+BREAK (hard reset)\n"
        "  Ctrl+D / SIGUSR1  Dump RAM to smaky6_ram_NNNN_pcXXXX.bin at any time\n",
        argv0);
}

int main(int argc, char *argv[])
{
    const char *disk_path  = NULL;
    const char *disk2_path = NULL;
    int trace    = 0;
    int autoboot = 0;
    int break_to_monitor = 0;  /* SHIFT+BREAK for monitor entry */
    int autoboot2_code = 0x20;
    int autoboot3_code = -1;   /* disabled by default */
    uint8_t inject_codes[128];  /* key sequence to inject at OS prompt */
    int inject_len = 0;
    int forced_vmode = -1;
    int gfx_msb_first = 0;
    int trace08 = 0;
    int traceflow = 0;
    int trace11 = 0;
    int tracecd = 0;
    int trace19 = 0;
    int tracefdc = 0;
    int tracekbd = 0;
    const char *dump_ram_path = NULL;  /* -dump-ram: write RAM to this file at exit */
    int inject_via_fifo = 0;           /* -inject-via-fifo: push inject-str through kbd FIFO */
    int display_scale = 2;             /* -scale N: integer pixel scale factor */
    int global_timeout_sec = -1;  /* -1 = auto policy */
    int autoboot_timeout_sec = -1;  /* -1 = default when autoboot is enabled */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-disk") == 0 && i + 1 < argc) {
            disk_path = argv[++i];
        } else if (strcmp(argv[i], "-disk2") == 0 && i + 1 < argc) {
            disk2_path = argv[++i];
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
        } else if (strcmp(argv[i], "-dump-ram") == 0 && i + 1 < argc) {
            dump_ram_path = argv[++i];
        } else if (strcmp(argv[i], "-inject-via-fifo") == 0) {
            inject_via_fifo = 1;
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

    signal(SIGINT,  handle_terminate_signal);
    signal(SIGTERM, handle_terminate_signal);
    signal(SIGUSR1, handle_dump_signal);
    fprintf(stderr, "[main] PID %d — send SIGUSR1 to dump RAM\n", (int)getpid());

    /* ── SDL2 init ──────────────────────────────────────────────────────── */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Smaky 6 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VIDEO_WIN_W * display_scale, VIDEO_WIN_H * display_scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
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
    if (machine_load_rom(m, ROM_PHANTOM, MEM_SYSMON_BASE, MEM_SYSMON_SIZE) != 0) {
        fprintf(stderr, "WARNING: Phantom ROM (%s) not found; CPU will execute garbage\n",
                ROM_PHANTOM);
    }

    /* Init video (after machine so chargen ROM path is available) */
    video_init(m, win, ren);
    video_load_chargen(m, ROM_CHARGEN);
    video_set_gfx_msb_first(m, gfx_msb_first);
    if (forced_vmode >= 0)
        video_set_mode(m, (VideoMode)forced_vmode);

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
    if (tracekbd)
        machine_set_trace_kbd(m, 1);

    /* ── Main loop ──────────────────────────────────────────────────────────────────────── */
    /*
     * Target: 50 Hz frame rate.  We track elapsed time manually and run
     * machine frames as needed.
     */

    /* Simpler approach: track elapsed time manually */
    Uint64 last_tick  = SDL_GetPerformanceCounter();
    Uint64 freq       = SDL_GetPerformanceFrequency();
    double accum_ms   = 0.0;

    int running    = 1;
    int frame_due  = 0;
    int frame_cnt  = 0;  /* counts 50Hz frames since reset; used for auto-boot */
    int stage1_pressed = 0;
    int stage1_release_at = -1;
    int stage2_pressed = 0;
    int stage2_release_at = -1;
    int stage3_pressed = 0;
    int stage3_release_at = -1;
    /* String injection state */
    int inject_idx          = 0;   /* next char index to inject */
    Uint64 boot_start_ms = SDL_GetTicks64();
    Uint64 global_timeout_ms = (Uint64)global_timeout_sec * 1000ULL;
    Uint64 autoboot_timeout_ms = 0;
    int timeout_reported = 0;

    if (autoboot) {
        if (autoboot_timeout_sec > 0)
            autoboot_timeout_ms = (Uint64)autoboot_timeout_sec * 1000ULL;
        /* no default timeout — use -autoboot-timeout N to set one */
    }

    if (break_to_monitor) {
        if (autoboot_timeout_sec > 0)
            autoboot_timeout_ms = (Uint64)autoboot_timeout_sec * 1000ULL;
        /* no default timeout — use -autoboot-timeout N to set one */
    }

    while (running) {
        if (g_terminate_requested) {
            fprintf(stderr, "[main] termination signal received; exiting cleanly\n");
            running = 0;
        }

        if (g_dump_ram_requested) {
            g_dump_ram_requested = 0;
            do_ram_dump(m);
        }
        if (global_timeout_ms > 0 && !timeout_reported) {
            Uint64 elapsed_ms = SDL_GetTicks64() - boot_start_ms;
            if (elapsed_ms >= global_timeout_ms) {
                fprintf(stderr,
                        "[main] global timeout after %u.%03u s; exiting cleanly\n",
                        (unsigned)(elapsed_ms / 1000ULL),
                        (unsigned)(elapsed_ms % 1000ULL));
                timeout_reported = 1;
                running = 0;
            }
        }

        /* ── Events ─────────────────────────────────────────────────────── */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;

            case SDL_KEYDOWN:
                if (ev.key.keysym.scancode == SDL_SCANCODE_F12) {
                    debug_toggle(m);
                } else if (ev.key.keysym.scancode == SDL_SCANCODE_PAUSE ||
                           ev.key.keysym.scancode == SDL_SCANCODE_F11) {
                    if (ev.key.keysym.mod & KMOD_SHIFT)
                        machine_reset(m);   /* SHIFT+BREAK → hard reset */
                    else
                        machine_nmi(m);     /* BREAK alone → NMI / monitor */
                } else if (ev.key.keysym.scancode == SDL_SCANCODE_Q &&
                           (ev.key.keysym.mod & KMOD_CTRL)) {
                    /* Ctrl+Q: quit the emulator */
                    running = 0;
                } else if (ev.key.keysym.scancode == SDL_SCANCODE_D &&
                           (ev.key.keysym.mod & KMOD_CTRL)) {
                    /* Ctrl+D: dump full 64 KB RAM to file */
                    do_ram_dump(m);
                } else {
                    keyboard_event(m, &ev.key);
                }
                break;

            case SDL_KEYUP:
                keyboard_event(m, &ev.key);
                break;

            default:
                break;
            }
        }

        /* ── Timing ─────────────────────────────────────────────────────── */
        Uint64 now      = SDL_GetPerformanceCounter();
        double elapsed  = (double)(now - last_tick) * 1000.0 / (double)freq;
        last_tick       = now;
        accum_ms       += elapsed;

        /* Enforce watchdog even if no frame is due yet. */
        if (autoboot_timeout_ms > 0 && !timeout_reported) {
            Uint64 elapsed_ms = SDL_GetTicks64() - boot_start_ms;
            if (elapsed_ms >= autoboot_timeout_ms) {
                fprintf(stderr,
                        "[main] autoboot timeout after %u.%03u s; exiting cleanly\n",
                        (unsigned)(elapsed_ms / 1000ULL),
                        (unsigned)(elapsed_ms % 1000ULL));
                timeout_reported = 1;
                running = 0;
            }
        }

        if (global_timeout_ms > 0 && !timeout_reported) {
            Uint64 elapsed_ms = SDL_GetTicks64() - boot_start_ms;
            if (elapsed_ms >= global_timeout_ms) {
                fprintf(stderr,
                        "[main] global timeout after %u.%03u s; exiting cleanly\n",
                        (unsigned)(elapsed_ms / 1000ULL),
                        (unsigned)(elapsed_ms % 1000ULL));
                timeout_reported = 1;
                running = 0;
            }
        }

        /* Run as many 50 Hz frames as the accumulated time allows */
        while (accum_ms >= 20.0) {
            if (autoboot_timeout_ms > 0 && !timeout_reported) {
                Uint64 elapsed_ms = SDL_GetTicks64() - boot_start_ms;
                if (elapsed_ms >= autoboot_timeout_ms) {
                    fprintf(stderr,
                            "[main] autoboot timeout after %u.%03u s; exiting cleanly\n",
                            (unsigned)(elapsed_ms / 1000ULL),
                            (unsigned)(elapsed_ms % 1000ULL));
                    timeout_reported = 1;
                    running = 0;
                    break;
                }
            }

            if (global_timeout_ms > 0 && !timeout_reported) {
                Uint64 elapsed_ms = SDL_GetTicks64() - boot_start_ms;
                if (elapsed_ms >= global_timeout_ms) {
                    fprintf(stderr,
                            "[main] global timeout after %u.%03u s; exiting cleanly\n",
                            (unsigned)(elapsed_ms / 1000ULL),
                            (unsigned)(elapsed_ms % 1000ULL));
                    timeout_reported = 1;
                    running = 0;
                    break;
                }
            }

            keyboard_frame_tick(m);  /* drain FIFO into SAMOS buffer before Z80 runs */
            machine_int(m);   /* assert 50 Hz display interrupt */
            machine_run_frame(m);
            /* After the frame: if tracing, log the circ-buf ptr so we can see
             * whether SAMOS consumed any keys during this frame. */
            if (m->dbg.trace_kbd) {
                uint16_t ptr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
                if (ptr != 0x4596u) {
                    fprintf(stderr, "[frame] end: circ ptr=0x%04X [ptr]=0x%02X"
                            "  fifo head=%d tail=%d\n",
                            ptr, (unsigned)m->bus[ptr],
                            m->kbd.fifo_head, m->kbd.fifo_tail);
                }
            }
            if (m->cpu_stalled) {
                fprintf(stderr, "[main] CPU stall detected; exiting cleanly\n");
                running = 0;
                break;
            }
            accum_ms -= 20.0;
            frame_due = 1;
            frame_cnt++;

            /* Monitor-mode entry: inject SHIFT+BREAK early (at 2 s / 100 frames)
             * to trigger "ROM de chargement" banner and allow typing MON to enter SYSMON. */
            if (break_to_monitor && !stage1_pressed && frame_cnt == 100) {
                machine_inject_shift_break(m);
                stage1_pressed = 1;
                stage1_release_at = frame_cnt + 1;
                if (trace)
                    fprintf(stderr, "[autoboot] injected SHIFT+BREAK at frame %d (monitor entry)\n",
                            frame_cnt);
            }

            /* Auto-boot stage 1: after 3 s (150 frames), inject Return (code 0)
             * to select floppy boot from the boot-key menu if no key was pressed. */
            if (autoboot && !stage1_pressed && frame_cnt == 150) {
                machine_inject_key(m, 0x00); /* Return → floppy boot */
                stage1_pressed = 1;
                stage1_release_at = frame_cnt + 1;
                if (trace)
                    fprintf(stderr, "[autoboot] injected Enter key at frame %d\n",
                            frame_cnt);
            }

            if (autoboot && stage1_release_at == frame_cnt) {
                machine_release_key(m);
            }

            if (break_to_monitor && stage1_release_at == frame_cnt) {
                machine_release_key(m);
            }

            /* Auto-boot stage 2: trigger when RAM monitor actually reaches
             * its post-handoff key-wait loop, not at a fixed frame. */
            if (autoboot && !stage2_pressed && machine_in_posthandoff_keywait(m)) {
                machine_inject_key(m, (uint8_t)autoboot2_code);
                stage2_pressed = 1;
                stage2_release_at = frame_cnt + 1;
                if (trace)
                    fprintf(stderr,
                            "[autoboot] injected stage2 key 0x%02X at frame %d (pc=%04X)\n",
                            (unsigned)autoboot2_code, frame_cnt, machine_get_pc(m));
            }

            if (autoboot && stage2_release_at == frame_cnt) {
                machine_release_key(m);
            }

            /* Auto-boot stage 3: many images appear to require an additional
             * key acknowledge shortly after the SAMOS banner is shown. */
            if (autoboot && autoboot3_code >= 0 && stage2_pressed && !stage3_pressed && frame_cnt >= 360) {
                machine_inject_key(m, (uint8_t)autoboot3_code);
                stage3_pressed = 1;
                stage3_release_at = frame_cnt + 5; /* hold 5 frames so Stage 2 sees it */
                if (trace)
                    fprintf(stderr,
                            "[autoboot] injected stage3 key 0x%02X at frame %d\n",
                            (unsigned)autoboot3_code, frame_cnt);
            }

            if (autoboot && stage3_release_at == frame_cnt) {
                machine_release_key(m);
            }

            /* ── CLI prompt detection: write all chars directly into the
             *    circular buffer when "* -" appears, bypassing the ISR
             *    pipeline (Stage 2 requires (0x4582)!=0x80, which is false
             *    after a normal boot with no physical keys). ──
             * With -inject-via-fifo: push chars through the software FIFO
             * instead, which is the same path as physical keypresses, to
             * verify the keyboard_frame_tick → circ_buf delivery chain. */
            if (inject_len > 0 && inject_idx == 0 &&
                stage2_pressed && machine_cli_prompt_visible(m)) {
                if (trace)
                    fprintf(stderr,
                            "[inject] CLI prompt detected at frame %d; "
                            "writing %d char(s) via %s\n",
                            frame_cnt, inject_len,
                            inject_via_fifo ? "FIFO" : "circular buffer");
                if (!inject_via_fifo) {
                    for (int _i = 0; _i < inject_len; _i++) {
                        uint8_t kc = inject_codes[_i];
                        machine_inject_to_circ_buf(m, kc);
                        if (trace)
                            fprintf(stderr,
                                    "[inject] circ_buf <- 0x%02X ('%c') (char %d/%d)\n",
                                    (unsigned)kc,
                                    (kc >= 0x20 && kc < 0x7F) ? (char)kc : '?',
                                    _i + 1, inject_len);
                    }
                    inject_idx = inject_len; /* mark done */
                } else {
                    inject_idx = 1; /* mark that injection has started */
                }
            }
            /* -inject-via-fifo: push one char per frame through the FIFO,
             * mimicking real keyboard_event() calls so keyboard_frame_tick()
             * drains them to the circ buf — same delivery path as physical keys. */
            if (inject_via_fifo && inject_idx > 0 && inject_idx <= inject_len) {
                uint8_t kc = inject_codes[inject_idx - 1];
                int next = (m->kbd.fifo_tail + 1) & 63;
                if (next != m->kbd.fifo_head) {
                    m->kbd.fifo[m->kbd.fifo_tail] = kc;
                    m->kbd.fifo_tail = next;
                    if (trace)
                        fprintf(stderr,
                                "[inject-fifo] FIFO <- 0x%02X ('%c') "
                                "(char %d/%d)\n",
                                (unsigned)kc,
                                (kc >= 0x20 && kc < 0x7F) ? (char)kc : '?',
                                inject_idx, inject_len);
                }
                inject_idx++;  /* advance; stops when inject_idx > inject_len */
            }
        }

        /* ── Render ─────────────────────────────────────────────────────── */
        if (frame_due) {
            video_render(m);
            frame_due = 0;
            SDL_Delay(1);  /* yield to OS after render; prevents busy-spin without VSync */
        } else {
            SDL_Delay(1);  /* avoid busy-spin */
        }
    }

    /* ── Cleanup ────────────────────────────────────────────────────────── */
    if (dump_ram_path) {
        FILE *f = fopen(dump_ram_path, "wb");
        if (!f) {
            fprintf(stderr, "[dump] fopen(%s): %s\n", dump_ram_path, strerror(errno));
        } else {
            if (fwrite(m->bus, 1, 65536, f) != 65536)
                fprintf(stderr, "[dump] short write to %s\n", dump_ram_path);
            else
                fprintf(stderr, "[dump] RAM written to %s  (PC=%04X)\n",
                        dump_ram_path, (unsigned)machine_get_pc(m));
            fclose(f);
        }
    }
    machine_destroy(m);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
