// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* debug.h – Built-in machine monitor / debugger */
#ifndef DEBUG_H
#define DEBUG_H

#include <SDL2/SDL.h>
#include <stdint.h>

struct Smaky6;

void debug_init(struct Smaky6 *m);
void debug_fini(struct Smaky6 *m);

/* Toggle the native debugger window (called on F12 keypress). */
void debug_toggle(struct Smaky6 *m);

/* Returns 1 when the native debugger window is open. */
int debug_is_visible(struct Smaky6 *m);

/* Returns 1 when debugger execution pause is active. */
int debug_is_paused(struct Smaky6 *m);

/* Queue a single-instruction step while paused. */
void debug_request_step_instruction(struct Smaky6 *m);

/* Queue a single-frame step while paused. */
void debug_request_step_frame(struct Smaky6 *m);

/* Consume pending step requests. */
int debug_consume_step_instruction(struct Smaky6 *m);
int debug_consume_step_frame(struct Smaky6 *m);

/* Record debugger timing after a run slice. */
void debug_note_instruction_run(struct Smaky6 *m, uint32_t tstates);
void debug_note_frame_run(struct Smaky6 *m, uint32_t tstates);

/* Handle events and redraw the debugger window when visible. */
int debug_handle_event(struct Smaky6 *m, const SDL_Event *ev);
void debug_render(struct Smaky6 *m);

/* Dump Z80 registers and flags to stdout */
void debug_dump_regs(struct Smaky6 *m);

/* Hex dump of memory range [from, from+len) to stdout */
void debug_hexdump(struct Smaky6 *m, uint16_t from, uint16_t len);

/* Returns 1 if the legacy single-step mode flag is active */
int debug_is_stepping(struct Smaky6 *m);

/* Called from the Z80 opcode-fetch hook; prints milestone labels to stderr
 * when -trace is active.  pc = current Z80 program counter. */
void debug_trace_pc(struct Smaky6 *m, uint16_t pc);

/* Enable / disable PC tracing (set by -trace flag in main.c) */
void debug_set_trace(struct Smaky6 *m, int on);

/* Enable / disable focused low-RAM flow tracing after ROM handoff. */
void debug_set_trace_flow(struct Smaky6 *m, int on);

#endif /* DEBUG_H */
