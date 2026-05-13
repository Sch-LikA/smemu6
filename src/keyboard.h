// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* keyboard.h – Smaky 6 keyboard controller */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <SDL2/SDL.h>

struct Smaky6;

void keyboard_init(struct Smaky6 *m);
void keyboard_fini(struct Smaky6 *m);

/* Feed an SDL key event (keydown / keyup) into the keyboard model */
void keyboard_event(struct Smaky6 *m, const SDL_KeyboardEvent *ev);

/*
 * Feed an SDL_TEXTINPUT event into the keyboard model.
 * Converts the UTF-8 text to Smaky display codes and pushes them to the FIFO.
 * Accepts printable ASCII (0x20-0x7E) plus the documented 2-byte UTF-8 Swiss-
 * French accented characters handled by ACCENT_TABLE in keyboard.c.
 * Use this for printable characters so that the host OS handles shift/Caps Lock.
 */
void keyboard_text_event(struct Smaky6 *m, const SDL_TextInputEvent *ev);

/* Call once per 50 Hz frame: decrements the post-KEYUP hold countdown */
void keyboard_frame_tick(struct Smaky6 *m);

/* Advance FOUND-reassert countdown by the given number of Z80 T-states.
 * Call after every z80_run() / z80_execute() slice. */
void keyboard_tick_cycles(struct Smaky6 *m, uint32_t cycles);

/*
 * Port 0x00 (CLA) read:
 *   bits[6:0] = keyboard code  (0 if no key pressed)
 *   bit 7     = NOT-FOUND      (1 = no key / function key)
 *   Reading clears the FOUND flip-flop.
 */
uint8_t keyboard_read_cla(struct Smaky6 *m);

/* Query the raw FOUND state (for INT polling if needed) */
int     keyboard_found(struct Smaky6 *m);
uint8_t keyboard_read_status(struct Smaky6 *m);

#endif /* KEYBOARD_H */
