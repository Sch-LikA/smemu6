/* keyboard.c – Smaky 6 keyboard controller (SDL2 → Smaky key codes) */
#include "machine_internal.h"
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
static const struct { SDL_Scancode scan; uint8_t code; } KEY_TABLE[] = {
    /* Letters (uppercase — Smaky 6 is uppercase-only on alphanumeric display) */
    { SDL_SCANCODE_A, 0x41 }, { SDL_SCANCODE_B, 0x42 }, { SDL_SCANCODE_C, 0x43 },
    { SDL_SCANCODE_D, 0x44 }, { SDL_SCANCODE_E, 0x45 }, { SDL_SCANCODE_F, 0x46 },
    { SDL_SCANCODE_G, 0x47 }, { SDL_SCANCODE_H, 0x48 }, { SDL_SCANCODE_I, 0x49 },
    { SDL_SCANCODE_J, 0x4A }, { SDL_SCANCODE_K, 0x4B }, { SDL_SCANCODE_L, 0x4C },
    { SDL_SCANCODE_M, 0x4D }, { SDL_SCANCODE_N, 0x4E }, { SDL_SCANCODE_O, 0x4F },
    { SDL_SCANCODE_P, 0x50 }, { SDL_SCANCODE_Q, 0x51 }, { SDL_SCANCODE_R, 0x52 },
    { SDL_SCANCODE_S, 0x53 }, { SDL_SCANCODE_T, 0x54 }, { SDL_SCANCODE_U, 0x55 },
    { SDL_SCANCODE_V, 0x56 }, { SDL_SCANCODE_W, 0x57 }, { SDL_SCANCODE_X, 0x58 },
    { SDL_SCANCODE_Y, 0x59 }, { SDL_SCANCODE_Z, 0x5A },
    /* Digits */
    { SDL_SCANCODE_0, 0x30 }, { SDL_SCANCODE_1, 0x31 }, { SDL_SCANCODE_2, 0x32 },
    { SDL_SCANCODE_3, 0x33 }, { SDL_SCANCODE_4, 0x34 }, { SDL_SCANCODE_5, 0x35 },
    { SDL_SCANCODE_6, 0x36 }, { SDL_SCANCODE_7, 0x37 }, { SDL_SCANCODE_8, 0x38 },
    { SDL_SCANCODE_9, 0x39 },
    /* Punctuation */
    { SDL_SCANCODE_SPACE,  0x20 }, { SDL_SCANCODE_RETURN, 0x0D },
    /* SDL_SCANCODE_ESCAPE omitted: 0x1B is the Smaky 6 ä display code, not ESC.
     * The BREAK key generates NMI (PAUSE / F11 in main.c). */
    { SDL_SCANCODE_BACKSPACE, 0x08 },
    { SDL_SCANCODE_DELETE,    0x7F },   /* DEL */
    { SDL_SCANCODE_F8,        0x1E },   /* MACRO  → « */
    { SDL_SCANCODE_F9,        0x1F },   /* DEFINE → » */
    { SDL_SCANCODE_PERIOD, 0x2E }, { SDL_SCANCODE_COMMA,  0x2C },
    { SDL_SCANCODE_MINUS,  0x2D }, { SDL_SCANCODE_EQUALS, 0x3D },
    { SDL_SCANCODE_SLASH,  0x2F }, { SDL_SCANCODE_SEMICOLON, 0x3A },
};
#define KEY_TABLE_LEN (int)(sizeof(KEY_TABLE) / sizeof(KEY_TABLE[0]))

/* Direct field access: struct Smaky6 is fully visible via machine_internal.h */

void keyboard_init(struct Smaky6 *m)
{
    m->kbd.key_code        = 0;
    m->kbd.found           = 0;
    m->kbd.physically_held = 0;
    m->kbd.key_hold_frames = 0;
    m->kbd.cla_seen        = 0;
    m->kbd.shift_pressed   = 0;
    m->kbd.fonct_bits      = 0;
    m->kbd.fifo_head       = 0;
    m->kbd.fifo_tail       = 0;
}

/* Called once per 50 Hz frame from the main loop.  Decrements the hold-time
 * countdown and clears the effective key_held state when the countdown expires
 * and the key has been physically released.
 * Also drains one key from the software FIFO into the SAMOS circular buffer
 * when the buffer slot is free (write pointer == 0x4596 = buffer base). */
