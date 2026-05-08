/* debug.c – Built-in machine monitor / debugger */
#include "machine_internal.h"
#include "debug.h"
#include "memory.h"

#include <Z80.h>
#include <stdio.h>

/* Key PC milestones in samos_sys17.rom used by debug_trace_pc() */
static const struct { uint16_t pc; const char *label; } MILESTONES[] = {
    { 0x003B, "boot_main"                      },
    { 0x003E, "kbd_wait#1 (boot key)"          },
    { 0x0057, "floppy_seek_sys call"           },
    { 0x0066, "nmi_handler"                    },
    { 0x006A, "kbd_wait#2 (OS load)"           },
    { 0x0070, "LDIR stub→0x5500"              },
    { 0x007B, "JP 0x5500"                      },
    { 0x0090, "floppy_boot (seek OK)"          },
    { 0x00AE, "block_copy#1 done (dir loaded)" },
    { 0x00EB, "block_copy#2 (SYS.SY load)"    },
    { 0x016E, "floppy_seek_sys"                },
    { 0x01AC, "floppy_load_sector"             },
    { 0x01EE, "block_copy"                     },
    { 0x021D, "setup_sector"                   },
    { 0x025A, "floppy_stream_read"             },
    { 0x02D0, "check_done"                     },
    { 0x030C, "winchester_init"                },
    { 0x046D, "pdp11_papertape_loader"         },
    { 0x5500, "self_test POST stub"            },
    { 0x57C0, "SYSMON_installer stub"          },
    { 0x0000, "SYSMON in RAM (booted!)"        },
};
#define N_MILESTONES (int)(sizeof(MILESTONES)/sizeof(MILESTONES[0]))

void debug_init(struct Smaky6 *m)
{
    m->dbg.stepping = 0;
    m->dbg.trace    = 0;
    m->dbg.trace_flow = 0;
    m->dbg.flow_budget = 0;
    m->dbg.last_flow_pc = 0xFFFF;
    m->dbg.flow_spin_count = 0;
    m->dbg.last_pc  = 0xFFFF;
    m->dbg.last_io19_pc = 0xFFFF;
    m->dbg.last_io19_data = 0xFF;
    m->dbg.io19_repeat_count = 0;
}
void debug_fini(struct Smaky6 *m)   { (void)m; }

/* Called from the Z80 hook each opcode fetch; prints a line the first time
 * the CPU reaches each labeled milestone address. */
