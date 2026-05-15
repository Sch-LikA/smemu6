// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* floppy.c – Micropolis hard-sectored floppy controller (discrete logic) */
#include "machine_internal.h"
#include "floppy.h"
#include "sound.h"
#include "virtual_floppy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Direct field access: struct Smaky6 fully visible via machine_internal.h */

static int floppy_media_is_mounted(const struct FloppyMedia *media)
{
    return media->kind != FLOPPY_MEDIA_NONE;
}

static void floppy_reset_stream_state(struct Smaky6 *m, int drive)
{
    m->fdc.byte_pos = 0;
    m->fdc.sec_csum = 0;
    memset(m->fdc.sec_buf, 0, sizeof(m->fdc.sec_buf));
    m->fdc.disk_active[drive] = 0;
    m->fdc.phased_sector[drive] = 0;
}

static void floppy_unmount_drive(struct Smaky6 *m, int drive)
{
    struct FloppyMedia *media = &m->fdc.media[drive];

    if (media->kind == FLOPPY_MEDIA_FILE && media->file) {
        fclose(media->file);
    }
    free(media->data);
    memset(media, 0, sizeof(*media));
    m->fdc.track[drive] = 0;
    m->fdc.num_tracks[drive] = FLOPPY_TRACKS_40;
    floppy_reset_stream_state(m, drive);
}

static void floppy_apply_geometry(struct Smaky6 *m, int drive, size_t size,
                                  const char *path, const char *kind)
{
    if (size == FLOPPY_IMAGE_77) {
        m->fdc.num_tracks[drive] = FLOPPY_TRACKS_77;
        fprintf(stderr, "floppy: mounted %s '%s' on DX%d (77 tracks, 315 KB)\n",
                kind, path, drive);
    } else if (size == FLOPPY_IMAGE_40) {
        m->fdc.num_tracks[drive] = FLOPPY_TRACKS_40;
        fprintf(stderr, "floppy: mounted %s '%s' on DX%d (40 tracks, 160 KB)\n",
                kind, path, drive);
    } else {
        m->fdc.num_tracks[drive] = FLOPPY_TRACKS_40;
        fprintf(stderr,
                "floppy: WARNING: %s '%s' has unexpected size %zu bytes; assuming 40 tracks\n",
                kind, path, size);
    }
    m->fdc.track[drive] = 0;
    floppy_reset_stream_state(m, drive);
}

static int floppy_read_sector_bytes(struct FloppyMedia *media, long offset,
                                    uint8_t *out)
{
    if (media->kind == FLOPPY_MEDIA_FILE) {
        if (fseek(media->file, offset, SEEK_SET) != 0) {
            return -1;
        }
        size_t n = fread(out, 1, FLOPPY_SECTOR_BYTES, media->file);
        if (n < FLOPPY_SECTOR_BYTES) {
            memset(out + n, 0, FLOPPY_SECTOR_BYTES - n);
        }
        return 0;
    }

    if (media->kind == FLOPPY_MEDIA_MEMORY) {
        if (offset < 0 || (size_t)offset >= media->size) {
            memset(out, 0, FLOPPY_SECTOR_BYTES);
            return -1;
        }

        size_t available = media->size - (size_t)offset;
        size_t n = available < FLOPPY_SECTOR_BYTES ? available : FLOPPY_SECTOR_BYTES;
        memcpy(out, media->data + offset, n);
        if (n < FLOPPY_SECTOR_BYTES) {
            memset(out + n, 0, FLOPPY_SECTOR_BYTES - n);
        }
        return 0;
    }

    memset(out, 0, FLOPPY_SECTOR_BYTES);
    return -1;
}