void keyboard_frame_tick(struct Smaky6 *m)
{
    if (m->kbd.key_hold_frames > 0) {
        m->kbd.key_hold_frames--;
        if (m->kbd.key_hold_frames == 0 && !m->kbd.physically_held) {
            /* Countdown expired and key was physically released — fully clear. */
            m->kbd.cla_seen = 0;
        }
    }

    /* Feed one key from the FIFO into the SAMOS circular buffer.
     * Skip when iff1=0 (monitor mode): the monitor polls CLA directly via
     * keyboard_read_cla(), so keys must stay in the FIFO for that path.
     * Only write when the buffer is at base (ptr == 0x4596).
     * After consume, ptr returns to 0x4596 — that is the "empty" signal
     * (peek returns Z when SBC HL,DE == 0).  The value at [0x4596] may
     * retain the previously-consumed character (LDIR with BC=0 when only
     * one item was in the buffer), so do NOT check [0x4596]==0x00. */
    if (!m->cpu.iff1) return;  /* monitor mode: leave FIFO for CLA path */
    if (m->kbd.fifo_head != m->kbd.fifo_tail) {
        uint16_t wr = (uint16_t)m->bus[0x457Cu] | ((uint16_t)m->bus[0x457Du] << 8);
        if (m->bus[wr] != 0x80u) {   /* 0x80 = guard sentinel, buffer full */
            uint8_t code = m->kbd.fifo[m->kbd.fifo_head];
            m->kbd.fifo_head = (m->kbd.fifo_head + 1) & 63;
            m->bus[wr] = code & 0x7Fu;
            wr++;
            m->bus[0x457Cu] = (uint8_t)(wr & 0xFFu);
            m->bus[0x457Du] = (uint8_t)(wr >> 8);
        }
    }
}

void keyboard_fini(struct Smaky6 *m) { (void)m; }

void keyboard_event(struct Smaky6 *m, const SDL_KeyboardEvent *ev)
{
    /* Handle SHIFT key for modifier combinations (SHIFT+BREAK) */
    if (ev->keysym.scancode == SDL_SCANCODE_LSHIFT || 
        ev->keysym.scancode == SDL_SCANCODE_RSHIFT) {
        m->kbd.shift_pressed = (ev->type == SDL_KEYDOWN) ? 1 : 0;
        return;
    }

    /* "Touches de fonction" — 7 function keys (F1-F7) set/clear a bitmask.
     * keyboard_read_cla() returns this when no regular key is pending. */
    {
        static const struct { SDL_Scancode scan; uint8_t bit; } FONCT[] = {
            { SDL_SCANCODE_F1, 0x01 }, /* CHANGE  */
            { SDL_SCANCODE_F2, 0x02 }, /* SEARCH  */
            { SDL_SCANCODE_F3, 0x04 }, /* SHOW    */
            { SDL_SCANCODE_F4, 0x08 }, /* COPY    */
            { SDL_SCANCODE_F5, 0x10 }, /* CURSOR  */
            { SDL_SCANCODE_F6, 0x20 }, /* PROGRA  */
            { SDL_SCANCODE_F7, 0x40 }, /* KILL    */
        };
        for (int i = 0; i < (int)(sizeof(FONCT)/sizeof(FONCT[0])); i++) {
            if (FONCT[i].scan == ev->keysym.scancode) {
                if (ev->type == SDL_KEYDOWN)
                    m->kbd.fonct_bits |= FONCT[i].bit;
                else
                    m->kbd.fonct_bits &= (uint8_t)~FONCT[i].bit;
                return;
            }
        }
    }

    if (ev->type == SDL_KEYUP)
        return;   /* FIFO-based delivery: nothing to do on key-up */
    if (ev->type != SDL_KEYDOWN) return;
    if (ev->repeat) return;   /* ignore SDL key-repeat; SAMOS handles its own repeat */

    SDL_Scancode scan = ev->keysym.scancode;
    for (int i = 0; i < KEY_TABLE_LEN; i++) {
        if (KEY_TABLE[i].scan == scan) {
            uint8_t code = KEY_TABLE[i].code;
            /* Physical keyboard uses FIFO-only delivery.  Do NOT touch the CLA
             * fields (key_code / found / physically_held / key_hold_frames) here —
             * those are reserved for machine_inject_key() / the autoboot ISR path.
             * Setting them here causes ISR Stage 2 to re-write the key to the
             * circular buffer every frame (for key_hold_frames frames), which loops
             * Enter and drops intermediate characters. */
            int next = (m->kbd.fifo_tail + 1) & 63;
            if (next != m->kbd.fifo_head) {   /* not full */
                m->kbd.fifo[m->kbd.fifo_tail] = code;
                m->kbd.fifo_tail = next;
            }
            return;
        }
    }
    /* Unknown key: no-op */
}

