// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* keyboard.c – strict CLA-centric Smaky 6 keyboard baseline */
#include "machine_internal.h"
#include "machine.h"
#include "keyboard.h"
#include <string.h>

typedef enum {
    S471_LAYER_NORMAL = 0,
    S471_LAYER_SHIFT  = 1,
    S471_LAYER_FNCT   = 2,
    S471_LAYER_CAPS   = 3,
} S471Layer;

enum {
    MATRIX_POS_COUNT = 64,
    MATRIX_POS_NONE = 0xFF,
    PENDING_ORDINARY_CAP = 8,
};

typedef uint8_t SmakyMatrixPosition;

typedef struct {
    uint16_t unicode;
    uint8_t code;
} AccentEntry;

static const uint8_t S471_TABLE[4][MATRIX_POS_COUNT] = {
    {
        0x06, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x12, 0x15, 0x30, 0x5C, 0x08, 0x00,
        0x09, 0x71, 0x77, 0x65, 0x72, 0x74, 0x7A, 0x75,
        0x69, 0x6F, 0x70, 0x0F, 0x5B, 0x5D, 0x04, 0x00,
        0x1E, 0x61, 0x73, 0x64, 0x66, 0x67, 0x68, 0x6A,
        0x6B, 0x6C, 0x1C, 0x1B, 0x27, 0x0D, 0x00, 0x00,
        0x20, 0x79, 0x78, 0x63, 0x76, 0x62, 0x6E, 0x6D,
        0x2C, 0x2E, 0x2D, 0x00, 0x0E, 0x1F, 0x00, 0x00,
    },
    {
        0x06, 0x24, 0x22, 0x2A, 0x25, 0x26, 0x28, 0x29,
        0x3F, 0x40, 0x1D, 0x16, 0x23, 0x60, 0x7F, 0x00,
        0x0B, 0x51, 0x57, 0x45, 0x52, 0x54, 0x5A, 0x55,
        0x49, 0x4F, 0x50, 0x2F, 0x3C, 0x3E, 0x05, 0x00,
        0x1E, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A,
        0x4B, 0x4C, 0x2B, 0x3D, 0x21, 0x0C, 0x00, 0x00,
        0x20, 0x59, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D,
        0x3B, 0x3A, 0x5F, 0x00, 0x0E, 0x1F, 0x00, 0x00,
    },
    {
        0x1B, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x00, 0x5E, 0x10, 0x7E, 0x01, 0x00,
        0x03, 0x11, 0x17, 0x05, 0x12, 0x14, 0x1A, 0x15,
        0x09, 0x0F, 0x10, 0x00, 0x7B, 0x7D, 0x07, 0x00,
        0x1E, 0x01, 0x13, 0x04, 0x06, 0x07, 0x08, 0x0A,
        0x0B, 0x0C, 0x00, 0x00, 0x7C, 0x0A, 0x00, 0x00,
        0x02, 0x19, 0x18, 0x03, 0x16, 0x02, 0x0E, 0x0D,
        0x00, 0x00, 0x00, 0x00, 0x0E, 0x1F, 0x00, 0x00,
    },
    {
        0x06, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x12, 0x15, 0x30, 0x5C, 0x08, 0x00,
        0x09, 0x51, 0x57, 0x45, 0x52, 0x54, 0x5A, 0x55,
        0x49, 0x4F, 0x50, 0x0F, 0x5B, 0x5D, 0x04, 0x00,
        0x1E, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A,
        0x4B, 0x4C, 0x1C, 0x1B, 0x27, 0x0D, 0x00, 0x00,
        0x20, 0x59, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D,
        0x2C, 0x2E, 0x2D, 0x00, 0x0E, 0x1F, 0x00, 0x00,
    },
};

static const struct {
    smemu6_scancode scan;
    SmakyMatrixPosition position;
} HOST_MATRIX_KEYS[] = {
    { SMEMU6_SCAN_ESCAPE,       0 },
    { SMEMU6_SCAN_1,            1 },
    { SMEMU6_SCAN_2,            2 },
    { SMEMU6_SCAN_3,            3 },
    { SMEMU6_SCAN_4,            4 },
    { SMEMU6_SCAN_5,            5 },
    { SMEMU6_SCAN_6,            6 },
    { SMEMU6_SCAN_7,            7 },
    { SMEMU6_SCAN_8,            8 },
    { SMEMU6_SCAN_9,            9 },
    { SMEMU6_SCAN_0,           10 },
    { SMEMU6_SCAN_BACKSLASH,   13 },
    { SMEMU6_SCAN_BACKSPACE,   14 },
    { SMEMU6_SCAN_TAB,         16 },
    { SMEMU6_SCAN_Q,           17 },
    { SMEMU6_SCAN_W,           18 },
    { SMEMU6_SCAN_E,           19 },
    { SMEMU6_SCAN_R,           20 },
    { SMEMU6_SCAN_T,           21 },
    { SMEMU6_SCAN_Z,           22 },
    { SMEMU6_SCAN_U,           23 },
    { SMEMU6_SCAN_I,           24 },
    { SMEMU6_SCAN_O,           25 },
    { SMEMU6_SCAN_P,           26 },
    { SMEMU6_SCAN_LEFTBRACKET, 28 },
    { SMEMU6_SCAN_RIGHTBRACKET,29 },
    { SMEMU6_SCAN_END,         30 },
    { SMEMU6_SCAN_LCTRL,       32 },
    { SMEMU6_SCAN_RCTRL,       32 },
    { SMEMU6_SCAN_A,           33 },
    { SMEMU6_SCAN_S,           34 },
    { SMEMU6_SCAN_D,           35 },
    { SMEMU6_SCAN_F,           36 },
    { SMEMU6_SCAN_G,           37 },
    { SMEMU6_SCAN_H,           38 },
    { SMEMU6_SCAN_J,           39 },
    { SMEMU6_SCAN_K,           40 },
    { SMEMU6_SCAN_L,           41 },
    { SMEMU6_SCAN_SEMICOLON,   42 },
    { SMEMU6_SCAN_APOSTROPHE,  43 },
    { SMEMU6_SCAN_NONUSBACKSLASH, 44 },
    { SMEMU6_SCAN_RETURN,      45 },
    { SMEMU6_SCAN_SPACE,       48 },
    { SMEMU6_SCAN_Y,           49 },
    { SMEMU6_SCAN_X,           50 },
    { SMEMU6_SCAN_C,           51 },
    { SMEMU6_SCAN_V,           52 },
    { SMEMU6_SCAN_B,           53 },
    { SMEMU6_SCAN_N,           54 },
    { SMEMU6_SCAN_M,           55 },
    { SMEMU6_SCAN_COMMA,       56 },
    { SMEMU6_SCAN_PERIOD,      57 },
    { SMEMU6_SCAN_MINUS,       58 },
    { SMEMU6_SCAN_F9,          61 },
};

