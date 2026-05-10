/* machine.c – Top-level Smaky 6 machine: wires CPU, memory and all I/O */
#include "machine_internal.h"  /* full struct Smaky6 definition */
#include "machine.h"
#include "memory.h"
#include "video.h"
#include "keyboard.h"
#include "rtc.h"
#include "usart.h"
#include "parallel.h"
#include "floppy.h"
#include "sound.h"
#include "debug.h"
#include "winchester.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Z80 library callbacks ──────────────────────────────────────────────────*/

/* Memory read (Z80 context → m via context pointer) */
static zuint8 z80_mem_read(void *ctx, zuint16 addr)
{
    return memory_read((struct Smaky6 *)ctx, addr);
}

/* Opcode fetch — same as memory read, but also fires the trace hook */
static zuint8 z80_opcode_fetch(void *ctx, zuint16 addr)
{
    struct Smaky6 *m = (struct Smaky6 *)ctx;
    debug_trace_pc(m, addr);
    return memory_read(m, addr);
}

/* Memory write */
static void z80_mem_write(void *ctx, zuint16 addr, zuint8 data)
{
    memory_write((struct Smaky6 *)ctx, addr, data);
}

/* I/O read */
static zuint8 z80_io_read(void *ctx, zuint16 port)
{
    struct Smaky6 *m = (struct Smaky6 *)ctx;
    uint8_t lo = port & 0x3Fu;   /* 6-bit decode */

    switch (lo) {
    case 0x00: {
        uint8_t cla = keyboard_read_cla(m);
        if (m->dbg.trace_kbd)
            fprintf(stderr, "[kbd] CLA read pc=%04X -> %02X (found=%d)\n",
                    (unsigned)Z80_PC(m->cpu), cla, m->kbd.found);
        return cla;
    }
    case 0x01: {
        uint8_t st = (uint8_t)(0x08u | (keyboard_found(m) ? 0x04u : 0x00u));
        if (m->dbg.trace_kbd)
            fprintf(stderr, "[kbd] ST  read pc=%04X -> %02X\n",
                    (unsigned)Z80_PC(m->cpu), st);
        return st;
    }
    case 0x02: return parallel_read_data(m);
    case 0x03: return parallel_read_status(m);
    case 0x04: return usart_read_data(m, USART_PAPER);
    case 0x05: return usart_read_status(m, USART_PAPER);
    case 0x06: return usart_read_data(m, USART_CASS);
    case 0x07: return usart_read_status(m, USART_CASS);
    case 0x08:
        if (m->dbg.trace_port08) {
            fprintf(stderr, "[io08] IN  pc=%04X -> %02X\n",
                    (unsigned)Z80_PC(m->cpu), rtc_read_port(&m->rtc));
        }
        return rtc_read_port(&m->rtc);
    /* Port 0x19 bits[3:0] = current hard-sector index (used by SAMOS floppy code) */
    case 0x19: return floppy_read_sector19(m);
    /* Port 0x1A read: bit 7 = byte-ready flag (polled via IN F,(C)) */
    case 0x1A: return floppy_read_cont(m);
    /* Port 0x1B: streaming sector data (sync / ID / 256 data bytes / checksum) */
    case 0x1B: return floppy_read_data(m);
    /* Winchester hard-disk controller */
    case 0x20: return winchester_read_data(&m->win);   /* data register       */
    case 0x21: return winchester_read_error(&m->win);  /* error register      */
    case 0x27: return winchester_read_status(&m->win); /* status register     */
        /* Port 0x11: unknown I/O device. Stub for now. */
        case 0x11:
            if (m->dbg.trace_port11) {
                fprintf(stderr, "[io11] IN  pc=%04X -> 0x00\n",
                        (unsigned)Z80_PC(m->cpu));
            }
            return 0x00u;
        /* Port 0xCD: Winchester DMA or status (UNDOCUMENTED). Critical for boot! */
        case 0x0D:  /* 0xCD & 0x3F = 0x0D */
            if (m->dbg.trace_port_cd) {
                fprintf(stderr, "[ioCD] IN  pc=%04X -> 0x00 (Winchester status?)\n",
                        (unsigned)Z80_PC(m->cpu));
            }
            return 0x00u;  /* Return 0 = ready/idle (was 0xFF before!) */
    default:
        return 0xFFu;
    }
}

