/* rtc.h – Smaky 6 real-time clock emulation (E405/08, extension board)
 *
 * Hardware (confirmed from R. Forster schematic, Oct 1979):
 *   Chip   : E405/08 (IC5)
 *   Port   : 0x08  R/W
 *   bit 3  : CK   – serial clock (rising edge latches data)
 *   bit 0  : I/O  – bidirectional data (MOSI on write, MISO on read)
 *   bit 2  : always 1 during a transaction (chip-select / direction)
 *   bit 1  : 1 during command phase, 0 during data phase
 *
 * Protocol (recovered from SYS.SY disassembly):
 *   1. OUT(0x08) = 0x00  → idle / reset
 *   2. Command phase: 4 rising CK edges, bit0 = command bit (LSB first)
 *        Read  command nibble: 0b1111 (C=0x0F in ROM)
 *        Write command nibble: 0b0111 (C=0x07 in ROM)
 *   3. Data phase: 7 bytes × 8 bits, MSB first
 *        Read : RTC drives bit0 (MISO) on each CK=0 phase; Z80 reads with IN
 *        Write: Z80 drives bit0 (MOSI) on each CK=0; latched on rising edge
 *
 * Register layout (7 bytes, BCD encoding):
 *   regs[0] = seconds   (00–59)
 *   regs[1] = minutes   (00–59)
 *   regs[2] = hours     (00–23, 24-hour)
 *   regs[3] = weekday   (1=Mon … 7=Sun, Smaky convention)
 *   regs[4] = day       (01–31)
 *   regs[5] = month     (01–12)
 *   regs[6] = year      (00–99, 2 digits, BCD)
 */
#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef enum {
    RTC_IDLE,       /* waiting for start of transaction          */
    RTC_CMD,        /* collecting 4 command bits (rising-CK)     */
    RTC_READING,    /* driving MISO: 7 bytes × 8 bits MSB-first  */
    RTC_WRITING     /* latching MOSI: 7 bytes × 8 bits MSB-first */
} RtcPhase;

typedef struct {
    uint8_t  regs[7];       /* BCD time/date registers (see layout above) */
    RtcPhase phase;
    int      cmd_bits;      /* number of command bits collected so far (0-4) */
    uint8_t  cmd_nibble;    /* accumulated command nibble (LSB first)        */
    int      bit_count;     /* bit index within current byte (0-7)          */
    int      byte_idx;      /* which of the 7 data bytes (0-6)              */
    uint8_t  shift_reg;     /* byte accumulator for write phase              */
    uint8_t  miso;          /* current MISO bit (bit 0 of port IN result)   */
    int      ck_prev;       /* previous CK value for edge detection         */
    int      frame_frac;    /* frames remaining until next second tick      */
} RtcState;

/* Initialise RTC, seeding registers from host localtime(). */
void rtc_init(RtcState *rtc);

/* Call once per 50 Hz frame to advance the emulated clock. */
void rtc_tick_frame(RtcState *rtc);

/* Handle an OUT to port 0x08 (drives CK / I/O / CS lines). */
void rtc_write_port(RtcState *rtc, uint8_t data);

/* Handle an IN from port 0x08; returns byte with bit 0 = MISO, rest = 0. */
uint8_t rtc_read_port(const RtcState *rtc);

#endif /* RTC_H */