static const struct {
    smemu6_scancode scan;
    uint8_t bit;
} FUNCTION_KEYS[] = {
    { SMEMU6_SCAN_F1, 0x40 }, /* CURSOR */
    { SMEMU6_SCAN_F2, 0x20 }, /* COPY */
    { SMEMU6_SCAN_F3, 0x10 }, /* KILL */
    { SMEMU6_SCAN_F4, 0x08 }, /* PROGRA */
    { SMEMU6_SCAN_F5, 0x04 }, /* SHOW */
    { SMEMU6_SCAN_F6, 0x02 }, /* SEARCH */
    { SMEMU6_SCAN_F7, 0x01 }, /* CHANGE */
};

static const AccentEntry ACCENT_TABLE[] = {
    { 0x00FCu, 0x0Fu }, /* u umlaut: u */
    { 0x00DCu, 0x0Fu }, /* U umlaut: U */
    { 0x00E0u, 0x10u }, /* a grave: a */
    { 0x00C0u, 0x10u }, /* A grave: A */
    { 0x00E2u, 0x11u }, /* a circumflex: a */
    { 0x00C2u, 0x11u }, /* A circumflex: A */
    { 0x00E9u, 0x12u }, /* e acute: e */
    { 0x00C9u, 0x12u }, /* E acute: E */
    { 0x00E8u, 0x13u }, /* e grave: e */
    { 0x00C8u, 0x13u }, /* E grave: E */
    { 0x00EBu, 0x14u }, /* e umlaut: e */
    { 0x00CBu, 0x14u }, /* E umlaut: E */
    { 0x00EAu, 0x15u }, /* e circumflex: e */
    { 0x00CAu, 0x15u }, /* E circumflex: E */
    { 0x00EFu, 0x16u }, /* i umlaut: i */
    { 0x00CFu, 0x16u }, /* I umlaut: I */
    { 0x00EEu, 0x17u }, /* i circumflex: i */
    { 0x00CEu, 0x17u }, /* I circumflex: I */
    { 0x00F4u, 0x18u }, /* o circumflex: o */
    { 0x00D4u, 0x18u }, /* O circumflex: O */
    { 0x00F9u, 0x19u }, /* u grave: u */
    { 0x00D9u, 0x19u }, /* U grave: U */
    { 0x00FBu, 0x1Au }, /* u circumflex: u */
    { 0x00DBu, 0x1Au }, /* U circumflex: U */
    { 0x00E4u, 0x1Bu }, /* a umlaut: a */
    { 0x00C4u, 0x1Bu }, /* A umlaut: A */
    { 0x00F6u, 0x1Cu }, /* o umlaut: o */
    { 0x00D6u, 0x1Cu }, /* O umlaut: O */
    { 0x00E7u, 0x1Du }, /* c cedilla: c */
    { 0x00C7u, 0x1Du }, /* C cedilla: C */
    { 0x00ABu, 0x1Eu }, /* left guillemet */
    { 0x00BBu, 0x1Fu }, /* right guillemet */
};

static void clear_ordinary_key(struct Smaky6 *m);

/* Recompute the effective function-key state from keyboard, cursor-alias, and
 * mouse-latch sources, keeping the consumed mask in sync. */
static void refresh_function_bits(struct Smaky6 *m)
{
    uint8_t old_bits = m->kbd.fonct_bits;
    uint8_t keyboard_bits = (uint8_t)(m->kbd.fonct_keyboard_bits |
                                      (m->kbd.cursor_alias_sources ? 0x40u : 0x00u));
    m->kbd.fonct_bits = (uint8_t)((keyboard_bits |
                                   m->kbd.fonct_mouse_bits) & 0x7Fu);
    m->kbd.fonct_consumed_bits &= m->kbd.fonct_bits;
    if (m->dbg.trace_kbd && old_bits != m->kbd.fonct_bits) {
        fprintf(stderr, "[kbd] fonct_bits changed: 0x%02X -> 0x%02X (kbd=0x%02X alias=0x%02X mouse=0x%02X)\n",
                (unsigned)old_bits,
                (unsigned)m->kbd.fonct_bits,
                (unsigned)m->kbd.fonct_keyboard_bits,
                (unsigned)(m->kbd.cursor_alias_sources ? 0x40u : 0x00u),
                (unsigned)m->kbd.fonct_mouse_bits);
    }
}

/* Return only the function bits that are still visible to the guest after any
 * one-shot keyboard consumption has been applied. */
static uint8_t visible_function_bits(const struct Smaky6 *m)
{
    return (uint8_t)(m->kbd.fonct_bits & (uint8_t)~m->kbd.fonct_consumed_bits & 0x7Fu);
}

/* Drop all host-owned key state after focus loss. */
void keyboard_cancel_host_input(struct Smaky6 *m)
{
    keyboard_clear_all_function_bits(m);
    m->kbd.shift_pressed = 0;
    m->kbd.host_text_down_count = 0;
    memset(m->kbd.host_text_down, 0, sizeof(m->kbd.host_text_down));
    m->kbd.pending_ordinary_head = 0;
    m->kbd.pending_ordinary_len = 0;
    m->bus[0x4558u] = 0;
    m->bus[0x4577u] = 0;

    clear_ordinary_key(m);
}

/* Clear every function-key source and recompute the effective visible bitmask. */
void keyboard_clear_all_function_bits(struct Smaky6 *m)
{
    m->kbd.fonct_consumed_bits = 0;
    m->kbd.fonct_keyboard_bits = 0;
    m->kbd.cursor_alias_sources = 0;
    m->kbd.fonct_mouse_bits = 0;
    refresh_function_bits(m);
}

