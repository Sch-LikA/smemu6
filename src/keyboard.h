// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* keyboard.h – Smaky 6 keyboard controller */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <SDL2/SDL.h>

struct Smaky6;

/* Initialize the strict keyboard model, including the power-on virtual Enter state. */
void keyboard_init(struct Smaky6 *m);

/* No explicit teardown is currently required for the keyboard subsystem. */
void keyboard_fini(struct Smaky6 *m);

/* Feed one SDL key event into the strict model.
 * Ordinary matrix keys latch an S471-resolved code, Shift and CAPS/LOCK select
 * the active layer, and F1..F7 update the separate non-matrix function bits. */
void keyboard_event(struct Smaky6 *m, const SDL_KeyboardEvent *ev);

/* Feed one SDL text-input event into the printable compatibility path. */
void keyboard_text_event(struct Smaky6 *m, const SDL_TextInputEvent *ev);

/* Call once per 50 Hz frame: handles boot-time virtual Enter release. */
void keyboard_frame_tick(struct Smaky6 *m);

/* Advance FOUND-reassert countdown by the given number of Z80 T-states.
 * Call after every z80_run() / z80_execute() slice. */
void keyboard_tick_cycles(struct Smaky6 *m, uint32_t cycles);

/*
 * Port 0x00 (CLA) read:
 *   FOUND=1 -> returns the latched ordinary-key code with bit 7 clear
 *   FOUND=0 -> returns fonct_bits with bit 7 clear for the 7 non-matrix function keys
 *   Reading CLA clears the FOUND latch and may schedule reassertion if the
 *   same ordinary key remains physically held.
 */
uint8_t keyboard_read_cla(struct Smaky6 *m);

/* Query the raw FOUND latch state without applying CLA-read side effects. */
int     keyboard_found(struct Smaky6 *m);

/* Port 0x01 (STATUS) read: bit 2 mirrors FOUND and bit 3 stays high. */
uint8_t keyboard_read_status(struct Smaky6 *m);

/* Function-key state ownership lives in the keyboard subsystem.
 * These helpers recompute the effective bitmask and update any compatibility mirrors. */
void keyboard_clear_all_function_bits(struct Smaky6 *m);
void keyboard_set_mouse_function_bits(struct Smaky6 *m, uint8_t bits);
void keyboard_acknowledge_function_bits(struct Smaky6 *m, uint8_t mask);

#endif /* KEYBOARD_H */
