#include "debug_history.h"

#include <string.h>

size_t debug_history_push(uint16_t *entries,
                          size_t count,
                          size_t capacity,
                          uint16_t value)
{
    if (!entries || capacity == 0) {
        return 0;
    }
    if (count > capacity) {
        count = capacity;
    }
    if (count > 0 && entries[count - 1] == value) {
        return count;
    }
    if (count >= capacity) {
        memmove(&entries[0], &entries[1], (capacity - 1u) * sizeof(entries[0]));
        count = capacity - 1u;
    }
    entries[count++] = value;
    return count;
}

int debug_history_pop(uint16_t *entries,
                      size_t *count,
                      uint16_t *value_out)
{
    (void)entries;

    if (!count || !value_out || *count == 0) {
        return 0;
    }

    *value_out = entries[--(*count)];
    return 1;
}