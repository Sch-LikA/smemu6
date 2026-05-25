// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* debug.c – Built-in machine monitor / debugger */
#include "machine_internal.h"
#include "debug.h"
#include "memory.h"

#include <Z80.h>
#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef SMEMU6_HAS_GENERATED_FLO_SYMBOLS
#include "generated_flo_st_symbols.h"
#endif

#define DBG_WIN_W 900
#define DBG_WIN_H 520
#define DBG_FONT_W 8
#define DBG_FONT_H 8
#define DBG_FONT_SCALE 1
#define DBG_LINE_H ((DBG_FONT_H * DBG_FONT_SCALE) + 8)
#define DBG_MEM_COLS 16
#define DBG_MEM_ROWS 16
#define DBG_MEM_PAGE_SIZE (DBG_MEM_COLS * DBG_MEM_ROWS)
#define DBG_MAX_BREAKPOINTS 16
#define DBG_DISASM_VISIBLE_ROWS 11
#define DBG_DISASM_BACK_ROWS 8

#define DBG_COL_BG      0xFF08100Cu
#define DBG_COL_PANEL   0xFF102018u
#define DBG_COL_BORDER  0xFF24543Cu
#define DBG_COL_TEXT    0xFFB8F0C8u
#define DBG_COL_DIM     0xFF6AA47Eu
#define DBG_COL_ACCENT  0xFFFFD36Au
#define DBG_COL_WARN    0xFFFF9C5Cu
#define DBG_COL_ACTIVE  0xFF24462Eu
#define DBG_COL_ROM     0xFF9E7C48u
#define DBG_COL_INVERT  0xFF08100Cu

enum DebugStopReason {
    DBG_STOP_NONE = 0,
    DBG_STOP_MANUAL_PAUSE,
    DBG_STOP_STEP_INSTRUCTION,
    DBG_STOP_STEP_FRAME,
    DBG_STOP_STEP_OVER,
    DBG_STOP_BREAKPOINT,
    DBG_STOP_RUN_TO_CURSOR,
    DBG_STOP_BREAKPOINT_AND_STEP_OVER,
    DBG_STOP_BREAKPOINT_AND_CURSOR,
};

static const char *const DBG_CC[8] = {
    "nz", "z", "nc", "c", "po", "pe", "p", "m"
};

static const char *const DBG_R8[8] = {
    "b", "c", "d", "e", "h", "l", "(hl)", "a"
};

static const char *const DBG_R16[4] = {
    "bc", "de", "hl", "sp"
};

static const char *const DBG_R16_AF[4] = {
    "bc", "de", "hl", "af"
};

static const char *const DBG_ALU[8] = {
    "add a,", "adc a,", "sub ", "sbc a,", "and ", "xor ", "or ", "cp "
};

static const char *const DBG_ROT[8] = {
    "rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl"
};

static const char *const DBG_MISC[8] = {
    "rlca", "rrca", "rla", "rra", "daa", "cpl", "scf", "ccf"
};

static const char *const DBG_BLOCK[4][4] = {
    { "ldi", "cpi", "ini", "outi" },
    { "ldd", "cpd", "ind", "outd" },
    { "ldir", "cpir", "inir", "otir" },
    { "lddr", "cpdr", "indr", "otdr" }
};

static uint8_t dbg_mem8(struct Smaky6 *m, uint16_t addr);
static uint16_t dbg_mem16(struct Smaky6 *m, uint16_t addr);

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

struct DebugInsn {
    uint16_t addr;
    uint8_t len;
    char text[96];
};

static void dbg_sync_disasm_cursor(struct Smaky6 *m)
{
    m->dbg.disasm_cursor = (uint16_t)Z80_PC(m->cpu);
}

static int dbg_flo_symbols_loaded(void)
{
#ifdef SMEMU6_HAS_GENERATED_FLO_SYMBOLS
    return smaky6_flo_symbol_count > 0;
#else
    return 0;
#endif
}

static const char *dbg_lookup_flo_symbol(uint16_t value, int require_code_like)
{
    const char *best = NULL;
    int best_score = -1;

    if (!dbg_flo_symbols_loaded()) {
        return NULL;
    }

#ifdef SMEMU6_HAS_GENERATED_FLO_SYMBOLS
    for (size_t i = 0; i < smaky6_flo_symbol_count; i++) {
        const struct GeneratedSmaky6StEntry *entry = &smaky6_flo_symbols[i];
        size_t len;
        int score;
        const char *name = entry->best_name ? entry->best_name : entry->name;

        if (entry->value != value) {
            continue;
        }
        if (require_code_like && value < 0x0100u && name[0] != '?') {
            continue;
        }

        len = strlen(name);
        score = (name[0] == '?') ? 100 : 0;
        if (value >= 0x0100u) {
            score += 20;
        }
        if (len >= 3) {
            score += 10;
        }
        score += (int)len;
        if (!best || score > best_score) {
            best = name;
            best_score = score;
        }
    }
#endif

    return best;
}

static const char *dbg_lookup_flo_target_symbol(struct Smaky6 *m, uint16_t addr)
{
    uint8_t op = dbg_mem8(m, addr);
    uint16_t target;

    if ((op & 0xC7u) == 0xC2u || op == 0xC3u || (op & 0xC7u) == 0xC4u || op == 0xCDu) {
        target = dbg_mem16(m, (uint16_t)(addr + 1u));
        return dbg_lookup_flo_symbol(target, 1);
    }
    if ((op & 0xC7u) == 0xC7u) {
        target = (uint16_t)(op & 0x38u);
        return dbg_lookup_flo_symbol(target, 1);
    }

    return NULL;
}

static const char *dbg_lookup_threaded_service_symbol(struct Smaky6 *m, uint16_t addr)
{
    uint8_t op = dbg_mem8(m, addr);
    uint16_t vector;

    if (op != 0xD7u && op != 0xE7u && op != 0xEFu) {
        return NULL;
    }

    vector = (uint16_t)(((uint16_t)dbg_mem8(m, (uint16_t)(addr + 1u)) << 8) | op);
    return dbg_lookup_flo_symbol(vector, 0);
}

static int dbg_find_breakpoint_index(const struct Smaky6 *m, uint16_t addr)
{
    for (int i = 0; i < m->dbg.breakpoint_count; i++) {
        if (m->dbg.breakpoints[i] == addr) {
            return i;
        }
    }
    return -1;
}

static int dbg_has_breakpoint(const struct Smaky6 *m, uint16_t addr)
{
    return dbg_find_breakpoint_index(m, addr) >= 0;
}

static void dbg_arm_breakpoint_resume(struct Smaky6 *m, uint16_t pc)
{
    m->dbg.breakpoint_resume_pc = pc;
    m->dbg.breakpoint_resume_armed = 1;
}

static void dbg_toggle_breakpoint(struct Smaky6 *m, uint16_t addr)
{
    int idx = dbg_find_breakpoint_index(m, addr);

    if (idx >= 0) {
        for (int i = idx; i + 1 < m->dbg.breakpoint_count; i++) {
            m->dbg.breakpoints[i] = m->dbg.breakpoints[i + 1];
        }
        m->dbg.breakpoint_count--;
        fprintf(stderr, "debug: cleared breakpoint at %04X\n", (unsigned)addr);
        return;
    }

    if (m->dbg.breakpoint_count >= DBG_MAX_BREAKPOINTS) {
        fprintf(stderr, "debug: breakpoint table full (%d entries)\n", DBG_MAX_BREAKPOINTS);
        return;
    }

    m->dbg.breakpoints[m->dbg.breakpoint_count++] = addr;
    fprintf(stderr, "debug: set breakpoint at %04X\n", (unsigned)addr);
}

static void dbg_set_color(SDL_Renderer *ren, uint32_t col)
{
    SDL_SetRenderDrawColor(ren,
                           (uint8_t)((col >> 16) & 0xFFu),
                           (uint8_t)((col >> 8) & 0xFFu),
                           (uint8_t)(col & 0xFFu),
                           (uint8_t)((col >> 24) & 0xFFu));
}

static void dbg_fill_rect(SDL_Renderer *ren, int x, int y, int w, int h, uint32_t col)
{
    SDL_Rect r = { x, y, w, h };

    dbg_set_color(ren, col);
    SDL_RenderFillRect(ren, &r);
}

static void dbg_draw_rect(SDL_Renderer *ren, int x, int y, int w, int h, uint32_t col)
{
    SDL_Rect r = { x, y, w, h };

    dbg_set_color(ren, col);
    SDL_RenderDrawRect(ren, &r);
}

static int dbg_draw_char(struct Smaky6 *m, int px, int py, char c, uint32_t col)
{
    unsigned char uc = (unsigned char)c;
    const uint8_t *glyph;

    if (!m->dbg.renderer) {
        return DBG_FONT_W * DBG_FONT_SCALE + 1;
    }
    if (uc >= 128) {
        uc = '?';
    }

    glyph = &m->vid.chargen[uc * 16u];
    dbg_set_color(m->dbg.renderer, col);
    for (int row = 0; row < DBG_FONT_H; row++) {
        uint8_t bits = glyph[row];

        for (int bit = 0; bit < DBG_FONT_W; bit++) {
            if (bits & (1u << bit)) {
                SDL_Rect dot = {
                    px + bit * DBG_FONT_SCALE,
                    py + row * DBG_FONT_SCALE,
                    DBG_FONT_SCALE,
                    DBG_FONT_SCALE
                };

                SDL_RenderFillRect(m->dbg.renderer, &dot);
            }
        }
    }

    return DBG_FONT_W * DBG_FONT_SCALE + 1;
}

static void dbg_draw_text(struct Smaky6 *m, int x, int y, const char *s, uint32_t col)
{
    while (*s) {
        x += dbg_draw_char(m, x, y, *s, col);
        s++;
    }
}

