#include "smaky6_st_symbols.h"

#include <stdio.h>
#include <stdlib.h>

/* Load one ST file and dump every decoded record in a human-readable form. */
static void st_dump_file(const char *path)
{
    struct Smaky6StTable table;

    if (smaky6_st_load_file(path, &table) != 0)
        exit(1);

    printf("== %s ==\n", path);
    printf("records=%zu\n", table.count);

    for (size_t index = 0; index < table.count; ++index) {
        const struct Smaky6StRecord *record = &table.records[index];
        printf("%03zu value=%04X flags=%02X name=%-6s raw=",
               index,
               record->value,
               record->high_bit_mask,
               record->decoded_name[0] ? record->decoded_name : "<blank>");

        for (size_t i = 0; i < SMAKY6_ST_NAME_SIZE; ++i) {
            printf("%s%02X", (i == 0) ? "" : " ", record->raw_name[i]);
        }
        putchar('\n');
    }

    putchar('\n');
    smaky6_st_free_table(&table);
}

/* Dump one or more archived ST symbol tables to stdout for inspection. */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <symbol-table.st> [more.st ...]\n", argv[0]);
        return 2;
    }

    for (int i = 1; i < argc; ++i)
        st_dump_file(argv[i]);

    return 0;
}