// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* memory.c – Smaky 6 address bus and memory subsystem */
#include "machine_internal.h"
#include "memory.h"
#include "keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t memory_read(struct Smaky6 *m, uint16_t addr)
{
    if (addr == 0x457Eu) {
        uint16_t pc = (uint16_t)Z80_PC(m->cpu);
        int is_syscall_0e_read = (pc >= 0x0516u && pc <= 0x0519u);
        uint8_t value = is_syscall_0e_read ? keyboard_read_stage1_code(m) : m->bus[addr];

        if (m->dbg.trace_flow) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            fprintf(stderr,
                "[kbd-r] pc=%04X [%04X] -> %02X sp=%04X ret=%04X\n",
                    (unsigned)Z80_PC(m->cpu),
                    (unsigned)addr,
                    (unsigned)value,
                    (unsigned)sp,
                    (unsigned)ret);
        }

        keyboard_trace_snapshot(m,
                                is_syscall_0e_read ? "mem-457e-getfo" : "mem-457e-raw",
                                pc, addr, value, value);

        return value;
    }
    return m->bus[addr];
}

void memory_write(struct Smaky6 *m, uint16_t addr, uint8_t data)
{
    if (m->rom_mask[addr]) return;   /* ignore writes to ROM */

    uint8_t snapshot_old = m->bus[addr];

    if (addr == 0x457Cu) {
        uint8_t old = m->bus[addr];
        uint16_t old_ptr = (uint16_t)old | ((uint16_t)m->bus[0x457Du] << 8);
        int enqueue_advanced = (uint8_t)(old + 1u) == data;
        if (old == 0xB6u && data == 0x96u)
            enqueue_advanced = 1;
        if (old != data && enqueue_advanced) {
            uint8_t committed_code = m->bus[old_ptr] & 0x7Fu;

            if (m->kbd.physically_held && committed_code != 0x00u) {
                m->bus[0x4558u] = 0x23u;
                m->bus[0x4577u] = committed_code;

                if (!m->kbd.release_after_buffer_commit) {
                    m->kbd.key_code = 0x00;
                    m->kbd.found = 0;
                    m->kbd.physically_held = 0;
                    m->kbd.regular_prefix_pending = 0;
                    m->kbd.reassert_pending = 0;
                    m->kbd.reassert_cycles = 0;
                }
            }

            if (m->kbd.release_after_buffer_commit) {
                m->kbd.key_code = 0x00;
                m->bus[0x4558u] = 0;
                m->bus[0x4577u] = 0;
                m->kbd.found = 0;
                m->kbd.physically_held = 0;
                m->kbd.cla_seen_current = 0;
                m->kbd.release_after_reassert = 0;
                m->kbd.release_after_buffer_commit = 0;
                m->kbd.regular_prefix_pending = 0;
                m->kbd.active_scancode = SDL_SCANCODE_UNKNOWN;
                m->kbd.active_matrix_position = 0xFFu;
                m->kbd.reassert_pending = 0;
                m->kbd.reassert_cycles = 0;
            }
        }
    }

    if (m->dbg.trace_kbd &&
        (addr == 0x4558u || addr == 0x4577u ||
         addr == 0x457Cu || addr == 0x457Du || addr == 0x457Eu ||
         (addr >= 0x4580u && addr <= 0x4595u) ||
         (addr >= 0x4596u && addr <= 0x45B6u))) {
        uint8_t old = m->bus[addr];
        if (old != data) {
            fprintf(stderr,
                    "[kbd-w] pc=%04X [%04X] %02X -> %02X\n",
                    (unsigned)Z80_PC(m->cpu),
                    (unsigned)addr,
                    (unsigned)old,
                    (unsigned)data);
        }
    }
    if (m->dbg.trace_flow &&
        ((addr >= 0x45C0u && addr <= 0x463Fu) || addr == 0x7014u || addr == 0x7015u)) {
        uint8_t old = m->bus[addr];
        if (old != data) {
            fprintf(stderr,
                    "[cli-w] pc=%04X [%04X] %02X -> %02X",
                    (unsigned)Z80_PC(m->cpu),
                    (unsigned)addr,
                    (unsigned)old,
                    (unsigned)data);
            if (addr >= 0x45C0u && addr <= 0x463Fu) {
                fprintf(stderr, " ('%c')",
                        (data >= 0x20u && data < 0x7Fu) ? (char)data : '?');
            }
            fputc('\n', stderr);
        }
    }
    m->bus[addr] = data;

    if (addr == 0x457Cu || addr == 0x457Du || addr == 0x457Eu ||
        addr == 0x4580u || addr == 0x4581u || addr == 0x4582u) {
        keyboard_trace_snapshot(m, "mem-write", (uint16_t)Z80_PC(m->cpu),
                                addr, snapshot_old, data);
    }
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
