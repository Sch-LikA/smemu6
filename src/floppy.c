/* floppy.c – Micropolis hard-sectored floppy controller (discrete logic) */
#include "machine_internal.h"
#include "floppy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Direct field access: struct Smaky6 fully visible via machine_internal.h */

void floppy_init(struct Smaky6 *m)
{
    for (int d = 0; d < 2; d++) {
        m->fdc.image[d]      = NULL;
        m->fdc.track[d]      = 0;
        m->fdc.num_tracks[d] = FLOPPY_TRACKS_40;
    }
    m->fdc.ctrl      = 0;
    m->fdc.sector    = 0;
    m->fdc.step_prev = 0;
    m->fdc.byte_pos  = 0;
    m->fdc.sec_csum  = 0;
    m->fdc.seek_busy = 0;
    m->fdc.disk_active[0] = 0;
    m->fdc.disk_active[1] = 0;
    m->fdc.nmi_armed = 0;
    m->fdc.phased_sector = 0;
    memset(m->fdc.sec_buf, 0, sizeof(m->fdc.sec_buf));
}

void floppy_fini(struct Smaky6 *m)
{
    for (int d = 0; d < 2; d++) {
        if (m->fdc.image[d]) { fclose(m->fdc.image[d]); m->fdc.image[d] = NULL; }
    }
}

int floppy_mount(struct Smaky6 *m, int drive, const char *path)
{
    if (m->fdc.image[drive]) { fclose(m->fdc.image[drive]); m->fdc.image[drive] = NULL; }
    if (!path) return 0;

    m->fdc.image[drive] = fopen(path, "r+b");
    if (!m->fdc.image[drive]) {
        fprintf(stderr, "floppy: cannot open '%s'\n", path);
        return -1;
    }

    /* Auto-detect geometry from file size */
    fseek(m->fdc.image[drive], 0, SEEK_END);
    long sz = ftell(m->fdc.image[drive]);
    rewind(m->fdc.image[drive]);

    if ((unsigned long)sz == FLOPPY_IMAGE_77) {
        m->fdc.num_tracks[drive] = FLOPPY_TRACKS_77;
        fprintf(stderr, "floppy: mounted '%s' on drive %c (77 tracks, 315 KB)\n",
                path, 'A' + drive);
    } else if ((unsigned long)sz == FLOPPY_IMAGE_40) {
        m->fdc.num_tracks[drive] = FLOPPY_TRACKS_40;
        fprintf(stderr, "floppy: mounted '%s' on drive %c (40 tracks, 160 KB)\n",
                path, 'A' + drive);
    } else {
        m->fdc.num_tracks[drive] = FLOPPY_TRACKS_40;
        fprintf(stderr, "floppy: WARNING: '%s' has unexpected size %ld bytes; "
                "assuming 40 tracks\n", path, sz);
    }
    m->fdc.track[drive] = 0;
    return 0;
}

uint8_t floppy_read_stat(struct Smaky6 *m)
{
    int     drive = (m->fdc.ctrl >> 4) & 1;
    uint8_t stat  = 0x00u;
    if (m->fdc.track[drive] == 0) stat |= 0x01u;   /* TRACK0 */
    if (m->fdc.image[drive])      stat |= 0x04u;   /* DRIVE_READY */
    if (m->fdc.image[drive])      stat |= 0x02u;   /* READ_REQ */
    return stat;
}

/* Port 0x19 multiplex read (Phantom ROM protocol):
 *
 * bits [3:0]  Current hard-sector index (0–15).  We sync this to the value
 *             the ROM stored in RAM at 0x4503 (the expected sector) so that
 *             the sector-match check in floppy_stream_read passes on the first
 *             read.  On real hardware the disk spins and the ROM retries until
 *             the rotating sector index matches; here we short-circuit that.
 *
 * bit  4      0 = byte ready (always asserted — ROM exits its spin immediately)
 * bit  5      0 = head at track 0 / step complete (asserted when seek_busy=0)
 * bit  6      0 = seek settled (asserted when seek_busy=0)
 *
 * We keep seek_busy as a down-counter so that seek/step poll loops do a few
 * iterations (realistic) before seeing the done condition.
 */