void debug_trace_pc(struct Smaky6 *m, uint16_t pc)
{
    static uint8_t ram_pc_seen[0x0800];
    static uint8_t milestone_seen[N_MILESTONES];
    static int ram_pc_printed = 0;
    static int dumped_ram_image = 0;
    int rom_active = (m->rom_mask[0x0000] != 0);

    if (!m->dbg.trace) return;
    if (pc == m->dbg.last_pc) return;
    m->dbg.last_pc = pc;

    /* After Phantom ROM is banked out, trace early low-RAM control flow once. */
    if (m->rom_mask[0x0000] == 0 && pc < 0x0800 && ram_pc_printed < 64) {
        if (!ram_pc_seen[pc]) {
            ram_pc_seen[pc] = 1;
            ram_pc_printed++;
            fprintf(stderr, "[ram] PC=%04X\n", pc);
        }
    }

    if (m->dbg.trace_flow && m->rom_mask[0x0000] == 0 && m->dbg.flow_budget > 0) {
        uint8_t a = Z80_A(m->cpu);

        /* 0x20C2: CP (HL) — actual track-ID comparison.
         * A = track byte just read from sector header (port 0x1B byte_pos=1).
         * HL = 0x2B8B or 0x2B8C (head track variable, set by sub 0x21C3).
         * (HL) = expected track number maintained by OS. */
        if (pc == 0x20C2) {
            uint16_t hl = (uint16_t)Z80_HL(m->cpu);
            uint8_t expected = m->bus[hl];
            int drive = (m->fdc.ctrl >> 4) & 1;
            /* Dump table at 0x2BA3 (16 entries x 2 bytes) */
            fprintf(stderr,
                    "[cphl] pc=20C2 A(trk_hdr)=%02X (HL=%04X)=%02X %s"
                    " emu_trk=%u sec=%u 2b88=%02X 2b8b=%02X 2b92=%02X\n"
                    "       table@2BA3: %02X %02X  %02X %02X  %02X %02X  %02X %02X"
                    "  %02X %02X  %02X %02X  %02X %02X  %02X %02X\n"
                    "                  %02X %02X  %02X %02X  %02X %02X  %02X %02X"
                    "  %02X %02X  %02X %02X  %02X %02X  %02X %02X\n",
                    a, hl, expected,
                    (a == expected) ? "MATCH" : "MISMATCH",
                    (unsigned)m->fdc.track[drive],
                    (unsigned)(m->fdc.sector & 0x0Fu),
                    m->bus[0x2B88], m->bus[0x2B8B], m->bus[0x2B92],
                    m->bus[0x2BA3], m->bus[0x2BA4],
                    m->bus[0x2BA5], m->bus[0x2BA6],
                    m->bus[0x2BA7], m->bus[0x2BA8],
                    m->bus[0x2BA9], m->bus[0x2BAA],
                    m->bus[0x2BAB], m->bus[0x2BAC],
                    m->bus[0x2BAD], m->bus[0x2BAE],
                    m->bus[0x2BAF], m->bus[0x2BB0],
                    m->bus[0x2BB1], m->bus[0x2BB2],
                    m->bus[0x2BB3], m->bus[0x2BB4],
                    m->bus[0x2BB5], m->bus[0x2BB6],
                    m->bus[0x2BB7], m->bus[0x2BB8],
                    m->bus[0x2BB9], m->bus[0x2BBA],
                    m->bus[0x2BBB], m->bus[0x2BBC],
                    m->bus[0x2BBD], m->bus[0x2BBE],
                    m->bus[0x2BBF], m->bus[0x2BC0],
                    m->bus[0x2BC1], m->bus[0x2BC2]);
            m->dbg.flow_budget--;
            return;
        }

        if ((pc == 0x012D && (a == 0x1B || a == 0x0A)) || (pc >= 0x2037 && pc <= 0x2059)) {
            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;

            fprintf(stderr,
                    "[flowerr] pc=%04X af=%04X bc=%04X de=%04X hl=%04X "
                    "dx=%u trk=%u sec=%u bpos=%u w4544=%02X%02X w4554=%02X w457c=%02X%02X w2b88=%02X\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    (unsigned)((m->fdc.ctrl >> 4) & 1),
                    (unsigned)m->fdc.track[(m->fdc.ctrl >> 4) & 1],
                    (unsigned)(m->fdc.sector & 0x0Fu),
                    (unsigned)m->fdc.byte_pos,
                    m->bus[0x4544], m->bus[0x4545],
                    m->bus[0x4554],
                    m->bus[0x457C], m->bus[0x457D],
                    m->bus[0x2B88]);
            m->dbg.flow_budget--;
            return;
        }

        if (pc == 0x1F66 || pc == 0x1F89 || pc == 0x20B1 || pc == 0x20E8 || pc == 0x21E8
            || pc == 0x2187 || pc == 0x21A2) {
            if (pc == m->dbg.last_flow_pc) {
                m->dbg.flow_spin_count++;
                if ((m->dbg.flow_spin_count & 0x7Fu) != 0u)
                    return;
            } else {
                m->dbg.last_flow_pc = pc;
                m->dbg.flow_spin_count = 0;
            }

            fprintf(stderr,
                    "[flow] pc=%04X iff1=%u iff2=%u int_line=%u irq=%d nmi_arm=%d "
                    "ctrl=%02X trk=%u sec=%u bpos=%u 450f=%02X%02X 4562=%02X%02X"
                    " 2b88=%02X 2b8b=%02X 2b92=%02X\n",
                    pc,
                    (unsigned)m->cpu.iff1,
                    (unsigned)m->cpu.iff2,
                    (unsigned)m->cpu.int_line,
                    m->irq_pending,
                    m->fdc.nmi_armed,
                    m->fdc.ctrl,
                    (unsigned)m->fdc.track[(m->fdc.ctrl >> 4) & 1],
                    (unsigned)(m->fdc.sector & 0x0Fu),
                    (unsigned)m->fdc.byte_pos,
                    m->bus[0x450F], m->bus[0x4510],
                    m->bus[0x4562], m->bus[0x4563],
                    m->bus[0x2B88], m->bus[0x2B8B], m->bus[0x2B92]);
            m->dbg.flow_budget--;
            return;
        }

        if (pc < 0x0800) {
        if (pc == 0x00B3 || pc == 0x00B5 || pc == 0x00B7 || pc == 0x00B9) {
            if (pc == 0x00B3) {
                m->dbg.flow_spin_count++;
                if ((m->dbg.flow_spin_count & 0xFFu) == 0u) {
                    fprintf(stderr,
                            "[flow] keywait spins=%u k=%02X/%d w58a=%02X w595=%02X\n",
                            (unsigned)m->dbg.flow_spin_count,
                            m->kbd.key_code, m->kbd.found,
                            m->bus[0x458A], m->bus[0x4595]);
                    m->dbg.flow_budget--;
                }
            }
        } else if ((pc >= 0x00B0 && pc <= 0x0150) || (pc >= 0x0400 && pc <= 0x0508)) {
            m->dbg.flow_spin_count = 0;
            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;
            fprintf(stderr,
                    "[flow] pc=%04X af=%04X bc=%04X de=%04X hl=%04X "
                    "k=%02X/%d w54=%02X w55=%02X w57f=%02X w58a=%02X w595=%02X\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    m->kbd.key_code, m->kbd.found,
                    m->bus[0x4554], m->bus[0x4555], m->bus[0x457F],
                    m->bus[0x458A], m->bus[0x4595]);
            m->dbg.flow_budget--;
        }
        }
    }

    for (int i = 0; i < N_MILESTONES; i++) {
        if (MILESTONES[i].pc == pc) {
            if (!rom_active && pc < 0x0800 && pc != 0x0000) {
                break;
            }
            if (milestone_seen[i]) {
                break;
            }
            milestone_seen[i] = 1;
            fprintf(stderr, "[trace] PC=%04X  %s\n", pc, MILESTONES[i].label);
            /* Also dump workspace bytes relevant to floppy state */
            if (pc == 0x025A || pc == 0x01AC || pc == 0x00AE || pc == 0x00EB) {
                fprintf(stderr,
                    "       4500=%02X 4501=%02X 4502=%02X "
                    "4503=%02X 4504=%02X 4505=%02X 4508=%02X 450F=%02X%02X\n",
                    m->bus[0x4500], m->bus[0x4501], m->bus[0x4502],
                    m->bus[0x4503], m->bus[0x4504], m->bus[0x4505],
                    m->bus[0x4508], m->bus[0x450F], m->bus[0x4510]);

                if (pc == 0x00AE) {
                    fprintf(stderr, "       dir @5800 (first 6 entries):\n");
                    for (int e = 0; e < 6; e++) {
                        uint16_t off = (uint16_t)(0x5800 + e * 32);
                        char name[9];
                        char ext[3];
                        for (int k = 0; k < 8; k++) {
                            uint8_t c = m->bus[off + k];
                            name[k] = (c >= 32 && c < 127) ? (char)c : '.';
                        }
                        name[8] = '\0';
                        ext[0] = (char)m->bus[off + 8];
                        ext[1] = (char)m->bus[off + 9];
                        ext[2] = '\0';
                        fprintf(stderr,
                            "         [%d] %04X '%s.%s' cnt=%02X%02X sec=%02X%02X load=%02X%02X ent=%02X%02X\n",
                            e, off, name, ext,
                            m->bus[off + 11], m->bus[off + 10],
                            m->bus[off + 13], m->bus[off + 12],
                            m->bus[off + 18], m->bus[off + 17],
                            m->bus[off + 20], m->bus[off + 19]);
                    }
                }

                if (pc == 0x00EB) {
                    uint16_t ix = (uint16_t)m->cpu.ix_iy[0].uint16_value;
                    uint16_t count = (uint16_t)(m->bus[ix + 10] | (m->bus[ix + 11] << 8));
                    uint16_t sec   = (uint16_t)(m->bus[ix + 12] | (m->bus[ix + 13] << 8));
                    uint16_t load  = (uint16_t)(m->bus[ix + 17] | (m->bus[ix + 18] << 8));
                    uint16_t entry = (uint16_t)(m->bus[ix + 19] | (m->bus[ix + 20] << 8));
                    fprintf(stderr,
                        "       IX=%04X  dir[count=%04X sec=%04X load=%04X entry=%04X]\n",
                        ix, count, sec, load, entry);
                }
            }

            if (pc == 0x57C0) {
                fprintf(stderr,
                    "       regs: AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X IX=%04X IY=%04X\n",
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    (unsigned)Z80_SP(m->cpu),
                    (unsigned)m->cpu.ix_iy[0].uint16_value,
                    (unsigned)m->cpu.ix_iy[1].uint16_value);
                fprintf(stderr,
                    "       mem6000: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    m->bus[0x6000], m->bus[0x6001], m->bus[0x6002], m->bus[0x6003],
                    m->bus[0x6004], m->bus[0x6005], m->bus[0x6006], m->bus[0x6007]);
            }

            if (pc == 0x0000) {
                fprintf(stderr,
                    "       mem0000: %02X %02X %02X %02X %02X %02X %02X %02X rom0=%u\n",
                    m->bus[0x0000], m->bus[0x0001], m->bus[0x0002], m->bus[0x0003],
                    m->bus[0x0004], m->bus[0x0005], m->bus[0x0006], m->bus[0x0007],
                    (unsigned)m->rom_mask[0x0000]);
                if (m->rom_mask[0x0000] == 0) {
                    if (!dumped_ram_image) {
                        FILE *f = fopen("/tmp/smaky6_ram_0000_22ff.bin", "wb");
                        if (f) {
                            fwrite(&m->bus[0x0000], 1, 0x2300, f);
                            fclose(f);
                            dumped_ram_image = 1;
                            fprintf(stderr,
                                    "       dumped RAM image: /tmp/smaky6_ram_0000_22ff.bin\n");
                        }
                    }
                    fprintf(stderr, "       code0090:");
                    for (int i = 0; i < 0x40; i++)
                        fprintf(stderr, " %02X", m->bus[0x0090 + i]);
                    fprintf(stderr, "\n");
                    fprintf(stderr, "       code0300:");
                    for (int i = 0; i < 0x60; i++)
                        fprintf(stderr, " %02X", m->bus[0x0300 + i]);
                    fprintf(stderr, "\n");
                    fprintf(stderr, "       code0560:");
                    for (int i = 0; i < 0x40; i++)
                        fprintf(stderr, " %02X", m->bus[0x0560 + i]);
                    fprintf(stderr, "\n");
                    for (int r = 0; r < 6; r++) {
                        char line[65];
                        for (int c = 0; c < 64; c++) {
                            uint8_t ch = m->bus[0x4000 + r * 64 + c] & 0x7Fu;
                            line[c] = (ch >= 32 && ch < 127) ? (char)ch : '.';
                        }
                        line[64] = '\0';
                        fprintf(stderr, "       alpha%02d: %s\n", r, line);
                    }
                }
            }
            break;
        }
    }
}