/* I/O write */
static void z80_io_write(void *ctx, zuint16 port, zuint8 data)
{
    struct Smaky6 *m = (struct Smaky6 *)ctx;
    uint8_t lo = port & 0x3Fu;

    switch (lo) {
    /* Port 0x00: Smaky 6 video mode control register.
     *
     * Bit encoding (confirmed from SAMOS syscalls 0x11/0x12/0x13):
     *   bit 0 = display enable  (0 = display off, 1 = normal operation)
     *   bit 1 = small-points ('P') mode
     *   bit 2 = graphics layer active
     *   bit 3 = graphics-only  (suppress alpha layer)
     *
     * Corresponding CLI MODE commands → port values:
     *   MODE A   → 0x01  (alpha only)
     *   MODE G   → 0x0D  (graphics only: bits 0,2,3)
     *   MODE G P → 0x0F  (graphics + small-points)
     *   MODE 2   → 0x05  (both layers: bits 0,2)
     *   MODE 2 P → 0x07  (both + small-points)
     *
     * Note: the keyboard FOUND flip-flop is cleared by READING port 0x00
     * (the CLA read path), NOT by writing it.  Writes here are video-only. */
    case 0x00:
        if (data & 0x01) { /* display enable bit — mode is meaningful */
            if (!m->vid.display_on) {
                fprintf(stderr, "[video] display ON (port00=0x%02X)\n", data);
                m->vid.display_on = 1;
            }
            VideoMode new_mode;
            if (data & 0x08)
                new_mode = VMODE_GRAPHIC; /* bit 3: graphics-only */
            else if (data & 0x04)
                new_mode = VMODE_SUPER;   /* bit 2 only: both layers */
            else
                new_mode = VMODE_ALPHA;   /* neither: alpha only */
            if (new_mode != m->vid.mode) {
                static const char *names[] = {"ALPHA","GRAPHIC","SUPER"};
                fprintf(stderr, "[video] mode -> %s (port00=0x%02X)\n",
                        names[new_mode], data);
                video_set_mode(m, new_mode);
            }
        }
        if (!(data & 0x01)) {
            /* Display-off: bit 0 = 0 blanks the screen */
            if (m->vid.display_on && !m->vid.no_display_off) {
                fprintf(stderr, "[video] display OFF (port00=0x%02X)\n", data);
                m->vid.display_on = 0;
            }
        }
        break;
    case 0x01:
        /* Phantom ROM bank-switch: disable 2 KB Phantom ROM, expose writable
         * RAM at 0x0000–0x07FF so SYSMON can be LDIR'd there from SYS.SY. */
        if (data == 0x00)
            memory_unprotect_rom(m, 0x0000, 0x0800);
        else {
            /* SAMOS ISR ACK: OUT (0x01), A with data=0x08 acknowledges the
             * 50 Hz frame tick and resets the CLA-seen flag so Stage 1 of
             * the next ISR frame starts fresh (cla_seen=0 means Stage 1 will
             * not forward key_held to the key path — only 'found' can trigger
             * it, ensuring Stage 2 fires on the frame after Stage 1). */
            if (m->dbg.trace_kbd) {
                uint16_t ptr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
                fprintf(stderr, "[kbd] ISR ACK pc=%04X data=%02X cla_seen=0  ptr=0x%04X [ptr]=0x%02X\n",
                        (unsigned)Z80_PC(m->cpu), data, ptr, (unsigned)m->bus[ptr]);
            }
            m->kbd.cla_seen = 0;
        }
        break;
    case 0x02: parallel_write_data(m, data);                    break;
    /* Port 0x03: sound bit-bang (RST 38 interrupt handler loops here for beep) */
    case 0x03:
        sound_set_bit(m, data & 1);
        if (m->dbg.trace_snd) {
            fprintf(stderr, "[snd] OUT 03 pc=%04X data=%02X frame_base=%u cpu.cyc=%u\n",
                    (unsigned)Z80_PC(m->cpu), data,
                    (unsigned)m->snd.frame_base, (unsigned)m->cpu.cycles);
        }
        break;
    case 0x04: usart_write_data(m, USART_PAPER, data);          break;
    case 0x05: usart_write_command(m, USART_PAPER, data);       break;
    case 0x06: usart_write_data(m, USART_CASS, data);           break;
    case 0x07: usart_write_command(m, USART_CASS, data);        break;
    /* Port 0x08: E405/08 RTC serial interface (extension board).
     * Bit 3=CK, bit 0=I/O (bidirectional), bit 2=CS (kept high during txn). */
    case 0x08:
        if (m->dbg.trace_port08) {
            fprintf(stderr, "[io08] OUT pc=%04X <- %02X\n",
                    (unsigned)Z80_PC(m->cpu), data);
        }
        rtc_write_port(&m->rtc, data);
        break;
    case 0x1A: floppy_write_cont(m, data);                      break;
    /* Port 0x19 write: Phantom ROM floppy command register.
     * This is NOT the bit-banged step-pulse port — that is port 0x1A only.
     * Port 0x19 is a command/status register; the ROM writes it to set
     * motor-on, head-load, NMI-enable and seek parameters, but individual
     * step pulses are always clocked on port 0x1A (and sometimes mirrored
     * here, but bit 2 here is NOT a step edge — only port 0x1A bit 2 is).
     *
     * During setup_sector polling, the ROM writes the SAME value repeatedly
     * while reading status. Only reset seek_busy when the value actually
     * CHANGES (new seek command), not on redundant writes. */
    case 0x19: {
        uint8_t old_ctrl = m->fdc.ctrl;
        m->fdc.ctrl           = data;
        m->fdc.nmi_armed = ((data & 0x0Cu) == 0x0Cu);
        /* Drive is selected by DRISEL1 (bit 5 = 0x20 → DX0) and
         * DRISEL2 (bit 6 = 0x40 → DX1) in the control register (Plan F5,
         * IC7 LS475 output bit weights in octal: 40=DX0, 100=DX1).
         * Update whenever any DRISEL bit is written — the Phantom ROM always
         * sets MOTORON alongside DRISEL, but SAMOS probes drive presence by
         * writing DRISEL alone (0x40 = DX1 with no MOTORON) before reading
         * status. Conditioning on MOTORON caused DX1 detection to fail. */
        if (data & 0x60u)
            m->fdc.selected_drive = (data & 0x40u) ? 1 : 0;
        if (data != old_ctrl) {
            m->fdc.seek_busy = 2;  /* realistic seek settle delay (~40ms at 50Hz) */
        }
        if (m->dbg.trace_port19) {
            uint16_t pc = (uint16_t)Z80_PC(m->cpu);
            if (pc == m->dbg.last_io19_pc && data == m->dbg.last_io19_data) {
                m->dbg.io19_repeat_count++;
                if ((m->dbg.io19_repeat_count & 0xFFu) == 0u) {
                    fprintf(stderr,
                            "[io19] OUT pc=%04X <- %02X repeated x%u (motor=%d nmi_armed=%d)\n",
                            (unsigned)pc, data,
                            (unsigned)m->dbg.io19_repeat_count,
                            (data >> 2) & 1, m->fdc.nmi_armed);
                }
            } else {
                m->dbg.last_io19_pc = pc;
                m->dbg.last_io19_data = data;
                m->dbg.io19_repeat_count = 0;
                fprintf(stderr, "[io19] OUT pc=%04X <- %02X (motor=%d nmi_armed=%d)\n",
                        (unsigned)pc, data,
                        (data >> 2) & 1, m->fdc.nmi_armed);
            }
        }
        break;
    }
    /* Winchester registers 0x20–0x27 */
    case 0x20: winchester_write_data      (&m->win, data); break;
    case 0x23: winchester_write_sector_num(&m->win, data); break;
    case 0x24: winchester_write_cyl_lo    (&m->win, data); break;
    case 0x25: winchester_write_cyl_hi    (&m->win, data); break;
    case 0x26: winchester_write_sdh       (&m->win, data); break;
    case 0x27: winchester_write_cmd       (&m->win, data); break;
    case 0x2B: /* unknown reset/select — no-op */ break;
    default:
        break;
    }
}

