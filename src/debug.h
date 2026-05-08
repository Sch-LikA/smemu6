/* debug.h – Built-in machine monitor / debugger */
#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

struct Smaky6;

void debug_init(struct Smaky6 *m);
void debug_fini(struct Smaky6 *m);

/* Toggle single-step / run mode (called on F12 keypress) */
void debug_toggle(struct Smaky6 *m);

/* Dump Z80 registers and flags to stdout */
void debug_dump_regs(struct Smaky6 *m);

/* Hex dump of memory range [from, from+len) to stdout */
void debug_hexdump(struct Smaky6 *m, uint16_t from, uint16_t len);

/* Returns 1 if single-step mode is active */
int debug_is_stepping(struct Smaky6 *m);

/* Called from the Z80 opcode-fetch hook; prints milestone labels to stderr
 * when -trace is active.  pc = current Z80 program counter. */
void debug_trace_pc(struct Smaky6 *m, uint16_t pc);

/* Enable / disable PC tracing (set by -trace flag in main.c) */
void debug_set_trace(struct Smaky6 *m, int on);

/* Enable / disable focused low-RAM flow tracing after ROM handoff. */
void debug_set_trace_flow(struct Smaky6 *m, int on);

#endif /* DEBUG_H */