/* Replace the mouse-owned function-key latch state used by the SDL status bar. */
void keyboard_set_mouse_function_bits(struct Smaky6 *m, uint8_t bits)
{
    m->kbd.fonct_mouse_bits = bits & 0x7Fu;
    refresh_function_bits(m);
}

/* Mark function bits as consumed for one-shot host-key delivery without
 * disturbing persistent mouse latches. */
void keyboard_acknowledge_function_bits(struct Smaky6 *m, uint8_t mask)
{
    uint8_t fkey_mask = mask & 0x7Fu;

    if (fkey_mask == 0x00)
        return;

    if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd] FKEY ACK mask=%02X (held state unchanged) fonct_kb=%02X fonct=%02X\n",
                (unsigned)fkey_mask,
                (unsigned)m->kbd.fonct_keyboard_bits,
                (unsigned)m->kbd.fonct_bits);
    }
}

static int matrix_position_uses_text_input(SmakyMatrixPosition position)
{
    switch (position) {
    case 0:  /* ESC */
    case 14: /* BACKSPACE */
    case 16: /* TAB */
    case 30: /* END / special key */
    case 32: /* CTRL */
    case 45: /* RETURN */
    case 61: /* F9 */
        return 0;
    default:
        return 1;
    }
}

/* Report whether one SDL scancode is expected to also produce SDL text input
 * for the printable compatibility path. */
static int is_host_text_scancode(smemu6_scancode scan)
{
    for (size_t i = 0; i < sizeof(HOST_MATRIX_KEYS) / sizeof(HOST_MATRIX_KEYS[0]); i++) {
        if (HOST_MATRIX_KEYS[i].scan == scan)
            return matrix_position_uses_text_input(HOST_MATRIX_KEYS[i].position);
    }

    return 0;
}

/* Track whether a printable host key is currently held so text input can be
 * matched back to a fresh keydown instead of stale repeats. */
static void set_host_text_scancode_down(struct Smaky6 *m, smemu6_scancode scan, int down)
{
    if (scan <= SMEMU6_SCAN_UNKNOWN || scan >= SMEMU6_NUM_SCANCODES)
        return;
    if (!is_host_text_scancode(scan))
        return;

    if (down) {
        if (!m->kbd.host_text_down[scan]) {
            m->kbd.host_text_down[scan] = 1;
            m->kbd.host_text_down_count++;
        }
    } else if (m->kbd.host_text_down[scan]) {
        m->kbd.host_text_down[scan] = 0;
        if (m->kbd.host_text_down_count > 0)
            m->kbd.host_text_down_count--;
    }
}

/* Claim one pending printable host scancode that has not yet been matched to
 * an SDL_TEXTINPUT event. */
static smemu6_scancode claim_pending_host_text_scancode(struct Smaky6 *m)
{
    for (int scan = SMEMU6_SCAN_UNKNOWN + 1; scan < SMEMU6_NUM_SCANCODES; scan++) {
        if (m->kbd.host_text_down[scan] == 1)
            return (smemu6_scancode)scan;
    }

    return SMEMU6_SCAN_UNKNOWN;
}

/* Decode one text codepoint into the 7-bit Smaky character space,
 * including the small accented-character compatibility table.
 * The SDL front-end decodes its UTF-8 text event to a Unicode codepoint; this
 * maps it (single printable char or composed accent) into the Smaky space. */
static int decode_text_input_code_point(uint32_t codepoint, uint8_t *code_out)
{
    if (codepoint == 0u)
        return 0;

    if (codepoint <= 0x7Eu) {
        if (codepoint >= 0x20u) {
            *code_out = (uint8_t)codepoint;
            return 1;
        }
        return 0;
    }

    /* Composed accented character: look it up in the compatibility table. */
    for (size_t i = 0; i < sizeof(ACCENT_TABLE) / sizeof(ACCENT_TABLE[0]); i++) {
        if (ACCENT_TABLE[i].unicode == codepoint) {
            *code_out = ACCENT_TABLE[i].code;
            return 1;
        }
    }

    return 0;
}

/* Report whether any function-key source is currently active. */
static int fnct_layer_active(const struct Smaky6 *m)
{
    return m->kbd.fonct_bits != 0;
}

/* Map letter key symbols to their logical Smaky matrix positions so host
 * keyboard layouts can still target the intended letter keys. */
static SmakyMatrixPosition lookup_letter_matrix_position(smemu6_keycode sym)
{
    switch (sym) {
    case SMEMU6_KD_q: return 17;
    case SMEMU6_KD_w: return 18;
    case SMEMU6_KD_e: return 19;
    case SMEMU6_KD_r: return 20;
    case SMEMU6_KD_t: return 21;
    case SMEMU6_KD_z: return 22;
    case SMEMU6_KD_u: return 23;
    case SMEMU6_KD_i: return 24;
    case SMEMU6_KD_o: return 25;
    case SMEMU6_KD_p: return 26;
    case SMEMU6_KD_a: return 33;
    case SMEMU6_KD_s: return 34;
    case SMEMU6_KD_d: return 35;
    case SMEMU6_KD_f: return 36;
    case SMEMU6_KD_g: return 37;
    case SMEMU6_KD_h: return 38;
    case SMEMU6_KD_j: return 39;
    case SMEMU6_KD_k: return 40;
    case SMEMU6_KD_l: return 41;
    case SMEMU6_KD_y: return 49;
    case SMEMU6_KD_x: return 50;
    case SMEMU6_KD_c: return 51;
    case SMEMU6_KD_v: return 52;
    case SMEMU6_KD_b: return 53;
    case SMEMU6_KD_n: return 54;
    case SMEMU6_KD_m: return 55;
    default:
        return MATRIX_POS_NONE;
    }
}

/* Resolve an SDL key event to one physical Smaky matrix position, optionally
 * preferring logical letter mapping over the host scancode. */
