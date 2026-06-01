// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/*
 * machine_internal.h – Full definition of struct Smaky6.
 *
 * Included ONLY by machine.c and the subsystem .c files that need
 * direct field access (memory.c, video.c, keyboard.c, …).
 * Never include this from .h files — keep the public API opaque.
 */
#ifndef MACHINE_INTERNAL_H
#define MACHINE_INTERNAL_H

#include "memory.h"
#include "psg.h"
#include "video.h"
#include "rtc.h"
#include "usart.h"
#include "winchester.h"
#include <Z80.h>
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>

enum FloppyMediaKind {
    FLOPPY_MEDIA_NONE = 0,
    FLOPPY_MEDIA_FILE,
    FLOPPY_MEDIA_MEMORY,
};

struct FloppyMedia {
    enum FloppyMediaKind kind;
    FILE    *file;
    uint8_t *data;
    size_t   size;
    int      read_only;
    int      refreshable;
    char     source_path[256];
    char     description[256];
};

struct DebugCpuSnapshot {
    uint16_t af;
    uint16_t af_shadow;
    uint16_t bc;
    uint16_t bc_shadow;
    uint16_t de;
    uint16_t de_shadow;
    uint16_t hl;
    uint16_t hl_shadow;
    uint16_t ix;
    uint16_t iy;
    uint16_t sp;
    uint16_t pc;
    uint8_t i;
    uint8_t r;
    uint8_t iff1;
    uint8_t iff2;
    uint8_t im;
};

struct Smaky6 {
    /* Memory bus (64 K flat) and ROM mask */
    uint8_t bus[MEM_TOTAL];
    uint8_t rom_mask[MEM_TOTAL];

    /* Z80 CPU */
    Z80 cpu;

    /* Keyboard: strict hardware-facing state.
     * Ordinary matrix keys resolve through the S471 table into key_code/found,
     * Shift and CAPS/LOCK select the active layer, and the 7 bottom-row
     * function keys live separately in fonct_bits. */
    struct {
        uint8_t key_code;          /* latched 7-bit code for the current ordinary key */
        int      found;             /* FOUND latch: 1 = key present, cleared by CLA read */
        int      reassert_pending;  /* 1 = scanner will reassert FOUND after scan latency */
        uint32_t reassert_cycles;   /* T-state countdown to reassert (≈500 at 2.5 MHz = 200 µs) */
        int      physically_held;   /* 1 while the current ordinary key is held */
        int      cla_seen_current;  /* 1 once the current ordinary key has been observed by a CLA read */
        int      release_after_reassert; /* 1 when a promoted released key must drop after one synthetic reassert */
        int      release_after_buffer_commit; /* 1 when a released promoted key should drop as soon as SYS.SY enqueues it */
        int      boot_key_held;     /* 1 only for the power-on virtual Enter autoboot key */
        int      regular_prefix_pending; /* 1 = armed one-shot bit7 prefix for the next ordinary CLA read */
        int      regular_prefix_armed; /* 1 until the first post-boot ordinary key claims the validated bit7 prefix */
        int      shift_pressed;     /* current Shift state for verified layer-sensitive keys */
        int      caps_lock_active;  /* latched CAPS/LOCK state selects the PROM caps layer */
        uint16_t host_text_down_count; /* number of printable host keys currently held down */
        uint8_t  host_text_down[SDL_NUM_SCANCODES]; /* guards SDL_TEXTINPUT against stray post-keyup events */
        SDL_Scancode active_scancode; /* host scancode currently owning the ordinary-key latch */
        uint8_t  active_matrix_position; /* 0..63 for the held ordinary key; 0xFF = none */
        struct {
            SDL_Scancode scancode;
            uint8_t matrix_position;
            uint8_t key_code;
            uint8_t released;
        } pending_ordinary[8];
        uint8_t  pending_ordinary_head;
        uint8_t  pending_ordinary_len;
        uint8_t  fonct_bits;        /* current effective state of the 7 bottom-row function keys */
        uint8_t  fonct_consumed_bits; /* function bits already delivered; re-expose only after release */
        uint8_t  fonct_keyboard_bits; /* host F1..F7 state */
        uint8_t  cursor_alias_sources; /* held host arrow-key aliases synthesizing CURSOR+r/d/f/c */
        uint8_t  fonct_mouse_bits;  /* status-bar button hold state */
    } kbd;

