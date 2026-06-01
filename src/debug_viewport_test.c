// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
#include "debug_viewport.h"

#include <stdio.h>

/* Assert one viewport-start calculation result. */
static int expect_start(int actual, int expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
        return 1;
    }
    return 0;
}

/* Exercise the disassembly viewport positioning logic around edge cases. */
int main(void)
{
    if (expect_start(debug_disasm_view_start(20, 10, 10, 11, 5), 5, "pc centered") != 0) {
        return 1;
    }
    if (expect_start(debug_disasm_view_start(6, 2, 2, 11, 5), 0, "short list") != 0) {
        return 1;
    }
    if (expect_start(debug_disasm_view_start(20, 19, 19, 11, 5), 9, "bottom clamp") != 0) {
        return 1;
    }
    if (expect_start(debug_disasm_view_start(24, 3, 3, 11, 5), 0, "top clamp") != 0) {
        return 1;
    }
    if (expect_start(debug_disasm_view_start(25, 2, 2, 11, 5), 0, "browse far backward") != 0) {
        return 1;
    }
    if (expect_start(debug_disasm_view_start(25, 18, 18, 11, 5), 13, "browse forward centered") != 0) {
        return 1;
    }
    if (expect_start(debug_disasm_view_start(12, 0, 11, 11, 5), 1, "selection forced visible") != 0) {
        return 1;
    }

    return 0;
}