static SmakyMatrixPosition lookup_matrix_position(smemu6_scancode scan, smemu6_keycode sym, int prefer_logical_letters)
{
    if (prefer_logical_letters) {
        SmakyMatrixPosition logical_position = lookup_letter_matrix_position(sym);
        if (logical_position != MATRIX_POS_NONE)
            return logical_position;
    }

    for (int i = 0; i < (int)(sizeof(HOST_MATRIX_KEYS) / sizeof(HOST_MATRIX_KEYS[0])); i++) {
        if (HOST_MATRIX_KEYS[i].scan == scan)
            return HOST_MATRIX_KEYS[i].position;
    }

    return MATRIX_POS_NONE;
}

/* Select the active S471 lookup layer from the current modifier state. */
static S471Layer current_layer(const struct Smaky6 *m)
{
    if (m->kbd.shift_pressed)
        return S471_LAYER_SHIFT;
    if (m->kbd.caps_lock_active)
        return S471_LAYER_CAPS;
    return S471_LAYER_NORMAL;
}

/* Drop the currently latched ordinary matrix key while leaving modifiers intact. */
static void clear_ordinary_key(struct Smaky6 *m)
{
    m->kbd.key_code = 0x00;
    m->kbd.found = 0;
    m->kbd.physically_held = 0;
    m->kbd.cla_seen_current = 0;
    m->kbd.release_after_reassert = 0;
    m->kbd.release_after_buffer_commit = 0;
    m->kbd.regular_prefix_pending = 0;
    m->kbd.active_scancode = SMEMU6_SCAN_UNKNOWN;
    m->kbd.active_matrix_position = MATRIX_POS_NONE;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;
}

static int ordinary_latch_idle(const struct Smaky6 *m)
{
    return !m->kbd.found && !m->kbd.physically_held && !m->kbd.reassert_pending;
}

/* Release the physical hold for the current ordinary key without clearing a pending latch.
 * Real hardware keeps FOUND set until CLA reads it; release only stops future reassertion. */
static void release_ordinary_key(struct Smaky6 *m)
{
    int pending_delivery = m->kbd.found || m->kbd.reassert_pending;
    int hold_until_commit = pending_delivery && !m->kbd.cla_seen_current;
    int seen_by_cla = m->kbd.cla_seen_current;

    m->kbd.physically_held = hold_until_commit;
    /* If the key was released before its in-flight latch reached the CLA
     * reader, the read must deliver it once and must NOT rearm reassertion:
     * the key is no longer on the matrix. */
    m->kbd.release_after_reassert = hold_until_commit;
    m->kbd.release_after_buffer_commit = hold_until_commit;
    m->kbd.active_scancode = SMEMU6_SCAN_UNKNOWN;
    m->kbd.active_matrix_position = MATRIX_POS_NONE;
    if (!hold_until_commit) {
        m->kbd.found = 0;
        m->kbd.reassert_pending = 0;
        m->kbd.reassert_cycles = 0;
    } else if (!m->kbd.reassert_pending) {
        m->kbd.reassert_cycles = 0;
    }

    if (seen_by_cla) {
        m->bus[0x4558u] = 0;
        m->bus[0x4577u] = 0;
    }
}

static void latch_matrix_key(struct Smaky6 *m, smemu6_scancode scan, SmakyMatrixPosition position);
static void latch_matrix_key_code(struct Smaky6 *m, smemu6_scancode scan, SmakyMatrixPosition position, uint8_t key_code);
static void latch_direct_key_code(struct Smaky6 *m, smemu6_scancode scan, uint8_t key_code);
static uint8_t resolve_matrix_code(const struct Smaky6 *m, SmakyMatrixPosition position);

/* Queue one ordinary matrix key until the current CLA-visible latch becomes available. */
static int queue_ordinary_key(struct Smaky6 *m, smemu6_scancode scan, SmakyMatrixPosition position)
{
    for (uint8_t i = 0; i < m->kbd.pending_ordinary_len; i++) {
        uint8_t idx = (uint8_t)((m->kbd.pending_ordinary_head + i) % PENDING_ORDINARY_CAP);
        if (m->kbd.pending_ordinary[idx].scancode == scan)
            return 0;
    }

    if (m->kbd.pending_ordinary_len >= PENDING_ORDINARY_CAP)
        return 0;

    uint8_t idx = (uint8_t)((m->kbd.pending_ordinary_head + m->kbd.pending_ordinary_len) % PENDING_ORDINARY_CAP);
    m->kbd.pending_ordinary[idx].scancode = scan;
    m->kbd.pending_ordinary[idx].matrix_position = position;
    m->kbd.pending_ordinary[idx].key_code = resolve_matrix_code(m, position);
    m->kbd.pending_ordinary[idx].released = 0;
    m->kbd.pending_ordinary_len++;

    if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd] queued scancode=%d pos=%u depth=%u\n",
                (int)scan,
                (unsigned)position,
                (unsigned)m->kbd.pending_ordinary_len);
    }

    return 1;
}

/* Queue one ordinary matrix key with an already-resolved key code override. */
static int queue_ordinary_key_code(struct Smaky6 *m, smemu6_scancode scan,
                                   SmakyMatrixPosition position, uint8_t key_code)
{
    for (uint8_t i = 0; i < m->kbd.pending_ordinary_len; i++) {
        uint8_t idx = (uint8_t)((m->kbd.pending_ordinary_head + i) % PENDING_ORDINARY_CAP);
        if (m->kbd.pending_ordinary[idx].scancode == scan)
            return 0;
    }

    if (m->kbd.pending_ordinary_len >= PENDING_ORDINARY_CAP)
        return 0;

    uint8_t idx = (uint8_t)((m->kbd.pending_ordinary_head + m->kbd.pending_ordinary_len) % PENDING_ORDINARY_CAP);
    m->kbd.pending_ordinary[idx].scancode = scan;
    m->kbd.pending_ordinary[idx].matrix_position = position;
    m->kbd.pending_ordinary[idx].key_code = key_code & 0x7Fu;
    m->kbd.pending_ordinary[idx].released = 0;
    m->kbd.pending_ordinary_len++;

    if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd] queued scancode=%d pos=%u code=%02X depth=%u\n",
                (int)scan,
                (unsigned)position,
                (unsigned)(key_code & 0x7Fu),
                (unsigned)m->kbd.pending_ordinary_len);
    }

    return 1;
}

