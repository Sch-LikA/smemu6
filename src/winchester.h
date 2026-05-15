// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* winchester.h – Smaky 6 Winchester hard-disk controller emulation
 *
 * Hardware: WD1000/WD1001/WD1002-compatible register set, base I/O address 0x20.
 *
 * I/O port map (port & 0x3F):
 *   0x20  R    Data register — read sector data (INIR reads 256 bytes)
 *   0x20  W    Data register — write sector data into an in-memory overlay
 *   0x21  R    Error register (read) — bit 2 = ECC error; return 0 = no error
 *   0x21  W    Write precompensation cyl (ignored)
 *   0x23  W    Sector number (bits[4:0] = sector 0-31)
 *   0x24  W    Cylinder low byte
 *   0x25  W    Cylinder high byte (ROM always writes 0; max 255 cylinders)
 *   0x26  W    SDH — bits[2:0] = head (0-5), bit[3] = drive select (0/1)
 *   0x27  R    Status register
 *   0x27  W    Command register
 *   0x2B  W    Unknown (1-2 uses; treated as no-op)
 *
 * Status register (port 0x27 read):
 *   bit 7 = BSY   (busy — command in progress)
 *   bit 6 = RDY   (drive ready)
 *   bit 4 = SC    (seek complete)
 *   bit 3 = DRQ   (data request — data ready for transfer)
 *   bit 0 = ERR   (error)
 *   0x50 = RDY+SC (idle, ready)
 *   0x58 = RDY+SC+DRQ (sector data available)
 *
 * Commands (port 0x27 write, upper nibble):
 *   0x1n = RESTORE (recalibrate to track 0)
 *   0x2n = READ SECTOR
 *   0x3n = WRITE SECTOR
 *
 * Geometry (confirmed from Phantom ROM disassembly at 0x0370–0x0398):
 *   Sectors per track : 32  (sector masked with 0x1F)
 *   Heads per cylinder: 6   (C=6 divisor in the head-extraction loop)
 *   Cylinders         : up to 255 (cylinder high always written as 0)
 *   Sector size       : 256 bytes (INIR with B=0 = 256 iterations)
 *
 * LBA formula: lba = cylinder×192 + head×32 + sector
 * This maps exactly to the DE-packed sector address used by the ROM's block_copy
 * loop: LBA == DE for all valid DE values.
 *
 * Images: SM6WIN0.DSK (drive 0) and SM6WIN1.DSK (drive 1).
 * Each 16 MB; only the first ~1054 sectors (269 KB) are non-zero.
 * Load with -harddisk <path> (drive 0) and -harddisk2 <path> (drive 1).
 * Guest writes update a per-drive in-memory sector overlay; the backing .DSK
 * file stays unchanged and the overlay is lost on remount or emulator exit.
 */
#ifndef WINCHESTER_H
#define WINCHESTER_H

#include <stdint.h>
#include <stdio.h>

#define WIN_HEADS_PER_CYL    6u
#define WIN_SECTORS_PER_TRK 32u
#define WIN_SECTOR_SIZE     256u

typedef enum {
    WD_IDLE,    /* ready, no transfer in progress     */
    WD_DRQ,     /* read sector data available          */
    WD_WRITING  /* write sector: collecting host data  */
} WdPhase;

typedef struct {
    uint32_t lba;
    uint8_t  data[WIN_SECTOR_SIZE];
} WinOverlaySector;

typedef struct {
    FILE    *image[2];          /* disk image files (NULL = not mounted)  */
    WdPhase  phase;
    uint8_t  sector_buf[WIN_SECTOR_SIZE]; /* current sector data           */
    int      data_idx;          /* next byte position in sector_buf        */
    uint32_t write_lba;         /* target LBA for current WRITE SECTOR     */
    uint8_t  write_drive;       /* target drive for current WRITE SECTOR   */

    /* CHS registers (written by Z80 before issuing a command) */
    uint8_t  sector_num;        /* port 0x23 — sector 0-31                 */
    uint8_t  cyl_lo;            /* port 0x24                               */
    uint8_t  cyl_hi;            /* port 0x25 (always 0 in Phantom ROM)     */
    uint8_t  sdh;               /* port 0x26 — bits[2:0]=head, bit[3]=drv  */

    /* Per-drive status (updated on each command; used by video status bar) */
    int      disk_active[2];    /* down-counter: non-zero = LED lit         */
    uint16_t last_cyl[2];       /* last cylinder accessed per drive         */
    uint8_t  last_head[2];      /* last head accessed per drive             */

    WinOverlaySector *overlay[2];
    size_t            overlay_count[2];
    size_t            overlay_capacity[2];

    int      trace;             /* non-zero: log transactions to stderr    */
} WinState;

/* Initialise state (image pointers set to NULL). */
void    winchester_init(WinState *w);

/* Load a disk image for drive 0 or 1. Returns 0 on success. */
int     winchester_load(WinState *w, int drive, const char *path);

/* Port I/O — called from machine.c z80_io_read/write. */
uint8_t winchester_read_data  (WinState *w);        /* port 0x20 IN  */
uint8_t winchester_read_status(WinState *w);        /* port 0x27 IN  */
uint8_t winchester_read_error (WinState *w);        /* port 0x21 IN  */
void    winchester_write_data (WinState *w, uint8_t data); /* port 0x20 OUT */
void    winchester_write_sector_num(WinState *w, uint8_t v); /* 0x23 OUT */
void    winchester_write_cyl_lo    (WinState *w, uint8_t v); /* 0x24 OUT */
void    winchester_write_cyl_hi    (WinState *w, uint8_t v); /* 0x25 OUT */
void    winchester_write_sdh       (WinState *w, uint8_t v); /* 0x26 OUT */
void    winchester_write_cmd       (WinState *w, uint8_t cmd); /* 0x27 OUT */

/* Tear down: close image files. */
void    winchester_fini(WinState *w);

#endif /* WINCHESTER_H */