uint8_t keyboard_read_cla(struct Smaky6 *m)
{
    /*
     * Hardware model (§10.4 CLAVIER):
     *   CLA read (IN A,(0x00)) generates STROBE which simultaneously returns
     *   the key code AND clears both FOUND and FULCLA latches.
     *   If the key is still held, the scanner reasserts FOUND within 200µs.
     *   Bit 7 of the return value encodes FOUND state:
     *     bit7=0 → FOUND was 1; regular key in bits 0-6
     *     bit7=1 → FOUND was 0; function key bitmask in bits 0-6 (0 if none)
     *
     * Stage 1 (ISR at 0x0160): first CLA read; if bit7=0, stores to 0x457E
     *   (syscall 0x0E path), sets cla_seen=1, returns early.
     * Stage 2 (ISR at 0x0183): second CLA read; only reached when Stage 1
     *   returned bit7=1.  If key still held (physically_held || cla_seen),
     *   returns key code → circular buffer → syscall 0x0D / CLI.
     * ISR ACK (OUT 0x01, data≠0) resets cla_seen=0 for the next frame.
     *
     * iff1=0 (Phantom ROM / monitor): ISR never fires; kbd_wait polls CLA
     *   directly.  CLA fields serve machine_inject_key(); FIFO serves physical
     *   keys typed in monitor mode.
     */
    if (!m->cpu.iff1) {
        /* Phantom ROM / monitor context (kbd_wait, iff1=0):
         * Serve machine_inject_key() CLA fields first, so injected keys (autoboot
         * Enter at stage1) are visible to the Phantom ROM kbd_wait polling loop.
         * Fall through to FIFO for physical keys (monitor mode, post-handoff).
         * Hardware: CLA read itself clears FOUND; if key still held, reasserts
         * within 200µs.  We mirror that by clearing found=0 here. */
        {
            int key_held = m->kbd.physically_held || (m->kbd.key_hold_frames > 0);
            int have_key = m->kbd.found || (key_held && m->kbd.cla_seen);
            m->kbd.cla_seen = 1;
            if (have_key) {
                m->kbd.found = 0;
                return m->kbd.key_code & 0x7Fu;  /* bit 7 = 0 → key present */
            }
        }
        /* No injected key: serve physical keys from FIFO. */
        if (m->kbd.fifo_head != m->kbd.fifo_tail) {
            uint8_t code = m->kbd.fifo[m->kbd.fifo_head];
            m->kbd.fifo_head = (m->kbd.fifo_head + 1) & 63;
            return code & 0x7Fu;  /* bit 7 = 0 → key present */
        }
        return 0x80u | m->kbd.fonct_bits;  /* no key; function bits in 0-6 (may be 0) */
    }
    int key_held = m->kbd.physically_held || (m->kbd.key_hold_frames > 0);
    int have_key = m->kbd.found || (key_held && m->kbd.cla_seen);
    m->kbd.cla_seen = 1;   /* mark that a CLA read has occurred this ISR cycle */
    if (have_key) {
        m->kbd.found = 0;  /* consume the 'new event' latch — mirrors HW: CLA read clears FOUND */
        return m->kbd.key_code & 0x7Fu;   /* bit 7 = 0 → key present */
    }
    /* No regular key: bit 7 = 1; bits 0-6 carry function key bitmask (may be 0).
     * Per §10.4 CLAVIER: "lorsque FOUND=0, la valeur lue correspond aux touches FONCTION". */
    return 0x80u | m->kbd.fonct_bits;
}

int keyboard_found(struct Smaky6 *m)
{
    /* Port 0x01 bit 2 (FOUND): 1 if the key is physically held OR the
     * hold-frames countdown is still running (key recently released). */
    return m->kbd.physically_held || (m->kbd.key_hold_frames > 0);
}

int keyboard_shift_break_pressed(struct Smaky6 *m)
{
    /* SHIFT+BREAK at boot: the phantom ROM kbd_wait detects keyboard code 0x1B.
     * This is injected via machine_inject_key() for the -break-to-monitor autoboot
     * path; SHIFT state is tracked here for reference only. */
    return m->kbd.shift_pressed;
}