static void dbg_draw_kv(struct Smaky6 *m, int x, int y, int value_x, const char *label, const char *value)
{
    dbg_draw_text(m, x, y, label, DBG_COL_DIM);
    dbg_draw_text(m, value_x, y, value, DBG_COL_TEXT);
}

static void dbg_draw_kv_col(struct Smaky6 *m,
                            int x,
                            int y,
                            int value_x,
                            const char *label,
                            const char *value,
                            uint32_t value_col)
{
    dbg_draw_text(m, x, y, label, DBG_COL_DIM);
    dbg_draw_text(m, value_x, y, value, value_col);
}

static void dbg_capture_snapshot(const struct Smaky6 *m, struct DebugCpuSnapshot *snap)
{
    snap->af = (uint16_t)Z80_AF(m->cpu);
    snap->af_shadow = m->cpu.af_.uint16_value;
    snap->bc = (uint16_t)Z80_BC(m->cpu);
    snap->bc_shadow = m->cpu.bc_.uint16_value;
    snap->de = (uint16_t)Z80_DE(m->cpu);
    snap->de_shadow = m->cpu.de_.uint16_value;
    snap->hl = (uint16_t)Z80_HL(m->cpu);
    snap->hl_shadow = m->cpu.hl_.uint16_value;
    snap->ix = (uint16_t)Z80_IX(m->cpu);
    snap->iy = (uint16_t)Z80_IY(m->cpu);
    snap->sp = (uint16_t)Z80_SP(m->cpu);
    snap->pc = (uint16_t)Z80_PC(m->cpu);
    snap->i = m->cpu.i;
    snap->r = m->cpu.r;
    snap->iff1 = (uint8_t)m->cpu.iff1;
    snap->iff2 = (uint8_t)m->cpu.iff2;
    snap->im = (uint8_t)m->cpu.im;
}

static void dbg_record_stop(struct Smaky6 *m, uint8_t reason)
{
    if (m->dbg.last_stop_valid) {
        m->dbg.prev_stop_snapshot = m->dbg.last_stop_snapshot;
        m->dbg.prev_stop_valid = 1;
    }
    dbg_capture_snapshot(m, &m->dbg.last_stop_snapshot);
    m->dbg.last_stop_valid = 1;
    m->dbg.stop_reason = reason;
}

static uint32_t dbg_value_color(int changed)
{
    return changed ? DBG_COL_ACCENT : DBG_COL_TEXT;
}

static void dbg_describe_mem_cursor(struct Smaky6 *m, char *out, size_t out_size)
{
    uint16_t addr = m->dbg.mem_cursor;
    uint8_t value = dbg_mem8(m, addr);
    char display = (value >= 32 && value < 127 && value != '\'' && value != '\\')
        ? (char)value
        : '.';

    snprintf(out,
             out_size,
             "MEM %04Xh=%02Xh/%03o/'%c' %s",
             addr,
             (unsigned)value,
             (unsigned)value,
             display,
             m->rom_mask[addr] ? "ROM" : "RAM");
}

static void dbg_format_stack_preview(struct Smaky6 *m, char *out, size_t out_size)
{
    uint16_t sp = (uint16_t)Z80_SP(m->cpu);
    uint16_t w0 = dbg_mem16(m, sp);
    uint16_t w1 = dbg_mem16(m, (uint16_t)(sp + 2u));
    uint16_t w2 = dbg_mem16(m, (uint16_t)(sp + 4u));
    uint16_t w3 = dbg_mem16(m, (uint16_t)(sp + 6u));

    snprintf(out,
             out_size,
             "%04X %04X %04X %04X",
             w0,
             w1,
             w2,
             w3);
}

static void dbg_format_stop_reason(const struct Smaky6 *m, char *out, size_t out_size)
{
    uint16_t pc = m->dbg.last_stop_valid
        ? m->dbg.last_stop_snapshot.pc
        : (uint16_t)Z80_PC(m->cpu);

    switch (m->dbg.stop_reason) {
    case DBG_STOP_MANUAL_PAUSE:
        snprintf(out, out_size, "STOP manual pause @ %04Xh", pc);
        break;
    case DBG_STOP_STEP_INSTRUCTION:
        snprintf(out, out_size, "STOP step instruction @ %04Xh", pc);
        break;
    case DBG_STOP_STEP_FRAME:
        snprintf(out, out_size, "STOP step frame @ %04Xh", pc);
        break;
    case DBG_STOP_STEP_OVER:
        snprintf(out, out_size, "STOP step over @ %04Xh", pc);
        break;
    case DBG_STOP_BREAKPOINT:
        snprintf(out, out_size, "STOP breakpoint @ %04Xh", pc);
        break;
    case DBG_STOP_RUN_TO_CURSOR:
        snprintf(out, out_size, "STOP run to cursor @ %04Xh", pc);
        break;
    case DBG_STOP_BREAKPOINT_AND_STEP_OVER:
        snprintf(out, out_size, "STOP step over + breakpoint @ %04Xh", pc);
        break;
    case DBG_STOP_BREAKPOINT_AND_CURSOR:
        snprintf(out, out_size, "STOP cursor + breakpoint @ %04Xh", pc);
        break;
    default:
        snprintf(out, out_size, "LIVE execution");
        break;
    }
}

static uint8_t dbg_mem8(struct Smaky6 *m, uint16_t addr)
{
    return memory_read(m, addr);
}

static uint16_t dbg_mem16(struct Smaky6 *m, uint16_t addr)
{
    return (uint16_t)dbg_mem8(m, addr) | ((uint16_t)dbg_mem8(m, (uint16_t)(addr + 1u)) << 8);
}

static void dbg_format_hex8(char *out, size_t out_size, uint8_t value)
{
    snprintf(out, out_size, "%02Xh", (unsigned)value);
}

static void dbg_format_oct8(char *out, size_t out_size, uint8_t value)
{
    snprintf(out, out_size, "%03o", (unsigned)value);
}

static void dbg_format_hex16(char *out, size_t out_size, uint16_t value)
{
    snprintf(out, out_size, "%04Xh", (unsigned)value);
}

static void dbg_format_disp(char *out, size_t out_size, const char *idx, uint8_t disp)
{
    if (disp < 0x80u) {
        snprintf(out, out_size, "(%s+%02Xh)", idx, (unsigned)disp);
    } else {
        snprintf(out, out_size, "(%s-%02Xh)", idx, (unsigned)(0x100u - disp));
    }
}

static void dbg_format_rel(char *out, size_t out_size, uint16_t pc, uint8_t disp)
{
    dbg_format_hex16(out, out_size, (uint16_t)(pc + 2u + (int8_t)disp));
}

static void dbg_format_flags(zuint8 flags, char *out, size_t out_size)
{
    snprintf(out,
             out_size,
             "%c%c%c%c%c%c%c%c",
             (flags & 0x80u) ? 'S' : '-',
             (flags & 0x40u) ? 'Z' : '-',
             (flags & 0x20u) ? '5' : '-',
             (flags & 0x10u) ? 'H' : '-',
             (flags & 0x08u) ? '3' : '-',
             (flags & 0x04u) ? 'P' : '-',
             (flags & 0x02u) ? 'N' : '-',
             (flags & 0x01u) ? 'C' : '-');
}

static const char *dbg_r16_name(int p, const char *idx)
{
    if (idx && p == 2) {
        return idx;
    }
    return DBG_R16[p & 3];
}

static const char *dbg_r16_af_name(int p, const char *idx)
{
    if (idx && p == 2) {
        return idx;
    }
    return DBG_R16_AF[p & 3];
}

static int dbg_r8_operand(struct Smaky6 *m, uint16_t arg_pc, int r, const char *idx, char *out, size_t out_size)
{
    if (!idx) {
        snprintf(out, out_size, "%s", DBG_R8[r & 7]);
        return 0;
    }

    switch (r & 7) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 7:
        snprintf(out, out_size, "%s", DBG_R8[r & 7]);
        return 0;
    case 4:
        snprintf(out, out_size, "%sh", idx);
        return 0;
    case 5:
        snprintf(out, out_size, "%sl", idx);
        return 0;
    case 6:
        dbg_format_disp(out, out_size, idx, dbg_mem8(m, arg_pc));
        return 1;
    default:
        snprintf(out, out_size, "?");
        return 0;
    }
}

static int dbg_decode_cb_prefixed(struct Smaky6 *m, uint16_t pc, char *out, size_t out_size)
{
    uint8_t op = dbg_mem8(m, (uint16_t)(pc + 1u));
    int x = op >> 6;
    int y = (op >> 3) & 7;
    int z = op & 7;

    switch (x) {
    case 0:
        snprintf(out, out_size, "%s %s", DBG_ROT[y], DBG_R8[z]);
        break;
    case 1:
        snprintf(out, out_size, "bit %d,%s", y, DBG_R8[z]);
        break;
    case 2:
        snprintf(out, out_size, "res %d,%s", y, DBG_R8[z]);
        break;
    default:
        snprintf(out, out_size, "set %d,%s", y, DBG_R8[z]);
        break;
    }

    return 2;
}