/* INT acknowledge — Smaky 6 uses IM 0 (no IM instruction ever executed in the
 * Phantom ROM; the Z80 defaults to IM 0 after reset).  In IM 0 the interrupting
 * device places an opcode on the data bus during the M1 acknowledge cycle:
 *
 *   • Floppy controller (motor+INT enabled via port 0x19 bits 2+3):
 *       puts RST 08h (0xCF) on the bus → executes the RST 08h vector at 0x0008,
 *       which is the indirect-call dispatcher: LD HL,(450Fh); EX (SP),HL; RET
 *       → jumps to floppy_stream_read (0x025A) during block_copy loading.
 *
 *   • Display frame timer (50 Hz, SAMOS running after bank-switch):
 *       puts RST 38h (0xFF) on the bus → executes SAMOS interrupt handler.
 *
 * We distinguish the two by the nmi_armed flag (set when OUT 0x19 has bits
 * 2 and 3 both set = motor-on + INT-enable; cleared on motor-off).
 *
 * Signature matches Z80Read: (void *context, zuint16 address). */

static zuint8 z80_int_fetch(void *ctx, zuint16 address)
{
    struct Smaky6 *m = (struct Smaky6 *)ctx;
    (void)address;
    if (m->fdc.nmi_armed)
        return 0xCFu;  /* RST 08h → floppy_stream_read via (0x450F) workspace ptr */
    return 0xFFu;      /* RST 38h → 50 Hz display/sound handler */
}
/* ── Lifecycle ──────────────────────────────────────────────────────────────*/

