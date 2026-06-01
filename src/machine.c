// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
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
#include "psg.h"
#include "winchester.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMAKY6_PSG_PORT_BASE 0x20u
#define SMAKY6_PSG_PORT_LAST 0x27u

/* Report whether a decoded 6-bit I/O port belongs to the optional PSG card.
 * The ownership check stays here so machine-level decode order remains explicit. */
static int psg_handles_port(const struct Smaky6 *m, uint8_t lo)
{
    return m->psg.enabled && lo >= SMAKY6_PSG_PORT_BASE && lo <= SMAKY6_PSG_PORT_LAST;
}

/* Map one decoded PSG port back to the corresponding AY chip index.
 * Ports are arranged as four consecutive even/odd register pairs. */
static unsigned psg_chip_from_port(uint8_t lo)
{
    return (unsigned)((lo - SMAKY6_PSG_PORT_BASE) >> 1);
}

/* Distinguish even PSG data ports from odd register-select ports. */
static int psg_port_is_data(uint8_t lo)
{
    return (lo & 1u) == 0u;
}

static void trace_psg_io(const struct Smaky6 *m, const char *dir, uint8_t lo, uint8_t value)
{
    unsigned chip = psg_chip_from_port(lo);

    if (!m->dbg.trace_psg) {
        return;
    }

    fprintf(stderr,
            "[psg] %s pc=%04X port=%02X chip=%u %s=%02X\n",
            dir,
            (unsigned)Z80_PC(m->cpu),
            (unsigned)lo,
            chip,
            psg_port_is_data(lo) ? "data" : "reg",
            (unsigned)value);
}

/* Execute a run budget one instruction at a time while debugger stop
 * conditions are active, preserving keyboard timing and stall detection. */
static int machine_run_budget_instruction_granular(struct Smaky6 *m, zusize budget, zusize *cycles)
{
    while (*cycles < budget) {
        zusize ran;

        if (debug_maybe_pause_on_pc(m, (uint16_t)Z80_PC(m->cpu))) {
            return 1;
        }

        ran = z80_execute(&m->cpu, 1);
        if (ran == 0) {
            fprintf(stderr, "[stall] CPU made no progress in debugger-controlled run at PC %04X; exiting\n",
                    (unsigned)Z80_PC(m->cpu));
            m->cpu_stalled = 1;
            return -1;
        }

        *cycles += ran;
        keyboard_tick_cycles(m, (uint32_t)ran);
    }

    return 0;
}

/* ── Z80 library callbacks ──────────────────────────────────────────────────*/

/* Forward one Z80 memory read callback into the machine memory subsystem. */
static zuint8 z80_mem_read(void *ctx, zuint16 addr)
{
    return memory_read((struct Smaky6 *)ctx, addr);
}

/* Fetch one opcode byte and report the PC to debugger trace hooks first. */
static zuint8 z80_opcode_fetch(void *ctx, zuint16 addr)
{
    struct Smaky6 *m = (struct Smaky6 *)ctx;
    debug_trace_pc(m, addr);
    return memory_read(m, addr);
}

/* Forward one Z80 memory write callback into the machine memory subsystem. */
static void z80_mem_write(void *ctx, zuint16 addr, zuint8 data)
{
    memory_write((struct Smaky6 *)ctx, addr, data);
}

/* Decode one Z80 I/O read across the machine's currently installed devices.
 * PSG ports take precedence over Winchester when the optional sound card is enabled. */
