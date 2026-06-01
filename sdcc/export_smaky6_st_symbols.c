#include "smaky6_st_symbols.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Open an output stream or map '-' to stdout for command-line tools. */
static FILE *st_open_output(const char *path)
{
    if (!path || strcmp(path, "-") == 0)
        return stdout;

    return fopen(path, "w");
}

/* Return the last path component without modifying the source string. */
static const char *st_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Derive the filename stem used for generated identifiers and metadata. */
static void st_stem(char *dst, size_t dst_size, const char *path)
{
    const char *base = st_basename(path);
    size_t len = strcspn(base, ".");

    if (len >= dst_size)
        len = dst_size - 1;

    memcpy(dst, base, len);
    dst[len] = '\0';
}

/* Convert an arbitrary filename stem into a lowercase C identifier fragment. */
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

/* Convert an arbitrary filename stem into an uppercase identifier fragment. */
static void st_upper_identifier(char *dst, size_t dst_size, const char *stem)
{
    st_identifier(dst, dst_size, stem);

    for (size_t i = 0; dst[i] != '\0'; ++i)
        dst[i] = (char)toupper((unsigned char)dst[i]);
}

/* Emit one JSON string literal with the minimal escaping this tool needs. */
static void emit_json_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (size_t i = 0; text[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)text[i];

        if (ch == '\\' || ch == '"')
            fputc('\\', out);
        fputc((int)ch, out);
    }
    fputc('"', out);
}

/* Emit the decoded symbol table as a JSON document for downstream tooling. */
static void emit_json(FILE *out, const char *path, const struct Smaky6StTable *table)
{
    char stem[32];

    st_stem(stem, sizeof(stem), path);

    fprintf(out, "{\n");
    fprintf(out, "  \"file\": ");
    emit_json_string(out, st_basename(path));
    fprintf(out, ",\n  \"stem\": ");
    emit_json_string(out, stem);
    fprintf(out, ",\n  \"record_count\": %zu,\n  \"records\": [\n", table->count);

    for (size_t i = 0; i < table->count; ++i) {
        const struct Smaky6StRecord *record = &table->records[i];
        const char *best_name = smaky6_st_best_name(record);

        fprintf(out, "    {\"index\": %zu, \"value\": %u, \"value_hex\": \"0x%04X\", ",
               i,
               record->value,
               record->value);
        fprintf(out, "\"high_bit_mask\": \"0x%02X\", \"name\": ", record->high_bit_mask);
        emit_json_string(out, record->decoded_name);
        if (strcmp(best_name, record->decoded_name) != 0) {
            fprintf(out, ", \"best_name\": ");
            emit_json_string(out, best_name);
        }
        fprintf(out, ", \"raw_name_hex\": \"");
        for (size_t j = 0; j < SMAKY6_ST_NAME_SIZE; ++j)
            fprintf(out, "%s%02X", (j == 0) ? "" : " ", record->raw_name[j]);
        fprintf(out, "\"}%s\n", (i + 1 == table->count) ? "" : ",");
    }

    fprintf(out, "  ]\n}\n");
}

/* Emit a host-side C header containing the decoded symbol table as static data. */
static void emit_header(FILE *out, const char *path, const struct Smaky6StTable *table)
{
    char stem[32];
    char ident[32];

    st_stem(stem, sizeof(stem), path);
    st_identifier(ident, sizeof(ident), stem);

    fprintf(out, "#ifndef GENERATED_SMAKY6_ST_%s_H\n", ident);
    fprintf(out, "#define GENERATED_SMAKY6_ST_%s_H\n\n", ident);
    fprintf(out, "#include <stddef.h>\n#include <stdint.h>\n\n");
    fprintf(out, "struct GeneratedSmaky6StEntry {\n");
    fprintf(out, "    uint16_t value;\n");
    fprintf(out, "    uint8_t high_bit_mask;\n");
    fprintf(out, "    const char *name;\n");
    fprintf(out, "    const char *best_name;\n");
    fprintf(out, "};\n\n");
    fprintf(out, "static const struct GeneratedSmaky6StEntry smaky6_%s_symbols[] = {\n", ident);

    for (size_t i = 0; i < table->count; ++i) {
        const struct Smaky6StRecord *record = &table->records[i];
        fprintf(out, "    {0x%04X, 0x%02X, \"%s\", \"%s\"},\n",
               record->value,
               record->high_bit_mask,
               record->decoded_name,
               smaky6_st_best_name(record));
    }

    fprintf(out, "};\n\n");
    fprintf(out, "static const size_t smaky6_%s_symbol_count = %zu;\n\n", ident, table->count);
    fprintf(out, "#endif\n");
}

