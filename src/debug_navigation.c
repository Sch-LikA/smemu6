#include "debug_navigation.h"

/* Allow navigation actions that would move debugger focus away from the live
 * PC only while execution is paused or an explicit run-to-cursor mode is active. */
int debug_navigation_can_follow(int paused, int run_to_cursor_active)
{
    return paused || run_to_cursor_active;
}

/* Treat low-memory stack targets as code only when a symbol lookup already
 * says so; otherwise reserve the code-like classification for normal code space. */
int debug_navigation_stack_target_is_code_like(uint16_t target, int has_code_symbol)
{
    return target >= 0x0100u || has_code_symbol;
}