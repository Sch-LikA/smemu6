// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* keyboard.c – Smaky 6 keyboard controller (SDL2 → Smaky key codes) */
#include "machine_internal.h"
#include "machine.h"
#include "keyboard.h"
#include <string.h>

/*
 * Smaky 6 keyboard code table (partial; extend from doc section 10.4).
 * The keyboard EPROM translates scan position to a 7-bit code.
 * Here we map SDL scancodes directly to Smaky display codes.
 * Bit 7 of the CLA result: 0 = normal key, 1 = no-key / function key.
 *
 * Mapping: SDL_SCANCODE → smaky_code (7-bit)
 * 0x00 = no key / unknown
 */
/*
 * Non-printable keys handled via SDL_KEYDOWN.
 * Printable characters (letters, digits, punctuation, space) are handled
 * via SDL_TEXTINPUT (keyboard_text_event) so that the host OS applies the
 * correct shift / Caps Lock / dead-key state, giving lowercase by default.
 */
static const struct { SDL_Scancode scan; uint8_t code; } KEY_TABLE[] = {
    { SDL_SCANCODE_RETURN,    0x0D },
    { SDL_SCANCODE_BACKSPACE, 0x08 },
    { SDL_SCANCODE_TAB,       0x09 },   /* TAB → inserts "DX1:" at command prompt */
    { SDL_SCANCODE_DELETE,    0x7F },   /* DEL */
    { SDL_SCANCODE_ESCAPE,    0x04 },   /* ESC / UNDO (top-left key) → 0x04 → CLI cancel/undo */
    { SDL_SCANCODE_F8,        0x1E },   /* MACRO  → « */
    { SDL_SCANCODE_F9,        0x1F },   /* DEFINE → » */
};
#define KEY_TABLE_LEN (int)(sizeof(KEY_TABLE) / sizeof(KEY_TABLE[0]))

/* Direct field access: struct Smaky6 is fully visible via machine_internal.h */

void keyboard_init(struct Smaky6 *m)
{
    m->kbd.key_code          = 0;
    m->kbd.found             = 0;
    m->kbd.reassert_pending  = 0;
    m->kbd.reassert_cycles   = 0;
    m->kbd.physically_held   = 0;
    m->kbd.boot_key_held     = 0;
    m->kbd.shift_pressed   = 0;
    m->kbd.fonct_bits      = 0;
    m->kbd.repeat_scan     = SDL_SCANCODE_UNKNOWN;
    m->kbd.fifo_head       = 0;
    m->kbd.fifo_tail       = 0;
    m->kbd.text_blocked    = 0;

    /* Power-on state: the FOUND latch (4013 FF2) powers up SET in practice.
     * physically_held=1 models the scanner continuously reasserting FOUND
     * (200µs reassertion) as long as a key is held — here the virtual Enter
     * key is held from power-on through all boot-phase kbd_wait loops until
     * EI is executed.  keyboard_frame_tick() releases it when iff1 first
     * becomes 1.  key_code=0x00 = Enter selects DX0 autoboot. */
    m->kbd.found             = 1;
    m->kbd.key_code          = 0x00;
    m->kbd.physically_held   = 1;
    m->kbd.boot_key_held     = 1;
}

/* Called once per 50 Hz frame from the main loop.  Decrements the hold-time
 * countdown and clears the effective key_held state when the countdown expires
 * and the key has been physically released.
 * Also drains one key from the software FIFO into the SAMOS circular buffer
 * when the buffer slot is free (write pointer == 0x4596 = buffer base). */
void keyboard_tick_cycles(struct Smaky6 *m, uint32_t cycles)
{
    /* Advance the FOUND-reassert countdown.  Called after every CPU execution
     * slice so the reassert fires at ~200 µs granularity (500 T-states at 2.5 MHz)
     * rather than once per 20 ms frame. */
    if (!m->kbd.reassert_pending) return;
    if (cycles >= m->kbd.reassert_cycles) {
        m->kbd.reassert_pending = 0;
        m->kbd.reassert_cycles  = 0;
        if (m->kbd.physically_held)
            m->kbd.found = 1;   /* scanner refires */
    } else {
        m->kbd.reassert_cycles -= cycles;
    }
}