static int dbg_decode_ed_prefixed(struct Smaky6 *m, uint16_t pc, char *out, size_t out_size)
{
    uint8_t op = dbg_mem8(m, (uint16_t)(pc + 1u));
    int x = op >> 6;
    int y = (op >> 3) & 7;
    int z = op & 7;
    int p = y >> 1;
    int q = y & 1;
    char imm[32];

    if (x == 1) {
        switch (z) {
        case 0:
            if (y == 6) {
                snprintf(out, out_size, "in (c)");
            } else {
                snprintf(out, out_size, "in %s,(c)", DBG_R8[y]);
            }
            return 2;
        case 1:
            if (y == 6) {
                snprintf(out, out_size, "out (c),0");
            } else {
                snprintf(out, out_size, "out (c),%s", DBG_R8[y]);
            }
            return 2;
        case 2:
            snprintf(out, out_size, "%s hl,%s", q ? "adc" : "sbc", DBG_R16[p]);
            return 2;
        case 3:
            dbg_format_hex16(imm, sizeof(imm), dbg_mem16(m, (uint16_t)(pc + 2u)));
            if (q) {
                snprintf(out, out_size, "ld %s,(%s)", DBG_R16[p], imm);
            } else {
                snprintf(out, out_size, "ld (%s),%s", imm, DBG_R16[p]);
            }
            return 4;
        case 4:
            snprintf(out, out_size, "neg");
            return 2;
        case 5:
            snprintf(out, out_size, "%s", y == 1 ? "reti" : "retn");
            return 2;
        case 6: {
            static const int im_map[8] = {0, 0, 1, 2, 0, 0, 1, 2};

            snprintf(out, out_size, "im %d", im_map[y]);
            return 2;
        }
        case 7:
            switch (y) {
            case 0: snprintf(out, out_size, "ld i,a"); break;
            case 1: snprintf(out, out_size, "ld r,a"); break;
            case 2: snprintf(out, out_size, "ld a,i"); break;
            case 3: snprintf(out, out_size, "ld a,r"); break;
            case 4: snprintf(out, out_size, "rrd"); break;
            case 5: snprintf(out, out_size, "rld"); break;
            default: snprintf(out, out_size, "db EDh,%02Xh", (unsigned)op); break;
            }
            return 2;
        }
    }

    if (x == 2 && z <= 3 && y >= 4) {
        snprintf(out, out_size, "%s", DBG_BLOCK[y - 4][z]);
        return 2;
    }

    snprintf(out, out_size, "db EDh,%02Xh", (unsigned)op);
    return 2;
}

static int dbg_decode_ddcb_prefixed(struct Smaky6 *m, uint16_t pc, const char *idx, char *out, size_t out_size)
{
    uint8_t disp = dbg_mem8(m, (uint16_t)(pc + 2u));
    uint8_t op = dbg_mem8(m, (uint16_t)(pc + 3u));
    int x = op >> 6;
    int y = (op >> 3) & 7;
    int z = op & 7;
    char mem[32];

    dbg_format_disp(mem, sizeof(mem), idx, disp);
    switch (x) {
    case 0:
        if (z == 6) {
            snprintf(out, out_size, "%s %s", DBG_ROT[y], mem);
        } else {
            snprintf(out, out_size, "%s %s,%s", DBG_ROT[y], mem, DBG_R8[z]);
        }
        break;
    case 1:
        snprintf(out, out_size, "bit %d,%s", y, mem);
        break;
    case 2:
        if (z == 6) {
            snprintf(out, out_size, "res %d,%s", y, mem);
        } else {
            snprintf(out, out_size, "res %d,%s,%s", y, mem, DBG_R8[z]);
        }
        break;
    default:
        if (z == 6) {
            snprintf(out, out_size, "set %d,%s", y, mem);
        } else {
            snprintf(out, out_size, "set %d,%s,%s", y, mem, DBG_R8[z]);
        }
        break;
    }

    return 4;
}

static int dbg_decode_core(struct Smaky6 *m, uint16_t opcode_pc, const char *idx, int prefix_len, char *out, size_t out_size)
{
    uint8_t op = dbg_mem8(m, opcode_pc);
    int x = op >> 6;
    int y = (op >> 3) & 7;
    int z = op & 7;
    int p = y >> 1;
    int q = y & 1;
    int extra_y = 0;
    int extra_z = 0;
    int base_len = prefix_len + 1;
    char lhs[32];
    char rhs[32];
    char imm[32];

    switch (x) {
    case 0:
        switch (z) {
        case 0:
            switch (y) {
            case 0: snprintf(out, out_size, "nop"); return base_len;
            case 1: snprintf(out, out_size, "ex af,af'"); return base_len;
            case 2:
                dbg_format_rel(imm, sizeof(imm), opcode_pc, dbg_mem8(m, (uint16_t)(opcode_pc + 1u)));
                snprintf(out, out_size, "djnz %s", imm);
                return base_len + 1;
            case 3:
                dbg_format_rel(imm, sizeof(imm), opcode_pc, dbg_mem8(m, (uint16_t)(opcode_pc + 1u)));
                snprintf(out, out_size, "jr %s", imm);
                return base_len + 1;
            default:
                dbg_format_rel(imm, sizeof(imm), opcode_pc, dbg_mem8(m, (uint16_t)(opcode_pc + 1u)));
                snprintf(out, out_size, "jr %s,%s", DBG_CC[y - 4], imm);
                return base_len + 1;
            }
        case 1:
            if (!q) {
                dbg_format_hex16(imm, sizeof(imm), dbg_mem16(m, (uint16_t)(opcode_pc + 1u)));
                snprintf(out, out_size, "ld %s,%s", dbg_r16_name(p, idx), imm);
                return base_len + 2;
            }
            snprintf(out, out_size, "add %s,%s", dbg_r16_name(2, idx), dbg_r16_name(p, idx));
            return base_len;
        case 2:
            switch (p) {
            case 0:
                snprintf(out, out_size, "%s", q ? "ld a,(bc)" : "ld (bc),a");
                return base_len;
            case 1:
                snprintf(out, out_size, "%s", q ? "ld a,(de)" : "ld (de),a");
                return base_len;
            case 2:
                dbg_format_hex16(imm, sizeof(imm), dbg_mem16(m, (uint16_t)(opcode_pc + 1u)));
                if (q) {
                    snprintf(out, out_size, "ld %s,(%s)", dbg_r16_name(2, idx), imm);
                } else {
                    snprintf(out, out_size, "ld (%s),%s", imm, dbg_r16_name(2, idx));
                }
                return base_len + 2;
            default:
                dbg_format_hex16(imm, sizeof(imm), dbg_mem16(m, (uint16_t)(opcode_pc + 1u)));
                if (q) {
                    snprintf(out, out_size, "ld a,(%s)", imm);
                } else {
                    snprintf(out, out_size, "ld (%s),a", imm);
                }
                return base_len + 2;
            }
        case 3:
            snprintf(out, out_size, "%s %s", q ? "dec" : "inc", dbg_r16_name(p, idx));
            return base_len;
        case 4:
            extra_y = dbg_r8_operand(m, (uint16_t)(opcode_pc + 1u), y, idx, lhs, sizeof(lhs));
            snprintf(out, out_size, "inc %s", lhs);
            return base_len + extra_y;
        case 5:
            extra_y = dbg_r8_operand(m, (uint16_t)(opcode_pc + 1u), y, idx, lhs, sizeof(lhs));
            snprintf(out, out_size, "dec %s", lhs);
            return base_len + extra_y;
        case 6:
            extra_y = dbg_r8_operand(m, (uint16_t)(opcode_pc + 1u), y, idx, lhs, sizeof(lhs));
            dbg_format_hex8(imm, sizeof(imm), dbg_mem8(m, (uint16_t)(opcode_pc + 1u + extra_y)));
            snprintf(out, out_size, "ld %s,%s", lhs, imm);
            return base_len + extra_y + 1;
        default:
            snprintf(out, out_size, "%s", DBG_MISC[y]);
            return base_len;
        }
    case 1:
        if (y == 6 && z == 6) {
            snprintf(out, out_size, "halt");
            return base_len;
        }
        extra_y = dbg_r8_operand(m, (uint16_t)(opcode_pc + 1u), y, idx, lhs, sizeof(lhs));
        extra_z = dbg_r8_operand(m, (uint16_t)(opcode_pc + 1u), z, idx, rhs, sizeof(rhs));
        snprintf(out, out_size, "ld %s,%s", lhs, rhs);
        return base_len + (extra_y > extra_z ? extra_y : extra_z);
    case 2:
        extra_z = dbg_r8_operand(m, (uint16_t)(opcode_pc + 1u), z, idx, rhs, sizeof(rhs));
        snprintf(out, out_size, "%s%s", DBG_ALU[y], rhs);
        return base_len + extra_z;
    default:
        switch (z) {
        case 0:
            snprintf(out, out_size, "ret %s", DBG_CC[y]);
            return base_len;
        case 1:
            if (!q) {
                snprintf(out, out_size, "pop %s", dbg_r16_af_name(p, idx));
                return base_len;
            }
            switch (p) {
            case 0: snprintf(out, out_size, "ret"); break;
            case 1: snprintf(out, out_size, "exx"); break;
            case 2: snprintf(out, out_size, "jp (%s)", dbg_r16_name(2, idx)); break;
            default: snprintf(out, out_size, "ld sp,%s", dbg_r16_name(2, idx)); break;
            }
            return base_len;
        case 2:
            dbg_format_hex16(imm, sizeof(imm), dbg_mem16(m, (uint16_t)(opcode_pc + 1u)));
            snprintf(out, out_size, "jp %s,%s", DBG_CC[y], imm);
            return base_len + 2;
        case 3:
            switch (y) {
            case 0:
                dbg_format_hex16(imm, sizeof(imm), dbg_mem16(m, (uint16_t)(opcode_pc + 1u)));
                snprintf(out, out_size, "jp %s", imm);
                return base_len + 2;
            case 2:
                dbg_format_hex8(imm, sizeof(imm), dbg_mem8(m, (uint16_t)(opcode_pc + 1u)));
                snprintf(out, out_size, "out (%s),a", imm);
                return base_len + 1;
            case 3:
                dbg_format_hex8(imm, sizeof(imm), dbg_mem8(m, (uint16_t)(opcode_pc + 1u)));
                snprintf(out, out_size, "in a,(%s)", imm);
                return base_len + 1;
            case 4: snprintf(out, out_size, "ex (sp),%s", dbg_r16_name(2, idx)); return base_len;
            case 5: snprintf(out, out_size, "ex de,hl"); return base_len;
            case 6: snprintf(out, out_size, "di"); return base_len;
            case 7: snprintf(out, out_size, "ei"); return base_len;
            default: break;
            }
            break;
        case 4:
            dbg_format_hex16(imm, sizeof(imm), dbg_mem16(m, (uint16_t)(opcode_pc + 1u)));
            snprintf(out, out_size, "call %s,%s", DBG_CC[y], imm);
            return base_len + 2;
        case 5:
            if (!q) {
                snprintf(out, out_size, "push %s", dbg_r16_af_name(p, idx));
                return base_len;
            }
            if (p == 0) {
                dbg_format_hex16(imm, sizeof(imm), dbg_mem16(m, (uint16_t)(opcode_pc + 1u)));
                snprintf(out, out_size, "call %s", imm);
                return base_len + 2;
            }
            break;
        case 6:
            dbg_format_hex8(imm, sizeof(imm), dbg_mem8(m, (uint16_t)(opcode_pc + 1u)));
            snprintf(out, out_size, "%s%s", DBG_ALU[y], imm);
            return base_len + 1;
        case 7:
            {
                const char *service = dbg_lookup_threaded_service_symbol(m, opcode_pc);

                if (service) {
                    snprintf(out, out_size, "rst %02Xh ; %s", (unsigned)(y * 8), service);
                    return base_len + 1;
                }
            }
            snprintf(out, out_size, "rst %02Xh", (unsigned)(y * 8));
            return base_len;
        default:
            break;
        }
    }

    snprintf(out, out_size, "db %02Xh", (unsigned)op);
    return base_len;
}