static zuint8 z80_io_read(void *ctx, zuint16 port)
{
    struct Smaky6 *m = (struct Smaky6 *)ctx;
    uint8_t lo = port & 0x3Fu;   /* 6-bit decode */

    if (psg_handles_port(m, lo)) {
        if (psg_port_is_data(lo)) {
            uint8_t value = smaky6_psg_read_data(&m->psg.card, psg_chip_from_port(lo));
            trace_psg_io(m, "IN ", lo, value);
            return value;
        }
        trace_psg_io(m, "IN ", lo, 0xFFu);
        return 0xFFu;
    }

    switch (lo) {
    case 0x00: {
        uint8_t cla = keyboard_read_cla(m);
        if (m->dbg.trace_kbd)
            fprintf(stderr, "[kbd] CLA read pc=%04X -> %02X (found=%d)\n",
                    (unsigned)Z80_PC(m->cpu), cla, m->kbd.found);
        keyboard_trace_snapshot(m, "io-cla", (uint16_t)Z80_PC(m->cpu), 0x0000u, cla, cla);
        return cla;
    }
    case 0x01: {
        uint8_t st = keyboard_read_status(m);
        if (m->dbg.trace_kbd)
            fprintf(stderr, "[kbd] ST  read pc=%04X -> %02X\n",
                    (unsigned)Z80_PC(m->cpu), st);
        keyboard_trace_snapshot(m, "io-st", (uint16_t)Z80_PC(m->cpu), 0x0001u, st, st);
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
    case 0x18: return floppy_read_data18_ready(m);
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
        if (m->dbg.trace_port11)
            fprintf(stderr, "[io11] IN  pc=%04X -> 0x00\n",
                    (unsigned)Z80_PC(m->cpu));
        return 0x00u;
    /* Port 0xCD: Winchester DMA or status (UNDOCUMENTED). Critical for boot! */
    case 0x0D:  /* 0xCD & 0x3F = 0x0D */
        if (m->dbg.trace_port_cd)
            fprintf(stderr, "[ioCD] IN  pc=%04X -> 0x00 (Winchester status?)\n",
                    (unsigned)Z80_PC(m->cpu));
        return 0x00u;  /* Return 0 = ready/idle (was 0xFF before!) */
    default:
        return 0xFFu;
    }
}

/* Decode one Z80 I/O write across video, storage, PSG, and debug-traced ports. */
static void z80_io_write(void *ctx, zuint16 port, zuint8 data)
{
    struct Smaky6 *m = (struct Smaky6 *)ctx;
    uint8_t lo = port & 0x3Fu;

    if (psg_handles_port(m, lo)) {
        trace_psg_io(m, "OUT", lo, data);
        if (psg_port_is_data(lo)) {
            smaky6_psg_write_data(&m->psg.card, psg_chip_from_port(lo), data);
        } else {
            smaky6_psg_write_select(&m->psg.card, psg_chip_from_port(lo), data);
        }
        return;
    }

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
                if (m->vid.verbose_video)
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
                if (m->vid.verbose_video)
                    fprintf(stderr, "[video] mode -> %s (port00=0x%02X)\n",
                            names[new_mode], data);
                video_set_mode(m, new_mode);
            }
        }
        if (!(data & 0x01)) {
            /* Display-off: bit 0 = 0 blanks the screen */
            if (m->vid.display_on && !m->vid.no_display_off) {
                if (m->vid.verbose_video)
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
            /* Function key acknowledgment: when OS writes bits 0-6 of port 0x01,
             * it's acknowledging/clearing those function key bits. Clear the
             * corresponding bits from fonct_keyboard_bits to terminate any
             * on-going function key repeat. Bits 0-6 map to F7, F6, F5, F4, F3, F2, F1. */
            uint8_t fkey_mask = data & 0x7Fu;  /* bits 0-6 only */
            keyboard_acknowledge_function_bits(m, fkey_mask);

            /* SAMOS ISR ACK: OUT (0x01), A with data=0x08 acknowledges the 50 Hz
             * frame tick.  No additional keyboard state changes needed beyond fkey ACK. */
            if (m->dbg.trace_kbd && (data & 0x08)) {
                uint16_t ptr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
                fprintf(stderr, "[kbd] ISR ACK pc=%04X data=%02X  ptr=0x%04X [ptr]=0x%02X\n",
                        (unsigned)Z80_PC(m->cpu), data, ptr, (unsigned)m->bus[ptr]);
            }
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
    case 0x18: floppy_write_data18(m, data);                    break;
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

/* Present the IM0 interrupt opcode currently driven by the active device.
 * Floppy sector-hole NMIs inject RST 08h; the regular 50 Hz interrupt injects RST 38h. */
static zuint8 z80_int_fetch(void *ctx, zuint16 address)
{
    struct Smaky6 *m = (struct Smaky6 *)ctx;
    (void)address;
    if (m->fdc.nmi_armed)
        return 0xCFu;  /* RST 08h → floppy_stream_read via (0x450F) workspace ptr */
    return 0xFFu;      /* RST 38h → 50 Hz display/sound handler */
}
/* ── Lifecycle ──────────────────────────────────────────────────────────────*/

/* Allocate and initialise the full machine, including all optional devices in
 * their powered-off default state and the Z80 callback wiring. */
struct Smaky6 *machine_create(void)
{
    struct Smaky6 *m = calloc(1, sizeof(*m));
    if (!m) return NULL;

    m->psg.chip_clock_hz = SMAKY6_PSG_CLOCK_DEFAULT_HZ;

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

/* Tear down every subsystem owned by the machine in reverse startup order. */
void machine_destroy(struct Smaky6 *m)
{
    if (!m) return;
    debug_fini(m);
    sound_fini(m);
    floppy_fini(m);
    smaky6_psg_fini(&m->psg.card);
    winchester_fini(&m->win);
    parallel_fini(m);
    usart_fini(m);
    keyboard_fini(m);
    video_fini(m);
    memory_fini(m);
    free(m);
}

/* Load one ROM image into the machine bus at the requested base address. */
int machine_load_rom(struct Smaky6 *m, const char *path, uint16_t base)
{
    return memory_load_file(m, path, base);
}

/* Execute one 50 Hz frame worth of machine time, including optional INT pulse,
 * storage/RTC side effects, audio mixing, and debugger-controlled pauses. */
void machine_run_frame(struct Smaky6 *m)
{
    /* Smaky 6: 2.5 MHz, 50 Hz → 50,000 T-states per frame.
     * All frame/interrupt timing constants are derived from machine.h macros. */
    const zusize TSTATES_PER_FRAME = SMAKY6_TSTATES_PER_FRAME;
    const zusize INT_PULSE_AT      = SMAKY6_INT_PULSE_AT;
    const zusize INT_PULSE_WIDTH   = SMAKY6_INT_PULSE_WIDTH;
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
                m->cpu_stalled = 1;
                floppy_tick(m);
                return;
            }
        }
    } else {
        m->dbg.stall_frames = 0;  /* PC changed, reset stall counter */
    }
    m->dbg.frame_start_pc = pc_now;

    {
        zusize cycles = 0;
        int use_instruction_granular = debug_stop_conditions_active(m);
        m->snd.frame_base = 0;   /* reset sound frame-position tracker */
        if (do_int_pulse) {
            /* Run to mid-frame, pulse INT briefly, then finish frame. */
            zusize budget = INT_PULSE_AT - cycles;
            if (budget > 0) {
                if (use_instruction_granular) {
                    int rc = machine_run_budget_instruction_granular(m, budget, &cycles);

                    if (rc < 0) {
                        floppy_tick(m);
                        return;
                    }
                    if (rc > 0) {
                        goto frame_done;
                    }
                } else {
                    zusize ran = z80_execute(&m->cpu, budget);
                    if (ran == 0) {
                        fprintf(stderr, "[stall] CPU made no progress in z80_execute at PC %04X; exiting\n",
                                (unsigned)Z80_PC(m->cpu));
                        m->cpu_stalled = 1;
                        floppy_tick(m);
                        return;
                    }
                    cycles += ran;
                    keyboard_tick_cycles(m, (uint32_t)ran);
                }
            }
            m->snd.frame_base = cycles;  /* update before INT-window batch */
            z80_int(&m->cpu, Z_TRUE);
            budget = (INT_PULSE_AT + INT_PULSE_WIDTH) - cycles;
            if (budget > 0) {
                if (use_instruction_granular) {
                    int rc = machine_run_budget_instruction_granular(m, budget, &cycles);

                    if (rc < 0) {
                        z80_int(&m->cpu, Z_FALSE);
                        floppy_tick(m);
                        return;
                    }
                    if (rc > 0) {
                        z80_int(&m->cpu, Z_FALSE);
                        goto frame_done;
                    }
                } else {
                    zusize ran = z80_run(&m->cpu, budget);
                    if (ran == 0) {
                        fprintf(stderr, "[stall] CPU made no progress in z80_run at PC %04X; exiting\n",
                                (unsigned)Z80_PC(m->cpu));
                        m->cpu_stalled = 1;
                        floppy_tick(m);
                        return;
                    }
                    cycles += ran;
                    keyboard_tick_cycles(m, (uint32_t)ran);
                }
            }
            z80_int(&m->cpu, Z_FALSE);
            m->snd.frame_base = cycles;  /* update before final batch */
            budget = TSTATES_PER_FRAME - cycles;
            if (budget > 0) {
                if (use_instruction_granular) {
                    int rc = machine_run_budget_instruction_granular(m, budget, &cycles);

                    if (rc < 0) {
                        floppy_tick(m);
                        return;
                    }
                    if (rc > 0) {
                        goto frame_done;
                    }
                } else {
                    zusize ran = z80_execute(&m->cpu, budget);
                    if (ran == 0) {
                        fprintf(stderr, "[stall] CPU made no progress in z80_execute at PC %04X; exiting\n",
                                (unsigned)Z80_PC(m->cpu));
                        m->cpu_stalled = 1;
                        floppy_tick(m);
                        return;
                    }
                    cycles += ran;
                    keyboard_tick_cycles(m, (uint32_t)ran);
                }
            }
        } else {
            if (use_instruction_granular) {
                int rc = machine_run_budget_instruction_granular(m, TSTATES_PER_FRAME, &cycles);

                if (rc < 0) {
                    floppy_tick(m);
                    return;
                }
                if (rc > 0) {
                    goto frame_done;
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
                keyboard_tick_cycles(m, (uint32_t)ran);
            }
        }
frame_done:
        sound_end_frame(m);
        rtc_tick_frame(&m->rtc);
        floppy_tick(m);
        debug_note_frame_run(m, (uint32_t)cycles);

        /* Drift check (debug builds only): once per second verify that the
         * accumulated executed cycles match the expected frame budget within
         * 1% tolerance.  Fires every SMAKY6_FRAME_HZ frames (= 1 s). */
#ifndef NDEBUG
        m->dbg.drift_cycles_accum += (uint64_t)cycles;
        m->dbg.drift_frames++;
        if (m->dbg.drift_frames >= (int)SMAKY6_FRAME_HZ) {
            const uint64_t expected = (uint64_t)SMAKY6_TSTATES_PER_FRAME
                                      * (uint64_t)SMAKY6_FRAME_HZ;
            const uint64_t got  = m->dbg.drift_cycles_accum;
            const uint64_t diff = (got > expected) ? (got - expected)
                                                   : (expected - got);
            if (diff > expected / 100u)
                fprintf(stderr,
                        "[drift] 1-s budget: expected %llu, got %llu"
                        " (drift %+lld)\n",
                        (unsigned long long)expected,
                        (unsigned long long)got,
                        (long long)((int64_t)got - (int64_t)expected));
            m->dbg.drift_cycles_accum = 0;
            m->dbg.drift_frames       = 0;
        }
#endif
    }

}

/* Execute exactly one instruction for debugger single-step mode and propagate
 * the resulting timing into the keyboard and debugger subsystems. */
uint32_t machine_step_instruction(struct Smaky6 *m)
{
    zusize ran = z80_execute(&m->cpu, 1);

    if (ran == 0) {
        fprintf(stderr, "[stall] CPU made no progress in single-step at PC %04X; exiting\n",
                (unsigned)Z80_PC(m->cpu));
        m->cpu_stalled = 1;
        return 0;
    }

    keyboard_tick_cycles(m, (uint32_t)ran);
    debug_note_instruction_run(m, (uint32_t)ran);
    return (uint32_t)ran;
}

/* Assert the non-maskable BREAK interrupt immediately. */
void machine_nmi(struct Smaky6 *m)
{
    z80_nmi(&m->cpu);
}

/* Raise the 50 Hz interrupt request; machine_run_frame presents the pulse. */
void machine_int(struct Smaky6 *m)
{
    m->irq_pending = 1;
}

/* Reset CPU-visible machine state and restore the power-on virtual Enter hold. */
/* Reset CPU-visible machine state and restore the boot-time keyboard defaults
 * that the Phantom loader expects before SAMOS takes over. */
void machine_reset(struct Smaky6 *m)
{
    z80_instant_reset(&m->cpu);
    m->irq_pending      = 0;
    m->fdc.nmi_armed    = 0;
    if (m->psg.enabled)
        smaky6_psg_reset(&m->psg.card);
    /* Re-arm power-on state: physically_held=1 so the scanner reasserts FOUND
     * after each CLA read, passing through all boot-phase kbd_wait loops until
     * EI is executed (keyboard_frame_tick releases it on iff1→1). */
    m->kbd.found           = 1;
    m->kbd.key_code        = 0x00;  /* Enter → boot from DX0 */
    m->kbd.physically_held = 1;
    m->kbd.boot_key_held   = 1;
    m->kbd.regular_prefix_pending = 0;
    m->kbd.regular_prefix_armed = 1;
    m->kbd.shift_pressed   = 0;
    m->kbd.caps_lock_active = 1;
    m->kbd.host_text_down_count = 0;
    memset(m->kbd.host_text_down, 0, sizeof(m->kbd.host_text_down));
    keyboard_clear_all_function_bits(m);
    m->kbd.active_scancode = SDL_SCANCODE_UNKNOWN;
    m->kbd.active_matrix_position = 0xFFu;
    m->kbd.pending_ordinary_head = 0;
    m->kbd.pending_ordinary_len = 0;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;
}

/* Enable or disable the optional PSG add-on and allocate/free its four-chip
 * audio core as part of the machine audio path. */
int machine_set_psg_enabled(struct Smaky6 *m, int on)
{
    int enable = on ? 1 : 0;

    if (!m) {
        return -1;
    }

    if (enable == m->psg.enabled) {
        return 0;
    }

    if (!enable) {
        smaky6_psg_fini(&m->psg.card);
        m->psg.enabled = 0;
        return 0;
    }

    if (smaky6_psg_init(&m->psg.card,
                        m->psg.chip_clock_hz,
                        SMAKY6_AUDIO_HZ) != 0) {
        return -1;
    }
    m->psg.enabled = 1;
    sound_init(m);
    return 0;
}

/* Store the machine-wide PSG clock configuration before the card is enabled.
 * Runtime retuning is rejected for now because the current wrapper recreates
 * the underlying AY cores only at initialisation time. */
int machine_set_psg_clock_hz(struct Smaky6 *m, uint32_t hz)
{
    if (!m || hz < SMAKY6_PSG_CLOCK_MIN_HZ || hz > SMAKY6_PSG_CLOCK_MAX_HZ) {
        return -1;
    }

    if (m->psg.enabled) {
        return -1;
    }

    m->psg.chip_clock_hz = hz;
    return 0;
}

/* Report whether the optional PSG card is currently installed in this machine. */
int machine_psg_enabled(const struct Smaky6 *m)
{
    return m && m->psg.enabled;
}

/* Seed the strict keyboard model with one synthetic ordinary key plus optional
 * function-bit chord, clearing any host-owned state first. */
static void machine_inject_key_state(struct Smaky6 *m, uint8_t code, uint8_t fonct_bits)
{
    m->kbd.key_code        = code & 0x7Fu;
    m->kbd.found           = 1;
    m->kbd.physically_held = 1;
    m->kbd.boot_key_held   = 0;
    m->kbd.regular_prefix_pending = 1;
    m->kbd.regular_prefix_armed = 0;
    m->kbd.shift_pressed   = 0;
    m->kbd.caps_lock_active = 0;
    m->kbd.host_text_down_count = 0;
    memset(m->kbd.host_text_down, 0, sizeof(m->kbd.host_text_down));
    m->kbd.fonct_keyboard_bits = fonct_bits & 0x7Fu;
    m->kbd.cursor_alias_sources = 0;
    m->kbd.fonct_mouse_bits = 0;
    m->kbd.fonct_bits = m->kbd.fonct_keyboard_bits;
    m->kbd.active_scancode = SDL_SCANCODE_UNKNOWN;
    m->kbd.active_matrix_position = 0xFFu;
    m->kbd.pending_ordinary_head = 0;
    m->kbd.pending_ordinary_len = 0;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;
}

/* Inject one ordinary CLA-visible keycode directly into the strict keyboard latch. */
void machine_inject_key(struct Smaky6 *m, uint8_t code)
{
    machine_inject_key_state(m, code, 0x00u);
}

/* Inject one ordinary key together with function-key bits for scripted chords. */
void machine_inject_key_chord(struct Smaky6 *m, uint8_t code, uint8_t fonct_bits)
{
    machine_inject_key_state(m, code, fonct_bits);
}

/* Clear the injected ordinary-key latch and stop any pending reassertion. */
void machine_release_key(struct Smaky6 *m)
{
    m->kbd.found           = 0;
    m->kbd.physically_held = 0;
    m->kbd.boot_key_held   = 0;
    m->kbd.regular_prefix_pending = 0;
    m->kbd.regular_prefix_armed = 0;
    m->kbd.shift_pressed   = 0;
    m->kbd.host_text_down_count = 0;
    memset(m->kbd.host_text_down, 0, sizeof(m->kbd.host_text_down));
    keyboard_clear_all_function_bits(m);
    m->kbd.active_scancode = SDL_SCANCODE_UNKNOWN;
    m->kbd.active_matrix_position = 0xFFu;
    m->kbd.pending_ordinary_head = 0;
    m->kbd.pending_ordinary_len = 0;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;
}

/* Inject the SHIFT + BREAK boot-time combination through the ordinary-key latch. */
void machine_inject_shift_break(struct Smaky6 *m)
{
    /* Inject SHIFT + BREAK to trigger monitor mode on real hardware.
     * Supply the physical ESC/UNDO key code (0x06) as the key, which the Phantom ROM
     * kbd_wait sees as nonzero — directing it away from the PDP-11 loader path.
     */
    m->kbd.shift_pressed   = 1;
    m->kbd.caps_lock_active = 0;
    m->kbd.key_code        = 0x06;  /* ESC / UNDO physical key code */
    m->kbd.found           = 1;
    m->kbd.physically_held = 1;
    m->kbd.boot_key_held   = 0;
    m->kbd.regular_prefix_pending = 1;
    m->kbd.regular_prefix_armed = 0;
    m->kbd.host_text_down_count = 0;
    memset(m->kbd.host_text_down, 0, sizeof(m->kbd.host_text_down));
    keyboard_clear_all_function_bits(m);
    m->kbd.active_scancode = SDL_SCANCODE_UNKNOWN;
    m->kbd.active_matrix_position = 0xFFu;
    m->kbd.pending_ordinary_head = 0;
    m->kbd.pending_ordinary_len = 0;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;
}

/* Report the current Z80 PC for debug and prompt-detection helpers. */
uint16_t machine_get_pc(const struct Smaky6 *m)
{
    return (uint16_t)Z80_PC(m->cpu);
}

/* Bypass the hardware keyboard path and append one code to the SAMOS circular buffer. */
void machine_inject_to_circ_buf(struct Smaky6 *m, uint8_t code)
{
    /* Directly write a key code into the SAMOS circular keyboard buffer.
     * The write pointer (0x457C, 16-bit LE) grows upward from 0x4596.
     * The guard sentinel 0x80 at 0x45B6 prevents overflow.
     * Bit 7 is cleared (7-bit key space, matching CLA hardware convention).
    * This bypasses the SAMOS ISR keyboard pipeline entirely.  Direct SYS.SY
    * binary audit (2026-05-13) showed that the old documentation conflated
    * 0x4582 (read by ISR Stage 2 at 0x0175) with 0x458A (written to 0x80 at
    * init at 0x00A1).  The precise post-boot hardware ISR delivery path is
    * therefore being re-audited; this helper remains the emulator's explicit
    * direct circular-buffer injection path for CLI input. */
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

/* Scan the rendered alpha plane for the SAMOS CLI prompt marker used by
 * scripted injection helpers. */
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

/* Toggle the coarse PC milestone trace owned by the debugger subsystem. */
void machine_set_trace(struct Smaky6 *m, int on)
{
    debug_set_trace(m, on);
}

/* Enable or disable focused RTC port tracing on port 0x08. */
void machine_set_trace_port08(struct Smaky6 *m, int on)
{
    m->dbg.trace_port08 = on ? 1 : 0;
}

/* Enable or disable focused keyboard port tracing. */
void machine_set_trace_kbd(struct Smaky6 *m, int on)
{
    m->dbg.trace_kbd = on ? 1 : 0;
}

/* Toggle the dense post-handoff low-RAM control-flow trace. */
void machine_set_trace_flow(struct Smaky6 *m, int on)
{
    debug_set_trace_flow(m, on);
}

/* Enable or disable logging for the unknown port 0x11 read path. */
void machine_set_trace_port11(struct Smaky6 *m, int on)
{
    m->dbg.trace_port11 = on ? 1 : 0;
}

/* Enable or disable logging for the Winchester-related port 0xCD probe path. */
void machine_set_trace_port_cd(struct Smaky6 *m, int on)
{
    m->dbg.trace_port_cd = on ? 1 : 0;
}

/* Enable or disable floppy control-port tracing on port 0x19. */
void machine_set_trace_port19(struct Smaky6 *m, int on)
{
    m->dbg.trace_port19 = on ? 1 : 0;
}

/* Enable or disable tracing for PSG ports 0x20..0x27. */
void machine_set_trace_psg(struct Smaky6 *m, int on)
{
    m->dbg.trace_psg = on ? 1 : 0;
}

/* Enable or disable focused floppy stream tracing. */
void machine_set_trace_fdc(struct Smaky6 *m, int on)
{
    m->dbg.trace_fdc = on ? 1 : 0;
}

/* Enable or disable buzzer write tracing. */
void machine_set_trace_snd(struct Smaky6 *m, int on)
{
    m->dbg.trace_snd = on ? 1 : 0;
}

/* Enable or disable changed-row screen dumps to stderr. */
void machine_set_trace_scr(struct Smaky6 *m, int on)
{
    m->dbg.trace_scr = on ? 1 : 0;
}

/* Force the display to ignore guest display-off writes when requested. */
void machine_set_no_display_off(struct Smaky6 *m, int on)
{
    m->vid.no_display_off = on ? 1 : 0;
}

/* Enable or disable verbose video mode/display transition logging. */
void machine_set_verbose_video(struct Smaky6 *m, int on)
{
    m->vid.verbose_video = on ? 1 : 0;
}

/* Toggle the scanline overlay used by the SDL renderer. */
void machine_set_scanlines(struct Smaky6 *m, int on)
{
    m->vid.scanlines = on ? 1 : 0;
}

/* Select the current phosphor palette used for rendering. */
void machine_set_phosphor(struct Smaky6 *m, int white)
{
    m->vid.phosphor = white ? PHOSPHOR_WHITE : PHOSPHOR_GREEN;
}

/* Forward a phosphor persistence change into the video subsystem. */
void machine_set_phosphor_decay(struct Smaky6 *m, float decay)
{
    video_set_phosphor_decay(m, decay);
}

/* ── Internal accessor helpers (used by subsystem .c files) ─────────────── */
/* NOTE: All subsystem .c files now include machine_internal.h and access
 * fields directly.  These helpers are kept only for external callers that
 * hold an opaque struct Smaky6 * (e.g., future unit-test harnesses). */

Z80 *machine_cpu(struct Smaky6 *m) { return &m->cpu; }
