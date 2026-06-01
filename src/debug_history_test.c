// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
#include "debug_history.h"

#include <stdio.h>

/* Assert that a size-producing helper returned the expected count. */
static int expect_size(size_t actual, size_t expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %zu, got %zu\n", label, expected, actual);
        return 1;
    }
    return 0;
}

/* Assert that a popped or shifted history value matches the expected word. */
static int expect_value(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %04X, got %04X\n", label, (unsigned)expected, (unsigned)actual);
        return 1;
    }
    return 0;
}

/* Exercise bounded push/pop history behaviour, dedupe, and sliding eviction. */
int main(void)
{
    uint16_t history[4] = { 0 };
    size_t count = 0;
    uint16_t value = 0;

    count = debug_history_push(history, count, 4u, 0x1000u);
    if (expect_size(count, 1u, "push first") != 0) {
        return 1;
    }
    count = debug_history_push(history, count, 4u, 0x1000u);
    if (expect_size(count, 1u, "dedupe top") != 0) {
        return 1;
    }
    count = debug_history_push(history, count, 4u, 0x1100u);
    count = debug_history_push(history, count, 4u, 0x1200u);
    count = debug_history_push(history, count, 4u, 0x1300u);
    if (expect_size(count, 4u, "fill capacity") != 0) {
        return 1;
    }
    count = debug_history_push(history, count, 4u, 0x1400u);
    if (expect_size(count, 4u, "slide full") != 0) {
        return 1;
    }
    if (expect_value(history[0], 0x1100u, "slid oldest") != 0 ||
        expect_value(history[3], 0x1400u, "newest after slide") != 0) {
        return 1;
    }
    if (!debug_history_pop(history, &count, &value) ||
        expect_value(value, 0x1400u, "pop newest") != 0 ||
        expect_size(count, 3u, "count after pop") != 0) {
        return 1;
    }
    if (!debug_history_pop(history, &count, &value) ||
        expect_value(value, 0x1300u, "pop second") != 0) {
        return 1;
    }
    count = 0;
    if (debug_history_pop(history, &count, &value) != 0) {
        fprintf(stderr, "empty pop should fail\n");
        return 1;
    }

    return 0;
}