static int dbg_disassemble_at(struct Smaky6 *m, uint16_t pc, char *out, size_t out_size)
{
    uint8_t op = dbg_mem8(m, pc);

    switch (op) {
    case 0xCB:
        return dbg_decode_cb_prefixed(m, pc, out, out_size);
    case 0xED:
        return dbg_decode_ed_prefixed(m, pc, out, out_size);
    case 0xDD:
        if (dbg_mem8(m, (uint16_t)(pc + 1u)) == 0xCB) {
            return dbg_decode_ddcb_prefixed(m, pc, "ix", out, out_size);
        }
        return dbg_decode_core(m, (uint16_t)(pc + 1u), "ix", 1, out, out_size);
    case 0xFD:
        if (dbg_mem8(m, (uint16_t)(pc + 1u)) == 0xCB) {
            return dbg_decode_ddcb_prefixed(m, pc, "iy", out, out_size);
        }
        return dbg_decode_core(m, (uint16_t)(pc + 1u), "iy", 1, out, out_size);
    default:
        return dbg_decode_core(m, pc, NULL, 0, out, out_size);
    }
}

static uint16_t dbg_prev_disasm_addr(struct Smaky6 *m, uint16_t addr)
{
    uint16_t scan = (addr > 64u) ? (uint16_t)(addr - 64u) : 0u;
    uint16_t prev = addr;
    char text[96];

    while (scan < addr) {
        int len = dbg_disassemble_at(m, scan, text, sizeof(text));

        if (len <= 0) {
            len = 1;
        }
        if ((uint16_t)(scan + len) >= addr) {
            break;
        }
        prev = scan;
        scan = (uint16_t)(scan + len);
    }

    return prev;
}

static void dbg_move_disasm_cursor(struct Smaky6 *m, int direction)
{
    if (direction < 0) {
        m->dbg.disasm_cursor = dbg_prev_disasm_addr(m, m->dbg.disasm_cursor);
        return;
    }

    {
        char text[96];
        int len = dbg_disassemble_at(m, m->dbg.disasm_cursor, text, sizeof(text));

        if (len <= 0) {
            len = 1;
        }
        m->dbg.disasm_cursor = (uint16_t)(m->dbg.disasm_cursor + len);
    }
}

static int dbg_is_step_over_candidate(uint8_t opcode)
{
    return opcode == 0xCDu ||
           (opcode & 0xC7u) == 0xC4u ||
           (opcode & 0xC7u) == 0xC7u;
}

static void dbg_set_memory_view(struct Smaky6 *m, uint16_t base)
{
    m->dbg.mem_base = base;
    m->dbg.mem_cursor = base;
    m->dbg.mem_edit_high_nibble = 1;
}

static void dbg_begin_run_to_cursor(struct Smaky6 *m)
{
    uint16_t pc = (uint16_t)Z80_PC(m->cpu);

    if (m->dbg.disasm_cursor == pc) {
        fprintf(stderr, "debug: disassembly cursor is already at PC %04X\n", (unsigned)pc);
        return;
    }

    if (dbg_has_breakpoint(m, pc)) {
        dbg_arm_breakpoint_resume(m, pc);
    }
    m->dbg.run_to_cursor_addr = m->dbg.disasm_cursor;
    m->dbg.run_to_cursor_active = 1;
    m->dbg.step_over_active = 0;
    m->dbg.paused = 0;
    m->dbg.stepping = 0;
    m->dbg.step_instruction_pending = 0;
    m->dbg.step_frame_pending = 0;
    m->dbg.stop_reason = DBG_STOP_NONE;
    fprintf(stderr, "debug: run to cursor %04X\n", (unsigned)m->dbg.run_to_cursor_addr);
}

static int dbg_begin_step_over(struct Smaky6 *m)
{
    uint16_t pc = (uint16_t)Z80_PC(m->cpu);
    uint8_t opcode = dbg_mem8(m, pc);
    uint16_t target;
    char text[96];
    int len;

    if (!dbg_is_step_over_candidate(opcode)) {
        return 0;
    }

    len = dbg_disassemble_at(m, pc, text, sizeof(text));
    if (len <= 0) {
        len = 1;
    }
    target = (uint16_t)(pc + len);
    if (target == pc) {
        return 0;
    }

    if (dbg_has_breakpoint(m, pc)) {
        dbg_arm_breakpoint_resume(m, pc);
    }
    m->dbg.disasm_cursor = target;
    m->dbg.run_to_cursor_addr = target;
    m->dbg.run_to_cursor_active = 1;
    m->dbg.step_over_active = 1;
    m->dbg.paused = 0;
    m->dbg.stepping = 0;
    m->dbg.step_instruction_pending = 0;
    m->dbg.step_frame_pending = 0;
    m->dbg.stop_reason = DBG_STOP_NONE;
    fprintf(stderr, "debug: step over %04X -> %04X\n", (unsigned)pc, (unsigned)target);
    return 1;
}

static void dbg_sync_memory_to_cursor(struct Smaky6 *m)
{
    if (m->dbg.mem_cursor < m->dbg.mem_base ||
        m->dbg.mem_cursor >= (uint16_t)(m->dbg.mem_base + DBG_MEM_PAGE_SIZE)) {
        m->dbg.mem_base = (uint16_t)(m->dbg.mem_cursor & 0xFF00u);
    }
}

static void dbg_move_memory_cursor(struct Smaky6 *m, int delta)
{
    m->dbg.mem_cursor = (uint16_t)(m->dbg.mem_cursor + delta);
    dbg_sync_memory_to_cursor(m);
}

static void dbg_cancel_hex_input(struct Smaky6 *m)
{
    m->dbg.mem_jump_active = 0;
    m->dbg.watch_edit_active = 0;
    m->dbg.mem_jump_len = 0;
    m->dbg.mem_jump_buf[0] = '\0';
}

static void dbg_apply_hex_input(struct Smaky6 *m)
{
    uint16_t value;

    if (m->dbg.mem_jump_len == 0) {
        return;
    }

    value = (uint16_t)strtoul(m->dbg.mem_jump_buf, NULL, 16);
    if (m->dbg.watch_edit_active) {
        m->dbg.watch_addrs[m->dbg.watch_selected] = value;
    } else {
        m->dbg.mem_cursor = value;
        m->dbg.mem_base = (uint16_t)(m->dbg.mem_cursor & 0xFF00u);
        m->dbg.mem_edit_high_nibble = 1;
    }
    dbg_cancel_hex_input(m);
}

static int dbg_hex_value(SDL_Keycode sym)
{
    if (sym >= SDLK_0 && sym <= SDLK_9) {
        return (int)(sym - SDLK_0);
    }
    if (sym >= SDLK_a && sym <= SDLK_f) {
        return 10 + (int)(sym - SDLK_a);
    }
    return -1;
}

static void dbg_edit_memory_nibble(struct Smaky6 *m, int nibble)
{
    uint16_t addr = m->dbg.mem_cursor;
    uint8_t value = m->bus[addr];

    if (m->dbg.mem_edit_high_nibble) {
        value = (uint8_t)((value & 0x0Fu) | ((uint8_t)nibble << 4));
        m->dbg.mem_edit_high_nibble = 0;
    } else {
        value = (uint8_t)((value & 0xF0u) | (uint8_t)nibble);
        m->dbg.mem_edit_high_nibble = 1;
    }

    memory_write(m, addr, value);
    if (m->dbg.mem_edit_high_nibble) {
        dbg_move_memory_cursor(m, 1);
    }
}

