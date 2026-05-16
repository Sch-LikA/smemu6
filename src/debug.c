// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
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
    static int sm_rst10_follow = 0;
    static int sm_tail_follow = 0;
    static int sm_tail_follow2 = 0;
    static int sm_short_follow = 0;
    static int sm_long_follow = 0;
    static int sm_build_follow = 0;
    int rom_active = (m->rom_mask[0x0000] != 0);

    if (!m->dbg.trace && !m->dbg.trace_flow) return;

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

        if (pc == 0x57D2 || pc == 0x57D7 || pc == 0x5812 || pc == 0x5CDA ||
            pc == 0x5DD5 ||
            pc == 0x5E69 || pc == 0x5EBB || pc == 0x5EC1 || pc == 0x5EC4 ||
            pc == 0x5EC8 || pc == 0x6457 ||
            pc == 0x6752 || pc == 0x6755 || pc == 0x6757 || pc == 0x675A ||
            pc == 0x675C || pc == 0x6761 ||
            pc == 0x647A || pc == 0x647D || pc == 0x6480 || pc == 0x6484 ||
            pc == 0x648C || pc == 0x6496 || pc == 0x649B || pc == 0x64A5 ||
            pc == 0x1F2C ||
            pc == 0x6F55 || pc == 0x6F5C || pc == 0x6F60 || pc == 0x6F70 ||
            pc == 0x6F7E || pc == 0x6F8F || pc == 0x6F94 || pc == 0x6F9A ||
            pc == 0x6FA5 || pc == 0x6FB0 || pc == 0x6FDF || pc == 0x6FE6 ||
            pc == 0x6F8F || pc == 0x6FB0 || pc == 0x7015 || pc == 0x701D ||
            pc == 0x7020 || pc == 0x7026 || pc == 0x702D || pc == 0x702E ||
            pc == 0x7017 || pc == 0x569E || pc == 0x56AE) {
            uint16_t line_ptr = (uint16_t)m->bus[0x70B4u] | ((uint16_t)m->bus[0x70B5u] << 8);
            uint16_t save_ptr = (uint16_t)m->bus[0x70C8u] | ((uint16_t)m->bus[0x70C9u] << 8);
            uint16_t suffix_ptr = (uint16_t)m->bus[0x70ECu] | ((uint16_t)m->bus[0x70EDu] << 8);
            uint8_t op0 = m->bus[pc];
            uint8_t op1 = m->bus[(uint16_t)(pc + 1u)];
            uint8_t op2 = m->bus[(uint16_t)(pc + 2u)];
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);

            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;

            fprintf(stderr,
                    "[flow-sm] pc=%04X af=%04X bc=%04X de=%04X hl=%04X op=%02X %02X %02X "
                    "sp=%04X top=%04X next=%04X 454B=%02X 45C0=%02X %02X %02X %02X %02X %02X %02X %02X "
                    "70B0=%02X 70B1=%02X 70B4=%04X 70C8=%04X 70CA=%02X 70CC=%02X 70D4=%04X 70E0=%02X 70E1=%02X 70EC=%04X 70F2=%04X 710A=%02X 710B=%04X\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    (unsigned)op0, (unsigned)op1, (unsigned)op2,
                    sp, ret0, ret1,
                    (unsigned)m->bus[0x454Bu],
                    (unsigned)m->bus[0x45C0u], (unsigned)m->bus[0x45C1u],
                    (unsigned)m->bus[0x45C2u], (unsigned)m->bus[0x45C3u],
                    (unsigned)m->bus[0x45C4u], (unsigned)m->bus[0x45C5u],
                    (unsigned)m->bus[0x45C6u], (unsigned)m->bus[0x45C7u],
                    (unsigned)m->bus[0x70B0u], (unsigned)m->bus[0x70B1u],
                    line_ptr, save_ptr,
                    (unsigned)m->bus[0x70CAu],
                    (unsigned)m->bus[0x70CCu],
                    (unsigned)((uint16_t)m->bus[0x70D4u] |
                               ((uint16_t)m->bus[0x70D5u] << 8)),
                    (unsigned)m->bus[0x70E0u],
                    (unsigned)m->bus[0x70E1u],
                    suffix_ptr,
                    (unsigned)((uint16_t)m->bus[0x70F2u] |
                               ((uint16_t)m->bus[0x70F3u] << 8)),
                    (unsigned)m->bus[0x710Au],
                    (unsigned)((uint16_t)m->bus[0x710Bu] |
                               ((uint16_t)m->bus[0x710Cu] << 8)));
            if (pc == 0x1F2C || pc == 0x647A || pc == 0x6755 || pc == 0x675A)
                sm_rst10_follow = 40;
            else if (pc == 0x649B || pc == 0x64A5 || pc == 0x7017)
                sm_rst10_follow = 0;
            m->dbg.flow_budget--;
            return;
        }

        if (pc == 0x217D)
            sm_tail_follow = 40;
        if (pc == 0x1F57 || pc == 0x2021)
            sm_tail_follow2 = 64;
        if (pc == 0x1D8F)
            sm_build_follow = 96;

        if (pc >= 0x0127 && pc <= 0x0147) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);
            uint16_t ret2 = (uint16_t)m->bus[(uint16_t)(sp + 4u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 5u)] << 8);

            if (ret0 == 0x18EB && (ret1 == 0x5600 || ret1 == 0x5602))
                sm_short_follow = 24;
            if ((ret0 == 0x1CF4 || ret0 == 0x1CFE) && ret1 == 0x1D99)
                sm_long_follow = 32;
            if (ret0 == 0x25E8 && ret1 == 0x1CFF)
                sm_long_follow = 32;

            if (ret0 == 0x1DB9 || ret0 == 0x1DBB || ret0 == 0x1DBD ||
                ret0 == 0x18EB) {
                if (pc == m->dbg.last_flow_pc)
                    return;
                m->dbg.last_flow_pc = pc;

                fprintf(stderr,
                        "[flow-pre560x] pc=%04X af=%04X bc=%04X de=%04X hl=%04X sp=%04X top=%04X next=%04X next2=%04X 4554=%02X 4555=%02X 4560=%02X%02X\n",
                        pc,
                        (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                        (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                        sp, ret0, ret1, ret2,
                        m->bus[0x4554u], m->bus[0x4555u],
                        m->bus[0x4560u], m->bus[0x4561u]);
                m->dbg.flow_budget--;
                return;
            }
        }

        if (sm_short_follow > 0 &&
            (pc == 0x11D2 || pc == 0x18EA || pc == 0x18EB || pc == 0x18EC ||
             pc == 0x1DBE || pc == 0x028D || pc == 0x028F || pc == 0x0560 ||
             pc == 0x0568 || pc == 0x5600 || pc == 0x5602)) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);
            uint16_t ret2 = (uint16_t)m->bus[(uint16_t)(sp + 4u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 5u)] << 8);

            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;

            fprintf(stderr,
                    "[flow-short] pc=%04X af=%04X bc=%04X de=%04X hl=%04X sp=%04X top=%04X next=%04X next2=%04X short=%d 455E=%02X%02X 4560=%02X%02X 456E=%02X%02X 4572=%02X%02X 2BC7=%02X%02X 2BD1=%02X%02X\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    sp, ret0, ret1, ret2, sm_short_follow,
                    m->bus[0x455Eu], m->bus[0x455Fu],
                    m->bus[0x4560u], m->bus[0x4561u],
                    m->bus[0x456Eu], m->bus[0x456Fu],
                    m->bus[0x4572u], m->bus[0x4573u],
                    m->bus[0x2BC7u], m->bus[0x2BC8u],
                    m->bus[0x2BD1u], m->bus[0x2BD2u]);
            sm_short_follow--;
            m->dbg.flow_budget--;
            return;
        }

        if (sm_long_follow > 0 &&
            (pc == 0x1CF4 || pc == 0x1CFE || pc == 0x1CFF || pc == 0x0E4F ||
             pc == 0x0E52 || pc == 0x0E54 || pc == 0x1CF6 || pc == 0x1D99 ||
             pc == 0x1E72 || pc == 0x25E8 ||
             pc == 0x5500 || pc == 0x5503 || pc == 0x5600 || pc == 0x5602 ||
             pc == 0x5611)) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);
            uint16_t ix = (uint16_t)Z80_IX(m->cpu);
            uint16_t ix0a = (uint16_t)m->bus[(uint16_t)(ix + 0x0Au)] |
                            ((uint16_t)m->bus[(uint16_t)(ix + 0x0Bu)] << 8);
            uint16_t ix0c = (uint16_t)m->bus[(uint16_t)(ix + 0x0Cu)] |
                            ((uint16_t)m->bus[(uint16_t)(ix + 0x0Du)] << 8);

            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;

            fprintf(stderr,
                    "[flow-long] pc=%04X af=%04X bc=%04X de=%04X hl=%04X ix=%04X ix0A=%04X ix0C=%04X sp=%04X top=%04X next=%04X long=%d 4554=%02X 4555=%02X 4560=%02X%02X 25E8=%02X %02X %02X %02X\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    ix, ix0a, ix0c, sp, ret0, ret1, sm_long_follow,
                    m->bus[0x4554u], m->bus[0x4555u],
                    m->bus[0x4560u], m->bus[0x4561u],
                    m->bus[0x25E8u], m->bus[0x25E9u],
                    m->bus[0x25EAu], m->bus[0x25EBu]);
            sm_long_follow--;
            m->dbg.flow_budget--;
            return;
        }

        if (sm_build_follow > 0 &&
            (pc == 0x11B9 || pc == 0x11CD || pc == 0x11D1 ||
             pc == 0x1743 || pc == 0x1757 || pc == 0x1771 || pc == 0x1790 ||
             pc == 0x17E7 || pc == 0x17EB || pc == 0x17F0 ||
             pc == 0x17FD || pc == 0x1803 || pc == 0x180F || pc == 0x1813 ||
             pc == 0x1816 || pc == 0x1821 || pc == 0x1824 || pc == 0x1834 ||
             pc == 0x183C || pc == 0x1841 || pc == 0x1848 || pc == 0x1855 ||
             pc == 0x1867 || pc == 0x1876 || pc == 0x1881 || pc == 0x1890 ||
             pc == 0x161B)) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);
            uint16_t ix = (uint16_t)Z80_IX(m->cpu);
            uint16_t ptr = (uint16_t)m->bus[0x2BC7u] |
                           ((uint16_t)m->bus[0x2BC8u] << 8);
            uint16_t cur = (uint16_t)m->bus[0x2BC5u] |
                           ((uint16_t)m->bus[0x2BC6u] << 8);
            uint16_t best = (uint16_t)m->bus[0x2B82u] |
                            ((uint16_t)m->bus[0x2B83u] << 8);
            uint16_t end = (uint16_t)m->bus[0x2BDCu] |
                           ((uint16_t)m->bus[0x2BDDu] << 8);
                uint16_t rec = (ix >= 0x2300u && ix < 0x25E8u) ? ix : 0u;

            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;

            fprintf(stderr,
                    "[flow-build] pc=%04X af=%04X bc=%04X de=%04X hl=%04X ix=%04X rec=%04X sp=%04X top=%04X next=%04X ptr=%04X cur=%04X best=%04X end=%04X slot=%02X %02X %02X %02X %02X %02X recnm=%02X %02X %02X %02X %02X %02X recse=%02X %02X %02X %02X recfg=%02X %02X out=%02X %02X %02X %02X %02X %02X %02X %02X build=%d\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    ix, rec, sp, ret0, ret1, ptr, cur, best, end,
                    (unsigned)m->bus[ptr], (unsigned)m->bus[(uint16_t)(ptr + 1u)],
                    (unsigned)m->bus[(uint16_t)(ptr + 2u)], (unsigned)m->bus[(uint16_t)(ptr + 3u)],
                    (unsigned)m->bus[(uint16_t)(ptr + 4u)], (unsigned)m->bus[(uint16_t)(ptr + 5u)],
                    (unsigned)m->bus[rec], (unsigned)m->bus[(uint16_t)(rec + 1u)],
                    (unsigned)m->bus[(uint16_t)(rec + 2u)], (unsigned)m->bus[(uint16_t)(rec + 3u)],
                    (unsigned)m->bus[(uint16_t)(rec + 4u)], (unsigned)m->bus[(uint16_t)(rec + 5u)],
                    (unsigned)m->bus[(uint16_t)(rec + 0x0Au)], (unsigned)m->bus[(uint16_t)(rec + 0x0Bu)],
                    (unsigned)m->bus[(uint16_t)(rec + 0x0Cu)], (unsigned)m->bus[(uint16_t)(rec + 0x0Du)],
                    (unsigned)m->bus[(uint16_t)(rec + 0x16u)], (unsigned)m->bus[(uint16_t)(rec + 0x17u)],
                    (unsigned)m->bus[0x711Au], (unsigned)m->bus[0x711Bu],
                    (unsigned)m->bus[0x711Cu], (unsigned)m->bus[0x711Du],
                    (unsigned)m->bus[0x711Eu], (unsigned)m->bus[0x711Fu],
                    (unsigned)m->bus[0x7120u], (unsigned)m->bus[0x7121u],
                    sm_build_follow);
            sm_build_follow--;
            m->dbg.flow_budget--;
            return;
        }

        if (sm_tail_follow2 > 0 &&
            (pc == 0x0020 || pc == 0x0026 || pc == 0x0028 || pc == 0x003E ||
             pc == 0x0041 || pc == 0x0E1C || pc == 0x18A8 || pc == 0x18A9 ||
             pc == 0x18AC || pc == 0x18AD || pc == 0x18AE || pc == 0x18AF ||
             pc == 0x18B0 || pc == 0x18B2 || pc == 0x18B4 || pc == 0x18B6 ||
             pc == 0x18B7 || pc == 0x18B8 || pc == 0x18BF || pc == 0x18C0 ||
             pc == 0x18C1 || pc == 0x18C3 || pc == 0x18C5 || pc == 0x18C7 ||
             pc == 0x18C8 || pc == 0x18C9 || pc == 0x18CC || pc == 0x18CE ||
             pc == 0x18CF || pc == 0x18D0 || pc == 0x18E0 || pc == 0x18E3 ||
             pc == 0x18E6 || pc == 0x18EA || pc == 0x18EB || pc == 0x1913 || pc == 0x1916 ||
             pc == 0x1918 || pc == 0x191A || pc == 0x191D || pc == 0x191E ||
             pc == 0x1920 || pc == 0x1921 || pc == 0x1922 || pc == 0x1923 ||
             pc == 0x194B || pc == 0x1A53 || pc == 0x1E23 || pc == 0x1F47 ||
             pc == 0x1F57 || pc == 0x1F6D || pc == 0x1F93 || pc == 0x1F95 ||
             pc == 0x1F97 || pc == 0x1F9E || pc == 0x1FA3 ||
             pc == 0x1FAE || pc == 0x1FC3 || pc == 0x1FC6 || pc == 0x1FD4 ||
             pc == 0x1FD7 || pc == 0x1FDC || pc == 0x1FF1 || pc == 0x1FF7 ||
             pc == 0x2155 || pc == 0x217D || pc == 0x2180 || pc == 0x2191 ||
             pc == 0x21B7 ||
             pc == 0x1FFA || pc == 0x1FFE || pc == 0x2003 ||
             pc == 0x1CD6 || pc == 0x1CF6 ||
             pc == 0x1D08 || pc == 0x1D29 || pc == 0x1D42 || pc == 0x1D57 ||
             pc == 0x1D80 || pc == 0x1D8F || pc == 0x1D99 || pc == 0x1DAB)) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);
            uint16_t ret2 = (uint16_t)m->bus[(uint16_t)(sp + 4u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 5u)] << 8);
            uint16_t ret3 = (uint16_t)m->bus[(uint16_t)(sp + 6u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 7u)] << 8);
            uint16_t ix = (uint16_t)Z80_IX(m->cpu);
            uint16_t ix0a = (uint16_t)m->bus[(uint16_t)(ix + 0x0Au)] |
                            ((uint16_t)m->bus[(uint16_t)(ix + 0x0Bu)] << 8);
            uint16_t ix0c = (uint16_t)m->bus[(uint16_t)(ix + 0x0Cu)] |
                            ((uint16_t)m->bus[(uint16_t)(ix + 0x0Du)] << 8);
            uint8_t ix10 = m->bus[(uint16_t)(ix + 0x10u)];
            uint16_t ix13_14 = (uint16_t)m->bus[(uint16_t)(ix + 0x13u)] |
                               ((uint16_t)m->bus[(uint16_t)(ix + 0x14u)] << 8);

            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;

            fprintf(stderr,
                      "[flow-tail2] pc=%04X af=%04X bc=%04X de=%04X hl=%04X ix=%04X ix0A=%04X ix0C=%04X ix10=%02X ix13_14=%04X sp=%04X top=%04X next=%04X next2=%04X next3=%04X 2B80=%04X 2B82=%04X 2B84=%04X 2B86=%04X 2B89=%04X 2BC5=%04X 2BC7=%04X 2BDC=%04X 2BD1=%04X 455C=%04X 4566=%04X 4568=%04X tail2=%d\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                      ix, ix0a, ix0c, ix10, ix13_14, sp, ret0, ret1, ret2, ret3,
                      (unsigned)((uint16_t)m->bus[0x2B80u] |
                          ((uint16_t)m->bus[0x2B81u] << 8)),
                      (unsigned)((uint16_t)m->bus[0x2B82u] |
                          ((uint16_t)m->bus[0x2B83u] << 8)),
                      (unsigned)((uint16_t)m->bus[0x2B84u] |
                          ((uint16_t)m->bus[0x2B85u] << 8)),
                      (unsigned)((uint16_t)m->bus[0x2B86u] |
                          ((uint16_t)m->bus[0x2B87u] << 8)),
                      (unsigned)((uint16_t)m->bus[0x2B89u] |
                          ((uint16_t)m->bus[0x2B8Au] << 8)),
                      (unsigned)((uint16_t)m->bus[0x2BC5u] |
                          ((uint16_t)m->bus[0x2BC6u] << 8)),
                    (unsigned)((uint16_t)m->bus[0x2BC7u] |
                           ((uint16_t)m->bus[0x2BC8u] << 8)),
                      (unsigned)((uint16_t)m->bus[0x2BDCu] |
                          ((uint16_t)m->bus[0x2BDDu] << 8)),
                    (unsigned)((uint16_t)m->bus[0x2BD1u] |
                           ((uint16_t)m->bus[0x2BD2u] << 8)),
                    (unsigned)((uint16_t)m->bus[0x455Cu] |
                               ((uint16_t)m->bus[0x455Du] << 8)),
                    (unsigned)((uint16_t)m->bus[0x4566u] |
                               ((uint16_t)m->bus[0x4567u] << 8)),
                    (unsigned)((uint16_t)m->bus[0x4568u] |
                               ((uint16_t)m->bus[0x4569u] << 8)),
                    sm_tail_follow2);
            sm_tail_follow2--;
            m->dbg.flow_budget--;
            return;
        }

        if (sm_tail_follow > 0 &&
            (pc == 0x18BF || pc == 0x194B || pc == 0x1D8F || pc == 0x1F57 ||
             pc == 0x1FAE || pc == 0x1FC1 || pc == 0x1FCD || pc == 0x1FD5 ||
             pc == 0x1FD8 || pc == 0x1FEC || pc == 0x200B || pc == 0x2021 ||
             pc == 0x206F || pc == 0x2075 || pc == 0x20B2 || pc == 0x212F ||
             pc == 0x213A || pc == 0x2155 || pc == 0x21B7)) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);

            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;

            fprintf(stderr,
                    "[flow-tail] pc=%04X af=%04X bc=%04X de=%04X hl=%04X sp=%04X top=%04X next=%04X tail=%d\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    sp, ret0, ret1, sm_tail_follow);
            sm_tail_follow--;
            m->dbg.flow_budget--;
            return;
        }

        if ((pc == 0x1238 || pc == 0x1253) && m->dbg.flow_budget > 0) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);
            uint8_t snap = m->bus[0x2BE2u];
            uint8_t src = m->bus[0x2B88u];

            if (!m->dbg.watch_2be2_valid || pc != m->dbg.last_watch_pc ||
                snap != m->dbg.last_watch_2be2 || src != m->dbg.last_watch_2b88) {
                fprintf(stderr,
                        "[flow-snap] pc=%04X af=%04X bc=%04X de=%04X hl=%04X sp=%04X top=%04X next=%04X 2BE2=%02X 2B88=%02X 2BDE=%04X 2BE0=%04X\n",
                        pc,
                        (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                        (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                        sp, ret0, ret1,
                        (unsigned)snap,
                        (unsigned)src,
                        (unsigned)((uint16_t)m->bus[0x2BDEu] |
                                   ((uint16_t)m->bus[0x2BDFu] << 8)),
                        (unsigned)((uint16_t)m->bus[0x2BE0u] |
                                   ((uint16_t)m->bus[0x2BE1u] << 8)));
                m->dbg.last_watch_pc = pc;
                m->dbg.last_watch_2be2 = snap;
                m->dbg.last_watch_2b88 = src;
                m->dbg.watch_2be2_valid = 1;
                m->dbg.flow_budget--;
                return;
            }
        }

        if (sm_rst10_follow > 0 &&
            (pc == 0x1063 || pc == 0x1074 || pc == 0x1087 || pc == 0x1088 ||
             pc == 0x1238 || pc == 0x1253 ||
             pc == 0x17B9 || pc == 0x17C4 || pc == 0x1A53 || pc == 0x1AC4 ||
             pc == 0x1B04 ||
             pc == 0x1EA9 ||
             pc == 0x1EC0 ||
             pc == 0x1C4D || pc == 0x1D08 || pc == 0x1D29 || pc == 0x1DAB ||
             pc == 0x1DDB || pc == 0x1DEB || pc == 0x1E23 || pc == 0x1F59 ||
             pc == 0x18D6 || pc == 0x18E6 || pc == 0x18EA || pc == 0x18FF ||
             pc == 0x1902 || pc == 0x192E || pc == 0x1935 || pc == 0x1938 ||
             pc == 0x193A || pc == 0x193D || pc == 0x1941 || pc == 0x1944 ||
             pc == 0x1948 || pc == 0x194C || pc == 0x194F || pc == 0x1951 ||
             pc == 0x1954 || pc == 0x1956 || pc == 0x1959 || pc == 0x195B ||
             pc == 0x195D || pc == 0x195F || pc == 0x1963)) {
            uint16_t sp = (uint16_t)Z80_SP(m->cpu);
            uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
            uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                            ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);

            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;

            fprintf(stderr,
                    "[flow-rst10] pc=%04X af=%04X bc=%04X de=%04X hl=%04X sp=%04X top=%04X next=%04X 2B88=%02X 4554=%02X follow=%d\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    sp, ret0, ret1,
                    (unsigned)m->bus[0x2B88u],
                    (unsigned)m->bus[0x4554u], sm_rst10_follow);
                sm_rst10_follow--;
            m->dbg.flow_budget--;
            return;
        }

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

        if (pc == 0x5857 || pc == 0x58DD || pc == 0x58F8 || pc == 0x590E ||
            pc == 0x590F || (pc >= 0x5B29 && pc <= 0x5B35)) {
            uint16_t cursor = (uint16_t)m->bus[0x7014u] | ((uint16_t)m->bus[0x7015u] << 8);
            uint8_t op0 = m->bus[pc];
            uint8_t op1 = m->bus[(uint16_t)(pc + 1u)];
            uint8_t op2 = m->bus[(uint16_t)(pc + 2u)];
            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;
            fprintf(stderr,
                    "[flow-line] pc=%04X af=%04X bc=%04X de=%04X hl=%04X "
                    "op=%02X %02X %02X cursor=%04X 45c0=%02X 45c1=%02X 45c2=%02X 457c=%02X%02X 457e=%02X\n",
                    pc,
                    (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                    (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                    (unsigned)op0,
                    (unsigned)op1,
                    (unsigned)op2,
                    cursor,
                    (unsigned)m->bus[0x45C0u],
                    (unsigned)m->bus[0x45C1u],
                    (unsigned)m->bus[0x45C2u],
                    (unsigned)m->bus[0x457Du],
                    (unsigned)m->bus[0x457Cu],
                    (unsigned)m->bus[0x457Eu]);
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
         } else if (pc == 0x0175 || pc == 0x0179 || pc == 0x0183 || pc == 0x0188 ||
             pc == 0x0198 || pc == 0x019B || pc == 0x01BB || pc == 0x01C9 ||
             pc == 0x01DF || pc == 0x01EE || pc == 0x0200 ||
               pc == 0x04D4 || pc == 0x04E4 || pc == 0x04F6 || pc == 0x0516 ||
               pc == 0x057E || pc == 0x058C ||
                   (pc >= 0x00B0 && pc <= 0x0150) || (pc >= 0x0400 && pc <= 0x0508)) {
            m->dbg.flow_spin_count = 0;
            if (pc == m->dbg.last_flow_pc)
                return;
            m->dbg.last_flow_pc = pc;
            if (pc == 0x0175 || pc == 0x0179 || pc == 0x0183 || pc == 0x0188 ||
                pc == 0x0198 || pc == 0x019B || pc == 0x01BB || pc == 0x01C9 ||
                pc == 0x01DF || pc == 0x01EE || pc == 0x0200) {
                uint16_t ptr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
                fprintf(stderr,
                        "[flow-kbd] pc=%04X af=%04X bc=%04X de=%04X hl=%04X "
                        "4558=%02X 4577=%02X 457c=%04X 457e=%02X 4580=%02X 4581=%02X 4582=%02X 458a=%02X\n",
                        pc,
                        (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                        (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                        (unsigned)m->bus[0x4558u],
                        (unsigned)m->bus[0x4577u],
                        ptr,
                        (unsigned)m->bus[0x457Eu],
                        (unsigned)m->bus[0x4580u],
                        (unsigned)m->bus[0x4581u],
                        (unsigned)m->bus[0x4582u],
                        (unsigned)m->bus[0x458Au]);
            } else if (pc == 0x04D4 || pc == 0x04E4 || pc == 0x04F6 || pc == 0x0516 ||
                       pc == 0x057E || pc == 0x058C) {
                uint16_t ptr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
                uint16_t cursor = (uint16_t)m->bus[0x7014u] | ((uint16_t)m->bus[0x7015u] << 8);
                fprintf(stderr,
                        "[flow-cli] pc=%04X af=%04X bc=%04X de=%04X hl=%04X "
                        "ptr=%04X [ptr]=%02X 457e=%02X 45c0=%02X 7014=%04X\n",
                        pc,
                        (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                        (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                        ptr, (unsigned)m->bus[ptr],
                        (unsigned)m->bus[0x457Eu],
                        (unsigned)m->bus[0x45C0u],
                        cursor);
            } else if (pc >= 0x0127 && pc <= 0x0147) {
                uint16_t sp = (uint16_t)Z80_SP(m->cpu);
                uint16_t ret0 = (uint16_t)m->bus[sp] | ((uint16_t)m->bus[(uint16_t)(sp + 1u)] << 8);
                uint16_t ret1 = (uint16_t)m->bus[(uint16_t)(sp + 2u)] |
                                ((uint16_t)m->bus[(uint16_t)(sp + 3u)] << 8);
                fprintf(stderr,
                        "[flow-disp] pc=%04X af=%04X bc=%04X de=%04X hl=%04X sp=%04X top=%04X next=%04X "
                        "4554=%02X 4555=%02X 455C=%02X%02X 4562=%02X%02X 4564=%02X%02X 4566=%02X%02X 711A=%02X%02X\n",
                        pc,
                        (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                        (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                        sp, ret0, ret1,
                        m->bus[0x4554], m->bus[0x4555],
                        m->bus[0x455Cu], m->bus[0x455Du],
                        m->bus[0x4562u], m->bus[0x4563u],
                        m->bus[0x4564u], m->bus[0x4565u],
                        m->bus[0x4566u], m->bus[0x4567u],
                        m->bus[0x711Au], m->bus[0x711Bu]);
            } else {
                fprintf(stderr,
                        "[flow] pc=%04X af=%04X bc=%04X de=%04X hl=%04X "
                        "k=%02X/%d w54=%02X w55=%02X w57f=%02X w58a=%02X w595=%02X\n",
                        pc,
                        (unsigned)Z80_AF(m->cpu), (unsigned)Z80_BC(m->cpu),
                        (unsigned)Z80_DE(m->cpu), (unsigned)Z80_HL(m->cpu),
                        m->kbd.key_code, m->kbd.found,
                        m->bus[0x4554], m->bus[0x4555], m->bus[0x457F],
                        m->bus[0x458A], m->bus[0x4595]);
            }
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
    m->dbg.flow_budget = on ? 1000000 : 0;
    m->dbg.last_flow_pc = 0xFFFF;
    m->dbg.flow_spin_count = 0;
    if (on)
        fprintf(stderr, "debug: post-handoff flow trace enabled\n");
}
