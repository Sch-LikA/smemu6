// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* rtc.c – Smaky 6 E405/08 real-time clock emulation */
#include "rtc.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

/* ── BCD helpers ─────────────────────────────────────────────────────────── */

static uint8_t int_to_bcd(int n)
{
    return (uint8_t)(((n / 10) << 4) | (n % 10));
}

/* ── MISO pre-load ───────────────────────────────────────────────────────── */

/*
 * Pre-load the MISO bit for the CURRENT bit_count position.
 * Called on each falling CK edge BEFORE advancing bit_count.
 *
 * Protocol: the Z80 receive loop does RRA;RR(HL) which places the first
 * received bit at position 0 (LSB) after 8 iterations.  The RTC therefore
 * transmits bytes LSB-first: bit_count=0 → bit0, bit_count=7 → bit7.
 */
static void rtc_preload_miso(RtcState *rtc)
{
    if (rtc->byte_idx >= 7) {
        rtc->miso = 0;
        return;
    }
    rtc->miso = (rtc->regs[rtc->byte_idx] >> rtc->bit_count) & 1u;
}

/* BCD increment with ceiling; returns 1 on carry (value wrapped to 0). */
static int bcd_inc(uint8_t *reg, int max_val)
{
    int lo  = *reg & 0x0F;
    int hi  = (*reg >> 4) & 0x0F;
    int val = hi * 10 + lo + 1;
    if (val > max_val) { *reg = int_to_bcd(0); return 1; }
    *reg = int_to_bcd(val);
    return 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void rtc_init(RtcState *rtc)
{
    memset(rtc, 0, sizeof(*rtc));
    rtc->phase      = RTC_IDLE;
    rtc->frame_frac = 50;   /* 50 frames = 1 second at 50 Hz */

    /* Seed from host wall clock */
    time_t now  = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return;

    /*
     * Weekday: localtime() tm_wday 0=Sun … 6=Sat
     * Smaky convention: 1=Mon … 7=Sun  (from SDAY prompt "(1=Mon ... 7=Sun)wd:")
     * Confirmed: value 5 → "Vendredi" (Friday), so Saturday = 6, Sunday = 7.
     */
    int wday_smaky = (t->tm_wday == 0) ? 7 : t->tm_wday;

    /* E405 register order confirmed from SAMOS display behaviour:
     *   SAMOS time hh:mm:ss reads bytes [0],[1],[6]
     *   SAMOS date DD/MM/YY reads bytes [2],[3],[4]
     *   SAMOS weekday name reads byte [5] */
    rtc->regs[0] = int_to_bcd(t->tm_hour);
    rtc->regs[1] = int_to_bcd(t->tm_min);
    rtc->regs[2] = int_to_bcd(t->tm_mday);
    rtc->regs[3] = int_to_bcd(t->tm_mon + 1);   /* tm_mon is 0-based */
    rtc->regs[4] = int_to_bcd(t->tm_year % 100); /* 2-digit year */
    rtc->regs[5] = int_to_bcd(wday_smaky);
    rtc->regs[6] = int_to_bcd(t->tm_sec);

    fprintf(stderr, "[rtc] init: %02X:%02X:%02X  wd=%02X  %02X/%02X/%02X\n",
            rtc->regs[0], rtc->regs[1], rtc->regs[6],
            rtc->regs[5],
            rtc->regs[2], rtc->regs[3], rtc->regs[4]);
}

void rtc_tick_frame(RtcState *rtc)
{
    /* Tick the seconds register once every 50 frames (50 Hz clock). */
    if (--rtc->frame_frac > 0) return;
    rtc->frame_frac = 50;

    /* Advance seconds(6) → minutes(1) → hours(0); propagate carry */
    if (!bcd_inc(&rtc->regs[6], 59)) return;  /* seconds */
    if (!bcd_inc(&rtc->regs[1], 59)) return;  /* minutes */
    if (!bcd_inc(&rtc->regs[0], 23)) return;  /* hours   */

    /* Advance weekday (1–7: 1=Mon…7=Sun, wraps 7→1) */
    {
        int wd = (rtc->regs[5] & 0x0F);   /* BCD single digit 1-7 */
        wd = (wd % 7) + 1;
        rtc->regs[5] = int_to_bcd(wd);
    }

    /* Day-of-month: simple 31-day ceiling */
    if (!bcd_inc(&rtc->regs[2], 31)) return;
    if (!bcd_inc(&rtc->regs[3], 12)) return;
    /* Year: 2-digit BCD, wraps 99→00 */
    {
        int yr = ((rtc->regs[4] >> 4) & 0x0F) * 10 + (rtc->regs[4] & 0x0F);
        yr = (yr + 1) % 100;
        rtc->regs[4] = int_to_bcd(yr);
    }
}

void rtc_write_port(RtcState *rtc, uint8_t data)
{
    int ck      = (data >> 3) & 1;
    int io      = data & 1;
    int rising  = (rtc->ck_prev == 0 && ck == 1);
    int falling = (rtc->ck_prev == 1 && ck == 0);

    /* OUT(0x00) = idle/reset: unconditionally return to IDLE */
    if (data == 0x00) {
        rtc->phase    = RTC_IDLE;
        rtc->miso     = 0;
        rtc->ck_prev  = 0;
        return;
    }

    switch (rtc->phase) {

    case RTC_IDLE:
        /*
         * Transaction begins when bit2 is asserted (chip select active).
         * Start collecting command bits on the first rising CK edge.
         */
        if ((data & 0x04) && !ck) {
            /* CS asserted with CK low — prepare for command phase */
            rtc->phase      = RTC_CMD;
            rtc->cmd_bits   = 0;
            rtc->cmd_nibble = 0;
        } else if (rising && (data & 0x04)) {
            /* Transition directly if we see a rising edge with CS active */
            rtc->phase      = RTC_CMD;
            rtc->cmd_bits   = 0;
            rtc->cmd_nibble = 0;
            goto latch_cmd_bit;
        }
        break;

    case RTC_CMD:
latch_cmd_bit:
        if (rising) {
            rtc->cmd_nibble |= (uint8_t)(io << rtc->cmd_bits);
            rtc->cmd_bits++;
            if (rtc->cmd_bits == 4) {
                /* Decode: 0b1111 = read, 0b0111 = write (anything else: write) */
                if (rtc->cmd_nibble == 0x0Fu) {
                    rtc->phase     = RTC_READING;
                    rtc->byte_idx  = 0;
                    rtc->bit_count = 0;
                    /* Do NOT preload here: the first falling edge of the receive
                     * loop (CK: 1→0 as Z80 transitions from command to data phase)
                     * will preload bit 0 before the first IN instruction. */
                } else {
                    rtc->phase     = RTC_WRITING;
                    rtc->byte_idx  = 0;
                    rtc->bit_count = 0;
                    rtc->shift_reg = 0;
                }
            }
        }
        break;

    case RTC_READING:
        /*
         * Protocol timing (Z80 receive loop):
         *   OUT(CK=0) → [falling edge] → IN(MISO) → OUT(CK=1) → repeat
         *
         * On each FALLING edge the Z80 is about to read MISO.  We preload the
         * CURRENT bit first, THEN advance the counter.  This is critical:
         * the first OUT(CK=0) of the receive loop produces a falling edge
         * (CK was left at 1 by the last command clock), so bit 0 must be
         * preloaded on that very first falling edge for the upcoming IN.
         */
        if (falling) {
            rtc_preload_miso(rtc);   /* latch bit_count BEFORE advancing */
            rtc->bit_count++;
            if (rtc->bit_count == 8) {
                rtc->byte_idx++;
                rtc->bit_count = 0;
                if (rtc->byte_idx >= 7)
                    rtc->phase = RTC_IDLE;
                    /* miso still holds last bit for upcoming IN; reset happens
                     * when Z80 outputs A=0 at the end of the transaction */
            }
        }
        break;

    case RTC_WRITING:
        /* Latch MOSI (bit0) on each rising CK edge, LSB first:
         * Z80 transmit loop shifts out bit0 of C first (RR C extracts LSB).
         * Build shift_reg by placing each received bit at position bit_count. */
        if (rising) {
            rtc->shift_reg |= (uint8_t)(io << rtc->bit_count);
            rtc->bit_count++;
            if (rtc->bit_count == 8) {
                if (rtc->byte_idx < 7)
                    rtc->regs[rtc->byte_idx] = rtc->shift_reg;
                rtc->byte_idx++;
                rtc->bit_count = 0;
                rtc->shift_reg = 0;
                if (rtc->byte_idx >= 7) {
                    rtc->phase = RTC_IDLE;
                    fprintf(stderr,
                            "[rtc] write: %02X:%02X:%02X  wd=%02X  %02X/%02X/%02X\n",
                            rtc->regs[0], rtc->regs[1], rtc->regs[6],
                            rtc->regs[5],
                            rtc->regs[2], rtc->regs[3], rtc->regs[4]);
                }
            }
        }
        break;
    }

    rtc->ck_prev = ck;
}

uint8_t rtc_read_port(const RtcState *rtc)
{
    return rtc->miso & 1u;
}