void floppy_init(struct Smaky6 *m)
{
    for (int d = 0; d < 2; d++) {
        memset(&m->fdc.media[d], 0, sizeof(m->fdc.media[d]));
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
    m->fdc.selected_drive    = 0;
    m->fdc.phased_sector[0]  = 0;
    m->fdc.phased_sector[1]  = 0;
    memset(m->fdc.sec_buf, 0, sizeof(m->fdc.sec_buf));
}

void floppy_fini(struct Smaky6 *m)
{
    for (int d = 0; d < 2; d++) {
        floppy_unmount_drive(m, d);
    }
}

int floppy_mount(struct Smaky6 *m, int drive, const char *path)
{
    floppy_unmount_drive(m, drive);
    if (!path) return 0;

    m->fdc.media[drive].file = fopen(path, "r+b");
    if (!m->fdc.media[drive].file) {
        fprintf(stderr, "floppy: cannot open '%s'\n", path);
        return -1;
    }
    m->fdc.media[drive].kind = FLOPPY_MEDIA_FILE;
    m->fdc.media[drive].read_only = 0;
    m->fdc.media[drive].refreshable = 0;
    snprintf(m->fdc.media[drive].source_path,
             sizeof(m->fdc.media[drive].source_path), "%s", path);
    snprintf(m->fdc.media[drive].description,
             sizeof(m->fdc.media[drive].description), "%s", path);

    /* Auto-detect geometry from file size */
    if (fseek(m->fdc.media[drive].file, 0, SEEK_END) != 0) {
        fprintf(stderr, "floppy: cannot size '%s'\n", path);
        floppy_unmount_drive(m, drive);
        return -1;
    }
    long sz = ftell(m->fdc.media[drive].file);
    rewind(m->fdc.media[drive].file);
    if (sz < 0) {
        fprintf(stderr, "floppy: cannot size '%s'\n", path);
        floppy_unmount_drive(m, drive);
        return -1;
    }

    floppy_apply_geometry(m, drive, (size_t)sz, path, "image");
    return 0;
}

int floppy_mount_hostdir(struct Smaky6 *m, int drive, const char *path)
{
    uint8_t *image = NULL;
    size_t image_size = 0;
    char error[256];

    floppy_unmount_drive(m, drive);
    if (!path) return 0;

    if (virtual_floppy_build_from_hostdir(path, &image, &image_size,
                                          error, sizeof(error)) != 0) {
        fprintf(stderr, "floppy: cannot build virtual floppy from '%s': %s\n",
                path, error);
        return -1;
    }

    m->fdc.media[drive].kind = FLOPPY_MEDIA_MEMORY;
    m->fdc.media[drive].data = image;
    m->fdc.media[drive].size = image_size;
    m->fdc.media[drive].read_only = 1;
    m->fdc.media[drive].refreshable = 1;
    snprintf(m->fdc.media[drive].source_path,
             sizeof(m->fdc.media[drive].source_path), "%s", path);
    snprintf(m->fdc.media[drive].description,
             sizeof(m->fdc.media[drive].description), "%s", path);

    floppy_apply_geometry(m, drive, image_size, path,
                          "virtual host directory");
    fprintf(stderr,
            "floppy: DX%d is virtual, read-only, and non-bootable in this first host-directory slice\n",
            drive);
    return 0;
}

int floppy_refresh_virtual(struct Smaky6 *m, int drive)
{
    char path[sizeof(m->fdc.media[drive].source_path)];

    if (drive < 0 || drive > 1) {
        return -1;
    }
    if (!m->fdc.media[drive].refreshable ||
        m->fdc.media[drive].source_path[0] == '\0') {
        return 0;
    }

    snprintf(path, sizeof(path), "%s", m->fdc.media[drive].source_path);
    if (floppy_mount_hostdir(m, drive, path) != 0) {
        return -1;
    }

    fprintf(stderr, "floppy: refreshed virtual host directory '%s' on DX%d\n",
            path, drive);
    return 1;
}

uint8_t floppy_read_stat(struct Smaky6 *m)
{
    int     drive = m->fdc.selected_drive;
    uint8_t stat  = 0x00u;
    if (m->fdc.track[drive] == 0) stat |= 0x01u;   /* TRACK0 */
    if (floppy_media_is_mounted(&m->fdc.media[drive])) stat |= 0x04u;   /* DRIVE_READY */
    if (floppy_media_is_mounted(&m->fdc.media[drive])) stat |= 0x02u;   /* READ_REQ */
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
    int drive = m->fdc.selected_drive;
    if (!floppy_media_is_mounted(&m->fdc.media[drive]))
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

    /* bits [3:0] = sector index; bit 7 currently stays asserted so the
     * post-boot SAMOS floppy helpers do not treat mounted media as protected
     * before any sector-read phase begins. Bits 4/5/6 remain 0 (settled,
     * ready) unless seek_busy > 0, in which case bit 5 = 1. */
    uint8_t val = (uint8_t)(sector | 0x80u);
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
    int drive = m->fdc.selected_drive;

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
             * Drive is determined from selected_drive (set by DRISEL1/DRISEL2
             * bits 5/6 of port 0x19 per Plan F5 IC7 LS475 schematic). */
            int sd = m->fdc.selected_drive;
            drive   = sd;
            req_sec = m->fdc.sector & 0x0Fu;
            id_sec  = m->fdc.track[sd];
            track   = m->fdc.track[sd];
        }

        if (floppy_media_is_mounted(&m->fdc.media[drive])) {
            /* Clamp track to the image geometry before computing the offset.
             * The track value comes from emulated RAM (0x4504), which is loaded
             * from the floppy image itself — a crafted image could place an
             * out-of-range value there and cause fseek to go beyond the image. */
            uint8_t max_track = m->fdc.num_tracks[drive] - 1u;
            if (track > max_track) {
                fprintf(stderr, "[fdc] track %u out of range (max %u), clamping\n",
                        (unsigned)track, (unsigned)max_track);
                track = max_track;
            }
            m->fdc.disk_active[drive] = 6;
            long offset = ((long)track * FLOPPY_SECTORS + req_sec) * FLOPPY_SECTOR_BYTES;
            (void)floppy_read_sector_bytes(&m->fdc.media[drive], offset,
                                           m->fdc.sec_buf);
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
        m->fdc.phased_sector[drive] = req_sec;
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
    int drive = m->fdc.selected_drive;

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
            sound_floppy_step(m, drive);
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
         * Drive from selected_drive (port 0x19), not ctrl (port 0x1A step
         * bytes can have bit4=1 for drive-A ops — cannot use as drive select). */
        int sd = m->fdc.selected_drive;
        uint8_t b88    = m->bus[0x2B88];
        uint8_t target = ((b88 >> 6) & 1) ? m->bus[0x2B8C] : m->bus[0x2B8B];
        if (m->fdc.track[sd] < target && m->fdc.track[sd] < m->fdc.num_tracks[sd] - 1u)
            m->fdc.track[sd]++;
        else if (m->fdc.track[sd] > target)
            m->fdc.track[sd]--;
        sound_floppy_step(m, drive);
    }
    m->fdc.ctrl = val;
}

