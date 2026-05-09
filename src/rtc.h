/* rtc.h – Smaky 6 real-time clock emulation (E405/08, extension board)
 *
 * Hardware (confirmed from R. Forster schematic, Oct 1979):
 *   Chip   : E405/08 (IC5)
 *   Port   : 0x08  R/W
 *   bit 3  : CK   – serial clock (rising edge latches MOSI; falling pre-loads MISO)
 *   bit 2  : DIR/CS – held high during a transaction
 *   bit 1  : DIR/CS – held high during command phase
 *   bit 0  : I/O  – bidirectional data (MOSI on OUT, MISO on IN)
 *
 * Interface type: 3-wire synchronous bit-bang serial, proprietary Epsitec
 * protocol.  The machine was built 1978-79, predating the SPI standard
 * (Motorola, mid-1980s).  The protocol is structurally similar (CLK + CS +
 * bidirectional DATA) but is not SPI and has its own command encoding.
 *
 * Protocol (recovered from SYS.SY disassembly, file offsets 0x0E50–0x0ED5):
 *   1. OUT(0x08) = 0x00  → idle / reset
 *   2. Command phase: 4 rising CK edges, bit0 = command bit (LSB first)
 *        Read  command nibble: 0b1111 (C=0x0F in ROM)
 *        Write command nibble: 0b0111 (C=0x07 in ROM)
 *   3. Data phase: 7 bytes, LSB first per byte
 *        (Z80 receive loop: RRA;RR(HL) → first received bit lands at bit0)
 *        Read : RTC drives bit0 (MISO) on falling CK; Z80 reads with IN
 *        Write: Z80 shifts via RL(HL)+RLA (MSB of source byte sent first)
 *
 * Register layout (7 bytes, BCD encoding, transmitted LSB-first per byte):
 *   regs[0] = hours     (00–23, 24-hour)       → SAMOS time hh field
 *   regs[1] = minutes   (00–59)                → SAMOS time mm field
 *   regs[2] = day       (01–31)                → SAMOS date DD field
 *   regs[3] = month     (01–12)                → SAMOS date MM field
 *   regs[4] = year      (00–99, 2-digit BCD)   → SAMOS date YY field
 *   regs[5] = weekday   (1=Mon … 7=Sun)        → SAMOS weekday name
 *   regs[6] = seconds   (00–59)                → SAMOS time ss field
 *
 * Layout confirmed empirically: SAMOS reads hh:mm:ss from bytes [0],[1],[6]
 * and date DD/MM/YY from bytes [2],[3],[4]; weekday name from byte [5].
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
