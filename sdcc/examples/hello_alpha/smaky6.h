/* Minimal first-target Smaky 6 helpers for standalone SDCC examples. */

#ifndef SMAKY6_H
#define SMAKY6_H

#ifdef SMAKY6_HAVE_GENERATED_SM6_SYMBOLS
#include "generated_sm6_symbols_sdcc.h"
#endif

#define SMAKY6_ALPHA_COLS 64u

#ifndef SMAKY6_SM6_ALPHA
#define SMAKY6_SM6_ALPHA 0x4000u
#endif

#define SMAKY6_ALPHA_RAM ((volatile unsigned char *)SMAKY6_SM6_ALPHA)

static inline unsigned short smaky6_alpha_offset(unsigned char row, unsigned char col)
{
    return (unsigned short)row * SMAKY6_ALPHA_COLS + col;
}

static inline void smaky6_alpha_put_text(unsigned short offset, const char *text)
{
    volatile unsigned char *cursor = SMAKY6_ALPHA_RAM + offset;

    while (*text != '\0') {
        *cursor++ = (unsigned char)*text++;
    }
}

static inline void smaky6_alpha_fill(unsigned short offset, unsigned short count,
                                     unsigned char value)
{
    volatile unsigned char *cursor = SMAKY6_ALPHA_RAM + offset;

    while (count-- != 0u) {
        *cursor++ = value;
    }
}

static inline void smaky6_alpha_put_text_xy(unsigned char row, unsigned char col,
                                            const char *text)
{
    smaky6_alpha_put_text(smaky6_alpha_offset(row, col), text);
}

static inline void smaky6_alpha_clear_row(unsigned char row)
{
    smaky6_alpha_fill(smaky6_alpha_offset(row, 0u), SMAKY6_ALPHA_COLS, ' ');
}

#endif