struct Smaky6 *machine_create(void)
{
    struct Smaky6 *m = calloc(1, sizeof(*m));
    if (!m) return NULL;

    memory_init(m);
    keyboard_init(m);
    usart_init(m);
    parallel_init(m);
    floppy_init(m);
    m->dbg.trace_scr = 0;   /* screen dump off by default; enable with -scrdump */
    sound_init(m);
    debug_init(m);
    rtc_init(&m->rtc);
    winchester_init(&m->win);

    /* Wire up Z80 callbacks */
    m->cpu.context      = m;
    m->cpu.fetch_opcode = z80_opcode_fetch; /* opcode fetch: fires trace hook */
    m->cpu.fetch        = z80_mem_read;     /* data fetch = normal read */
    m->cpu.read         = z80_mem_read;
    m->cpu.write        = z80_mem_write;
    m->cpu.in           = z80_io_read;
    m->cpu.out          = z80_io_write;
    m->cpu.inta         = z80_int_fetch;   /* IM0 opcode on INT acknowledge */
    m->cpu.int_fetch    = z80_int_fetch;
    m->cpu.halt         = NULL;
    m->cpu.nmia         = NULL;
    m->cpu.nop          = NULL;
    m->cpu.hook         = NULL;
    m->cpu.illegal      = NULL;

    z80_power(&m->cpu, Z_TRUE);
    z80_instant_reset(&m->cpu);

    return m;
}

void machine_destroy(struct Smaky6 *m)
{
    if (!m) return;
    debug_fini(m);
    sound_fini(m);
    floppy_fini(m);
    winchester_fini(&m->win);
    parallel_fini(m);
    usart_fini(m);
    keyboard_fini(m);
    video_fini(m);
    memory_fini(m);
    free(m);
}

int machine_load_rom(struct Smaky6 *m, const char *path, uint16_t base, uint16_t size)
{
    (void)size;  /* memory_load_file uses file size; size param reserved */
    return memory_load_file(m, path, base);
}