void keyboard_frame_tick(struct Smaky6 *m)
{
    /* Power-on virtual Enter key: physically_held=1 from keyboard_init() models
     * the 4013 FF2 FOUND latch being SET at power-on.  The first kbd_wait exits on
     * the initial found=1.  Subsequent polls are served by keyboard_tick_cycles()
     * (~500 T-state / 200 µs countdown), matching real hardware's ≤200µs scan cycle.
     *
     * Release trigger: the SAMOS 50 Hz ISR vector at bus[0x4566..7] == 0x003E.
     * This is written by SAMOS init at 0x00CD, AFTER the init kbd_wait exits,
     * and BEFORE EI enables the ISR.  At this point SAMOS is fully initialised
     * and the virtual Enter key is no longer needed.
     *
     * We do NOT use iff1 as the trigger: EI fires as early as 0x020D during the
     * Phantom ROM floppy loader, which is before SAMOS init's kbd_wait at 0x00B5.
     * Using iff1 would release the virtual key too early and hang the machine. */
    if (m->kbd.boot_key_held) {
        uint16_t vec = (uint16_t)m->bus[0x4566u] | ((uint16_t)m->bus[0x4567u] << 8);
        if (vec == 0x003Eu) {
            m->kbd.boot_key_held    = 0;
            m->kbd.physically_held  = 0;
            m->kbd.reassert_pending = 0;
            m->kbd.reassert_cycles  = 0;
            m->kbd.found            = 0;
            m->kbd.fifo_head        = m->kbd.fifo_tail;  /* discard pre-SAMOS FIFO */
        }
    }

    /* Drain the FIFO into the SAMOS circular buffer once SAMOS is running.
     * Skip while iff1=0: either pre-SAMOS boot or inside the 50 Hz ISR (Z80
     * clears IFF1 on INT acknowledgment).  In both cases keys must not be
     * written to the circular buffer from here. */
    if (!m->cpu.iff1) return;

    /* Direct write of fonct_bits to the GETFON register (0x4580).
     * The SAMOS ISR (Stage 2 at 0x016E-0x0170) does update 0x4580, but only
     * when FOUND=0 AND the ISR reaches Stage 2.  Between ISR ticks (most of
     * the game loop) 0x4580 holds the PREVIOUS frame's ISR value.  For
     * programs that poll 0x4580 directly (GETFON syscall) or poll IN A,(0x00)
     * for real-time function-key state (e.g. Flipper game flippers), we must
     * keep 0x4580 live at all times.  Writing here (before machine_run_frame)
     * ensures every GETFON poll in the frame sees the current value.
     * The ISR's own write at 0x0170 is harmless (writes the same value). */
    m->bus[0x4580u] = m->kbd.fonct_bits;

    /* Mirror fonct_bits into the SAMOS "last key" latch (0x457E) so that
     * syscall 0x0E (LD A,(0x457E); OR A; RET) returns the current function-
     * key bitmask.  Games like FLIPPER.SM read flippers exclusively via
     * syscall 0x0E and check (result & 0xF0) for left flipper and
     * (result & 0x0F) for right flipper — which maps exactly to the fonct_bits:
     *   CURSOR (F1) = bit 4 = 0x10 → AND 0xF0 = 0x10 ≠ 0 → left flipper
     *   CHANGE (F7) = bit 0 = 0x01 → AND 0x0F = 0x01 ≠ 0 → right flipper
     * Physical regular-key presses travel via the FIFO→circular-buffer path
     * and never touch 0x457E in the emulator, so this write does not conflict.
     * Writing 0x00 when no function key is held releases the flipper each frame,
     * giving true press-and-hold (not toggle/latch) behaviour. */
    m->bus[0x457Eu] = m->kbd.fonct_bits;
    while (m->kbd.fifo_head != m->kbd.fifo_tail) {
        uint16_t wr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
        /* Sanity check: if write pointer is outside the circular buffer area,
         * SAMOS workspace is not yet initialized or was corrupted.
         * Print a warning and stop — do NOT write to a wrong address. */
        if (wr < 0x4596u || wr > 0x45B6u) {
            fprintf(stderr,
                    "[kbd_tick] write ptr 0x%04X out of range [0x4596,0x45B6]"
                    " — SAMOS workspace not ready, deferring\n", wr);
            break;
        }
        if (m->bus[wr] == 0x80u) {
            if (m->dbg.trace_kbd)
                fprintf(stderr, "[kbd_tick] guard sentinel at 0x%04X — buffer full\n", wr);
            break;
        }
        uint8_t raw  = m->kbd.fifo[m->kbd.fifo_head];
        uint8_t physical = raw & 0x80u;   /* set by keyboard_event() for real keys */
        uint8_t code = raw & 0x7Fu;
        m->kbd.fifo_head = (m->kbd.fifo_head + 1) & 63;
        m->bus[wr] = code;
        wr++;
        m->bus[0x457Cu] = (uint8_t)(wr & 0xFFu);
        m->bus[0x457Du] = (uint8_t)(wr >> 8);
        /* Arm SAMOS Stage 4 auto-repeat for physical keystrokes only.
         * Enter (0x0D) and ESC (0x04) are excluded: arming Enter causes the
         * ISR to re-inject it 700 ms later, making the line editor process a
         * spurious empty command; arming ESC would cancel the current line
         * again after the initial cancel.
         * keyboard_event() zeroes 0x4558 on KEYUP, so repeat stops on release.
         * OS-level text repeat is suppressed by text_blocked in keyboard_event(),
         * so SAMOS Stage 4 is the sole source of key repeat. */
        if (physical && code != 0x0Du && code != 0x04u) {
            m->bus[0x4558u] = 0x23u;   /* 35 frames = 700 ms initial delay */
            m->bus[0x4577u] = code;    /* repeat key code */
        }
        if (m->dbg.trace_kbd)
            fprintf(stderr, "[kbd_tick] circ[0x%04X] <- 0x%02X ('%c')  ptr now 0x%04X  [ptr]=0x%02X\n",
                    (unsigned)(wr-1), (unsigned)code,
                    (code >= 0x20 && code < 0x7F) ? (char)code : '?',
                    (unsigned)wr, (unsigned)m->bus[wr]);
    }
}