/* Queue one direct text-derived key code that may not correspond to a matrix position. */
static int queue_direct_key_code(struct Smaky6 *m, smemu6_scancode scan, uint8_t key_code)
{
    if (scan != SMEMU6_SCAN_UNKNOWN) {
        for (uint8_t i = 0; i < m->kbd.pending_ordinary_len; i++) {
            uint8_t idx = (uint8_t)((m->kbd.pending_ordinary_head + i) % PENDING_ORDINARY_CAP);
            if (m->kbd.pending_ordinary[idx].scancode == scan)
                return 0;
        }
    }

    if (m->kbd.pending_ordinary_len >= PENDING_ORDINARY_CAP)
        return 0;

    uint8_t idx = (uint8_t)((m->kbd.pending_ordinary_head + m->kbd.pending_ordinary_len) % PENDING_ORDINARY_CAP);
    m->kbd.pending_ordinary[idx].scancode = scan;
    m->kbd.pending_ordinary[idx].matrix_position = MATRIX_POS_NONE;
    m->kbd.pending_ordinary[idx].key_code = key_code & 0x7Fu;
    m->kbd.pending_ordinary[idx].released = (scan == SMEMU6_SCAN_UNKNOWN) ? 1 : 0;
    m->kbd.pending_ordinary_len++;

    if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd] queued text code=%02X depth=%u\n",
                (unsigned)(key_code & 0x7Fu),
                (unsigned)m->kbd.pending_ordinary_len);
    }

    return 1;
}

/* Mark a queued host key as released so the promoted latch can drop after
 * buffer commit instead of pretending the key is still held. */
static void mark_queued_key_released(struct Smaky6 *m, smemu6_scancode scan)
{
    for (uint8_t i = 0; i < m->kbd.pending_ordinary_len; i++) {
        uint8_t idx = (uint8_t)((m->kbd.pending_ordinary_head + i) % PENDING_ORDINARY_CAP);
        if (m->kbd.pending_ordinary[idx].scancode == scan) {
            m->kbd.pending_ordinary[idx].released = 1;
            return;
        }
    }
}

/* Promote the oldest queued ordinary key into the active CLA-visible latch
 * once the current latch is fully idle. */
static int promote_pending_ordinary_key(struct Smaky6 *m)
{
    if (!ordinary_latch_idle(m) || m->kbd.pending_ordinary_len == 0)
        return 0;

    uint8_t idx = m->kbd.pending_ordinary_head;
    smemu6_scancode scan = m->kbd.pending_ordinary[idx].scancode;
    SmakyMatrixPosition position = m->kbd.pending_ordinary[idx].matrix_position;
    uint8_t key_code = m->kbd.pending_ordinary[idx].key_code;
    int released = m->kbd.pending_ordinary[idx].released;

    m->kbd.pending_ordinary_head = (uint8_t)((m->kbd.pending_ordinary_head + 1) % PENDING_ORDINARY_CAP);
    m->kbd.pending_ordinary_len--;

    if (position == MATRIX_POS_NONE)
        latch_direct_key_code(m, scan, key_code);
    else
        latch_matrix_key_code(m, scan, position, key_code);
    if (released) {
        m->kbd.release_after_buffer_commit = 1;
    }

    if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd] promoted scancode=%d pos=%u released=%d remaining=%u\n",
                (int)scan,
                (unsigned)position,
                released,
                (unsigned)m->kbd.pending_ordinary_len);
    }

    return 1;
}

/* Resolve one physical matrix position through the active S471 layer. */
static uint8_t resolve_matrix_code(const struct Smaky6 *m, SmakyMatrixPosition position)
{
    return S471_TABLE[current_layer(m)][position] & 0x7Fu;
}

/* Host keymap selection: modern (default) resolves host text characters
 * through the S471 table so a modern keyboard types the character it shows;
 * position maps each host key to the Smaky key at the same physical spot. */
static int keymap_modern = 1;

/* Select the host keymap. Call before the first text event. */
void keyboard_set_keymap_modern(int modern)
{
    keymap_modern = (modern != 0);
}

/* Find the matrix position whose output on the given layer equals the host
 * character. Returns MATRIX_POS_NONE when the layer has no such key. */
static SmakyMatrixPosition reverse_lookup_matrix_position(S471Layer layer, uint8_t code)
{
    for (int i = 0; i < MATRIX_POS_COUNT; i++)
        if (S471_TABLE[layer][i] == code)
            return (SmakyMatrixPosition)i;
    return MATRIX_POS_NONE;
}

/* Translate host arrow-key aliases into CURSOR+letter chord components. */
static int lookup_cursor_alias(smemu6_scancode scan, SmakyMatrixPosition *position_out,
                               uint8_t *key_code_out, uint8_t *source_out)
{
    SmakyMatrixPosition position;
    uint8_t source;

    switch (scan) {
    case SMEMU6_SCAN_UP:
        position = 20;  /* r */
        source = 0x01u;
        break;
    case SMEMU6_SCAN_LEFT:
        position = 35;  /* d */
        source = 0x02u;
        break;
    case SMEMU6_SCAN_RIGHT:
        position = 36;  /* f */
        source = 0x04u;
        break;
    case SMEMU6_SCAN_DOWN:
        position = 51;  /* c */
        source = 0x08u;
        break;
    default:
        return 0;
    }

    *position_out = position;
    *key_code_out = S471_TABLE[S471_LAYER_NORMAL][position] & 0x7Fu;
    *source_out = source;
    return 1;
}

/* Latch one ordinary matrix key into the CLA-visible key state. */
static void latch_matrix_key(struct Smaky6 *m, smemu6_scancode scan, SmakyMatrixPosition position)
{
    latch_matrix_key_code(m, scan, position, resolve_matrix_code(m, position));
}