void floppy_tick(struct Smaky6 *m)
{
    m->fdc.sector   = (m->fdc.sector + 1) % FLOPPY_SECTORS;
    m->fdc.byte_pos = 0;   /* new hard-sector: reset data stream */

    /* Sector-hole sensor click (audible while motor is spinning) */
    sound_floppy_sector(m);

    /* Decay seek_busy counter — simulates realistic head-settle timing */
    if (m->fdc.seek_busy > 0)
        m->fdc.seek_busy--;

    /* Decay disk-activity LEDs one step per frame */
    for (int d = 0; d < 2; d++)
        if (m->fdc.disk_active[d] > 0)
            m->fdc.disk_active[d]--;

    /* Decay Winchester activity LEDs one step per frame */
    for (int d = 0; d < 2; d++)
        if (m->win.disk_active[d] > 0)
            m->win.disk_active[d]--;

    /* NOTE (Phase 1N): floppy loading uses maskable INT in IM 0, NOT NMI.
     * The Micropolis floppy controller places 0xCF (RST 08h) on the data bus
     * during the INT acknowledge cycle.  machine.c::z80_int_fetch returns 0xCF
     * when nmi_armed is set.  The 50Hz machine_int drives the INT line.
     * Do NOT call z80_nmi here — that would trigger the BREAK/NMI handler at
     * 0x0066 which blocks floppy loading by waiting for a keypress. */
}
