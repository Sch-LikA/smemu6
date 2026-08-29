// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* memory.h – Smaky 6 address bus */
#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

struct Smaky6;

/* Clear the flat bus and re-arm Phantom ROM write protection. */
void memory_init(struct Smaky6 *m);
/* No heap-owned memory exists today; kept for symmetric subsystem teardown. */
void memory_fini(struct Smaky6 *m);

/* Embedded targets (compiled when SMEMU6_HAVE_BACKEND is defined): one
 * reusable per-render framebuffer, allocated once in a target-appropriate
 * region (PSRAM on ESP32).  The weak default in memory.c uses malloc; a
 * backend may supply a strong override that allocates from SRAM-capable /
 * PSRAM memory.  Only referenced when SMEMU6_HAVE_BACKEND is set — the host
 * SDL build keeps the stack array for byte-identical output. */
#if defined(SMEMU6_HAVE_BACKEND)
void  *smemu6_framebuffer_alloc(size_t size);
#endif

/* Load one raw binary file into the flat machine bus at base. */
int memory_load_file(struct Smaky6 *m, const char *path, uint16_t base);
/* Load one raw ROM image from a memory buffer into the flat bus at base. */
int memory_load_mem(struct Smaky6 *m, const void *data, size_t size, uint16_t base);

/* Read one byte from the flat machine bus with ROM/workspace side effects. */
uint8_t memory_read(struct Smaky6 *m, uint16_t addr);
/* Write one byte to the flat machine bus while honouring ROM masks and hooks. */
void    memory_write(struct Smaky6 *m, uint16_t addr, uint8_t data);

/* Remove ROM write-protection from a region after the Phantom bank-switch. */
void memory_unprotect_rom(struct Smaky6 *m, uint16_t base, uint16_t len);

/*
 * Smaky 6 memory map — 64 KB Phantom model
 *
 *   0x0000 – 0x07FF  Phantom ROM (2 KB, TMS2716 "SYS17")
 *                    Write-protected by rom_mask[]; unmapped on MOVROM signal.
 *   0x0800 – 0x3FFF  RAM (lower area, ~14 KB)
 *   0x4000 – 0x44FF  Alpha screen buffer (20 rows × 64 cols = 1280 bytes)
 *                    Display DMA (HOLD cycles) reads this region each frame.
 *   0x4500 – 0x45FF  SAMOS OS workspace / variables (256 bytes)
 *   0x4600 – 0x54FF  Graphic bitmap (nibble-interleaved, 60 pairs × 64 bytes = 3840 bytes)
 *                    Each byte: high nibble → 4 px on even line, low nibble → 4 px on odd line.
 *                    Native 256×120, displayed 512×480 (2× wide, 4× tall).
 *                    Addresses confirmed in octal on MÉMOIRE schematic (J. Zuba,
 *                    Nov 1978): 040000=0x4000, 046000=0x4600, 100000=0x8000.
 *   0x5500 – 0x77FF  SYS.SY loaded here by Phantom (~8960 bytes)
 *   0x7800 – 0xFFFF  RAM (upper, ~32 KB on 64 KB model)
 *
 * Initial revision (MÉMOIRE board): 2 × 8 × 4116 DRAM = 32 KB total; option
 * EPROMs C21–C24 (4 × 2708/2716) at 0x4000 or 0x6000 via jumper.
 * 64 KB Phantom model adds upper DRAM bank and replaces fixed ROMs with the
 * Phantom bootstrap.  This emulator targets the 64 KB variant.
 */
#define MEM_PHANTOM_BASE   0x0000u
#define MEM_PHANTOM_SIZE   0x0800u   /* 2 KB Phantom bootloader ROM */
#define MEM_SYSMON_BASE    MEM_PHANTOM_BASE   /* alias used by machine_load_rom() */
#define MEM_SYSMON_SIZE    MEM_PHANTOM_SIZE
#define MEM_ALPHA_BASE     0x4000u
#define MEM_ALPHA_SIZE     0x0500u   /* 20 rows × 64 cols = 1280 bytes */
#define MEM_GFX_BASE       0x4600u
#define MEM_GFX_SIZE       0x0F00u   /* 1 graphic page: 60 rows × 64 bytes = 3840 bytes */
#define MEM_TOTAL          0x10000u  /* 64 KB flat bus */

#endif /* MEMORY_H */
