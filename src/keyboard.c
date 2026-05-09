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
/*
 * Non-printable keys handled via SDL_KEYDOWN.
 * Printable characters (letters, digits, punctuation, space) are handled
 * via SDL_TEXTINPUT (keyboard_text_event) so that the host OS applies the
 * correct shift / Caps Lock / dead-key state, giving lowercase by default.
 *
 * SDL_SCANCODE_ESCAPE omitted: 0x1B is the Smaky 6 ä display code, not ESC.
 * The BREAK key generates NMI (PAUSE / F11 in main.c).
 */
static const struct { SDL_Scancode scan; uint8_t code; } KEY_TABLE[] = {
    { SDL_SCANCODE_RETURN,    0x0D },
    { SDL_SCANCODE_BACKSPACE, 0x08 },
    { SDL_SCANCODE_DELETE,    0x7F },   /* DEL */
    { SDL_SCANCODE_F8,        0x1E },   /* MACRO  → « */
    { SDL_SCANCODE_F9,        0x1F },   /* DEFINE → » */
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
    m->kbd.repeat_scan     = SDL_SCANCODE_UNKNOWN;
    m->kbd.fifo_head       = 0;
    m->kbd.fifo_tail       = 0;
    m->kbd.samos_loaded    = 0;

    /* Simulate real hardware power-on state: the FOUND latch is undefined at
     * power-on and typically powers up asserted.  The Phantom ROM keyboard wait
     * at 0x003E (CALL 0x00FD) reads CLA; if FOUND=1 it exits immediately with
     * A=key_code.  Key code 0x00 = Enter → ROM selects DX0 floppy boot.
     * Without this the emulator loops forever at the keyboard wait menu.
     * The latch is consumed (found→0) on the first CLA read, so it does not
     * interfere with user input once the machine is running. */
    m->kbd.found     = 1;
    m->kbd.key_code  = 0x00;  /* Enter → boot from DX0 */
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

    /* Detect when SAMOS has installed its 50 Hz ISR, which happens AFTER the
     * boot-menu keyboard wait (kbd_wait at 0x00B5).  The execution order is:
     *
     *   JP 0x0105 → CALL 0x0095 (SAMOS init)
     *     0x0095: fill 0x454A..0x45F9 with 0x00
     *     0x00A4: LD (0x4595),A    ; sentinel written — but kbd_wait NOT YET
     *     0x00B5: IN A,(0x00)      ; kbd_wait — boot menu key read here
     *     ... (key exits the wait loop) ...
     *     0x00CD: LD HL,0x003E
     *     0x00D0: LD (0x4566),HL   ; ISR vector installed ← trigger HERE
     *
     * Using bus[0x4595]==0x80 fires too early (before kbd_wait).
     * Using bus[0x4566..7]==0x003E fires after kbd_wait but before the first
     * ISR executes with iff1=0 — which is what we need to prevent FIFO
     * entries from leaking into Stage 1's CLA read. */
    if (!m->kbd.samos_loaded) {
        uint16_t vec = (uint16_t)m->bus[0x4566u] | ((uint16_t)m->bus[0x4567u] << 8);
        if (vec == 0x003Eu)
            m->kbd.samos_loaded = 1;
    }

    /* Feed pending keys from the FIFO into the SAMOS circular buffer.
     * Skip when iff1=0 (monitor mode): the monitor polls CLA directly via
     * keyboard_read_cla(), so keys must stay in the FIFO for that path.
     *
     * Drain the entire FIFO each frame (not just one entry per frame).
     * The guard sentinel at ~0x45B6 prevents overflow.  Draining all pending
     * keys matches how machine_inject_to_circ_buf() works for autoboot.
     * Writing one-per-frame would require SAMOS to read and consume a key
     * within a single 20ms frame before the next key can enter — which is
     * fine at human typing speed but creates an unnecessary 20ms floor
     * between each character. */
    if (!m->cpu.iff1) return;  /* monitor mode: leave FIFO for CLA path */
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
        uint8_t code = m->kbd.fifo[m->kbd.fifo_head];
        m->kbd.fifo_head = (m->kbd.fifo_head + 1) & 63;
        m->bus[wr] = code & 0x7Fu;
        wr++;
        m->bus[0x457Cu] = (uint8_t)(wr & 0xFFu);
        m->bus[0x457Du] = (uint8_t)(wr >> 8);
        /* Arm SAMOS ISR Stage 4 auto-repeat (0x01DF–0x0206):
         *   0x4558 = initial-delay countdown (0x23 = 35 frames = 700 ms)
         *   0x4577 = key code to re-inject when countdown reaches zero
         * Cleared by KEYUP in keyboard_event() when the held key is released. */
        m->bus[0x4558u] = 0x23u;
        m->bus[0x4577u] = code & 0x7Fu;
        if (m->dbg.trace_kbd)
            fprintf(stderr, "[kbd_tick] circ[0x%04X] <- 0x%02X ('%c')  ptr now 0x%04X  [ptr]=0x%02X\n",
                    (unsigned)(wr-1), (unsigned)(code & 0x7Fu),
                    (code >= 0x20 && code < 0x7F) ? (char)(code & 0x7Fu) : '?',
                    (unsigned)wr, (unsigned)m->bus[wr]);
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

    /* "Touches de fonction" — 7 function keys set/clear a bitmask.
     * keyboard_read_cla() returns this when no regular key is pending. */
    {
        static const struct { SDL_Scancode scan; uint8_t bit; } FONCT[] = {
            { SDL_SCANCODE_RCTRL,       0x01 }, /* CHANGE  */
            { SDL_SCANCODE_APPLICATION, 0x02 }, /* SEARCH  (Menu / App key) */
            { SDL_SCANCODE_F10,         0x04 }, /* SHOW    */
            { SDL_SCANCODE_LALT,        0x08 }, /* COPY    */
            { SDL_SCANCODE_LCTRL,       0x10 }, /* CURSOR  */
            { SDL_SCANCODE_RALT,        0x20 }, /* PROGRA  (AltGr) */
            { SDL_SCANCODE_LGUI,        0x40 }, /* KILL    (Left Windows / Super) */
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

    if (ev->type == SDL_KEYUP) {
        /* Cancel SAMOS auto-repeat when the user releases any regular key.
         * Zeroing 0x4558 stops the Stage 4 countdown before the next
         * re-injection fires.  Safe to do unconditionally: if samos_loaded
         * is still 0 the address is uninitialised RAM and no repeat is running. */
        if (m->kbd.samos_loaded)
            m->bus[0x4558u] = 0u;
        return;
    }
    if (ev->type != SDL_KEYDOWN) return;
    if (ev->repeat) return;   /* ignore SDL key-repeat; SAMOS handles its own repeat */

    SDL_Scancode scan = ev->keysym.scancode;
    /* Track scancode so we can cancel repeat on the matching KEYUP. */
    m->kbd.repeat_scan = scan;
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
 * Feed an SDL_TEXTINPUT event into the keyboard model.
 *
 * SDL_TEXTINPUT is generated by the OS after applying shift, Caps Lock,
 * dead keys, and compose sequences.  This gives us lowercase letters by
 * default and uppercase with Shift — the host keyboard layout does the
 * right thing automatically.
 *
 * ev->text is UTF-8.  We accept single-byte codepoints in 0x20–0x7E
 * (printable ASCII) and ignore anything else.  Multi-byte UTF-8 sequences
 * (accented characters) require a Unicode→Smaky mapping table; that is
 * left for a follow-on implementation.
 */
void keyboard_text_event(struct Smaky6 *m, const SDL_TextInputEvent *ev)
{
    const unsigned char *p = (const unsigned char *)ev->text;
    for (; *p != '\0'; p++) {
        uint8_t ch = *p;
        /* Accept only single-byte printable ASCII (0x20–0x7E). */
        if (ch < 0x20 || ch > 0x7E) continue;
        /* Skip the byte if it is the start of a multi-byte UTF-8 sequence. */
        if (ch >= 0x80) continue;

        int next = (m->kbd.fifo_tail + 1) & 63;
        if (next != m->kbd.fifo_head) {
            m->kbd.fifo[m->kbd.fifo_tail] = ch;
            m->kbd.fifo_tail = next;
            if (m->dbg.trace_kbd)
                fprintf(stderr, "[kbd_text] pushed 0x%02X ('%c') to FIFO\n",
                        (unsigned)ch, (char)ch);
        } else {
            if (m->dbg.trace_kbd)
                fprintf(stderr, "[kbd_text] FIFO FULL — dropped 0x%02X\n",
                        (unsigned)ch);
        }
    }
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
        /* No injected key: serve physical keys from FIFO.
         * Only done when samos_loaded=0 (Phantom ROM / pre-SAMOS boot).
         * Once SAMOS has run EI, iff1=0 means the Z80 is inside the ISR
         * (IFF1 cleared on INT acknowledgment).  In that case the FIFO
         * must NOT be popped here — keyboard_frame_tick() drains it to
         * the SAMOS circular buffer instead. */
        if (!m->kbd.samos_loaded && m->kbd.fifo_head != m->kbd.fifo_tail) {
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
