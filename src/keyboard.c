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
        if (wr == 0x4596u) {
            uint8_t code = m->kbd.fifo[m->kbd.fifo_head];
            m->kbd.fifo_head = (m->kbd.fifo_head + 1) & 63;
            m->bus[0x4596u] = code & 0x7Fu;
            wr = 0x4597u;
            m->bus[0x457Cu] = 0x97u;
            m->bus[0x457Du] = 0x45u;
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
     * Hardware model for the two-stage SAMOS ISR keyboard pipeline:
     *
     * Stage 1 (at 0x0160): First CLA read in the ISR cycle.  If 'found'=1
     *   (new key event), we return the key code and clear 'found'.  Stage 1
     *   stores it in 0x457E (syscall 0x0E path) and returns early.
     *   We set cla_seen=1 so Stage 2 knows a CLA read has already occurred.
     *
     * Stage 2 (at 0x0183): Second CLA read, reached only when Stage 1 returned
     *   0x80.  If key_held=1 AND cla_seen=1 (i.e. the ISR ACK already cleared
     *   'found' and Stage 1 confirmed no new event), we return the key code.
     *   This feeds the circular buffer that syscall 0x0D / the CLI polls.
     *
     * The ISR ACK (OUT port 0x01 with non-zero data) resets cla_seen=0 so the
     * cycle starts fresh every 50 Hz frame.
     *
     * Monitor mode (iff1=0): interrupts are disabled, the ISR never fires.
     * The SYSMON monitor polls CLA directly in a spin loop.  Physical keyboard
     * keys are held in the FIFO (keyboard_frame_tick() skips circ-buf drain
     * when iff1=0), so drain from FIFO here instead.
     */
    if (!m->cpu.iff1) {
        /* Monitor / NMI context: serve physical keys directly from FIFO. */
        if (m->kbd.fifo_head != m->kbd.fifo_tail) {
            uint8_t code = m->kbd.fifo[m->kbd.fifo_head];
            m->kbd.fifo_head = (m->kbd.fifo_head + 1) & 63;
            return code & 0x7Fu;  /* bit 7 = 0 → key present */
        }
        return 0x80u;  /* no key */
    }
    int key_held = m->kbd.physically_held || (m->kbd.key_hold_frames > 0);
    int have_key = m->kbd.found || (key_held && m->kbd.cla_seen);
    m->kbd.cla_seen = 1;   /* mark that a CLA read has occurred this ISR cycle */
    if (have_key) {
        m->kbd.found = 0;  /* consume the 'new event' latch */
        return m->kbd.key_code & 0x7Fu;   /* bit 7 = 0 → key present */
    }
    return 0x80u;  /* bit 7 = 1 → no key */
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
