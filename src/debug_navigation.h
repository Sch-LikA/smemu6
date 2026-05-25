#ifndef DEBUG_NAVIGATION_H
#define DEBUG_NAVIGATION_H

#include <stdint.h>

int debug_navigation_can_follow(int paused, int run_to_cursor_active);
int debug_navigation_stack_target_is_code_like(uint16_t target, int has_code_symbol);

#endif /* DEBUG_NAVIGATION_H */