void machine_run_frame(struct Smaky6 *m)
{
    /* Smaky 6: 2.5 MHz, 50 Hz → 50,000 T-states per frame */
    const zusize TSTATES_PER_FRAME = 50000;
    const zusize INT_PULSE_AT      = 25000;
    const zusize INT_PULSE_WIDTH   = 64;
    const int STALL_THRESHOLD      = 150;  /* if PC doesn't change for 150 frames (~3s), exit */
    int do_int_pulse = m->irq_pending;
    m->irq_pending = 0;

    /* Detect CPU stall: if PC doesn't change for many frames, we're in an infinite loop */
    uint16_t pc_now = (uint16_t)Z80_PC(m->cpu);
    if (pc_now == m->dbg.frame_start_pc) {
        /* Interrupt-driven floppy loops can legally return to the same PC
         * each frame (e.g. post-banner wait at 0x1F89) while progress occurs
         * in ISR context. Do not treat this as a hard CPU stall. */
        if (m->fdc.nmi_armed || !m->cpu.iff1) {
            /* nmi_armed: floppy sector-hole loop — progress occurs in ISR.
             * iff1=0: monitor/breakpoint spin loop waiting for keyboard input.
             * Neither is a true stall. */
            m->dbg.stall_frames = 0;
        } else {
        m->dbg.stall_frames++;
        if (m->dbg.stall_frames >= STALL_THRESHOLD) {
            fprintf(stderr,
                "[stall] PC %04X stuck for %d frames (~%.1f sec); "
                "iff1=%u iff2=%u int_line=%u irq=%d nmi_armed=%d ctrl=%02X; exiting\n",
                pc_now, m->dbg.stall_frames, m->dbg.stall_frames / 50.0,
                (unsigned)m->cpu.iff1,
                (unsigned)m->cpu.iff2,
                (unsigned)m->cpu.int_line,
                m->irq_pending,
                m->fdc.nmi_armed,
                m->fdc.ctrl);
            m->cpu_stalled = 1;  /* Signal main loop to exit */
            floppy_tick(m);
            return;  /* Break out of infinite loop */
        }
        }
    } else {
        m->dbg.stall_frames = 0;  /* PC changed, reset stall counter */
    }
    m->dbg.frame_start_pc = pc_now;

    if (!debug_is_stepping(m)) {
        zusize cycles = 0;
        m->snd.frame_base = 0;   /* reset sound frame-position tracker */
        if (do_int_pulse) {
            /* Run to mid-frame, pulse INT briefly, then finish frame. */
            zusize budget = INT_PULSE_AT - cycles;
            if (budget > 0) {
                zusize ran = z80_execute(&m->cpu, budget);
                if (ran == 0) {
                    fprintf(stderr, "[stall] CPU made no progress in z80_execute at PC %04X; exiting\n",
                            (unsigned)Z80_PC(m->cpu));
                    m->cpu_stalled = 1;
                    floppy_tick(m);
                    return;
                }
                cycles += ran;
            }
            m->snd.frame_base = cycles;  /* update before INT-window batch */
            z80_int(&m->cpu, Z_TRUE);
            budget = (INT_PULSE_AT + INT_PULSE_WIDTH) - cycles;
            if (budget > 0) {
                zusize ran = z80_run(&m->cpu, budget);
                if (ran == 0) {
                    fprintf(stderr, "[stall] CPU made no progress in z80_run at PC %04X; exiting\n",
                            (unsigned)Z80_PC(m->cpu));
                    m->cpu_stalled = 1;
                    floppy_tick(m);
                    return;
                }
                cycles += ran;
            }
            z80_int(&m->cpu, Z_FALSE);
            m->snd.frame_base = cycles;  /* update before final batch */
            budget = TSTATES_PER_FRAME - cycles;
            if (budget > 0) {
                zusize ran = z80_execute(&m->cpu, budget);
                if (ran == 0) {
                    fprintf(stderr, "[stall] CPU made no progress in z80_execute at PC %04X; exiting\n",
                            (unsigned)Z80_PC(m->cpu));
                    m->cpu_stalled = 1;
                    floppy_tick(m);
                    return;
                }
                cycles += ran;
            }
        } else {
            zusize ran = z80_execute(&m->cpu, TSTATES_PER_FRAME);
            if (ran == 0) {
                fprintf(stderr, "[stall] CPU made no progress in z80_execute at PC %04X; exiting\n",
                        (unsigned)Z80_PC(m->cpu));
                m->cpu_stalled = 1;
                floppy_tick(m);
                return;
            }
            cycles += ran;
        }
        sound_end_frame(m);  /* flush per-frame buzzer buffer to audio queue */
        rtc_tick_frame(&m->rtc);
        floppy_tick(m);
    } else {
        /* Single-step: execute one instruction */
        z80_execute(&m->cpu, 1);
        debug_dump_regs(m);
    }

}

void machine_nmi(struct Smaky6 *m)
{
    z80_nmi(&m->cpu);
}

void machine_int(struct Smaky6 *m)
{
    m->irq_pending = 1;
}

void machine_reset(struct Smaky6 *m)
{
    z80_instant_reset(&m->cpu);
    m->irq_pending      = 0;
    m->fdc.nmi_armed    = 0;
}

void machine_inject_key(struct Smaky6 *m, uint8_t code)
{
    m->kbd.key_code        = code & 0x7Fu;
    m->kbd.found           = 1;
    m->kbd.physically_held = 1;
    m->kbd.key_hold_frames = 0;
}

void machine_release_key(struct Smaky6 *m)
{
    m->kbd.found           = 0;
    m->kbd.physically_held = 0;
    m->kbd.key_hold_frames = 0;
    m->kbd.cla_seen        = 0;
}

void machine_inject_shift_break(struct Smaky6 *m)
{
    /* Inject SHIFT + BREAK (Escape 0x1B) combination.
     * This triggers monitor mode on real hardware.
     */
    m->kbd.shift_pressed   = 1;
    m->kbd.key_code        = 0x1B;  /* BREAK = Escape */
    m->kbd.found           = 1;
    m->kbd.physically_held = 1;
    m->kbd.key_hold_frames = 0;
}

uint16_t machine_get_pc(const struct Smaky6 *m)
{
    return (uint16_t)Z80_PC(m->cpu);
}

