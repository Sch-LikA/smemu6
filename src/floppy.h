// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* floppy.h – Micropolis hard-sectored floppy controller */
#ifndef FLOPPY_H
#define FLOPPY_H

#include <stdint.h>

struct Smaky6;

/*
 * Micropolis 5.25" hard-sectored single-sided:
 *   40 tracks × 16 sectors × 256 bytes = 163,840 bytes per disk
 *
 * Port map (bit-banged discrete logic, per Plan F3–F5 schematics):
 *
 *   Port 0x19 (CTRL) write — IC7 LS475 control register (Plan F5):
 *     bit 1 = WRTMOD      (write mode)
 *     bit 2 = INTON       (interrupt enable / NMI arm)
 *     bit 3 = MOTORON     (spindle motor on; also triggers drive-select latch)
 *     bit 4 = STPDIRIN    (step direction: 1 = toward track 0)
 *     bit 5 = DRISEL1     (drive select: 1 = DX0, mutually exclusive with bit 6)
 *     bit 6 = DRISEL2     (drive select: 1 = DX1, mutually exclusive with bit 5)
 *     bit 7 = DRISEL3     (reserved / third drive select)
 *   Port 0x19 (CTRL) read:
 *     bits [3:0] = current hard-sector index (0–15)
 *     bit 5 = 0 → head at track 0 (TRACK0 signal)
 *     bit 6 = 0 → seek settled (SEEK_BUSY cleared)
 *
 *   Port 0x1A (CONT) write — step/motor control (Plan F4):
 *     bit 0 = MOTOR       (1 = spindle on)
 *     bit 1 = HEAD_LOAD   (1 = head pressed against disk)
 *     bit 2 = STEP_PULSE  (0→1 rising edge = one track step)
 *     bit 3 = DIRECTION   (step direction, mirrors STPDIRIN on port 0x19)
 *   NOTE: bit 4 of port 0x1A writes should NOT be interpreted as drive select;
 *         drive selection is controlled exclusively via port 0x19 DRISEL1/2 bits.
 *
 *   Port 0x1B (STAT) read:
 *     bit 0 = TRACK0      (1 = head at track 0)
 *     bit 1 = READ_REQ    (1 = sector index pulse / data ready)
 *     bit 2 = DRIVE_READY (1 = drive spinning and stable)
 */

#define FLOPPY_PORT_CONT   0x1Au
#define FLOPPY_PORT_STAT   0x1Bu

#define FLOPPY_SECTORS      16u
#define FLOPPY_SECTOR_BYTES 256u

/* Standard 40-track (163,840 bytes) and 77-track (315,392 bytes) variants */
#define FLOPPY_TRACKS_40   40u
#define FLOPPY_TRACKS_77   77u
#define FLOPPY_IMAGE_40   (FLOPPY_TRACKS_40 * FLOPPY_SECTORS * FLOPPY_SECTOR_BYTES)
#define FLOPPY_IMAGE_77   (FLOPPY_TRACKS_77 * FLOPPY_SECTORS * FLOPPY_SECTOR_BYTES)

void    floppy_init(struct Smaky6 *m);
void    floppy_fini(struct Smaky6 *m);

/* Mount a flat disk image file (read/write); pass NULL to eject */
int     floppy_mount(struct Smaky6 *m, int drive, const char *path);
int     floppy_mount_hostdir(struct Smaky6 *m, int drive, const char *path);
int     floppy_refresh_virtual(struct Smaky6 *m, int drive);

/* Port read/write from Z80 */
uint8_t floppy_read_stat(struct Smaky6 *m);    /* legacy status bits */
uint8_t floppy_read_sector19(struct Smaky6 *m);/* port 0x19 [3:0] = sector */
uint8_t floppy_read_cont(struct Smaky6 *m);    /* port 0x1A read: bit7=ready */
uint8_t floppy_read_data(struct Smaky6 *m);    /* port 0x1B: streaming bytes */
void    floppy_write_cont(struct Smaky6 *m, uint8_t val);

/* Advance the sector counter (call every ~2 ms for 300 RPM emulation) */
void    floppy_tick(struct Smaky6 *m);

#endif /* FLOPPY_H */