/* Latch one ordinary matrix key with an explicit resolved key code. */
static void latch_matrix_key_code(struct Smaky6 *m, smemu6_scancode scan, SmakyMatrixPosition position, uint8_t key_code)
{
    m->kbd.key_code = key_code & 0x7Fu;
    m->bus[0x457Eu] = 0x00u;
    m->kbd.found = 1;
    m->kbd.physically_held = 1;
    m->kbd.cla_seen_current = 0;
    m->kbd.regular_prefix_pending = 1;
    m->kbd.regular_prefix_armed = 0;
    m->kbd.release_after_reassert = 0;
    m->kbd.release_after_buffer_commit = 0;
    m->kbd.active_scancode = scan;
    m->kbd.active_matrix_position = (uint8_t)position;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;

    if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd] scancode=%d pos=%u layer=%d code=%02X\n",
                (int)scan,
                (unsigned)position,
                (int)current_layer(m),
                (unsigned)m->kbd.key_code);
    }
}

/* Latch one direct 7-bit key code that may not correspond to a matrix position. */
static void latch_direct_key_code(struct Smaky6 *m, smemu6_scancode scan, uint8_t key_code)
{
    m->kbd.key_code = key_code & 0x7Fu;
    m->bus[0x457Eu] = 0x00u;
    m->kbd.found = 1;
    m->kbd.physically_held = (scan != SMEMU6_SCAN_UNKNOWN);
    m->kbd.cla_seen_current = 0;
    m->kbd.regular_prefix_pending = 1;
    m->kbd.regular_prefix_armed = 0;
    m->kbd.release_after_reassert = 0;
    m->kbd.release_after_buffer_commit = (scan == SMEMU6_SCAN_UNKNOWN) ? 1 : 0;
    m->kbd.active_scancode = scan;
    m->kbd.active_matrix_position = MATRIX_POS_NONE;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;

    if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd] text code=%02X\n",
                (unsigned)m->kbd.key_code);
    }
}

/* Initialize the strict keyboard model.  Power-on is plain idle (no key,
 * FOUND clear); the Phantom ROM's own OUT(0x00,0) reset write latches the
 * one-shot neutral that passes kbd_wait — no virtual key is armed here. */
void keyboard_init(struct Smaky6 *m)
{
    m->kbd.key_code = 0x00;
    m->kbd.found = 0;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;
    m->kbd.physically_held = 0;
    m->kbd.release_after_reassert = 0;
    m->kbd.release_after_buffer_commit = 0;
    m->kbd.regular_prefix_pending = 0;
    m->kbd.regular_prefix_armed = 1;
    m->kbd.shift_pressed = 0;
    m->kbd.caps_lock_active = 0;
    m->kbd.host_text_down_count = 0;
    memset(m->kbd.host_text_down, 0, sizeof(m->kbd.host_text_down));
    m->kbd.active_scancode = SMEMU6_SCAN_UNKNOWN;
    m->kbd.active_matrix_position = MATRIX_POS_NONE;
    m->kbd.pending_ordinary_head = 0;
    m->kbd.pending_ordinary_len = 0;
    m->kbd.fonct_keyboard_bits = 0;
    m->kbd.cursor_alias_sources = 0;
    m->kbd.fonct_mouse_bits = 0;
    m->kbd.fonct_bits = 0;
    m->kbd.fonct_consumed_bits = 0;
    
}

/* No dynamic keyboard-side resources currently need explicit teardown. */
void keyboard_fini(struct Smaky6 *m)
{
    (void)m;
}

/* Advance the scan-latency countdown that reasserts FOUND while a key stays held. */
void keyboard_tick_cycles(struct Smaky6 *m, uint32_t cycles)
{
    if (!m->kbd.reassert_pending) return;
    if (cycles >= m->kbd.reassert_cycles) {
        m->kbd.reassert_pending = 0;
        m->kbd.reassert_cycles = 0;
        if (m->kbd.physically_held)
            m->kbd.found = 1;
    } else {
        m->kbd.reassert_cycles -= cycles;
    }
}

/* Promote the next pending ordinary key once the active latch becomes idle. */
void keyboard_frame_tick(struct Smaky6 *m)
{
    promote_pending_ordinary_key(m);
}

/* Map a portable scancode to its logical keycode for the fnct-layer letter fallback. */
static smemu6_keycode smemu6_keycode_from_scancode(smemu6_scancode scan);

/* Translate one portable scancode into strict Smaky matrix, caps, shift, or
 * function-key state. The logical keycode is derived from the scancode so the
 * fnct-layer letter fallback stays SDL-free. */
void keyboard_event(struct Smaky6 *m, smemu6_scancode scan, int down, int repeat)
{
    SmakyMatrixPosition position = MATRIX_POS_NONE;
    uint8_t cursor_alias_key_code = 0;
    uint8_t cursor_alias_source = 0;
    int has_cursor_alias = lookup_cursor_alias(scan, &position,
                                               &cursor_alias_key_code,
                                               &cursor_alias_source);

    if (m->dbg.trace_kbd) {
        fprintf(stderr,
                "[kbd-ev] type=%s scan=%d sym=%d repeat=%d found=%d held=%d prefix=%d active=%d pending=%u\n",
                down ? "down" : "up",
                (int)scan,
                (int)smemu6_keycode_from_scancode(scan),
                (int)repeat,
                m->kbd.found,
                m->kbd.physically_held,
                m->kbd.regular_prefix_pending,
                (int)m->kbd.active_scancode,
                (unsigned)m->kbd.pending_ordinary_len);
    }

    if (down)
        set_host_text_scancode_down(m, scan, 1);
    else
        set_host_text_scancode_down(m, scan, 0);

    if (scan == SMEMU6_SCAN_LSHIFT || scan == SMEMU6_SCAN_RSHIFT) {
        m->kbd.shift_pressed = down ? 1 : 0;
        return;
    }

    if (scan == SMEMU6_SCAN_CAPSLOCK) {
        if (down && !repeat)
            m->kbd.caps_lock_active = !m->kbd.caps_lock_active;
        return;
    }

    if (has_cursor_alias) {
        if (down && !repeat) {
            m->kbd.cursor_alias_sources |= cursor_alias_source;
            refresh_function_bits(m);
            if (m->kbd.pending_ordinary_len > 0 || !ordinary_latch_idle(m))
                queue_ordinary_key_code(m, scan, position, cursor_alias_key_code);
            else
                latch_matrix_key_code(m, scan, position, cursor_alias_key_code);
        } else if (!down) {
            m->kbd.cursor_alias_sources &= (uint8_t)~cursor_alias_source;
            refresh_function_bits(m);
            if (m->kbd.active_scancode == scan) {
                release_ordinary_key(m);
                promote_pending_ordinary_key(m);
            } else {
                mark_queued_key_released(m, scan);
            }
        }
        return;
    }

    for (int i = 0; i < (int)(sizeof(FUNCTION_KEYS) / sizeof(FUNCTION_KEYS[0])); i++) {
        if (FUNCTION_KEYS[i].scan != scan)
            continue;
        if (m->dbg.trace_kbd) {
            fprintf(stderr, "[kbd-fn] matched fkey[%d] scan=%d type=%s\n",
                    i, (int)scan,
                    down ? "down" : "up");
        }
        if (down && !repeat)
            m->kbd.fonct_keyboard_bits |= FUNCTION_KEYS[i].bit;
        else if (!down)
            m->kbd.fonct_keyboard_bits &= (uint8_t)~FUNCTION_KEYS[i].bit;
        refresh_function_bits(m);
        return;
    }

    if (!down) {
        if (m->kbd.active_scancode == scan) {
            release_ordinary_key(m);
            promote_pending_ordinary_key(m);
        } else {
            mark_queued_key_released(m, scan);
        }
        return;
    }

    if (repeat)
        return;

    if (is_host_text_scancode(scan) && !fnct_layer_active(m))
        return;

    position = lookup_matrix_position(scan, smemu6_keycode_from_scancode(scan), fnct_layer_active(m));
    if (position != MATRIX_POS_NONE) {
        if (m->kbd.pending_ordinary_len > 0 || !ordinary_latch_idle(m))
            queue_ordinary_key(m, scan, position);
        else
            latch_matrix_key(m, scan, position);
        return;
    }

    if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd-ev] unmapped scan=%d sym=%d\n",
                (int)scan,
                (int)smemu6_keycode_from_scancode(scan));
    }
}

