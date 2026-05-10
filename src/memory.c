// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* memory.c – Smaky 6 address bus and memory subsystem */
#include "machine_internal.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t memory_read(struct Smaky6 *m, uint16_t addr)
{
    return m->bus[addr];
}

void memory_write(struct Smaky6 *m, uint16_t addr, uint8_t data)
{
    if (m->rom_mask[addr]) return;   /* ignore writes to ROM */
    m->bus[addr] = data;
}

/* ---- init / fini --------------------------------------------------------- */

void memory_init(struct Smaky6 *m)
{
    memset(m->bus,      0x00, sizeof(m->bus));
    memset(m->rom_mask, 0x00, sizeof(m->rom_mask));
}

void memory_fini(struct Smaky6 *m)
{
    /* nothing to free – bus is embedded in struct */
    (void)m;
}

/* ---- ROM loader ---------------------------------------------------------- */

int memory_load_file(struct Smaky6 *m, const char *path, uint16_t base)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "memory: cannot open '%s'\n", path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "memory: fseek failed on '%s'\n", path);
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "memory: ftell failed on '%s'\n", path);
        fclose(f);
        return -1;
    }
    rewind(f);

    if (size > (long)(MEM_TOTAL - base)) {
        fprintf(stderr, "memory: '%s' (%ld bytes) overflows bus at 0x%04X\n",
                path, size, base);
        fclose(f);
        return -1;
    }

    size_t n = fread(m->bus + base, 1, (size_t)size, f);
    fclose(f);

    if ((long)n != size) {
        fprintf(stderr, "memory: short read from '%s'\n", path);
        return -1;
    }

    /* Mark loaded range as ROM (read-only) */
    memset(m->rom_mask + base, 1, (size_t)size);

    fprintf(stderr, "memory: loaded '%s' (%ld bytes) at 0x%04X\n",
            path, size, base);
    return 0;
}

void memory_unprotect_rom(struct Smaky6 *m, uint16_t base, uint16_t len)
{
    /* Clear rom_mask for [base, base+len), making the region writable.
     * Used to implement the Phantom ROM bank-switch on OUT(0x01),A=0:
     * once unprotected, LDIR can install SYSMON at 0x0000–0x07FF. */
    uint32_t end = (uint32_t)base + len;
    if (end > MEM_TOTAL) end = MEM_TOTAL;
    memset(m->rom_mask + base, 0, end - base);
}
