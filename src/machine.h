// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* machine.h – Top-level Smaky 6 machine state */
#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>

/* Integer arithmetic helpers (no floating-point, C99/C11 safe) */
#define SMAKY_DIV_ROUND_NEAREST(n, d)  (((n) + ((d) / 2u)) / (d))
#define SMAKY_DIV_ROUND_UP(n, d)       (((n) + (d) - 1u) / (d))

/* Hardware timing constants */
#define SMAKY6_CPU_HZ              2500000u  /* Z80 clock: 2.5 MHz */
#define SMAKY6_FRAME_HZ            50u       /* display / ISR rate */
#define SMAKY6_TSTATES_PER_FRAME   (SMAKY6_CPU_HZ / SMAKY6_FRAME_HZ)  /* 50 000 */

/* 50 Hz INT pulse fired at mid-frame; width = 64 T-states (~25.6 µs) */
#define SMAKY6_INT_PULSE_AT        (SMAKY6_TSTATES_PER_FRAME / 2u)     /* 25 000 */
#define SMAKY6_INT_PULSE_WIDTH     64u

/* Keyboard scanner reassert: one full 8×8 scan at 300 kHz ≈ 200 µs */
#define SMAKY6_SCAN_REASSERT_US    200u
#define SMAKY6_SCAN_REASSERT_TSTATES \
    ((SMAKY6_CPU_HZ / 1000000u) * SMAKY6_SCAN_REASSERT_US)             /* = 500 */

/* Audio */
#define SMAKY6_AUDIO_HZ            44100u
#define SMAKY6_SAMPLES_PER_FRAME   SMAKY_DIV_ROUND_NEAREST(SMAKY6_AUDIO_HZ, SMAKY6_FRAME_HZ)  /* 882 */

/* Compile-time sanity checks */
#if ((SMAKY6_CPU_HZ) % (SMAKY6_FRAME_HZ)) != 0
#warning "SMAKY6_CPU_HZ is not an integer multiple of SMAKY6_FRAME_HZ; TSTATES_PER_FRAME truncates."
#endif
#if ((SMAKY6_CPU_HZ) / 1000000u) == 0
#error "SMAKY6_CPU_HZ is too low for microsecond-based conversions."
#endif

/* Forward declarations */
struct Smaky6;

/* Initialise and shutdown */
struct Smaky6 *machine_create(void);
void           machine_destroy(struct Smaky6 *m);

/* Load ROM image into the machine; returns 0 on success */
int  machine_load_rom(struct Smaky6 *m, const char *path, uint16_t base);

/* Run one full 50 Hz frame (~50 000 T-states at 2.5 MHz) */
void machine_run_frame(struct Smaky6 *m);

/* Assert / release NMI (BREAK key) */
void machine_nmi(struct Smaky6 *m);

/* Assert INT (50 Hz display interrupt) */
void machine_int(struct Smaky6 *m);

/* Reset the machine */
void machine_reset(struct Smaky6 *m);

/* Optional PSG expansion card. Returns 0 on success. */
int machine_set_psg_enabled(struct Smaky6 *m, int on);
int machine_psg_enabled(const struct Smaky6 *m);

/* Enable/disable PC-milestone tracing to stderr (set by -trace flag) */
void machine_set_trace(struct Smaky6 *m, int on);

/* Enable/disable verbose port 0x08 I/O tracing. */
void machine_set_trace_port08(struct Smaky6 *m, int on);
void machine_set_trace_kbd(struct Smaky6 *m, int on);

/* Enable/disable focused post-handoff low-RAM flow tracing. */
void machine_set_trace_flow(struct Smaky6 *m, int on);

/* Enable/disable debug tracing for mystery ports */
void machine_set_trace_port11(struct Smaky6 *m, int on);
void machine_set_trace_port_cd(struct Smaky6 *m, int on);
void machine_set_trace_port19(struct Smaky6 *m, int on);
void machine_set_trace_fdc(struct Smaky6 *m, int on);
void machine_set_trace_snd(struct Smaky6 *m, int on);
void machine_set_trace_scr(struct Smaky6 *m, int on);

/* When enabled, port 0x00 writes with bit0=0 are ignored (screen stays on).
 * Useful when the emulated OS does a display-off during boot that would
 * otherwise blank the window before the first useful frame is rendered. */
void machine_set_no_display_off(struct Smaky6 *m, int on);
void machine_set_verbose_video(struct Smaky6 *m, int on);
void machine_set_scanlines(struct Smaky6 *m, int on);
void machine_set_phosphor(struct Smaky6 *m, int white); /* 0=green (default), 1=white */
void machine_set_phosphor_decay(struct Smaky6 *m, float decay); /* 0.0=instant, ~0.7=P31 */

/* Inject a key into the keyboard buffer (code=0 → Return/floppy-boot) */
void machine_inject_key(struct Smaky6 *m, uint8_t code);

/* Inject one simultaneous function-key mask plus ordinary CLA-visible key. */
void machine_inject_key_chord(struct Smaky6 *m, uint8_t code, uint8_t fonct_bits);

/* Release/clear injected key latch (simulate key-up) */
void machine_release_key(struct Smaky6 *m);

/* Inject SHIFT + BREAK using the physical ESC/UNDO key code (0x06) for boot-time monitor entry. */
void machine_inject_shift_break(struct Smaky6 *m);

/* Current Z80 program counter (for debug/trace coordination). */
uint16_t machine_get_pc(const struct Smaky6 *m);

int machine_cli_prompt_visible(const struct Smaky6 *m);

/* Write a 7-bit key code directly into the SAMOS circular keyboard buffer
 * at (0x457C), bypassing the ISR pipeline.  Safe to call from the main loop. */
void machine_inject_to_circ_buf(struct Smaky6 *m, uint8_t code);

#endif /* MACHINE_H */
