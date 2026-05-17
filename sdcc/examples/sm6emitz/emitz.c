#include "smaky6.h"

void smaky6_emit_text(const char *text);

volatile unsigned char smaky6_emit_text_returned;

void main(void)
{
    smaky6_alpha_clear_screen();

    smaky6_alpha_put_text_xy(8u, 0u, "SM6 RST20/06 CALL");
    smaky6_alpha_put_text_xy(9u, 0u, "HL -> ZTEXT");
    smaky6_alpha_put_text_xy(15u, 0u, "EXPECTED EMITTER LINE:");

    smaky6_emit_text("RST20/06 CALL OK");
    smaky6_emit_text_returned = 1u;
}