    /* Video */
    struct {
        SDL_Renderer *ren;
        SDL_Texture  *tex;
        VideoMode     mode;
        int           display_on;       /* 1 = enabled (normal), 0 = blanked (port 0x00 bit 0 = 0) */
        int           no_display_off;   /* 1 = ignore display-off writes (keeps screen always on) */
        int           verbose_video;      /* 1 = log display-on/off/mode changes to stderr */
        int           reset_armed;        /* 1 = RESET button awaiting confirm click */
        Uint32        reset_armed_at;     /* SDL_GetTicks() at which reset was first clicked */
        int           scanlines;          /* 1 = draw semi-transparent dark lines on every other output row */
        PhosphorColour phosphor;        /* PHOSPHOR_GREEN (default) or PHOSPHOR_WHITE */
        int           gfx_msb_first;
        /* Phosphor persistence: heap buffer [VIDEO_PX_W × VIDEO_ASPECT_H] floats.
         * NULL = disabled (allocated by video_init; freed by video_fini).
         * Each element holds the current glow level [0.0, 1.0] for one output pixel.
         * Lit pixels snap to 1.0; dark pixels multiply by phosphor_decay each frame.
         * Skipped when no_display_off is set (display never actually blanks). */
        float        *phosphor_buf;
        float         phosphor_decay; /* per-frame decay: 0=instant, ~0.7≈P31 medium */
        uint8_t       chargen[2048];
        /* Shadow copy of the alpha plane for change-detection / stderr dump */
        uint8_t       shadow_alpha[VIDEO_COLS_CHAR * VIDEO_ROWS_CHAR];
    } vid;

    /* USARTs */
    struct Usart usart[USART_COUNT];

    /* Parallel */
    struct {
        uint8_t data;
        uint8_t status;
    } par;

    /* Floppy */
    struct {
        struct FloppyMedia media[2];
        uint8_t  ctrl;
        uint8_t  track[2];
        uint8_t  sector;        /* current hard-sector index 0-15 */
        int      step_prev;
        uint8_t  num_tracks[2]; /* 40 or 77, detected from image size */

        /* Sector data-stream state machine (port 0x1B reads) */
        uint16_t byte_pos;      /* 0=sync, 1=id, 2..257=data[0..255], 258=cksum */
        uint8_t  sec_buf[256];  /* current sector data */
        uint8_t  sec_csum;      /* checksum (sum of sec_buf[]) */

        /* Post-ROM write-stream state machine (port 0x18 writes).
         * SYS.SY emits: 40x 0x28 preamble, 0xFF marker, track byte,
         * 256 data bytes, checksum, 0x00 trailer. */
        uint16_t write_byte_pos;
        uint8_t  write_track;
        uint8_t  write_sector;
        uint8_t  write_buf[256];
        uint8_t  write_csum;

        /* Seek-busy simulation: counts down to 0 after a seek command.
         * While > 0: port 0x19 bit6=1 (seeking). When 0: bit6=0 (settled). */
        int      seek_busy;

        /* Disk-activity LED: set to N frames on any I/O access; decremented
         * each floppy_tick().  Non-zero = LED lit for that drive. */
        int      disk_active[2];

        /* NMI-enable: set when port 0x19 is written with bits 2+3 set
         * (motor-on + sector-hole NMI enable), cleared on any other write.
         * floppy_tick() fires z80_nmi() each frame while this is non-zero
         * and a disk is mounted, replicating the Micropolis sector-hole NMI. */
        int      nmi_armed;

        /* Active drive as selected by the last port 0x19 write (bit 4).
         * Kept separate from ctrl so that port 0x1A step-pulse bytes (which
         * also have a drive bit but encode step direction, not selection)
         * cannot override the drive choice set by the OS. */
        int      selected_drive;

        /* Per-drive sector latch: updated at byte_pos=1 to track which sector
         * is being read on each drive (for the status-bar display). */
        uint8_t  phased_sector[2];
    } fdc;

