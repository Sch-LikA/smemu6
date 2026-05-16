#include "smaky6_st_symbols.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void smaky6_st_decode_name(struct Smaky6StRecord *record)
{
    size_t out = 0;

    record->high_bit_mask = 0;

    for (size_t i = 0; i < SMAKY6_ST_NAME_SIZE; ++i) {
        uint8_t raw = record->raw_name[SMAKY6_ST_NAME_SIZE - 1 - i];
        uint8_t ch = (uint8_t)(raw & 0x7Fu);

        if ((raw & 0x80u) != 0u)
            record->high_bit_mask |= (uint8_t)(1u << i);

        if (ch == ' ' && out == 0)
            continue;

        if (ch < 0x20u || ch >= 0x7Fu)
            ch = '?';

        record->decoded_name[out++] = (char)ch;
    }

    while (out > 0 && record->decoded_name[out - 1] == ' ')
        --out;

    record->decoded_name[out] = '\0';
}

int smaky6_st_load_file(const char *path, struct Smaky6StTable *table)
{
    FILE *fp;
    long size;
    size_t count;

    if (!table)
        return -1;

    table->records = NULL;
    table->count = 0;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "%s: fseek failed\n", path);
        fclose(fp);
        return -1;
    }

    size = ftell(fp);
    if (size < 0) {
        fprintf(stderr, "%s: ftell failed\n", path);
        fclose(fp);
        return -1;
    }

    if ((size % SMAKY6_ST_RECORD_SIZE) != 0) {
        fprintf(stderr, "%s: invalid ST size %ld\n", path, size);
        fclose(fp);
        return -1;
    }

    rewind(fp);

    count = (size_t)(size / SMAKY6_ST_RECORD_SIZE);
    table->records = calloc(count, sizeof(*table->records));
    if (!table->records) {
        fprintf(stderr, "%s: allocation failed\n", path);
        fclose(fp);
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        uint8_t raw[SMAKY6_ST_RECORD_SIZE];
        size_t got = fread(raw, 1, sizeof(raw), fp);

        if (got != sizeof(raw)) {
            fprintf(stderr, "%s: short ST record at index %zu\n", path, i);
            smaky6_st_free_table(table);
            fclose(fp);
            return -1;
        }

        table->records[i].value = ((uint16_t)raw[0] << 8) | (uint16_t)raw[1];
        memcpy(table->records[i].raw_name, &raw[2], SMAKY6_ST_NAME_SIZE);
        smaky6_st_decode_name(&table->records[i]);
    }

    table->count = count;
    fclose(fp);
    return 0;
}

void smaky6_st_free_table(struct Smaky6StTable *table)
{
    if (!table)
        return;

    free(table->records);
    table->records = NULL;
    table->count = 0;
}

const struct Smaky6StRecord *smaky6_st_find_by_name(const struct Smaky6StTable *table,
                                                    const char *name)
{
    if (!table || !name)
        return NULL;

    for (size_t i = 0; i < table->count; ++i) {
        if (strcmp(table->records[i].decoded_name, name) == 0)
            return &table->records[i];
    }

    return NULL;
}