uint8_t floppy_read_sector19(struct Smaky6 *m)
{
    int drive = (m->fdc.ctrl >> 4) & 1;
    if (!m->fdc.image[drive])
        return 0xFFu;

    /* During Phantom ROM loading, force-match expected sector (0x4503) so the
     * bootloader's tight polling loops can progress with our coarse 50 Hz tick.
     * After bank-switch (ROM unmapped), expose the real rotating sector index so
     * relocated SYS.SY can observe sector transitions naturally. */
    uint8_t sector = m->fdc.sector & 0x0Fu;
    if (m->rom_mask[0x0000] != 0) {
        sector = m->bus[0x4503] & 0x0Fu;
        m->fdc.sector = sector;
    }

    /* bits [3:0] = sector index; bits 4/5/6 = 0 (settled, ready)
     * unless seek_busy > 0, in which case bit 5 = 1 (step in progress). */
    uint8_t val = sector;
    if (m->fdc.seek_busy > 0) {
        val |= 0x20u;  /* bit 5 = 1 (head stepping, not settled) */
    } else {
    }
    return val;
}

/* Port 0x1A read: bit 7 = 1 means "data byte ready".
   We always report ready so the ROM's IN F,(C) spin completes instantly. */
uint8_t floppy_read_cont(struct Smaky6 *m)
{
    (void)m;
    return 0x80u;   /* sign bit set → jp p skips, byte available */
}

/* Port 0x1B read: streaming sector data.
 *
 * Byte stream per sector (Micropolis hard-sectored protocol):
 *   byte_pos 0   : sync byte (0x00) — consumed by ROM before sector-ID check
 *   byte_pos 1   : sector ID = sector index from ROM's 0x4503
 *   byte_pos 2–257: 256 bytes of sector data from the flat image
 *   byte_pos 258 : 8-bit checksum (sum of data bytes)
 *
 * Track and sector are taken from ROM workspace RAM (0x4504 = track, 0x4503 = sector)
 * so the data matches what the ROM actually requested, regardless of our internal
 * step-pulse counter.
 */
uint8_t floppy_read_data(struct Smaky6 *m)
{
    int drive = (m->fdc.ctrl >> 4) & 1;

    if (m->fdc.byte_pos == 0) {
        /* Sync byte */
        m->fdc.byte_pos = 1;
        return 0x00u;
    }

    if (m->fdc.byte_pos == 1) {
        uint8_t id_sec;
        uint8_t req_sec;
        uint8_t track;

        if (m->rom_mask[0x0000] != 0) {
            /* Phantom ROM loader path:
             *  - ID byte validated against ROM workspace 0x4508
             *  - payload source selected by ROM workspace 0x4503/0x4504 */
            id_sec  = m->bus[0x4508] & 0x0Fu;
            req_sec = m->bus[0x4503] & 0x0Fu;
            track   = m->bus[0x4504];
            m->fdc.sector = req_sec;
            m->fdc.track[drive] = track;
        } else {
            /* Relocated SYS.SY path:
             * The Micropolis sector header contains the physical track number
             * as byte 1 (the ID byte).  The OS compares this against its
             * expected-track variable at (0x2B8B) via CP (HL) at 0x20C2.
             * Always use drive 0 (drive A): post-ROM stepping keeps track[0]
             * current, and the ARM command sets ctrl to drive=0 (e.g., 0x2C). */
            req_sec = m->fdc.sector & 0x0Fu;
            id_sec  = m->fdc.track[0];  /* track number, drive A always */
            track   = m->fdc.track[0];
        }

        if (m->fdc.image[drive]) {
            m->fdc.disk_active[drive] = 6;
            long offset = ((long)track * FLOPPY_SECTORS + req_sec) * FLOPPY_SECTOR_BYTES;
            if (fseek(m->fdc.image[drive], offset, SEEK_SET) == 0) {
                size_t n = fread(m->fdc.sec_buf, 1, FLOPPY_SECTOR_BYTES,
                                 m->fdc.image[drive]);
                if (n < FLOPPY_SECTOR_BYTES)
                    memset(m->fdc.sec_buf + n, 0, FLOPPY_SECTOR_BYTES - n);
            }
            uint8_t ck = 0;
            for (unsigned i = 0; i < FLOPPY_SECTOR_BYTES; i++) ck += m->fdc.sec_buf[i];
            m->fdc.sec_csum = ck;
        }
        if (m->dbg.trace_fdc && m->rom_mask[0x0000] == 0) {
            fprintf(stderr,
                    "[fdc] id pc=%04X drv=%d trk=%u req=%u id=%u ctrl=%02X\n",
                    (unsigned)Z80_PC(m->cpu), drive,
                    (unsigned)track, (unsigned)req_sec, (unsigned)id_sec,
                    (unsigned)m->fdc.ctrl);
        }
        m->fdc.byte_pos = 2;
        return id_sec;   /* sector ID = what the ROM expects */
    }

    if (m->fdc.byte_pos <= 257) {
        uint8_t b = m->fdc.sec_buf[m->fdc.byte_pos - 2];
        m->fdc.byte_pos++;
        return b;
    }

    /* byte_pos == 258: checksum — end of sector */
    uint8_t ck = m->fdc.sec_csum;
    if (m->dbg.trace_fdc && m->rom_mask[0x0000] == 0) {
        fprintf(stderr,
                "[fdc] cks pc=%04X drv=%d trk=%u sec=%u sum=%02X\n",
                (unsigned)Z80_PC(m->cpu), drive,
                (unsigned)m->fdc.track[drive], (unsigned)(m->fdc.sector & 0x0Fu),
                (unsigned)ck);
    }
    m->fdc.sector   = (m->fdc.sector + 1) % FLOPPY_SECTORS;
    m->fdc.byte_pos = 0;
    return ck;
}

