// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
#include "debug_navigation.h"

#include <stdio.h>

/* Assert one integer-valued navigation helper result. */
static int expect_int(int actual, int expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
        return 1;
    }
    return 0;
}

/* Exercise debugger follow-policy and stack-target classification helpers. */
int main(void)
{
    if (expect_int(debug_navigation_can_follow(0, 0), 0, "running blocks follow") != 0) {
        return 1;
    }
    if (expect_int(debug_navigation_can_follow(1, 0), 1, "paused allows follow") != 0) {
        return 1;
    }
    if (expect_int(debug_navigation_can_follow(0, 1), 1, "run-to-cursor allows follow") != 0) {
        return 1;
    }
    if (expect_int(debug_navigation_stack_target_is_code_like(0x0020u, 0), 0, "low target without symbol") != 0) {
        return 1;
    }
    if (expect_int(debug_navigation_stack_target_is_code_like(0x0020u, 1), 1, "low target with symbol") != 0) {
        return 1;
    }
    if (expect_int(debug_navigation_stack_target_is_code_like(0x2180u, 0), 1, "high target without symbol") != 0) {
        return 1;
    }

    return 0;
}