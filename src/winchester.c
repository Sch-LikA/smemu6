// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* winchester.c – Smaky 6 Winchester hard-disk controller emulation */
#include "winchester.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/*
 * CHS → LBA.
 *
 * Geometry confirmed from Phantom ROM disassembly (0x0370–0x0398):
 *   sector   = sdword & 0x1F                  (5 bits, 0-based)
 *   track    = sdword >> 5                     (11 bits)
 *   head     = track % HEADS_PER_CYL (= 6)
 *   cylinder = track / HEADS_PER_CYL
 *
 * This yields LBA == DE (the 16-bit packed address the ROM uses), so
 * the disk image can be treated as a flat array of 256-byte sectors.
 */
static uint32_t chs_to_lba(const WinState *w)
{
    uint16_t cyl  = ((uint16_t)w->cyl_hi << 8) | w->cyl_lo;
    uint8_t  head = w->sdh & 0x07u;
    uint8_t  sec  = w->sector_num & 0x1Fu;

    return (uint32_t)cyl  * WIN_HEADS_PER_CYL * WIN_SECTORS_PER_TRK
         + (uint32_t)head * WIN_SECTORS_PER_TRK
         + (uint32_t)sec;
}

/* Return which drive is currently selected (bit 3 of SDH). */
static int selected_drive(const WinState *w)
{
    return (w->sdh >> 3) & 1;
}

/* Fill sector_buf with 256 bytes from the image at LBA.
 * Returns 0 on success, -1 if no image or seek error. */
