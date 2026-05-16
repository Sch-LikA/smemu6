/* Minimal first-target Smaky 6 helpers for standalone SDCC examples. */

#ifndef SMAKY6_H
#define SMAKY6_H

#define SMAKY6_ALPHA_RAM ((volatile unsigned char *)0x4000u)

static inline void smaky6_alpha_put_text(unsigned short offset, const char *text)
{
    volatile unsigned char *cursor = SMAKY6_ALPHA_RAM + offset;

    while (*text != '\0') {
        *cursor++ = (unsigned char)*text++;
    }
}

#endif