static void dbg_close_window(struct Smaky6 *m)
{
    if (m->dbg.renderer) {
        SDL_DestroyRenderer(m->dbg.renderer);
        m->dbg.renderer = NULL;
    }
    if (m->dbg.window) {
        SDL_DestroyWindow(m->dbg.window);
        m->dbg.window = NULL;
    }

    m->dbg.visible = 0;
    m->dbg.paused = 0;
    m->dbg.stepping = 0;
    m->dbg.step_instruction_pending = 0;
    m->dbg.step_frame_pending = 0;
    m->dbg.run_to_cursor_active = 0;
    dbg_cancel_hex_input(m);
    m->dbg.window_id = 0;
}

static int dbg_open_window(struct Smaky6 *m)
{
#ifdef __EMSCRIPTEN__
    (void)m;
    fprintf(stderr, "debug: native debugger window is unavailable in the web build\n");
    return -1;
#else
    SDL_Renderer *ren;

    if (m->dbg.window) {
        return 0;
    }

    m->dbg.window = SDL_CreateWindow("Smaky 6 Debugger",
                                     SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED,
                                     DBG_WIN_W,
                                     DBG_WIN_H,
                                     SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!m->dbg.window) {
        fprintf(stderr, "debug: failed to create debugger window: %s\n", SDL_GetError());
        return -1;
    }

    ren = SDL_CreateRenderer(m->dbg.window,
                             -1,
                             SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        ren = SDL_CreateRenderer(m->dbg.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        fprintf(stderr, "debug: failed to create debugger renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(m->dbg.window);
        m->dbg.window = NULL;
        return -1;
    }

    m->dbg.renderer = ren;
    m->dbg.window_id = SDL_GetWindowID(m->dbg.window);
    SDL_RenderSetLogicalSize(m->dbg.renderer, DBG_WIN_W, DBG_WIN_H);
    dbg_sync_disasm_cursor(m);
    dbg_sync_memory_to_cursor(m);
    m->dbg.visible = 1;
    return 0;
#endif
}

static int dbg_event_targets_window(const struct Smaky6 *m, const SDL_Event *ev)
{
    if (!m->dbg.visible || m->dbg.window_id == 0) {
        return 0;
    }

    switch (ev->type) {
    case SDL_WINDOWEVENT:
        return ev->window.windowID == m->dbg.window_id;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        return ev->key.windowID == m->dbg.window_id;
    case SDL_TEXTINPUT:
        return ev->text.windowID == m->dbg.window_id;
    default:
        return 0;
    }
}

static void dbg_render_disassembly(struct Smaky6 *m, int x, int y, int w, int h)
{
    struct DebugInsn insn[40];
    uint16_t pc = (uint16_t)Z80_PC(m->cpu);
    uint16_t view = m->dbg.disasm_cursor;
    uint16_t scan;
    int count = 0;
    int current = 0;
    int selected = 0;
    int start;
    int max_start;
    char header[96];

    if (!m->dbg.paused && !m->dbg.run_to_cursor_active) {
        dbg_sync_disasm_cursor(m);
        view = m->dbg.disasm_cursor;
    }
    scan = view;
    for (int i = 0; i < DBG_DISASM_BACK_ROWS; i++) {
        uint16_t prev = dbg_prev_disasm_addr(m, scan);

        if (prev == scan) {
            break;
        }
        scan = prev;
    }

    dbg_fill_rect(m->dbg.renderer, x, y, w, h, DBG_COL_PANEL);
    dbg_draw_rect(m->dbg.renderer, x, y, w, h, DBG_COL_BORDER);
    snprintf(header,
             sizeof(header),
             "DISASSEMBLY CUR=%04Xh BP=%u%s%s",
             m->dbg.disasm_cursor,
             (unsigned)m->dbg.breakpoint_count,
             m->dbg.run_to_cursor_active ? " RUN" : "",
             dbg_flo_symbols_loaded() ? " FLO" : "");
    dbg_draw_text(m, x + 16, y + 16, header, DBG_COL_ACCENT);

    while (count < (int)(sizeof(insn) / sizeof(insn[0])) && scan < (uint16_t)(view + 96u)) {
        insn[count].addr = scan;
        insn[count].len = (uint8_t)dbg_disassemble_at(m, scan, insn[count].text, sizeof(insn[count].text));
        if (insn[count].len == 0) {
            insn[count].len = 1;
        }
        if (scan <= pc && (uint16_t)(scan + insn[count].len) > pc) {
            current = count;
        }
        if (scan == m->dbg.disasm_cursor) {
            selected = count;
        }
        count++;
        if ((uint16_t)(scan + insn[count - 1].len) <= scan) {
            break;
        }
        scan = (uint16_t)(scan + insn[count - 1].len);
    }

    max_start = count > DBG_DISASM_VISIBLE_ROWS ? count - DBG_DISASM_VISIBLE_ROWS : 0;
    start = current > DBG_DISASM_BACK_ROWS ? current - DBG_DISASM_BACK_ROWS : 0;
    if (start > max_start) {
        start = max_start;
    }
    if (selected < start && current - selected <= DBG_DISASM_BACK_ROWS) {
        start = selected;
    } else if (selected >= start + DBG_DISASM_VISIBLE_ROWS &&
               selected - current <= (DBG_DISASM_VISIBLE_ROWS - DBG_DISASM_BACK_ROWS)) {
        start = selected - (DBG_DISASM_VISIBLE_ROWS - 1);
        if (start > max_start) {
            start = max_start;
        }
    }
    for (int row = 0; row < DBG_DISASM_VISIBLE_ROWS && start + row < count; row++) {
        char bytes[24] = "";
        char line[192];
        char symbol_col[8] = "";
        char target_suffix[24] = "";
        int line_y = y + 40 + row * DBG_LINE_H;
        uint32_t line_col = DBG_COL_TEXT;
        int insn_idx = start + row;
        int is_current = (insn_idx == current);
        int is_selected = (insn_idx == selected);
        int has_breakpoint = dbg_has_breakpoint(m, insn[insn_idx].addr);
        const char *row_symbol = dbg_lookup_flo_symbol(insn[insn_idx].addr, 1);
        const char *target_symbol = dbg_lookup_flo_target_symbol(m, insn[insn_idx].addr);

        for (int i = 0; i < insn[insn_idx].len && i < 4; i++) {
            char byte[8];

            snprintf(byte, sizeof(byte), "%s%02X", i ? " " : "", (unsigned)dbg_mem8(m, (uint16_t)(insn[insn_idx].addr + i)));
            strncat(bytes, byte, sizeof(bytes) - strlen(bytes) - 1);
        }
        if (is_current) {
            dbg_fill_rect(m->dbg.renderer, x + 8, line_y - 2, w - 16, DBG_LINE_H, DBG_COL_WARN);
            line_col = DBG_COL_INVERT;
        }
        if (is_selected) {
            dbg_draw_rect(m->dbg.renderer, x + 8, line_y - 2, w - 16, DBG_LINE_H, DBG_COL_ACCENT);
            if (!is_current) {
                line_col = DBG_COL_ACCENT;
            }
        }
        if (row_symbol) {
            snprintf(symbol_col, sizeof(symbol_col), "%s:", row_symbol);
        }
        if (target_symbol) {
            snprintf(target_suffix, sizeof(target_suffix), " ;%s", target_symbol);
        }
        snprintf(line,
                 sizeof(line),
                 "%c%c%c %04X %-7s %-11s %s%s",
                 is_current ? '>' : ' ',
                 is_selected ? '*' : ' ',
                 has_breakpoint ? 'B' : ' ',
                 insn[insn_idx].addr,
                 symbol_col,
                 bytes,
                 insn[insn_idx].text,
                 target_suffix);
        dbg_draw_text(m, x + 16, line_y, line, line_col);
    }
}

static void dbg_render_memory(struct Smaky6 *m, int x, int y, int w, int h)
{
    const int octal = m->dbg.mem_view_octal != 0;
    const int addr_x = x + 16;
    const int value_x = x + 72;
    const int value_col_w = octal ? 28 : 21;
    const int group_gap = octal ? 8 : 5;
    const int show_ascii = octal ? 0 : 1;
    const int ascii_x = x + w - 16 - DBG_MEM_COLS * 9;
    int watch_x = x + 16;
    char line[160];
    char summary[96];
    char watch_slot[24];

    dbg_fill_rect(m->dbg.renderer, x, y, w, h, DBG_COL_PANEL);
    dbg_draw_rect(m->dbg.renderer, x, y, w, h, DBG_COL_BORDER);
    snprintf(line,
             sizeof(line),
             "MEMORY %04Xh-%04Xh  CURSOR=%04Xh  VIEW=%s  EDIT=HEX",
             m->dbg.mem_base,
             (uint16_t)(m->dbg.mem_base + DBG_MEM_PAGE_SIZE - 1u),
             m->dbg.mem_cursor,
             octal ? "OCTAL" : "HEX");
    dbg_draw_text(m, x + 16, y + 16, line, DBG_COL_ACCENT);
    dbg_draw_text(m, x + 16, y + 28, "ARROWS MOVE  PGUP/PGDN PAGE  O TOGGLE BASE", DBG_COL_DIM);
    dbg_draw_text(m,
                  x + 16,
                  y + 40,
                  m->dbg.mem_jump_active
                      ? (m->dbg.watch_edit_active ? "WATCH: TYPE 4 HEX DIGITS" : "JUMP: TYPE 4 HEX DIGITS")
                      : "G JUMP  P=PC  CTRL+A/V PRESETS",
                  DBG_COL_DIM);
    if (m->dbg.mem_jump_active) {
        snprintf(line,
                 sizeof(line),
                 "%s>%s",
                 m->dbg.watch_edit_active ? "W" : "JUMP",
                 m->dbg.mem_jump_buf);
        dbg_draw_text(m, x + w - 96, y + 40, line, DBG_COL_WARN);
    }

    for (int group = 1; group < 4; group++) {
        int sep_x = value_x + group * 4 * value_col_w + (group - 1) * group_gap + group_gap / 2;

        dbg_fill_rect(m->dbg.renderer, sep_x, y + 56, 1, h - 88, DBG_COL_BORDER);
    }
    if (show_ascii) {
        dbg_fill_rect(m->dbg.renderer, ascii_x - 8, y + 56, 1, h - 88, DBG_COL_BORDER);
    }

    for (int row = 0; row < DBG_MEM_ROWS; row++) {
        int row_y = y + 60 + row * 10;

        snprintf(line, sizeof(line), "%04X:", (unsigned)(m->dbg.mem_base + row * DBG_MEM_COLS));
        dbg_draw_text(m, addr_x, row_y, line, DBG_COL_DIM);
        for (int col = 0; col < DBG_MEM_COLS; col++) {
            uint16_t addr = (uint16_t)(m->dbg.mem_base + row * DBG_MEM_COLS + col);
            uint8_t value = dbg_mem8(m, addr);
            char byte[8];
            int byte_x = value_x + col * value_col_w + (col / 4) * group_gap;
            int byte_ascii_x = ascii_x + col * 9;
            uint32_t byte_col = m->rom_mask[addr] ? DBG_COL_ROM : DBG_COL_TEXT;
            uint32_t ascii_col = byte_col;

            if (octal) {
                dbg_format_oct8(byte, sizeof(byte), value);
            } else {
                snprintf(byte, sizeof(byte), "%02X", (unsigned)value);
            }
            if (addr == m->dbg.mem_cursor) {
                dbg_fill_rect(m->dbg.renderer, byte_x - 2, row_y - 2, octal ? 29 : 20, 11, DBG_COL_ACTIVE);
                if (show_ascii) {
                    dbg_fill_rect(m->dbg.renderer, byte_ascii_x - 1, row_y - 2, 9, 11, DBG_COL_ACTIVE);
                }
                byte_col = DBG_COL_WARN;
                ascii_col = DBG_COL_WARN;
            }
            dbg_draw_text(m, byte_x, row_y, byte, byte_col);
            if (show_ascii) {
                byte[0] = (value >= 32 && value < 127) ? (char)value : '.';
                byte[1] = '\0';
                dbg_draw_text(m, byte_ascii_x, row_y, byte, ascii_col);
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        snprintf(watch_slot,
                 sizeof(watch_slot),
                 "%c%04X:%02X",
                 (i == (int)m->dbg.watch_selected) ? '>' : ' ',
                 (unsigned)m->dbg.watch_addrs[i],
                 (unsigned)dbg_mem8(m, m->dbg.watch_addrs[i]));
        dbg_draw_text(m,
                      watch_x,
                      y + h - 18,
                      watch_slot,
                      i == (int)m->dbg.watch_selected ? DBG_COL_ACCENT : DBG_COL_DIM);
        watch_x += (int)strlen(watch_slot) * (DBG_FONT_W * DBG_FONT_SCALE + 1) + 8;
    }
    dbg_fill_rect(m->dbg.renderer, x + 12, y + h - 16, w - 24, 1, DBG_COL_BORDER);
    dbg_describe_mem_cursor(m, summary, sizeof(summary));
    dbg_draw_text(m, x + 320, y + h - 18, summary, DBG_COL_WARN);
}

static void dbg_render_registers(struct Smaky6 *m)
{
    char buf[64];
    char flags[16];
    char flags_shadow[16];
    char mode[80];
    char stop[96];
    char mem_summary[96];
    char stack_preview[96];
    const char *mode_label;
    const struct DebugCpuSnapshot *prev = m->dbg.prev_stop_valid ? &m->dbg.prev_stop_snapshot : NULL;
    const int reg_x = 28;
    const int reg_value_x = 112;
    const int state_x = 28;
    const int state_value_x = 160;
    const int top_y = 48;
    const int state_panel_y = 260;
    const int state_panel_h = 176;
    const int shortcuts_y = 444;

    dbg_fill_rect(m->dbg.renderer, 12, 12, 876, 28, DBG_COL_PANEL);
    dbg_draw_rect(m->dbg.renderer, 12, 12, 876, 28, DBG_COL_BORDER);
    mode_label = m->dbg.run_to_cursor_active
        ? (m->dbg.step_over_active ? "STEP" : "CURSOR")
        : (m->dbg.paused ? "PAUSE" : "RUN");
    snprintf(mode,
             sizeof(mode),
             "MODE %-6s PC %04Xh  CUR %04Xh  FRAMES %llu",
             mode_label,
             (unsigned)Z80_PC(m->cpu),
             (unsigned)m->dbg.disasm_cursor,
             (unsigned long long)m->dbg.frame_counter);
    dbg_format_stop_reason(m, stop, sizeof(stop));
    dbg_describe_mem_cursor(m, mem_summary, sizeof(mem_summary));
    dbg_format_stack_preview(m, stack_preview, sizeof(stack_preview));
    dbg_draw_text(m, 24, 20, mode, DBG_COL_ACCENT);
    dbg_draw_text(m,
                  430,
                  20,
                  m->dbg.run_to_cursor_active
                      ? (m->dbg.step_over_active ? "STEP OVER ACTIVE" : "TARGET ACTIVE")
                      : stop,
                  DBG_COL_WARN);

    dbg_fill_rect(m->dbg.renderer, 12, top_y, 260, 210, DBG_COL_PANEL);
    dbg_draw_rect(m->dbg.renderer, 12, top_y, 260, 210, DBG_COL_BORDER);
    dbg_draw_text(m, reg_x, top_y + 16, "CPU REGISTERS", DBG_COL_ACCENT);

    snprintf(buf, sizeof(buf), "%04X", (unsigned)Z80_AF(m->cpu));
    dbg_draw_kv_col(m, reg_x, top_y + 52, reg_value_x, "AF", buf,
                    dbg_value_color(prev && prev->af != (uint16_t)Z80_AF(m->cpu)));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)m->cpu.af_.uint16_value);
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H, reg_value_x, "AF'", buf,
                    dbg_value_color(prev && prev->af_shadow != m->cpu.af_.uint16_value));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)Z80_BC(m->cpu));
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 2, reg_value_x, "BC", buf,
                    dbg_value_color(prev && prev->bc != (uint16_t)Z80_BC(m->cpu)));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)m->cpu.bc_.uint16_value);
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 3, reg_value_x, "BC'", buf,
                    dbg_value_color(prev && prev->bc_shadow != m->cpu.bc_.uint16_value));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)Z80_DE(m->cpu));
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 4, reg_value_x, "DE", buf,
                    dbg_value_color(prev && prev->de != (uint16_t)Z80_DE(m->cpu)));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)m->cpu.de_.uint16_value);
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 5, reg_value_x, "DE'", buf,
                    dbg_value_color(prev && prev->de_shadow != m->cpu.de_.uint16_value));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)Z80_HL(m->cpu));
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 6, reg_value_x, "HL", buf,
                    dbg_value_color(prev && prev->hl != (uint16_t)Z80_HL(m->cpu)));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)m->cpu.hl_.uint16_value);
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 7, reg_value_x, "HL'", buf,
                    dbg_value_color(prev && prev->hl_shadow != m->cpu.hl_.uint16_value));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)Z80_IX(m->cpu));
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 8, reg_value_x, "IX", buf,
                    dbg_value_color(prev && prev->ix != (uint16_t)Z80_IX(m->cpu)));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)Z80_IY(m->cpu));
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 9, reg_value_x, "IY", buf,
                    dbg_value_color(prev && prev->iy != (uint16_t)Z80_IY(m->cpu)));
    snprintf(buf, sizeof(buf), "%04X", (unsigned)Z80_SP(m->cpu));
    dbg_draw_kv_col(m, reg_x, top_y + 52 + DBG_LINE_H * 10, reg_value_x, "SP", buf,
                    dbg_value_color(prev && prev->sp != (uint16_t)Z80_SP(m->cpu)));

    dbg_fill_rect(m->dbg.renderer, 12, state_panel_y, 260, state_panel_h, DBG_COL_PANEL);
    dbg_draw_rect(m->dbg.renderer, 12, state_panel_y, 260, state_panel_h, DBG_COL_BORDER);
    dbg_draw_text(m, state_x, state_panel_y + 16, "STATE", DBG_COL_ACCENT);

    snprintf(buf, sizeof(buf), "%04X", (unsigned)Z80_PC(m->cpu));
    dbg_draw_kv_col(m, state_x, state_panel_y + 32, state_value_x, "PC", buf,
                    dbg_value_color(prev && prev->pc != (uint16_t)Z80_PC(m->cpu)));
    snprintf(buf, sizeof(buf), "%02X / %02X", (unsigned)m->cpu.i, (unsigned)m->cpu.r);
    dbg_draw_kv_col(m, state_x, state_panel_y + 32 + DBG_LINE_H, state_value_x, "I / R", buf,
                    dbg_value_color(prev && (prev->i != m->cpu.i || prev->r != m->cpu.r)));
    snprintf(buf, sizeof(buf), "%u / %u / IM%u",
             (unsigned)m->cpu.iff1,
             (unsigned)m->cpu.iff2,
             (unsigned)m->cpu.im);
    dbg_draw_kv_col(m,
                    state_x,
                    state_panel_y + 32 + DBG_LINE_H * 2,
                    state_value_x,
                    "IFF1/IFF2/IM",
                    buf,
                    dbg_value_color(prev && (prev->iff1 != (uint8_t)m->cpu.iff1 ||
                                             prev->iff2 != (uint8_t)m->cpu.iff2 ||
                                             prev->im != (uint8_t)m->cpu.im)));
    snprintf(buf, sizeof(buf), "%s", m->dbg.paused ? "PAUSED" : "RUNNING");
    dbg_draw_kv(m, state_x, state_panel_y + 32 + DBG_LINE_H * 3, state_value_x, "EXEC", buf);
    snprintf(buf, sizeof(buf), "%u", (unsigned)m->dbg.last_run_tstates);
    dbg_draw_kv(m, state_x, state_panel_y + 32 + DBG_LINE_H * 4, state_value_x, "T-STATES", buf);
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)m->dbg.frame_counter);
    dbg_draw_kv(m, state_x, state_panel_y + 32 + DBG_LINE_H * 5, state_value_x, "FRAMES", buf);
    dbg_format_flags((zuint8)(Z80_AF(m->cpu) & 0x00FFu), flags, sizeof(flags));
    dbg_draw_kv_col(m, state_x, state_panel_y + 32 + DBG_LINE_H * 6, state_value_x, "FLAGS", flags,
                    dbg_value_color(prev && ((prev->af & 0x00FFu) != ((uint16_t)Z80_AF(m->cpu) & 0x00FFu))));
    dbg_format_flags((zuint8)(m->cpu.af_.uint16_value & 0x00FFu), flags_shadow, sizeof(flags_shadow));
    dbg_draw_kv_col(m,
                    state_x,
                    state_panel_y + 32 + DBG_LINE_H * 7,
                    state_value_x,
                    "FLAGS'",
                    flags_shadow,
                    dbg_value_color(prev && ((prev->af_shadow & 0x00FFu) != (m->cpu.af_.uint16_value & 0x00FFu))));

    dbg_fill_rect(m->dbg.renderer, 24, state_panel_y + 152, 236, 1, DBG_COL_BORDER);
    dbg_draw_text(m, state_x, state_panel_y + 160, "STACK", DBG_COL_DIM);
    dbg_draw_text(m, state_x + 56, state_panel_y + 160, stack_preview, DBG_COL_WARN);

    dbg_fill_rect(m->dbg.renderer, 12, shortcuts_y, 260, 64, DBG_COL_PANEL);
    dbg_draw_rect(m->dbg.renderer, 12, shortcuts_y, 260, 64, DBG_COL_BORDER);
    dbg_draw_text(m, 28, shortcuts_y + 16, "SHORTCUTS", DBG_COL_ACCENT);
    dbg_draw_text(m, 28, shortcuts_y + 32, "SPC RUN  S/F6 STP  SF7 OVR", DBG_COL_WARN);
    dbg_draw_text(m, 28, shortcuts_y + 32 + DBG_LINE_H, "F7 FRM  F8 CUR  F9 BP SF6<", DBG_COL_WARN);

    dbg_draw_text(m, 430, shortcuts_y + 16, mem_summary, DBG_COL_DIM);

    dbg_render_disassembly(m, 284, top_y, 604, 214);
    dbg_render_memory(m, 284, 266, 604, 242);
}