static int read_sector(WinState *w, uint32_t lba)
{
    int drv = selected_drive(w);
    memset(w->sector_buf, 0, WIN_SECTOR_SIZE);

    if (!w->image[drv]) {
        if (w->trace)
            fprintf(stderr, "[win] read_sector lba=%u  drive %d: no image\n",
                    (unsigned)lba, drv);
        return -1;
    }

    /* Validate LBA against the maximum geometry before seeking.
     * The LBA is derived from Z80 registers written by emulated code loaded
     * from the disk image; a crafted image could produce an out-of-range LBA
     * and drive fseek far beyond the image.
     * Max cylinders = 255 (cyl_hi is always 0 per ROM disassembly). */
    static const uint32_t WIN_MAX_LBA =
        255u * WIN_HEADS_PER_CYL * WIN_SECTORS_PER_TRK;
    if (lba >= WIN_MAX_LBA) {
        fprintf(stderr, "[win] LBA %u out of range (max %u)\n",
                (unsigned)lba, (unsigned)(WIN_MAX_LBA - 1u));
        return -1;
    }

    /* Use int64_t for the offset so the multiplication cannot overflow on
     * 32-bit platforms where sizeof(long)==4.  After the bounds check above,
     * the maximum offset is (255*6*32-1)*256 = 12,533,504 bytes — well within
     * int32_t, but the int64_t cast makes the intent explicit. */
    int64_t off = (int64_t)lba * (int64_t)WIN_SECTOR_SIZE;
    if (fseek(w->image[drv], (long)off, SEEK_SET) != 0) {
        fprintf(stderr, "[win] fseek(lba=%u, off=0x%llX): error\n",
                (unsigned)lba, (unsigned long long)off);
        return -1;
    }
    size_t n = fread(w->sector_buf, 1, WIN_SECTOR_SIZE, w->image[drv]);
    if (n < WIN_SECTOR_SIZE) {
        /* Image too small — zero-fill remainder (already done by memset) */
    }

    if (w->trace) {
        uint16_t cyl  = ((uint16_t)w->cyl_hi << 8) | w->cyl_lo;
        fprintf(stderr, "[win] READ drv=%d cyl=%u head=%u sec=%u  lba=%u  off=0x%llX\n",
                drv, (unsigned)cyl, (unsigned)(w->sdh & 7u),
                (unsigned)(w->sector_num & 0x1Fu), (unsigned)lba,
                (unsigned long long)off);
    }
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void winchester_init(WinState *w)
{
    memset(w, 0, sizeof(*w));
    w->phase = WD_IDLE;
}

int winchester_load(WinState *w, int drive, const char *path)
{
    if (drive < 0 || drive > 1) return -1;
    if (w->image[drive]) {
        fclose(w->image[drive]);
        w->image[drive] = NULL;
    }
    w->image[drive] = fopen(path, "rb");
    if (!w->image[drive]) {
        fprintf(stderr, "[win] cannot open drive %d image: %s\n", drive, path);
        return -1;
    }
    fprintf(stderr, "[win] drive %d mounted: %s\n", drive, path);
    return 0;
}

void winchester_fini(WinState *w)
{
    for (int i = 0; i < 2; i++) {
        if (w->image[i]) { fclose(w->image[i]); w->image[i] = NULL; }
    }
}

/* ── Port reads ─────────────────────────────────────────────────────────── */

/*
 * Status register (port 0x27 IN):
 *   No image mounted → 0xFF (BSY forever; winchester_init times out → "Disque inactif")
 *   IDLE             → 0x50 (RDY=bit6, SC=bit4)
 *   DRQ (data ready) → 0x58 (RDY + SC + DRQ=bit3)
 *   WRITING          → 0x58 (RDY + SC + DRQ — ready for host to write next byte)
 */
uint8_t winchester_read_status(WinState *w)
{
    int drv = selected_drive(w);
    if (!w->image[drv])
        return 0xFFu;   /* drive not ready — triggers timeout in winchester_init */
    if (w->phase == WD_DRQ || w->phase == WD_WRITING)
        return 0x58u;   /* RDY + SC + DRQ */
    return 0x50u;       /* RDY + SC (idle) */
}

/* Error register (port 0x21 IN): 0 = no error. */
uint8_t winchester_read_error(WinState *w)
{
    (void)w;
    return 0x00u;
}

/*
 * Data register (port 0x20 IN):
 * Returns the next byte from the current sector buffer.
 * The ROM uses INIR with B=0 → 256 reads in sequence.
 */
uint8_t winchester_read_data(WinState *w)
{
    if (w->phase != WD_DRQ) return 0xFFu;

    uint8_t b = w->sector_buf[w->data_idx];
    w->data_idx = (w->data_idx + 1) & 0xFF;

    if (w->data_idx == 0) {
        /* All 256 bytes consumed — return to idle */
        w->phase = WD_IDLE;
    }
    return b;
}

/* ── Port writes ─────────────────────────────────────────────────────────── */

void winchester_write_sector_num(WinState *w, uint8_t v)
{
    w->sector_num = v & 0x1Fu;
}

void winchester_write_cyl_lo(WinState *w, uint8_t v) { w->cyl_lo = v; }
void winchester_write_cyl_hi(WinState *w, uint8_t v) { w->cyl_hi = v; }
void winchester_write_sdh   (WinState *w, uint8_t v) { w->sdh    = v; }

/*
 * Command register (port 0x27 OUT).
 *
 * Supported commands:
 *   0x1n — RESTORE (recalibrate): seek to cylinder 0
 *   0x2n — READ SECTOR: load sector into buf, set DRQ
 *   0x3n — WRITE SECTOR: set up write phase (stub — discards data for now)
 *
 * The emulator executes commands instantly (no BSY delay) because the
 * Phantom ROM polls port 0x27 in a tight loop and the emulator runs
 * synchronously — there is no real benefit to inserting a delay.
 */
void winchester_write_cmd(WinState *w, uint8_t cmd)
{
    uint8_t op  = cmd & 0xF0u;
    int     drv = selected_drive(w);

    switch (op) {
    case 0x10u:  /* RESTORE */
        w->cyl_lo  = 0;
        w->cyl_hi  = 0;
        w->phase   = WD_IDLE;
        w->data_idx = 0;
        /* Update per-drive status */
        w->last_cyl[drv]  = 0;
        w->last_head[drv] = w->sdh & 0x07u;
        w->disk_active[drv] = 6;
        if (w->trace)
            fprintf(stderr, "[win] CMD RESTORE drv=%d\n", drv);
        break;

    case 0x20u:  /* READ SECTOR */
        {
            uint32_t lba = chs_to_lba(w);
            read_sector(w, lba);
            w->data_idx = 0;
            w->phase    = WD_DRQ;
            /* Update per-drive status */
            w->last_cyl[drv]  = ((uint16_t)w->cyl_hi << 8) | w->cyl_lo;
            w->last_head[drv] = w->sdh & 0x07u;
            w->disk_active[drv] = 6;
        }
        break;

    case 0x30u:  /* WRITE SECTOR (stub — data discarded) */
        w->data_idx = 0;
        w->phase    = WD_WRITING;
        /* Update per-drive status */
        w->last_cyl[drv]  = ((uint16_t)w->cyl_hi << 8) | w->cyl_lo;
        w->last_head[drv] = w->sdh & 0x07u;
        w->disk_active[drv] = 6;
        if (w->trace) {
            uint32_t lba = chs_to_lba(w);
            fprintf(stderr, "[win] CMD WRITE lba=%u (stub — discarded)\n",
                    (unsigned)lba);
        }
        break;

    case 0x70u:  /* SEEK — position heads to cylinder in CHS registers */
        w->phase = WD_IDLE;
        w->last_cyl[drv]  = ((uint16_t)w->cyl_hi << 8) | w->cyl_lo;
        w->last_head[drv] = w->sdh & 0x07u;
        w->disk_active[drv] = 3;
        if (w->trace)
            fprintf(stderr, "[win] CMD SEEK drv=%d cyl=%u head=%u\n",
                    drv,
                    (unsigned)w->last_cyl[drv],
                    (unsigned)w->last_head[drv]);
        break;

    default:
        if (w->trace)
            fprintf(stderr, "[win] CMD 0x%02X unrecognised (ignored)\n", cmd);
        break;
    }
}

/*
 * Data write (port 0x20 OUT): collect bytes during WRITE SECTOR.
 * Currently a stub — bytes are counted but discarded.
 */
void winchester_write_data(WinState *w, uint8_t data)
{
    (void)data;
    if (w->phase != WD_WRITING) return;

    w->data_idx = (w->data_idx + 1) & 0xFF;
    if (w->data_idx == 0)
        w->phase = WD_IDLE;
}