/* Map a portable scancode to its logical keycode for the fnct-layer letter
 * fallback. lookup_letter_matrix_position resolves only the letter positions,
 * so only those scancodes need a keycode here. */
static smemu6_keycode smemu6_keycode_from_scancode(smemu6_scancode scan)
{
    switch (scan) {
    case SMEMU6_SCAN_A: return SMEMU6_KD_a;
    case SMEMU6_SCAN_B: return SMEMU6_KD_b;
    case SMEMU6_SCAN_C: return SMEMU6_KD_c;
    case SMEMU6_SCAN_D: return SMEMU6_KD_d;
    case SMEMU6_SCAN_E: return SMEMU6_KD_e;
    case SMEMU6_SCAN_F: return SMEMU6_KD_f;
    case SMEMU6_SCAN_G: return SMEMU6_KD_g;
    case SMEMU6_SCAN_H: return SMEMU6_KD_h;
    case SMEMU6_SCAN_I: return SMEMU6_KD_i;
    case SMEMU6_SCAN_J: return SMEMU6_KD_j;
    case SMEMU6_SCAN_K: return SMEMU6_KD_k;
    case SMEMU6_SCAN_L: return SMEMU6_KD_l;
    case SMEMU6_SCAN_M: return SMEMU6_KD_m;
    case SMEMU6_SCAN_N: return SMEMU6_KD_n;
    case SMEMU6_SCAN_O: return SMEMU6_KD_o;
    case SMEMU6_SCAN_P: return SMEMU6_KD_p;
    case SMEMU6_SCAN_Q: return SMEMU6_KD_q;
    case SMEMU6_SCAN_R: return SMEMU6_KD_r;
    case SMEMU6_SCAN_S: return SMEMU6_KD_s;
    case SMEMU6_SCAN_T: return SMEMU6_KD_t;
    case SMEMU6_SCAN_U: return SMEMU6_KD_u;
    case SMEMU6_SCAN_V: return SMEMU6_KD_v;
    case SMEMU6_SCAN_W: return SMEMU6_KD_w;
    case SMEMU6_SCAN_X: return SMEMU6_KD_x;
    case SMEMU6_SCAN_Y: return SMEMU6_KD_y;
    case SMEMU6_SCAN_Z: return SMEMU6_KD_z;
    default:
        return 0;
    }
}

/* Translate one decoded text codepoint into the strict Smaky ordinary-key path,
 * pairing it with the freshest compatible host keydown when possible. */
void keyboard_text(struct Smaky6 *m, uint32_t codepoint)
{
    uint8_t key_code;
    smemu6_scancode scan = SMEMU6_SCAN_UNKNOWN;
    SmakyMatrixPosition position = MATRIX_POS_NONE;
    SmakyMatrixPosition modern_position = MATRIX_POS_NONE;
    int needs_fresh_text_key = (codepoint != 0u && codepoint < 0x80u);

    if (fnct_layer_active(m)) {
        if (m->dbg.trace_kbd)
            fprintf(stderr, "[kbd-text] ignored codepoint=%u while FNCT layer active\n", (unsigned)codepoint);
        return;
    }

    if (!decode_text_input_code_point(codepoint, &key_code)) {
        if (m->dbg.trace_kbd)
            fprintf(stderr, "[kbd-text] skipped codepoint=%u\n", (unsigned)codepoint);
        return;
    }

    if (needs_fresh_text_key) {
        if (m->kbd.host_text_down_count == 0) {
            if (m->dbg.trace_kbd)
                fprintf(stderr, "[kbd-text] ignored codepoint=%u with no text key held\n", (unsigned)codepoint);
            return;
        }

        scan = claim_pending_host_text_scancode(m);
        if (scan == SMEMU6_SCAN_UNKNOWN) {
            if (m->dbg.trace_kbd)
                fprintf(stderr, "[kbd-text] ignored repeat codepoint=%u with no fresh text keydown\n", (unsigned)codepoint);
            return;
        }

        position = lookup_matrix_position(scan, smemu6_keycode_from_scancode(scan), 0);
        if (keymap_modern) {
            modern_position = reverse_lookup_matrix_position(current_layer(m), key_code);
            if (modern_position != MATRIX_POS_NONE)
                position = modern_position;
        }
        if (position != MATRIX_POS_NONE)
            key_code = resolve_matrix_code(m, position);

        m->kbd.host_text_down[scan] = 2;
    } else if (m->dbg.trace_kbd) {
        fprintf(stderr, "[kbd-text] fallback direct text codepoint=%u\n", (unsigned)codepoint);
    }

    if (m->kbd.pending_ordinary_len > 0 || !ordinary_latch_idle(m))
        queue_direct_key_code(m, scan, key_code);
    else
        latch_direct_key_code(m, scan, key_code);
}