    /* Sound */
    struct {
        int    buzzer_bit;
        zusize frame_base;  /* total frame T-states before the current z80_run
                             * sub-batch; set by machine_run_frame so that
                             * sound_set_bit can compute the absolute frame
                             * position as (frame_base + cpu.cycles). */
    } snd;

    /* Debug */
    struct {
        int visible;
        int paused;
        int stepping;
        int step_instruction_pending;
        int step_frame_pending;
        int trace;       /* 1 = log Z80 PC at boot milestones to stderr */
        int trace_port08; /* 1 = log port 0x08 IN/OUT traffic */
        int trace_kbd;    /* 1 = log every CLA / status port read */
        int trace_port11; /* 1 = log port 0x11 IN/OUT */
        int trace_port_cd;/* 1 = log port 0xCD IN/OUT */
        int trace_port19; /* 1 = log port 0x19 writes */
        int trace_fdc;    /* 1 = log focused floppy ID/checksum stream events */
        int trace_flow;   /* 1 = log dense post-handoff control flow */
        int trace_snd;   /* 1 = log every port 0x03 write (buzzer) */
        int trace_scr;   /* 1 = dump changed screen rows to stderr (opt-in via -scrdump) */
        int flow_budget; /* max flow log lines per run */
        uint16_t last_flow_pc;
        uint16_t last_watch_pc;
        uint8_t last_watch_2be2;
        uint8_t last_watch_2b88;
        int watch_2be2_valid;
        uint32_t flow_spin_count;
        uint16_t last_pc;/* last PC seen by trace hook (avoid duplicate prints) */
        uint16_t last_io19_pc;   /* last PC logged for port 0x19 */
        uint8_t  last_io19_data; /* last value logged for port 0x19 */
        uint32_t io19_repeat_count; /* suppressed repeated 0x19 writes */
        uint16_t frame_start_pc; /* PC at start of frame for stall detection */
        int stall_frames; /* consecutive frames with no PC progress */
        uint32_t last_run_tstates;
        uint64_t frame_counter;
        uint16_t disasm_cursor;
        uint16_t disasm_history[16];
        uint16_t run_to_cursor_addr;
        uint16_t breakpoint_resume_pc;
        uint16_t breakpoints[16];
        uint8_t disasm_history_count;
        uint8_t breakpoint_count;
        uint8_t run_to_cursor_active;
        uint8_t step_over_active;
        uint8_t breakpoint_resume_armed;
        uint16_t mem_base;
        uint16_t mem_cursor;
        uint16_t watch_addrs[3];
        uint8_t mem_view_octal;
        uint8_t mem_edit_high_nibble;
        uint8_t mem_jump_active;
        uint8_t watch_edit_active;
        uint8_t watch_selected;
        uint8_t mem_jump_len;
        uint8_t stop_reason;
        uint8_t last_stop_valid;
        uint8_t prev_stop_valid;
        char mem_jump_buf[5];
        struct DebugCpuSnapshot last_stop_snapshot;
        struct DebugCpuSnapshot prev_stop_snapshot;
        SDL_Window *window;
        SDL_Renderer *renderer;
        Uint32 window_id;
        /* Drift check: accumulate executed cycles; every SMAKY6_FRAME_HZ frames
         * (= 1 second) verify total is within 1% of expected. */
        uint64_t drift_cycles_accum; /* running sum of executed cycles */
        int      drift_frames;       /* frame counter within current 1-s window */
    } dbg;

    /* Port 0x08: E405/08 RTC serial interface (extension board) */
    RtcState rtc;

    /* Ports 0x20–0x27: Winchester hard-disk controller */
    WinState win;

    /* Optional PSG add-on currently mapped to ports 0x20–0x27 when enabled. */
    struct {
        int enabled;
        uint32_t chip_clock_hz;
        struct Smaky6Psg card;
    } psg;

    /* 50 Hz interrupt pending */
    int irq_pending;

    /* Stall detector: set when CPU stuck in infinite loop, causes main to exit */
    int cpu_stalled;
};

#endif /* MACHINE_INTERNAL_H */