void debug_init(struct Smaky6 *m)
{
    m->dbg.visible = 0;
    m->dbg.paused = 0;
    m->dbg.stepping = 0;
    m->dbg.step_instruction_pending = 0;
    m->dbg.step_frame_pending = 0;
    m->dbg.trace    = 0;
    m->dbg.trace_flow = 0;
    m->dbg.flow_budget = 0;
    m->dbg.last_flow_pc = 0xFFFF;
    m->dbg.flow_spin_count = 0;
    m->dbg.last_pc  = 0xFFFF;
    m->dbg.last_io19_pc = 0xFFFF;
    m->dbg.last_io19_data = 0xFF;
    m->dbg.io19_repeat_count = 0;
    m->dbg.last_run_tstates = 0;
    m->dbg.frame_counter = 0;
    m->dbg.disasm_cursor = 0;
    m->dbg.run_to_cursor_addr = 0;
    m->dbg.breakpoint_resume_pc = 0;
    m->dbg.breakpoint_count = 0;
    m->dbg.run_to_cursor_active = 0;
    m->dbg.step_over_active = 0;
    m->dbg.breakpoint_resume_armed = 0;
    m->dbg.mem_base = 0;
    m->dbg.mem_cursor = 0;
    m->dbg.watch_addrs[0] = 0x457Eu;
    m->dbg.watch_addrs[1] = 0x4580u;
    m->dbg.watch_addrs[2] = 0x45C0u;
    m->dbg.mem_view_octal = 0;
    m->dbg.mem_edit_high_nibble = 1;
    m->dbg.mem_jump_active = 0;
    m->dbg.watch_edit_active = 0;
    m->dbg.watch_selected = 0;
    m->dbg.mem_jump_len = 0;
    m->dbg.stop_reason = DBG_STOP_NONE;
    m->dbg.last_stop_valid = 0;
    m->dbg.prev_stop_valid = 0;
    m->dbg.mem_jump_buf[0] = '\0';
    m->dbg.window = NULL;
    m->dbg.renderer = NULL;
    m->dbg.window_id = 0;
}

