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

static inline volatile unsigned char *smaky6_ptr(unsigned short address)
{
    return (volatile unsigned char *)address;
}

static inline unsigned short smaky6_alpha_offset(unsigned char row, unsigned char col)
{
    return (unsigned short)row * SMAKY6_ALPHA_COLS + col;
}

static inline unsigned char smaky6_peek8(unsigned short address)
{
    return *smaky6_ptr(address);
}

static inline unsigned short smaky6_peek16(unsigned short address)
{
    unsigned short low = smaky6_peek8(address);
    unsigned short high = smaky6_peek8(address + 1u);

    return low | (unsigned short)(high << 8);
}

static inline char smaky6_hex_digit(unsigned char value)
{
    value &= 0x0Fu;
    return (value < 10u) ? (char)('0' + value) : (char)('A' + (value - 10u));
}

static inline void smaky6_format_hex8(char *dst, unsigned char value)
{
    dst[0] = smaky6_hex_digit((unsigned char)(value >> 4));
    dst[1] = smaky6_hex_digit(value);
    dst[2] = '\0';
}

static inline void smaky6_format_hex16(char *dst, unsigned short value)
{
    dst[0] = smaky6_hex_digit((unsigned char)(value >> 12));
    dst[1] = smaky6_hex_digit((unsigned char)(value >> 8));
    dst[2] = smaky6_hex_digit((unsigned char)(value >> 4));
    dst[3] = smaky6_hex_digit((unsigned char)value);
    dst[4] = '\0';
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