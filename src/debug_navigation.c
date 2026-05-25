#include "debug_navigation.h"

int debug_navigation_can_follow(int paused, int run_to_cursor_active)
{
    return paused || run_to_cursor_active;
}

int debug_navigation_stack_target_is_code_like(uint16_t target, int has_code_symbol)
{
    return target >= 0x0100u || has_code_symbol;
}