void debug_toggle(struct Smaky6 *m)
{
    m->dbg.stepping = !m->dbg.stepping;
    fprintf(stderr, "debug: single-step %s\n", m->dbg.stepping ? "ON" : "OFF");
    if (m->dbg.stepping) debug_dump_regs(m);
}

void debug_dump_regs(struct Smaky6 *m)
{
    Z80 *cpu = &m->cpu;
    fprintf(stderr,
        "PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X\n",
        (unsigned)Z80_PC(*cpu),
        (unsigned)Z80_SP(*cpu),
        (unsigned)Z80_AF(*cpu),
        (unsigned)Z80_BC(*cpu),
        (unsigned)Z80_DE(*cpu),
        (unsigned)Z80_HL(*cpu),
        (unsigned)cpu->ix_iy[0].uint16_value,
        (unsigned)cpu->ix_iy[1].uint16_value);
}

void debug_hexdump(struct Smaky6 *m, uint16_t from, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (i % 16 == 0) fprintf(stderr, "\n%04X: ", (unsigned)(from + i));
        fprintf(stderr, "%02X ", memory_read(m, (uint16_t)(from + i)));
    }
    fprintf(stderr, "\n");
}

int debug_is_stepping(struct Smaky6 *m) { return m->dbg.stepping; }

void debug_set_trace(struct Smaky6 *m, int on)
{
    m->dbg.trace   = on;
    m->dbg.last_pc = 0xFFFF;
    if (on) fprintf(stderr, "debug: PC trace enabled\n");
}

void debug_set_trace_flow(struct Smaky6 *m, int on)
{
    m->dbg.trace_flow = on ? 1 : 0;
    m->dbg.flow_budget = on ? 200000 : 0;
    m->dbg.last_flow_pc = 0xFFFF;
    m->dbg.flow_spin_count = 0;
    if (on)
        fprintf(stderr, "debug: post-handoff flow trace enabled\n");
}
