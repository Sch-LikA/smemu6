#ifndef DEBUG_NAVIGATION_H
#define DEBUG_NAVIGATION_H

#include <stdint.h>

/* Report whether follow/run-to-cursor navigation is currently allowed. */
int debug_navigation_can_follow(int paused, int run_to_cursor_active);

/* Classify a stacked return target as code-like for debugger hints. */
int debug_navigation_stack_target_is_code_like(uint16_t target, int has_code_symbol);

#endif /* DEBUG_NAVIGATION_H */