void keyboard_fini(struct Smaky6 *m) { (void)m; }

void keyboard_event(struct Smaky6 *m, const SDL_KeyboardEvent *ev)
{
    /* Handle SHIFT key for modifier combinations (SHIFT+BREAK, SHIFT+ESC) */
    if (ev->keysym.scancode == SDL_SCANCODE_LSHIFT || 
        ev->keysym.scancode == SDL_SCANCODE_RSHIFT) {
        m->kbd.shift_pressed = (ev->type == SDL_KEYDOWN) ? 1 : 0;
        return;
    }

    /* "Touches de fonction" — 7 function keys set/clear a bitmask.
     * keyboard_read_cla() returns this when no regular key is pending.
     * Primary mapping: F1–F7 (reliable; not intercepted by WM on Linux/Win/Mac).
     * Secondary mapping: modifier/nav keys for users who prefer them.
     * Both sets are active simultaneously; holding either fires the same bit. */
    {
        static const struct { SDL_Scancode scan; uint8_t bit; } FONCT[] = {
            /* Primary: F1–F7 (left-to-right keyboard order) */
            { SDL_SCANCODE_F1,          0x10 }, /* CURSOR  */
            { SDL_SCANCODE_F2,          0x08 }, /* COPY    */
            { SDL_SCANCODE_F3,          0x40 }, /* KILL    */
            { SDL_SCANCODE_F4,          0x20 }, /* PROGRA  */
            { SDL_SCANCODE_F5,          0x04 }, /* SHOW    */
            { SDL_SCANCODE_F6,          0x02 }, /* SEARCH  */
            { SDL_SCANCODE_F7,          0x01 }, /* CHANGE  */
            /* Secondary: original modifier/nav mappings */
            { SDL_SCANCODE_END,         0x01 }, /* CHANGE  */
            { SDL_SCANCODE_HOME,        0x02 }, /* SEARCH  */
            { SDL_SCANCODE_INSERT,      0x04 }, /* SHOW    */
            { SDL_SCANCODE_LALT,        0x08 }, /* COPY    */
            { SDL_SCANCODE_LCTRL,       0x10 }, /* CURSOR  */
            { SDL_SCANCODE_LGUI,        0x20 }, /* PROGRA  (Left Windows / Super) */
            { SDL_SCANCODE_RALT,        0x40 }, /* KILL    (AltGr) */
        };
        for (int i = 0; i < (int)(sizeof(FONCT)/sizeof(FONCT[0])); i++) {
            if (FONCT[i].scan == ev->keysym.scancode) {
                if (ev->type == SDL_KEYDOWN)
                    m->kbd.fonct_bits |= FONCT[i].bit;
                else
                    m->kbd.fonct_bits &= (uint8_t)~FONCT[i].bit;
                if (m->dbg.trace_kbd)
                    fprintf(stderr, "[kbd] fonct %s bit=0x%02X fonct_bits=0x%02X\n",
                            ev->type == SDL_KEYDOWN ? "DOWN" : "UP  ",
                            FONCT[i].bit, m->kbd.fonct_bits);
                return;
            }
        }
    }

    if (ev->type == SDL_KEYUP) {
        /* Cancel SAMOS auto-repeat on key release.  Always safe to write: if
         * SAMOS is not yet running, address 0x4558 is uninitialised RAM and no
         * repeat is active. */
        m->bus[0x4558u] = 0u;
        m->kbd.text_blocked = 0;
        return;
    }
    if (ev->type != SDL_KEYDOWN) return;
    if (ev->repeat) return;   /* ignore SDL key-repeat; SAMOS handles its own repeat */

    /* New (non-repeated) KEYDOWN: unblock text input for this new keypress. */
    m->kbd.text_blocked = 0;

    SDL_Scancode scan = ev->keysym.scancode;
    /* Track scancode so we can cancel repeat on the matching KEYUP. */
    m->kbd.repeat_scan = scan;

    /* ESC / UNDO key: smart dual behaviour matching the SAMOS manual.
     * The S471 encoder generates two distinct codes for ESC-related actions:
     *   0x04 — cancel/clear the current command line (EFFACE / CLR)
     *   0x05 — recall previous command (UNDO / recall)
     * The SAMOS line editor at 0x577E dispatches on these codes independently.
     * The user-facing behaviour documented in the SAMOS guide is:
     *   ESC on non-empty line → cancel/clear it (0x04)
     *   ESC on empty line     → recall previous command (0x05)
     * We implement this by examining the SAMOS line-buffer-length byte at
     * 0x454B: if 0 (empty) send 0x05 (recall); if non-zero send 0x04 (cancel). */
    for (int i = 0; i < KEY_TABLE_LEN; i++) {
        if (KEY_TABLE[i].scan == scan) {
            uint8_t code = KEY_TABLE[i].code;
            /* Physical keyboard uses FIFO-only delivery.  Do NOT touch the CLA
             * fields (key_code / found / physically_held / key_hold_frames) here —
             * those are reserved for machine_inject_key() / the CLA inject path.
             * Setting them here causes ISR Stage 2 to re-write the key to the
             * circular buffer every frame (for key_hold_frames frames), which loops
             * Enter and drops intermediate characters. */
            int next = (m->kbd.fifo_tail + 1) & 63;
            if (next != m->kbd.fifo_head) {   /* not full */
                /* Bit 7 = "physical key" marker: keyboard_frame_tick() uses it
                 * to arm the SAMOS ISR auto-repeat only for physical keystrokes
                 * (not for injected or programmatic codes). */
                m->kbd.fifo[m->kbd.fifo_tail] = code | 0x80u;
                m->kbd.fifo_tail = next;
                if (m->dbg.trace_kbd)
                    fprintf(stderr, "[kbd_event] pushed 0x%02X ('%c') to FIFO[%d]"
                            "  iff1=%d\n",
                            (unsigned)code,
                            (code >= 0x20 && code < 0x7F) ? (char)code : '?',
                            m->kbd.fifo_tail - 1,
                            m->cpu.iff1);
            } else {
                if (m->dbg.trace_kbd)
                    fprintf(stderr, "[kbd_event] FIFO FULL — dropped 0x%02X\n",
                            (unsigned)code);
            }
            return;
        }
    }
    /* Unknown key: no-op */
}

