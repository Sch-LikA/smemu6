/* floppy.h – Micropolis hard-sectored floppy controller */
#ifndef FLOPPY_H
#define FLOPPY_H

#include <stdint.h>

struct Smaky6;

/*
 * Micropolis 5.25" hard-sectored single-sided:
 *   40 tracks × 16 sectors × 256 bytes = 163,840 bytes per disk
 *
 * Port map (bit-banged discrete logic):
 *   Port 0x1A (CONT) write:
 *     bit 0 = MOTOR       (1 = on)
 *     bit 1 = HEAD_LOAD   (1 = loaded)
 *     bit 2 = STEP_PULSE  (0→1 rising edge = one step)
 *     bit 3 = DIRECTION   (1 = toward track 0)
 *     bit 4 = DRIVE_SEL   (drive B if 1, A if 0)
 *     bit 5 = INT_ENABLE  (enable sector interrupt)
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

/* Port read/write from Z80 */
uint8_t floppy_read_stat(struct Smaky6 *m);    /* legacy status bits */
uint8_t floppy_read_sector19(struct Smaky6 *m);/* port 0x19 [3:0] = sector */
uint8_t floppy_read_cont(struct Smaky6 *m);    /* port 0x1A read: bit7=ready */
uint8_t floppy_read_data(struct Smaky6 *m);    /* port 0x1B: streaming bytes */
void    floppy_write_cont(struct Smaky6 *m, uint8_t val);

/* Advance the sector counter (call every ~2 ms for 300 RPM emulation) */
void    floppy_tick(struct Smaky6 *m);

#endif /* FLOPPY_H */
