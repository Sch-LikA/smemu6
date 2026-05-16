/* Minimal first-target Smaky 6 helpers for standalone SDCC examples. */

#ifndef SMAKY6_H
#define SMAKY6_H

#define SMAKY6_ALPHA_COLS 64u
#define SMAKY6_ALPHA_RAM ((volatile unsigned char *)0x4000u)

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

static inline void smaky6_alpha_put_text_xy(unsigned char row, unsigned char col,
                                            const char *text)
{
    smaky6_alpha_put_text(smaky6_alpha_offset(row, col), text);
}

#endif