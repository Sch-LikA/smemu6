#include "debug_history.h"

#include <string.h>

/* Push one address into the bounded debugger history, deduplicating the top
 * entry and sliding older entries out when capacity is full. */
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

/* Pop the newest debugger history entry if one is available. */
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