/*
 * Unicode (2-byte UTF-8 BMP) → Smaky 7-bit code for Swiss-French accented
 * characters.  The Smaky 6 keyboard EPROM maps these keys to codes 0x0F–0x1D
 * (doc §10.4 p.214).  The real keyboard is uppercase-only, so uppercase and
 * lowercase Unicode variants map to the same Smaky code.
 */
static const struct { uint16_t unicode; uint8_t code; } ACCENT_TABLE[] = {
    /* lowercase */
    { 0x00FC, 0x0F }, /* ü */
    { 0x00E0, 0x10 }, /* à */
    { 0x00E2, 0x11 }, /* â */
    { 0x00E9, 0x12 }, /* é */
    { 0x00E8, 0x13 }, /* è */
    { 0x00EB, 0x14 }, /* ë */
    { 0x00EA, 0x15 }, /* ê */
    { 0x00EF, 0x16 }, /* ï */
    { 0x00EE, 0x17 }, /* î */
    { 0x00F4, 0x18 }, /* ô */
    { 0x00F9, 0x19 }, /* ù */
    { 0x00FB, 0x1A }, /* û */
    { 0x00E4, 0x1B }, /* ä */
    { 0x00F6, 0x1C }, /* ö */
    { 0x00E7, 0x1D }, /* ç */
    /* uppercase variants → same Smaky code */
    { 0x00DC, 0x0F }, /* Ü */
    { 0x00C0, 0x10 }, /* À */
    { 0x00C2, 0x11 }, /* Â */
    { 0x00C9, 0x12 }, /* É */
    { 0x00C8, 0x13 }, /* È */
    { 0x00CB, 0x14 }, /* Ë */
    { 0x00CA, 0x15 }, /* Ê */
    { 0x00CF, 0x16 }, /* Ï */
    { 0x00CE, 0x17 }, /* Î */
    { 0x00D4, 0x18 }, /* Ô */
    { 0x00D9, 0x19 }, /* Ù */
    { 0x00DB, 0x1A }, /* Û */
    { 0x00C4, 0x1B }, /* Ä */
    { 0x00D6, 0x1C }, /* Ö */
    { 0x00C7, 0x1D }, /* Ç */
};
#define ACCENT_TABLE_LEN (int)(sizeof(ACCENT_TABLE) / sizeof(ACCENT_TABLE[0]))

