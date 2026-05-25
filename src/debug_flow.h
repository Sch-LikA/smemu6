#ifndef SMEMU6_DEBUG_FLOW_H
#define SMEMU6_DEBUG_FLOW_H

#include <stddef.h>
#include <stdint.h>

int debug_flow_decode_target(uint16_t addr,
                             const uint8_t *bytes,
                             size_t size,
                             uint16_t *target_out);

int debug_flow_is_step_over_candidate(uint8_t opcode);

#endif