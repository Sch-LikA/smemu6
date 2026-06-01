#include "smaky6.h"

#ifndef SMAKY6_SM6_MAXMEM
#define SMAKY6_SM6_MAXMEM 0x4560u
#endif

#ifndef SMAKY6_SM6_OUTCAR
#define SMAKY6_SM6_OUTCAR 0x4550u
#endif

#define PROBE_ROW 10u

/* Render one labelled address/value probe line into the alpha screen. */
static void put_value_line(unsigned char row, const char *label,
                           unsigned short address, unsigned short value)
{
    char address_hex[5];
    char value_hex[5];

    smaky6_format_hex16(address_hex, address);
    smaky6_format_hex16(value_hex, value);

    smaky6_alpha_clear_row(row);
    smaky6_alpha_put_text_xy(row, 0u, label);
    smaky6_alpha_put_text_xy(row, 8u, address_hex);
    smaky6_alpha_put_text_xy(row, 14u, value_hex);
}

/* Display a few live SM6 workspace addresses using the standalone SDCC helpers. */
void main(void)
{
    smaky6_alpha_clear_screen();

    put_value_line(PROBE_ROW + 0u, "ALPHA", SMAKY6_SM6_ALPHA, 0u);
    put_value_line(PROBE_ROW + 1u, "OUTCAR", SMAKY6_SM6_OUTCAR,
                   (unsigned short)smaky6_peek8(SMAKY6_SM6_OUTCAR));
    put_value_line(PROBE_ROW + 2u, "MAXMEM", SMAKY6_SM6_MAXMEM,
                   smaky6_peek16(SMAKY6_SM6_MAXMEM));

    smaky6_alpha_clear_row(PROBE_ROW + 3u);
    smaky6_alpha_put_text_xy(PROBE_ROW + 3u, 0u, "SM6 SYMBOL PROBE");
}