#include "smaky6.h"

/* External text emitter entry provided by the matching assembly helper. */
void smaky6_emit_text(const char *text);

/* Exercise the RST20/06 text-emitter helper and leave a visible post-call marker. */
void main(void)
{
    smaky6_alpha_clear_screen();

    smaky6_alpha_put_text_xy(8u, 0u, "SM6 RST20/06 CALL");
    smaky6_alpha_put_text_xy(9u, 0u, "HL -> ZTEXT");
    smaky6_alpha_put_text_xy(15u, 0u, "EXPECTED EMITTER LINE:");

    smaky6_emit_text("RST20/06 CALL OK");
    smaky6_alpha_put_text_xy(18u, 0u, "AFTER CALL");
}