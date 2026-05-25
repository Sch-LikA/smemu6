// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
#include "debug_flow.h"

#include <stdio.h>

static int expect_decode(uint16_t addr,
                         const uint8_t *bytes,
                         size_t size,
                         int expected_found,
                         uint16_t expected_target,
                         const char *label)
{
    uint16_t target = 0;
    int found = debug_flow_decode_target(addr, bytes, size, &target);

    if (found != expected_found) {
        fprintf(stderr, "%s: expected found=%d, got %d\n", label, expected_found, found);
        return 1;
    }
    if (found && target != expected_target) {
        fprintf(stderr, "%s: expected target=%04X, got %04X\n",
                label,
                (unsigned)expected_target,
                (unsigned)target);
        return 1;
    }
    return 0;
}

static int expect_step_over(uint8_t opcode, int expected, const char *label)
{
    int actual = debug_flow_is_step_over_candidate(opcode);

    if (actual != expected) {
        fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const uint8_t call_abs[] = { 0xCDu, 0x34u, 0x12u };
    static const uint8_t jp_cond[] = { 0xCAu, 0x78u, 0x56u };
    static const uint8_t jr_fwd[] = { 0x18u, 0x05u };
    static const uint8_t djnz_back[] = { 0x10u, 0xFCu };
    static const uint8_t rst20[] = { 0xE7u };
    static const uint8_t nop[] = { 0x00u };

    if (expect_decode(0x4000u, call_abs, sizeof(call_abs), 1, 0x1234u, "call abs") != 0) {
        return 1;
    }
    if (expect_decode(0x4000u, jp_cond, sizeof(jp_cond), 1, 0x5678u, "jp cond") != 0) {
        return 1;
    }
    if (expect_decode(0x4100u, jr_fwd, sizeof(jr_fwd), 1, 0x4107u, "jr forward") != 0) {
        return 1;
    }
    if (expect_decode(0x2180u, djnz_back, sizeof(djnz_back), 1, 0x217Eu, "djnz backward") != 0) {
        return 1;
    }
    if (expect_decode(0x4000u, rst20, sizeof(rst20), 1, 0x0020u, "rst 20h") != 0) {
        return 1;
    }
    if (expect_decode(0x4000u, nop, sizeof(nop), 0, 0u, "nop") != 0) {
        return 1;
    }
    if (expect_decode(0x4000u, call_abs, 2u, 0, 0u, "short call") != 0) {
        return 1;
    }

    if (expect_step_over(0xCDu, 1, "call step over") != 0) {
        return 1;
    }
    if (expect_step_over(0xC4u, 1, "cond call step over") != 0) {
        return 1;
    }
    if (expect_step_over(0xE7u, 1, "rst step over") != 0) {
        return 1;
    }
    if (expect_step_over(0x10u, 1, "djnz step over") != 0) {
        return 1;
    }
    if (expect_step_over(0xC3u, 0, "jp no step over") != 0) {
        return 1;
    }
    if (expect_step_over(0x18u, 0, "jr no step over") != 0) {
        return 1;
    }

    return 0;
}