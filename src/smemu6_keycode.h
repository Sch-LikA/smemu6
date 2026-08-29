// SPDX-License-Identifier: GPL-3.0-or-later
/* smemu6 keycode.h — portable logical keycodes.
 *
 * Generated from the installed SDL2 header so numeric values are
 * byte-compatible with SDL's enum. The SDL backend maps its raw
 * device codes to these with an identity cast; any other input
 * backend (web, HID, ...) maps its own codes onto this same space.
 * The core keyboard subsystem depends only on this header — never SDL.
 */
#ifndef SMEMU6_Keycode_H
#define SMEMU6_Keycode_H

#include <stdint.h>

typedef uint16_t smemu6_keycode;

enum SMEMU6_Keycode_id_id {
    SMEMU6_KD_BACKSLASH      = 0,
    SMEMU6_KD_BACKSPACE      = 0,
    SMEMU6_KD_DELETE         = 0,
    SMEMU6_KD_ESCAPE         = 0,
    SMEMU6_KD_RETURN         = 0,
    SMEMU6_KD_TAB            = 0,
    SMEMU6_KD_UNKNOWN        = 0,
    SMEMU6_KD_SPACE          = 32,
    SMEMU6_KD_EXCLAIM        = 33,
    SMEMU6_KD_QUOTEDBL       = 34,
    SMEMU6_KD_HASH           = 35,
    SMEMU6_KD_DOLLAR         = 36,
    SMEMU6_KD_PERCENT        = 37,
    SMEMU6_KD_AMPERSAND      = 38,
    SMEMU6_KD_LEFTPAREN      = 40,
    SMEMU6_KD_RIGHTPAREN     = 41,
    SMEMU6_KD_ASTERISK       = 42,
    SMEMU6_KD_PLUS           = 43,
    SMEMU6_KD_COMMA          = 44,
    SMEMU6_KD_MINUS          = 45,
    SMEMU6_KD_PERIOD         = 46,
    SMEMU6_KD_SLASH          = 47,
    SMEMU6_KD_0              = 48,
    SMEMU6_KD_1              = 49,
    SMEMU6_KD_2              = 50,
    SMEMU6_KD_3              = 51,
    SMEMU6_KD_4              = 52,
    SMEMU6_KD_5              = 53,
    SMEMU6_KD_6              = 54,
    SMEMU6_KD_7              = 55,
    SMEMU6_KD_8              = 56,
    SMEMU6_KD_9              = 57,
    SMEMU6_KD_COLON          = 58,
    SMEMU6_KD_SEMICOLON      = 59,
    SMEMU6_KD_LESS           = 60,
    SMEMU6_KD_EQUALS         = 61,
    SMEMU6_KD_GREATER        = 62,
    SMEMU6_KD_QUESTION       = 63,
    SMEMU6_KD_AT             = 64,
    SMEMU6_KD_LEFTBRACKET    = 91,
    SMEMU6_KD_QUOTE          = 92,
    SMEMU6_KD_RIGHTBRACKET   = 93,
    SMEMU6_KD_CARET          = 94,
    SMEMU6_KD_UNDERSCORE     = 95,
    SMEMU6_KD_BACKQUOTE      = 96,
    SMEMU6_KD_a              = 97,
    SMEMU6_KD_b              = 98,
    SMEMU6_KD_c              = 99,
    SMEMU6_KD_d              = 100,
    SMEMU6_KD_e              = 101,
    SMEMU6_KD_f              = 102,
    SMEMU6_KD_g              = 103,
    SMEMU6_KD_h              = 104,
    SMEMU6_KD_i              = 105,
    SMEMU6_KD_j              = 106,
    SMEMU6_KD_k              = 107,
    SMEMU6_KD_l              = 108,
    SMEMU6_KD_m              = 109,
    SMEMU6_KD_n              = 110,
    SMEMU6_KD_o              = 111,
    SMEMU6_KD_p              = 112,
    SMEMU6_KD_q              = 113,
    SMEMU6_KD_r              = 114,
    SMEMU6_KD_s              = 115,
    SMEMU6_KD_t              = 116,
    SMEMU6_KD_u              = 117,
    SMEMU6_KD_v              = 118,
    SMEMU6_KD_w              = 119,
    SMEMU6_KD_x              = 120,
    SMEMU6_KD_y              = 121,
    SMEMU6_KD_z              = 122,

    SMEMU6_NUM_KEYCODES = 123 /* count / range bound */
};

#endif /* SMEMU6_Keycode_H */
