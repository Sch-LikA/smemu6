#ifndef SMEMU6_DEBUG_FLOW_H
#define SMEMU6_DEBUG_FLOW_H

#include <stddef.h>
#include <stdint.h>

/* Decode one Z80 control-flow target from the instruction bytes at addr.
 * Returns 1 when target_out receives a valid absolute destination. */
int debug_flow_decode_target(uint16_t addr,
                             const uint8_t *bytes,
                             size_t size,
                             uint16_t *target_out);

/* Report whether opcode should keep the debugger on the sequential path when
 * step-over is requested. */
int debug_flow_is_step_over_candidate(uint8_t opcode);

#endif