int machine_in_posthandoff_keywait(const struct Smaky6 *m)
{
    uint16_t pc = (uint16_t)Z80_PC(m->cpu);
    if (m->rom_mask[0x0000] != 0)
        return 0;
    return (pc == 0x00B5 || pc == 0x00B7 || pc == 0x00B9);
}

void machine_inject_to_circ_buf(struct Smaky6 *m, uint8_t code)
{
    /* Directly write a key code into the SAMOS circular keyboard buffer.
     * The write pointer (0x457C, 16-bit LE) grows upward from 0x4596.
     * The guard sentinel 0x80 at 0x45B6 prevents overflow.
     * Bit 7 is cleared (7-bit key space, matching CLA hardware convention).
     * This bypasses the ISR Stage 2 pipeline entirely, which is needed
     * for CLI injection because Stage 2 requires (0x4582) != 0x80 — a
     * register that is 0x80 ("no pending key") after a normal boot. */
    uint16_t wr = (uint16_t)m->bus[0x457Cu]
                | ((uint16_t)m->bus[0x457Du] << 8);
    if (m->bus[wr] == 0x80u)
        return;                  /* guard sentinel hit — buffer full, drop */
    m->bus[wr]    = code & 0x7Fu;
    wr++;
    m->bus[0x457Cu] = (uint8_t)(wr & 0xFFu);
    m->bus[0x457Du] = (uint8_t)(wr >> 8);
    if (m->dbg.trace_kbd)
        fprintf(stderr, "[inject_circ] code=0x%02X ('%c')  ptr now 0x%04X\n",
                (unsigned)(code & 0x7Fu),
                ((code & 0x7Fu) >= 0x20 && (code & 0x7Fu) < 0x7F) ? (char)(code & 0x7Fu) : '?',
                (unsigned)wr);
}

int machine_cli_prompt_visible(const struct Smaky6 *m)
{
    /* The CLI prompt "* -" appears on screen row 16 (0-based).
     * Alpha RAM: 0x4000 + row*64 + col.  Row 16 = 0x4400.
     * Bit 7 is an attribute bit, strip it before comparing.
     * Also check row 0 in case the prompt moves there on some builds.
     */
    for (int row = 0; row < 20; row++) {
        const uint8_t *r = &m->bus[0x4000 + row * 64];
        if ((r[0] & 0x7F) == '*' && (r[1] & 0x7F) == ' ' && (r[2] & 0x7F) == '-')
            return 1;
    }
    return 0;
}

void machine_set_trace(struct Smaky6 *m, int on)
{
    debug_set_trace(m, on);
}

void machine_set_trace_port08(struct Smaky6 *m, int on)
{
    m->dbg.trace_port08 = on ? 1 : 0;
}

void machine_set_trace_kbd(struct Smaky6 *m, int on)
{
    m->dbg.trace_kbd = on ? 1 : 0;
}

void machine_set_trace_flow(struct Smaky6 *m, int on)
{
    debug_set_trace_flow(m, on);
}

void machine_set_trace_port11(struct Smaky6 *m, int on)
{
    m->dbg.trace_port11 = on ? 1 : 0;
}

void machine_set_trace_port_cd(struct Smaky6 *m, int on)
{
    m->dbg.trace_port_cd = on ? 1 : 0;
}

void machine_set_trace_port19(struct Smaky6 *m, int on)
{
    m->dbg.trace_port19 = on ? 1 : 0;
}

void machine_set_trace_fdc(struct Smaky6 *m, int on)
{
    m->dbg.trace_fdc = on ? 1 : 0;
}

void machine_set_trace_snd(struct Smaky6 *m, int on)
{
    m->dbg.trace_snd = on ? 1 : 0;
}

void machine_set_trace_scr(struct Smaky6 *m, int on)
{
    m->dbg.trace_scr = on ? 1 : 0;
}

void machine_set_no_display_off(struct Smaky6 *m, int on)
{
    m->vid.no_display_off = on ? 1 : 0;
}

void machine_set_scanlines(struct Smaky6 *m, int on)
{
    m->vid.scanlines = on ? 1 : 0;
}

void machine_set_phosphor(struct Smaky6 *m, int white)
{
    m->vid.phosphor = white ? PHOSPHOR_WHITE : PHOSPHOR_GREEN;
}

/* ── Internal accessor helpers (used by subsystem .c files) ─────────────── */
/* NOTE: All subsystem .c files now include machine_internal.h and access
 * fields directly.  These helpers are kept only for external callers that
 * hold an opaque struct Smaky6 * (e.g., future unit-test harnesses). */

Z80 *machine_cpu(struct Smaky6 *m) { return &m->cpu; }
