#ifndef SMEMU6_DEBUG_HISTORY_H
#define SMEMU6_DEBUG_HISTORY_H

#include <stddef.h>
#include <stdint.h>

size_t debug_history_push(uint16_t *entries,
                          size_t count,
                          size_t capacity,
                          uint16_t value);

int debug_history_pop(uint16_t *entries,
                      size_t *count,
                      uint16_t *value_out);

#endif