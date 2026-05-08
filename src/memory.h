/* memory.h – Smaky 6 address bus */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

struct Smaky6;

/* Initialise / teardown */
void memory_init(struct Smaky6 *m);
void memory_fini(struct Smaky6 *m);

/* Load binary file into flat bus; returns 0 on success */
int memory_load_file(struct Smaky6 *m, const char *path, uint16_t base);

/* Flat read / write (used by Z80 callbacks and internal modules) */
uint8_t memory_read(struct Smaky6 *m, uint16_t addr);
void    memory_write(struct Smaky6 *m, uint16_t addr, uint8_t data);

/* Remove ROM write-protection from a region (Phantom ROM bank-switch) */
void memory_unprotect_rom(struct Smaky6 *m, uint16_t base, uint16_t len);

/*
 * Smaky 6 memory map — 64 KB Phantom model
 *
 *   0x0000 – 0x07FF  Phantom ROM (2 KB, TMS2716 "SYS17")
 *                    Write-protected by rom_mask[].
 *   0x0800 – 0x3FFF  RAM (lower area, ~14 KB)
 *   0x4000 – 0x44FF  Alpha screen buffer (20 rows × 64 cols = 1280 bytes)
 *   0x4500 – 0x80FF  Graphic bitmap      (240 rows × 64 bytes = 15360 bytes)
 *   0x4500 – 0x4FFF  OS workspace / variables (overlaps graphic plane base)
 *   0x5500 – 0x77FF  SYS.SY loaded here by Phantom (~8960 bytes)
 *   0x8100 – 0xFFFF  RAM (upper, ~32 KB)
 *
 * Earlier 32 KB / 48 KB models had two 2716 ROMs (SYSMON + SAMOS) at
 * 0x0000–0x0FFF and no upper RAM.  This emulator targets the 64 KB variant.
 */
#define MEM_PHANTOM_BASE   0x0000u
#define MEM_PHANTOM_SIZE   0x0800u   /* 2 KB Phantom bootloader ROM */
#define MEM_SYSMON_BASE    MEM_PHANTOM_BASE   /* alias used by machine_load_rom() */
#define MEM_SYSMON_SIZE    MEM_PHANTOM_SIZE
#define MEM_ALPHA_BASE     0x4000u
#define MEM_ALPHA_SIZE     0x0500u   /* 20 rows × 64 cols = 1280 bytes */
#define MEM_GFX_BASE       0x4500u
#define MEM_GFX_SIZE       0x3C00u   /* 240 rows × 64 bytes = 15360 bytes */
#define MEM_TOTAL          0x10000u  /* 64 KB flat bus */

#endif /* MEMORY_H */
