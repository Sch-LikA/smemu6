#ifndef SMAKY6_ST_SYMBOLS_H
#define SMAKY6_ST_SYMBOLS_H

#include <stddef.h>
#include <stdint.h>

enum {
    SMAKY6_ST_RECORD_SIZE = 8,
    SMAKY6_ST_NAME_SIZE = 6,
};

struct Smaky6StRecord {
    uint16_t value;
    uint8_t raw_name[SMAKY6_ST_NAME_SIZE];
    uint8_t high_bit_mask;
    char decoded_name[SMAKY6_ST_NAME_SIZE + 1];
};

struct Smaky6StTable {
    struct Smaky6StRecord *records;
    size_t count;
};

int smaky6_st_load_file(const char *path, struct Smaky6StTable *table);
void smaky6_st_free_table(struct Smaky6StTable *table);
const struct Smaky6StRecord *smaky6_st_find_by_name(const struct Smaky6StTable *table,
                                                    const char *name);

#endif