/* Emit a compact SDCC-friendly header with one constant per exported symbol. */
static void emit_sdcc_header(FILE *out, const char *path, const struct Smaky6StTable *table)
{
    char stem[32];
    char ident[32];

    st_stem(stem, sizeof(stem), path);
    st_upper_identifier(ident, sizeof(ident), stem);

    fprintf(out, "#ifndef GENERATED_SMAKY6_SDCC_%s_H\n", ident);
    fprintf(out, "#define GENERATED_SMAKY6_SDCC_%s_H\n\n", ident);

    for (size_t i = 0; i < table->count; ++i) {
        const struct Smaky6StRecord *record = &table->records[i];
        const char *best_name = smaky6_st_best_name(record);
        char decoded_ident[64];
        char best_ident[64];

        st_upper_identifier(decoded_ident, sizeof(decoded_ident), record->decoded_name);
        fprintf(out, "#ifndef SMAKY6_%s_%s\n", ident, decoded_ident);
        fprintf(out, "#define SMAKY6_%s_%s 0x%04Xu\n", ident, decoded_ident, record->value);
        fprintf(out, "#endif\n");

        if (strcmp(best_name, record->decoded_name) != 0) {
            st_upper_identifier(best_ident, sizeof(best_ident), best_name);
            fprintf(out, "#ifndef SMAKY6_%s_%s\n", ident, best_ident);
            fprintf(out, "#define SMAKY6_%s_%s 0x%04Xu\n", ident, best_ident, record->value);
            fprintf(out, "#endif\n");
        }
    }

    fprintf(out, "\n#endif\n");
}

/* Emit a CALM/assembler include with one .equ per exported symbol. */
static void emit_sdcc_asm(FILE *out, const char *path, const struct Smaky6StTable *table)
{
    char stem[32];
    char ident[32];

    st_stem(stem, sizeof(stem), path);
    st_upper_identifier(ident, sizeof(ident), stem);

    fprintf(out, "; Generated from %s\n", st_basename(path));

    for (size_t i = 0; i < table->count; ++i) {
        const struct Smaky6StRecord *record = &table->records[i];
        const char *best_name = smaky6_st_best_name(record);
        char decoded_ident[64];
        char best_ident[64];

        st_upper_identifier(decoded_ident, sizeof(decoded_ident), record->decoded_name);
        fprintf(out, "SMAKY6_%s_%s .equ 0x%04X\n", ident, decoded_ident, record->value);

        if (strcmp(best_name, record->decoded_name) != 0) {
            st_upper_identifier(best_ident, sizeof(best_ident), best_name);
            fprintf(out, "SMAKY6_%s_%s .equ 0x%04X\n", ident, best_ident, record->value);
        }
    }
}

/* Convert one archived ST file into JSON, host-C header, SDCC header, or
 * assembler constants according to the selected CLI mode. */
int main(int argc, char **argv)
{
    struct Smaky6StTable table;
    FILE *out;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s <--json|--header|--sdcc-header|--sdcc-asm> <symbol-table.st> [output-file|-]\n", argv[0]);
        return 2;
    }

    if (smaky6_st_load_file(argv[2], &table) != 0)
        return 1;

    out = st_open_output((argc == 4) ? argv[3] : "-");
    if (!out) {
        perror("open output");
        smaky6_st_free_table(&table);
        return 1;
    }

    if (strcmp(argv[1], "--json") == 0) {
        emit_json(out, argv[2], &table);
    } else if (strcmp(argv[1], "--header") == 0) {
        emit_header(out, argv[2], &table);
    } else if (strcmp(argv[1], "--sdcc-header") == 0) {
        emit_sdcc_header(out, argv[2], &table);
    } else if (strcmp(argv[1], "--sdcc-asm") == 0) {
        emit_sdcc_asm(out, argv[2], &table);
    } else {
        fprintf(stderr, "unknown mode: %s\n", argv[1]);
        if (out != stdout)
            fclose(out);
        smaky6_st_free_table(&table);
        return 2;
    }

    if (out != stdout)
        fclose(out);
    smaky6_st_free_table(&table);
    return 0;
}