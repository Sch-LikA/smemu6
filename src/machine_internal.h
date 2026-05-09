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
#include "video.h"
#include "rtc.h"
#include "usart.h"
#include "winchester.h"
#include <Z80.h>
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>

struct Smaky6 {
    /* Memory bus (64 K flat) and ROM mask */
    uint8_t bus[MEM_TOTAL];
    uint8_t rom_mask[MEM_TOTAL];

    /* Z80 CPU */
    Z80 cpu;

    /* Keyboard */
    struct {
        uint8_t key_code;
        int     found;           /* set on KEYDOWN, cleared by CLA read */
        int     physically_held; /* 1 while SDL key is physically down (cleared by KEYUP) */
        int     key_hold_frames; /* extra frames to maintain key_held after KEYUP.
                                  * On KEYUP: set to 2 so Stage 2 has at least 2 frames
                                  * to see the key, even if KEYUP fires before Stage 2 runs.
                                  * Decremented by keyboard_frame_tick() each 50 Hz frame;
                                  * when 0, keyboard_found() returns 0. */
        int     cla_seen;        /* 1 after first CLA read in current ISR cycle;
                                  * reset to 0 by the ISR ACK (OUT port 0x01, data!=0).
                                  * Allows Stage 2's CLA read to return the key even
                                  * though Stage 1 already cleared 'found'. */
        int     shift_pressed;   /* 1 if SHIFT is held (for SHIFT+BREAK detection) */
        uint8_t fonct_bits;      /* bitmask of the 7 "touches de fonction" */
        SDL_Scancode repeat_scan; /* scancode of the key whose code was last drained
                                   * to the SAMOS circ-buf; KEYUP of this scan zeros
                                   * the SAMOS repeat-countdown register (0x4558). */
        /* Software FIFO feeding SAMOS circular buffer one key per frame.
         * keyboard_event() pushes here; keyboard_frame_tick() pops to SAMOS
         * when the buffer slot is free (ptr == 0x4596 = empty). */
        uint8_t fifo[64];
        int     fifo_head;       /* next read index */
        int     fifo_tail;       /* next write index */
        int     samos_loaded;    /* set once iff1 has been 1 (EI executed = SAMOS running).
                                  * While 0: Phantom ROM boot — keyboard_read_cla iff1=0
                                  * branch serves the FIFO for Phantom ROM kbd_wait.
                                  * Once 1: iff1=0 means ISR context — FIFO must NOT be
                                  * served from CLA; it drains only via keyboard_frame_tick. */
    } kbd;

    /* Video */
    struct {
        SDL_Renderer *ren;
        SDL_Texture  *tex;
        VideoMode     mode;
        int           display_on;       /* 1 = enabled (normal), 0 = blanked (port 0x00 bit 0 = 0) */
        int           no_display_off;   /* 1 = ignore display-off writes (keeps screen always on) */
        int           scanlines;          /* 1 = draw semi-transparent dark lines on every other output row */
        int           gfx_msb_first;
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
        FILE    *image[2];
        uint8_t  ctrl;
        uint8_t  track[2];
        uint8_t  sector;        /* current hard-sector index 0-15 */
        int      step_prev;
        uint8_t  num_tracks[2]; /* 40 or 77, detected from image size */

        /* Sector data-stream state machine (port 0x1B reads) */
        uint16_t byte_pos;      /* 0=sync, 1=id, 2..257=data[0..255], 258=cksum */
        uint8_t  sec_buf[256];  /* current sector data */
        uint8_t  sec_csum;      /* checksum (sum of sec_buf[]) */

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
        int stepping;
        int trace;       /* 1 = log Z80 PC at boot milestones to stderr */
        int trace_port08;/* 1 = log port 0x08 IN/OUT traffic */
        int trace_kbd;   /* 1 = log every CLA / status port read */
            int trace_port11;  /* 1 = log port 0x11 IN/OUT */
            int trace_port_cd;  /* 1 = log port 0xCD IN/OUT */
            int trace_port19;  /* 1 = log port 0x19 writes */
            int trace_fdc;     /* 1 = log focused floppy ID/checksum stream events */
        int trace_flow;  /* 1 = log dense post-handoff control flow */
        int trace_snd;   /* 1 = log every port 0x03 write (buzzer) */
        int trace_scr;   /* 1 = dump changed screen rows to stderr (opt-in via -scrdump) */
        int flow_budget; /* max flow log lines per run */
        uint16_t last_flow_pc;
        uint32_t flow_spin_count;
        uint16_t last_pc;/* last PC seen by trace hook (avoid duplicate prints) */
        uint16_t last_io19_pc;   /* last PC logged for port 0x19 */
        uint8_t  last_io19_data; /* last value logged for port 0x19 */
        uint32_t io19_repeat_count; /* suppressed repeated 0x19 writes */
        uint16_t frame_start_pc; /* PC at start of frame for stall detection */
        int stall_frames; /* consecutive frames with no PC progress */
    } dbg;

    /* Port 0x08: E405/08 RTC serial interface (extension board) */
    RtcState rtc;

    /* Ports 0x20–0x27: Winchester hard-disk controller */
    WinState win;

    /* 50 Hz interrupt pending */
    int irq_pending;

    /* Stall detector: set when CPU stuck in infinite loop, causes main to exit */
    int cpu_stalled;
};

#endif /* MACHINE_INTERNAL_H */
