#include "debug_flow.h"

int debug_flow_decode_target(uint16_t addr,
                             const uint8_t *bytes,
                             size_t size,
                             uint16_t *target_out)
{
    uint8_t op;

    if (!bytes || size == 0 || !target_out) {
        return 0;
    }

    op = bytes[0];
    if (((op & 0xC7u) == 0xC2u || op == 0xC3u || (op & 0xC7u) == 0xC4u || op == 0xCDu) && size >= 3) {
        *target_out = (uint16_t)bytes[1] | ((uint16_t)bytes[2] << 8);
        return 1;
    }
    if ((op == 0x10u || op == 0x18u || op == 0x20u || op == 0x28u || op == 0x30u || op == 0x38u) && size >= 2) {
        *target_out = (uint16_t)(addr + 2u + (int8_t)bytes[1]);
        return 1;
    }
    if ((op & 0xC7u) == 0xC7u) {
        *target_out = (uint16_t)(op & 0x38u);
        return 1;
    }

    return 0;
}

int debug_flow_is_step_over_candidate(uint8_t opcode)
{
    return opcode == 0xCDu ||
           opcode == 0x10u ||
           (opcode & 0xC7u) == 0xC4u ||
           (opcode & 0xC7u) == 0xC7u;
}