/*
 * Feed an SDL_TEXTINPUT event into the keyboard model.
 *
 * SDL_TEXTINPUT is generated by the OS after applying shift, Caps Lock,
 * dead keys, and compose sequences.  This gives us lowercase letters by
 * default and uppercase with Shift — the host keyboard layout does the
 * right thing automatically.
 *
 * ev->text is UTF-8.  We accept:
 *   • Single-byte printable ASCII (0x20–0x7E) — pushed directly.
 *   • 2-byte UTF-8 sequences (0xC0–0xDF lead, 0x80–0xBF cont): decoded to a
 *     Unicode codepoint and looked up in ACCENT_TABLE for Swiss-French chars.
 *   • Everything else (3+ byte sequences, lone surrogates) is silently skipped.
 */
void keyboard_text_event(struct Smaky6 *m, const SDL_TextInputEvent *ev)
{
    /* SDL_TextInputEvent has no repeat field: the host OS fires a new
     * SDL_TEXTINPUT for every OS-level key-repeat tick while a key is held.
     * We suppress all but the first text event per physical keypress using
     * text_blocked, which is cleared on each non-repeated SDL_KEYDOWN and on
     * SDL_KEYUP (in keyboard_event()), and set after the first text is pushed. */
    if (m->kbd.text_blocked) return;
    m->kbd.text_blocked = 1;
    const unsigned char *p = (const unsigned char *)ev->text;
    while (*p != '\0') {
        uint8_t b0 = *p;

        if (b0 < 0x80) {
            /* Single-byte ASCII */
            p++;
            if (b0 < 0x20 || b0 > 0x7E) continue;  /* skip control / DEL */
            int next = (m->kbd.fifo_tail + 1) & 63;
            if (next != m->kbd.fifo_head) {
                m->kbd.fifo[m->kbd.fifo_tail] = b0 | 0x80u;  /* bit 7 = physical */
                m->kbd.fifo_tail = next;
                if (m->dbg.trace_kbd)
                    fprintf(stderr, "[kbd_text] pushed 0x%02X ('%c') to FIFO\n",
                            (unsigned)b0, (char)b0);
            } else {
                if (m->dbg.trace_kbd)
                    fprintf(stderr, "[kbd_text] FIFO FULL — dropped 0x%02X\n",
                            (unsigned)b0);
            }
        } else if ((b0 & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            /* 2-byte UTF-8 sequence: decode and look up accent table */
            uint16_t cp = (uint16_t)(((b0 & 0x1F) << 6) | (p[1] & 0x3F));
            p += 2;
            uint8_t smaky_code = 0;
            for (int i = 0; i < ACCENT_TABLE_LEN; i++) {
                if (ACCENT_TABLE[i].unicode == cp) {
                    smaky_code = ACCENT_TABLE[i].code;
                    break;
                }
            }
            if (smaky_code == 0) continue;  /* unmapped codepoint */
            int next = (m->kbd.fifo_tail + 1) & 63;
            if (next != m->kbd.fifo_head) {
                m->kbd.fifo[m->kbd.fifo_tail] = smaky_code | 0x80u;  /* bit 7 = physical */
                m->kbd.fifo_tail = next;
                if (m->dbg.trace_kbd)
                    fprintf(stderr, "[kbd_text] accent U+%04X → 0x%02X pushed to FIFO\n",
                            (unsigned)cp, (unsigned)smaky_code);
            } else {
                if (m->dbg.trace_kbd)
                    fprintf(stderr, "[kbd_text] FIFO FULL — dropped accent U+%04X\n",
                            (unsigned)cp);
            }
        } else {
            /* 3+ byte sequence or invalid byte: skip one byte and continue */
            p++;
        }
    }
}

uint8_t keyboard_read_cla(struct Smaky6 *m)
{
    /*
     * Pure hardware model (§10.4 CLAVIER, schematic Nov 1978 — J. Zahn):
     *
     *   CLA read (IN A,(0x00)) generates STROBE which simultaneously:
     *     1. Returns the current latched value to the Z80
     *     2. Clears both FOUND and FULCLA latches (4013 FF2)
     *   If the key is still physically held, the scanner reasserts FOUND+FULCLA
     *   within 200µs (one full scan cycle at 300 kHz).
     *
     *   Return value (bit7 encodes FOUND state per §10.4):
     *     bit7=0, bits6-0=key_code → FOUND was 1 (regular key present)
     *     bit7=1, bits6-0=fonct    → FOUND was 0 (function keys or idle)
     *
     *   Idle (no key, no function keys): returns 0x80.
     *   kbd_wait (0x00FD / 0x00B5): `AND 0x80; JR NZ, loop` — loops while
     *   bit7=1 (idle), exits when bit7=0 (regular key).  Confirmed from ROM
     *   disassembly (byte sequence DB 00 E6 80 20 F8).
     *
     * Auto-boot without a keypress:
     *   On real hardware, the 4013 FF2 FOUND latch powers up SET (in practice).
     *   The scanner immediately reasserts FOUND as long as a key is physically
     *   held (200µs reassertion).  The user presses Enter at the Phantom ROM
     *   boot menu; FOUND=1 persists through both the Phantom ROM kbd_wait AND
     *   the SAMOS init kbd_wait at 0x00B5.  No second keypress required.
     *   The emulator models this with physically_held=1 from power-on until EI
     *   is executed (iff1→1): keyboard_frame_tick() then auto-releases the
     *   virtual key.  This function needs no phase-detection branching at all.
     *
     * This function is identical for all callers: Phantom ROM kbd_wait,
     * SAMOS 50 Hz ISR (Stage 1 at 0x0160, Stage 2 at 0x0183), and monitor.
     */

    if (m->kbd.found) {
        m->kbd.found = 0;
        /* Scanner immediately reasserts FOUND if key still held (§10.4: 200µs).
         * Models a key held down: every CLA read keeps returning the same code. */
        /* Hardware: scanner reasserts FOUND within ≤200µs after CLA read clears
         * the latch (one full 8×8 scan at 300 kHz / 32 divider).  At 2.5 MHz
         * Z80 that is ≈500 T-states.  Store a countdown; keyboard_tick_cycles()
         * promotes it to found=1 once enough cycles have elapsed. */
        if (m->kbd.physically_held) {
            m->kbd.reassert_pending = 1;
            m->kbd.reassert_cycles  = SMAKY6_SCAN_REASSERT_TSTATES;
        }
        return m->kbd.key_code & 0x7Fu;   /* bit7=0: regular key */
    }

    /* FOUND=0: return function key bitmask (0x80 if none held = idle).
     * SAMOS ISR Stage 1 (0x015E-0x016D):
     *   LD (0x4580),0x00; IN A,(0); BIT 7,A; JR NZ,0x016E
     *   Falls through when bit7=0 → stores key to 0x457E (syscall 0x0E).
     *   Jumps to 0x016E when bit7=1 → Stage 2: AND 0x7F; LD (0x4580),A
     *   stores fonct_bits to the GETFON register automatically.
     * Direct SYS.SY binary audit (2026-05-13) confirmed that Stage 2 reads
     * 0x4582 at 0x0175, but the init sentinel write is to 0x458A at 0x00A1,
     * not to 0x4582.  The old "Stage 2 is permanently blocked by 0x4582=0x80"
     * model is therefore unproven and needs re-audit before relying on it. */
    return 0x80u | m->kbd.fonct_bits;
}

uint8_t keyboard_read_status(struct Smaky6 *m)
{
    /* Port 0x01 (IN): bit2=FOUND (4013 FF2 latch output), bit3 fixed high (pull-up).
     * All other bits are undefined on hardware; we return 0 for them. */
    uint8_t st = 0x08u;
    if (keyboard_found(m)) st |= 0x04u;
    return st;
}

int keyboard_found(struct Smaky6 *m)
{
    /* Port 0x01 bit 2 is the output of the 4013 FF2 FOUND latch — NOT a raw
     * "physically held" signal.  The latch is:
     *   SET   when the scanner detects a pressed key (= found=1)
     *   RESET by the CLA read STROBE (= found=0 after keyboard_read_cla())
     *   RE-SET by the scanner within ≤200µs if the key is still held
     *          (= reassert_pending → found=1 at the next frame tick)
     *
     * Returning physically_held here would give bit2=1 even immediately after a
     * CLA read cleared the latch — contradicting hardware where bit2 follows the
     * latch, not the raw physical signal.
     *
     * Stage 2 reads this port at 0x017E, but its CLA Read #2 is permanently
     * blocked by the sentinel at 0x0178, so this is dead code while SAMOS runs. */
    return m->kbd.found;
}