void floppy_write_cont(struct Smaky6 *m, uint8_t val)
{
    int drive = (val >> 4) & 1;

    if (m->rom_mask[0x0000] != 0) {
        /* Phantom ROM mode: edge-triggered step on bit 2, direction from bit 3
         * (bit3=0 → outward/track++, bit3=1 → inward/track--).  This matches
         * the ROM's bit-bang protocol which toggles bit 2 for each step. */
        int step_now = (val >> 2) & 1;
        if (step_now && !m->fdc.step_prev) {
            int dir = (val >> 3) & 1;
            uint8_t max_track = m->fdc.num_tracks[drive] - 1;
            if (dir) { if (m->fdc.track[drive] > 0)          m->fdc.track[drive]--; }
            else     { if (m->fdc.track[drive] < max_track)   m->fdc.track[drive]++; }
        }
        m->fdc.step_prev = step_now;
    } else {
        /* Post-ROM (SYS.SY) mode: one step per write to port 0x1A.
         *
         * Sub 0x2187 updates the head-track variable at (0x2B8B) or (0x2B8C)
         * to the DESTINATION track BEFORE starting the step loop, then runs
         * the loop B = |target - current| times.  Rather than decoding the
         * direction from the A-register encoding (which depends on runtime
         * workspace byte (0x2B88)), we derive direction by comparing the
         * emulated track against the OS's target in that variable:
         *   - (0x2B88) bit6=0 → single-sided → target in (0x2B8B)
         *   - (0x2B88) bit6=1 → double-sided → target in (0x2B8C)
         * This is robust against any value of (0x2B88).
         * SYS.SY always operates on drive A (drive 0). */
        uint8_t b88    = m->bus[0x2B88];
        uint8_t target = ((b88 >> 6) & 1) ? m->bus[0x2B8C] : m->bus[0x2B8B];
        if (m->fdc.track[0] < target && m->fdc.track[0] < m->fdc.num_tracks[0] - 1u)
            m->fdc.track[0]++;
        else if (m->fdc.track[0] > target)
            m->fdc.track[0]--;
    }
    m->fdc.ctrl = val;
}

void floppy_tick(struct Smaky6 *m)
{
    m->fdc.sector   = (m->fdc.sector + 1) % FLOPPY_SECTORS;
    m->fdc.byte_pos = 0;   /* new hard-sector: reset data stream */

    /* Decay seek_busy counter — simulates realistic head-settle timing */
    if (m->fdc.seek_busy > 0)
        m->fdc.seek_busy--;

    /* Decay disk-activity LEDs one step per frame */
    for (int d = 0; d < 2; d++)
        if (m->fdc.disk_active[d] > 0)
            m->fdc.disk_active[d]--;

    /* NOTE (Phase 1N): floppy loading uses maskable INT in IM 0, NOT NMI.
     * The Micropolis floppy controller places 0xCF (RST 08h) on the data bus
     * during the INT acknowledge cycle.  machine.c::z80_int_fetch returns 0xCF
     * when nmi_armed is set.  The 50Hz machine_int drives the INT line.
     * Do NOT call z80_nmi here — that would trigger the BREAK/NMI handler at
     * 0x0066 which blocks floppy loading by waiting for a keypress. */
}