void debug_fini(struct Smaky6 *m)
{
    dbg_close_window(m);
}

int debug_is_visible(struct Smaky6 *m) { return m->dbg.visible; }
int debug_is_paused(struct Smaky6 *m) { return m->dbg.paused; }

void debug_request_step_instruction(struct Smaky6 *m)
{
    if (dbg_has_breakpoint(m, (uint16_t)Z80_PC(m->cpu))) {
        dbg_arm_breakpoint_resume(m, (uint16_t)Z80_PC(m->cpu));
    }
    m->dbg.run_to_cursor_active = 0;
    m->dbg.step_over_active = 0;
    m->dbg.paused = 1;
    m->dbg.stepping = 1;
    m->dbg.step_instruction_pending = 1;
}

void debug_request_step_frame(struct Smaky6 *m)
{
    if (dbg_has_breakpoint(m, (uint16_t)Z80_PC(m->cpu))) {
        dbg_arm_breakpoint_resume(m, (uint16_t)Z80_PC(m->cpu));
    }
    m->dbg.run_to_cursor_active = 0;
    m->dbg.step_over_active = 0;
    m->dbg.paused = 1;
    m->dbg.stepping = 1;
    m->dbg.step_frame_pending = 1;
}

int debug_stop_conditions_active(struct Smaky6 *m)
{
    return m->dbg.run_to_cursor_active || m->dbg.breakpoint_count > 0;
}

int debug_maybe_pause_on_pc(struct Smaky6 *m, uint16_t pc)
{
    int hit_breakpoint;
    int hit_cursor;
    int hit_step_over;

    if (m->dbg.breakpoint_resume_armed && pc == m->dbg.breakpoint_resume_pc) {
        m->dbg.breakpoint_resume_armed = 0;
        return 0;
    }

    hit_breakpoint = dbg_has_breakpoint(m, pc);
    hit_cursor = m->dbg.run_to_cursor_active && pc == m->dbg.run_to_cursor_addr;
    hit_step_over = hit_cursor && m->dbg.step_over_active;
    if (!hit_breakpoint && !hit_cursor) {
        return 0;
    }

    m->dbg.paused = 1;
    m->dbg.stepping = 0;
    m->dbg.step_instruction_pending = 0;
    m->dbg.step_frame_pending = 0;
    m->dbg.run_to_cursor_active = 0;
    m->dbg.step_over_active = 0;
    m->dbg.disasm_cursor = pc;
    dbg_record_stop(m,
                    hit_cursor && hit_breakpoint
                        ? (hit_step_over ? DBG_STOP_BREAKPOINT_AND_STEP_OVER
                                         : DBG_STOP_BREAKPOINT_AND_CURSOR)
                        : (hit_cursor ? (hit_step_over ? DBG_STOP_STEP_OVER : DBG_STOP_RUN_TO_CURSOR)
                                      : DBG_STOP_BREAKPOINT));
    if (hit_cursor && hit_breakpoint) {
        fprintf(stderr,
                hit_step_over ? "debug: step over hit breakpoint at %04X\n"
                              : "debug: run-to-cursor hit breakpoint at %04X\n",
                (unsigned)pc);
    } else if (hit_cursor) {
        fprintf(stderr,
                hit_step_over ? "debug: step over stopped at %04X\n"
                              : "debug: run-to-cursor stopped at %04X\n",
                (unsigned)pc);
    } else {
        fprintf(stderr, "debug: breakpoint hit at %04X\n", (unsigned)pc);
    }
    return 1;
}

int debug_consume_step_instruction(struct Smaky6 *m)
{
    int pending = m->dbg.step_instruction_pending;
    m->dbg.step_instruction_pending = 0;
    return pending;
}

int debug_consume_step_frame(struct Smaky6 *m)
{
    int pending = m->dbg.step_frame_pending;
    m->dbg.step_frame_pending = 0;
    return pending;
}

void debug_note_instruction_run(struct Smaky6 *m, uint32_t tstates)
{
    m->dbg.last_run_tstates = tstates;
    if ((!m->dbg.paused || m->dbg.stepping) && !m->dbg.run_to_cursor_active) {
        dbg_sync_disasm_cursor(m);
    }
    if (m->dbg.stepping && m->dbg.paused) {
        dbg_record_stop(m, DBG_STOP_STEP_INSTRUCTION);
    }
}

void debug_note_frame_run(struct Smaky6 *m, uint32_t tstates)
{
    m->dbg.last_run_tstates = tstates;
    m->dbg.frame_counter++;
    if ((!m->dbg.paused || m->dbg.stepping) && !m->dbg.run_to_cursor_active) {
        dbg_sync_disasm_cursor(m);
    }
    if (m->dbg.stepping && m->dbg.paused) {
        dbg_record_stop(m, DBG_STOP_STEP_FRAME);
    }
}

