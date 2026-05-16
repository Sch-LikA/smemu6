#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ST_RECORD_SIZE = 8,
    ST_NAME_SIZE = 6,
};

struct StRecord {
    uint16_t value;
    uint8_t raw_name[ST_NAME_SIZE];
    uint8_t high_bit_mask;
    char decoded_name[ST_NAME_SIZE + 1];
};

static void st_decode_name(struct StRecord *record)
{
    size_t out = 0;

    record->high_bit_mask = 0;

    for (size_t i = 0; i < ST_NAME_SIZE; ++i) {
        uint8_t raw = record->raw_name[ST_NAME_SIZE - 1 - i];
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

static int st_read_record(FILE *fp, struct StRecord *record)
{
    uint8_t raw[ST_RECORD_SIZE];
    size_t got = fread(raw, 1, sizeof(raw), fp);

    if (got == 0)
        return 0;
    if (got != sizeof(raw)) {
        fprintf(stderr, "short ST record: got %zu bytes\n", got);
        return -1;
    }

    record->value = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
    memcpy(record->raw_name, &raw[2], ST_NAME_SIZE);
    st_decode_name(record);
    return 1;
}

static void st_dump_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long size;
    size_t index = 0;

    if (!fp) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        exit(1);
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "%s: fseek failed\n", path);
        fclose(fp);
        exit(1);
    }

    size = ftell(fp);
    if (size < 0) {
        fprintf(stderr, "%s: ftell failed\n", path);
        fclose(fp);
        exit(1);
    }
    rewind(fp);

    printf("== %s ==\n", path);
    printf("size=%ld records=%ld remainder=%ld\n",
           size,
           size / ST_RECORD_SIZE,
           size % ST_RECORD_SIZE);

    for (;;) {
        struct StRecord record;
        int status = st_read_record(fp, &record);

        if (status == 0)
            break;
        if (status < 0) {
            fclose(fp);
            exit(1);
        }

        printf("%03zu value=%04X flags=%02X name=%-6s raw=",
               index,
               record.value,
               record.high_bit_mask,
               record.decoded_name[0] ? record.decoded_name : "<blank>");

        for (size_t i = 0; i < ST_NAME_SIZE; ++i) {
            printf("%s%02X", (i == 0) ? "" : " ", record.raw_name[i]);
        }
        putchar('\n');
        ++index;
    }

    putchar('\n');
    fclose(fp);
}

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