/* Port 0x00 reset write ("reset the keyboard FOUND flip-flop"): latch a
 * one-shot neutral code 0x00.  The Phantom ROM writes OUT(0x00,0) at 0x003C
 * and immediately polls kbd_wait, which exits when bit 7 is clear — so this
 * neutral (FOUND set, code 0x00, not physically held, no reassertion) lets
 * the ROM autoboot DX0 with no physical key press, exactly like real
 * hardware.  A real key latched before the next CLA read overwrites it. */
void keyboard_reset_found(struct Smaky6 *m)
{
    /* A physically held key is kept: the scanner re-latches it, matching
     * real hardware where a key held across the reset write still wins. */
    if (m->kbd.physically_held)
        return;
    m->kbd.key_code = 0x00u;
    m->bus[0x457Eu] = 0x00u;
    m->kbd.found = 1;
    m->kbd.physically_held = 0;
    m->kbd.cla_seen_current = 0;
    m->kbd.regular_prefix_pending = 0;
    m->kbd.release_after_reassert = 0;
    m->kbd.release_after_buffer_commit = 0;
    m->kbd.active_scancode = SMEMU6_SCAN_UNKNOWN;
    m->kbd.active_matrix_position = MATRIX_POS_NONE;
    m->kbd.pending_ordinary_head = 0;
    m->kbd.pending_ordinary_len = 0;
    m->kbd.reassert_pending = 0;
    m->kbd.reassert_cycles = 0;
}

/* Emulate CLA port reads: ordinary keys return bit7 clear, otherwise idle/function reads return bit7 set. */
uint8_t keyboard_read_cla(struct Smaky6 *m)
{
    if (m->kbd.found) {
        uint8_t value = m->kbd.key_code & 0x7Fu;
        int held = m->kbd.physically_held;
        int chord_repeat = held && (m->kbd.fonct_bits != 0);

        if (m->kbd.regular_prefix_pending) {
            value |= 0x80u;
            m->kbd.regular_prefix_pending = 0;
        }

        m->kbd.cla_seen_current = 1;
        m->kbd.found = 0;
        if (held && !chord_repeat) {
            if (m->kbd.release_after_reassert) {
                /* In-flight latch for a key that was already released:
                 * deliver once, stop holding, no reassert, no repeat arming. */
                m->kbd.release_after_reassert = 0;
                m->kbd.physically_held = 0;
                m->bus[0x4558u] = 0;
                m->bus[0x4577u] = 0;
            } else {
                m->kbd.reassert_pending = 1;
                m->kbd.reassert_cycles = SMAKY6_SCAN_REASSERT_TSTATES;
            }
        } else {
            if (chord_repeat)
                m->kbd.physically_held = 0;
            m->bus[0x4558u] = 0;
            m->bus[0x4577u] = 0;
        }

        return value;
    }

    /* No ordinary key is currently latched.  With no function key held the
     * S471 presents the neutral state: bit 7 clear, code 0x00.  This is what
     * lets the real machine autoboot without a key press — the ROM's
     * kbd_wait loops (bit-7-clear = "code present") exit immediately on a
     * neutral read.  While a function key is held the bit-7-set path carries
     * the function bitmask instead. */
    uint8_t bits = visible_function_bits(m);
    return bits ? (uint8_t)(0x80u | bits) : 0x00u;
}

/* Emulate the keyboard status port, exposing FOUND on bit 2 and the fixed board high bit on bit 3. */
uint8_t keyboard_read_status(struct Smaky6 *m)
{
    uint8_t st = 0x08u;
    if (m->kbd.found)
        st |= 0x04u;
    return st;
}

uint8_t keyboard_read_stage1_code(struct Smaky6 *m)
{
    /* ?GETFO is the function-key accessor. It must report the current held
     * function-bit state even while an ordinary key byte is staged in 0x457E.
     * Ordinary-key delivery stays on the CLA / circular-buffer path instead of
     * being consumed here. */
    uint8_t value = m->kbd.fonct_bits;
    m->kbd.fonct_consumed_bits |= (uint8_t)(value & 0x7Fu);
    return value;
}

/* Emit one focused keyboard trace snapshot including the key SAMOS workspace
 * bytes used during the current hardware-model investigation. */
void keyboard_trace_snapshot(struct Smaky6 *m, const char *site,
                             uint16_t pc, uint16_t addr,
                             uint8_t before, uint8_t after)
{
    if (!m->dbg.trace_kbd)
        return;

    fprintf(stderr,
            "[kbd-snap] site=%s pc=%04X addr=%04X %02X->%02X found=%d held=%d seen=%d "
            "reassert=%d cycles=%u prefix=%d code=%02X fonct=%02X "
            "457E=%02X 4580=%02X 4581=%02X 4582=%02X 457C=%02X 457D=%02X\n",
            site,
            (unsigned)pc,
            (unsigned)addr,
            (unsigned)before,
            (unsigned)after,
            m->kbd.found,
            m->kbd.physically_held,
            m->kbd.cla_seen_current,
            m->kbd.reassert_pending,
            (unsigned)m->kbd.reassert_cycles,
            m->kbd.regular_prefix_pending,
            (unsigned)m->kbd.key_code,
            (unsigned)m->kbd.fonct_bits,
            (unsigned)m->bus[0x457Eu],
            (unsigned)m->bus[0x4580u],
            (unsigned)m->bus[0x4581u],
            (unsigned)m->bus[0x4582u],
            (unsigned)m->bus[0x457Cu],
            (unsigned)m->bus[0x457Du]);
}

/* Report the raw FOUND latch state without applying any CLA side effects. */
int keyboard_found(struct Smaky6 *m)
{
    return m->kbd.found;
}