int debug_handle_event(struct Smaky6 *m, const SDL_Event *ev)
{
    if (!dbg_event_targets_window(m, ev)) {
        return 0;
    }

    switch (ev->type) {
    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_CLOSE) {
            dbg_close_window(m);
        }
        return 1;
    case SDL_KEYDOWN:
        if (m->dbg.mem_jump_active) {
            int hex = dbg_hex_value(ev->key.keysym.sym);

            if (ev->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                dbg_cancel_hex_input(m);
                return 1;
            }
            if (ev->key.keysym.scancode == SDL_SCANCODE_RETURN ||
                ev->key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                dbg_apply_hex_input(m);
                return 1;
            }
            if (ev->key.keysym.scancode == SDL_SCANCODE_BACKSPACE && m->dbg.mem_jump_len > 0) {
                m->dbg.mem_jump_len--;
                m->dbg.mem_jump_buf[m->dbg.mem_jump_len] = '\0';
                return 1;
            }
            if (hex >= 0 && m->dbg.mem_jump_len < 4) {
                m->dbg.mem_jump_buf[m->dbg.mem_jump_len++] = (char)toupper((unsigned char)ev->key.keysym.sym);
                m->dbg.mem_jump_buf[m->dbg.mem_jump_len] = '\0';
                if (m->dbg.mem_jump_len == 4) {
                    dbg_apply_hex_input(m);
                }
            }
            return 1;
        }

        switch (ev->key.keysym.scancode) {
        case SDL_SCANCODE_F12:
            dbg_close_window(m);
            return 1;
        case SDL_SCANCODE_SPACE:
            if (dbg_has_breakpoint(m, (uint16_t)Z80_PC(m->cpu)) && m->dbg.paused) {
                dbg_arm_breakpoint_resume(m, (uint16_t)Z80_PC(m->cpu));
            }
            m->dbg.run_to_cursor_active = 0;
            m->dbg.step_over_active = 0;
            m->dbg.paused = !m->dbg.paused;
            m->dbg.stepping = m->dbg.paused;
            if (!m->dbg.paused) {
                m->dbg.step_instruction_pending = 0;
                m->dbg.step_frame_pending = 0;
                m->dbg.stop_reason = DBG_STOP_NONE;
            } else {
                dbg_record_stop(m, DBG_STOP_MANUAL_PAUSE);
            }
            return 1;
        case SDL_SCANCODE_F8:
            dbg_begin_run_to_cursor(m);
            return 1;
        case SDL_SCANCODE_S:
            debug_request_step_instruction(m);
            return 1;
        case SDL_SCANCODE_F6:
            if (ev->key.keysym.mod & KMOD_SHIFT) {
                dbg_move_disasm_cursor(m, -1);
            } else {
                debug_request_step_instruction(m);
            }
            return 1;
        case SDL_SCANCODE_F7:
            if (ev->key.keysym.mod & KMOD_SHIFT) {
                if (!dbg_begin_step_over(m)) {
                    debug_request_step_instruction(m);
                }
            } else {
                debug_request_step_frame(m);
            }
            return 1;
        case SDL_SCANCODE_F9:
            dbg_toggle_breakpoint(m, m->dbg.disasm_cursor);
            return 1;
        case SDL_SCANCODE_LEFT:
            dbg_move_memory_cursor(m, -1);
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_RIGHT:
            dbg_move_memory_cursor(m, 1);
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_UP:
            if (ev->key.keysym.mod & KMOD_SHIFT) {
                dbg_move_disasm_cursor(m, -1);
                return 1;
            }
            dbg_move_memory_cursor(m, -DBG_MEM_COLS);
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_DOWN:
            if (ev->key.keysym.mod & KMOD_SHIFT) {
                dbg_move_disasm_cursor(m, 1);
                return 1;
            }
            dbg_move_memory_cursor(m, DBG_MEM_COLS);
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_PAGEUP:
            m->dbg.mem_base = (uint16_t)(m->dbg.mem_base - DBG_MEM_PAGE_SIZE);
            m->dbg.mem_cursor = (uint16_t)(m->dbg.mem_base + (m->dbg.mem_cursor & 0x00FFu));
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_PAGEDOWN:
            m->dbg.mem_base = (uint16_t)(m->dbg.mem_base + DBG_MEM_PAGE_SIZE);
            m->dbg.mem_cursor = (uint16_t)(m->dbg.mem_base + (m->dbg.mem_cursor & 0x00FFu));
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_HOME:
            m->dbg.mem_cursor = (uint16_t)(m->dbg.mem_cursor & 0xFFF0u);
            dbg_sync_memory_to_cursor(m);
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_END:
            m->dbg.mem_cursor = (uint16_t)((m->dbg.mem_cursor & 0xFFF0u) | 0x000Fu);
            dbg_sync_memory_to_cursor(m);
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_TAB:
            if (ev->key.keysym.mod & KMOD_SHIFT) {
                m->dbg.watch_selected = (uint8_t)((m->dbg.watch_selected + 2u) % 3u);
            } else {
                m->dbg.watch_selected = (uint8_t)((m->dbg.watch_selected + 1u) % 3u);
            }
            return 1;
        case SDL_SCANCODE_G:
            m->dbg.mem_jump_active = 1;
            m->dbg.watch_edit_active = 0;
            m->dbg.mem_jump_len = 0;
            m->dbg.mem_jump_buf[0] = '\0';
            return 1;
        case SDL_SCANCODE_O:
            m->dbg.mem_view_octal = m->dbg.mem_view_octal ? 0 : 1;
            return 1;
        case SDL_SCANCODE_P:
            m->dbg.mem_cursor = (uint16_t)Z80_PC(m->cpu);
            dbg_sync_memory_to_cursor(m);
            m->dbg.mem_edit_high_nibble = 1;
            return 1;
        case SDL_SCANCODE_W:
            m->dbg.mem_jump_active = 1;
            m->dbg.watch_edit_active = 1;
            m->dbg.mem_jump_len = 0;
            m->dbg.mem_jump_buf[0] = '\0';
            return 1;
        default: {
            if ((ev->key.keysym.mod & KMOD_CTRL) && ev->key.keysym.scancode == SDL_SCANCODE_A) {
                dbg_set_memory_view(m, MEM_ALPHA_BASE);
                return 1;
            }
            if ((ev->key.keysym.mod & KMOD_CTRL) && ev->key.keysym.scancode == SDL_SCANCODE_V) {
                dbg_set_memory_view(m, MEM_GFX_BASE);
                return 1;
            }
            int hex = dbg_hex_value(ev->key.keysym.sym);

            if (hex >= 0) {
                dbg_edit_memory_nibble(m, hex);
            }
            return 1;
        }
        }
    case SDL_KEYUP:
    case SDL_TEXTINPUT:
        return 1;
    default:
        return 0;
    }
}

void debug_render(struct Smaky6 *m)
{
    if (!m->dbg.visible || !m->dbg.renderer) {
        return;
    }

    dbg_fill_rect(m->dbg.renderer, 0, 0, DBG_WIN_W, DBG_WIN_H, DBG_COL_BG);
    dbg_render_registers(m);
    SDL_RenderPresent(m->dbg.renderer);
}

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

            if (ret0 == 0x18EB &&
                (ret1 == 0x5600 || ret1 == 0x5602 || ret1 == 0x6000))
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
             pc == 0x0568 || pc == 0x5600 || pc == 0x5602 || pc == 0x6000 ||
             pc == 0x6002 || pc == 0x6007 || pc == 0x600A || pc == 0x600E ||
             pc == 0x601F || pc == 0xF300 || pc == 0x3031)) {
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
             pc == 0x1F79 || pc == 0x1F86 || pc == 0x1F8A || pc == 0x1F97 ||
             pc == 0x1F9E ||
             pc == 0x1FA3 ||
             pc == 0x1FAE || pc == 0x1FC3 || pc == 0x1FC6 || pc == 0x1FD4 ||
             pc == 0x1FD7 || pc == 0x1FDC || pc == 0x1FF1 || pc == 0x1FF7 ||
             pc == 0x2067 || pc == 0x206A || pc == 0x2070 || pc == 0x2073 ||
             pc == 0x207B || pc == 0x2081 || pc == 0x2084 || pc == 0x2089 ||
             pc == 0x2186 || pc == 0x2187 ||
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
                      "[flow-tail2] pc=%04X af=%04X bc=%04X de=%04X hl=%04X ix=%04X ix0A=%04X ix0C=%04X ix10=%02X ix13_14=%04X sp=%04X top=%04X next=%04X next2=%04X next3=%04X 2B80=%04X 2B82=%04X 2B84=%04X 2B86=%04X 2B89=%04X 2B91=%04X 2B94=%04X 2B9D=%04X 2BC5=%04X 2BC7=%04X 2BDC=%04X 2BD1=%04X 455C=%04X 4566=%04X 4568=%04X tail2=%d\n",
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
                      (unsigned)((uint16_t)m->bus[0x2B91u] |
                          ((uint16_t)m->bus[0x2B92u] << 8)),
                      (unsigned)((uint16_t)m->bus[0x2B94u] |
                          ((uint16_t)m->bus[0x2B95u] << 8)),
                      (unsigned)((uint16_t)m->bus[0x2B9Du] |
                          ((uint16_t)m->bus[0x2B9Eu] << 8)),
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
    if (m->dbg.visible) {
        dbg_close_window(m);
        fprintf(stderr, "debug: window hidden\n");
        return;
    }

    if (dbg_open_window(m) == 0) {
        fprintf(stderr, "debug: window shown (F12 toggle, Space pause, S/F6 step, F7 frame, F8 run, F9 breakpoint)\n");
        debug_render(m);
    }
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

int debug_is_stepping(struct Smaky6 *m)
{
    return m->dbg.stepping || m->dbg.step_instruction_pending || m->dbg.step_frame_pending;
}

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
