#include "smaky6_st_symbols.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *st_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void st_stem(char *dst, size_t dst_size, const char *path)
{
    const char *base = st_basename(path);
    size_t len = strcspn(base, ".");

    if (len >= dst_size)
        len = dst_size - 1;

    memcpy(dst, base, len);
    dst[len] = '\0';
}

static void st_identifier(char *dst, size_t dst_size, const char *stem)
{
    size_t out = 0;

    for (size_t i = 0; stem[i] != '\0' && out + 1 < dst_size; ++i) {
        unsigned char ch = (unsigned char)stem[i];

        if (isalnum(ch)) {
            dst[out++] = (char)tolower(ch);
        } else {
            dst[out++] = '_';
        }
    }

    dst[out] = '\0';
}

static void emit_json_string(const char *text)
{
    putchar('"');
    for (size_t i = 0; text[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)text[i];

        if (ch == '\\' || ch == '"')
            putchar('\\');
        putchar((int)ch);
    }
    putchar('"');
}

static void emit_json(const char *path, const struct Smaky6StTable *table)
{
    char stem[32];

    st_stem(stem, sizeof(stem), path);

    printf("{\n");
    printf("  \"file\": ");
    emit_json_string(st_basename(path));
    printf(",\n  \"stem\": ");
    emit_json_string(stem);
    printf(",\n  \"record_count\": %zu,\n  \"records\": [\n", table->count);

    for (size_t i = 0; i < table->count; ++i) {
        const struct Smaky6StRecord *record = &table->records[i];

        printf("    {\"index\": %zu, \"value\": %u, \"value_hex\": \"0x%04X\", ",
               i,
               record->value,
               record->value);
        printf("\"high_bit_mask\": \"0x%02X\", \"name\": ", record->high_bit_mask);
        emit_json_string(record->decoded_name);
        printf(", \"raw_name_hex\": \"");
        for (size_t j = 0; j < SMAKY6_ST_NAME_SIZE; ++j)
            printf("%s%02X", (j == 0) ? "" : " ", record->raw_name[j]);
        printf("\"}%s\n", (i + 1 == table->count) ? "" : ",");
    }

    printf("  ]\n}\n");
}

static void emit_header(const char *path, const struct Smaky6StTable *table)
{
    char stem[32];
    char ident[32];

    st_stem(stem, sizeof(stem), path);
    st_identifier(ident, sizeof(ident), stem);

    printf("#ifndef GENERATED_SMAKY6_ST_%s_H\n", ident);
    printf("#define GENERATED_SMAKY6_ST_%s_H\n\n", ident);
    printf("#include <stddef.h>\n#include <stdint.h>\n\n");
    printf("struct GeneratedSmaky6StEntry {\n");
    printf("    uint16_t value;\n");
    printf("    uint8_t high_bit_mask;\n");
    printf("    const char *name;\n");
    printf("};\n\n");
    printf("static const struct GeneratedSmaky6StEntry smaky6_%s_symbols[] = {\n", ident);

    for (size_t i = 0; i < table->count; ++i) {
        const struct Smaky6StRecord *record = &table->records[i];
        printf("    {0x%04X, 0x%02X, \"%s\"},\n",
               record->value,
               record->high_bit_mask,
               record->decoded_name);
    }

    printf("};\n\n");
    printf("static const size_t smaky6_%s_symbol_count = %zu;\n\n", ident, table->count);
    printf("#endif\n");
}

int main(int argc, char **argv)
{
    struct Smaky6StTable table;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <--json|--header> <symbol-table.st>\n", argv[0]);
        return 2;
    }

    if (smaky6_st_load_file(argv[2], &table) != 0)
        return 1;

    if (strcmp(argv[1], "--json") == 0) {
        emit_json(argv[2], &table);
    } else if (strcmp(argv[1], "--header") == 0) {
        emit_header(argv[2], &table);
    } else {
        fprintf(stderr, "unknown mode: %s\n", argv[1]);
        smaky6_st_free_table(&table);
        return 2;
    }

    smaky6_st_free_table(&table);
    return 0;
}