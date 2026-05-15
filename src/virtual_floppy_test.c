// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
#include "floppy.h"
#include "virtual_floppy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_RMDIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define TEST_MKDIR(path) mkdir(path, 0700)
#define TEST_RMDIR(path) rmdir(path)
#endif

static int write_file_bytes(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (!file) {
        fprintf(stderr, "cannot create %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (fwrite(data, 1, size, file) != size) {
        fprintf(stderr, "short write for %s\n", path);
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}

static int write_pattern_file(const char *path, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (!file) {
        fprintf(stderr, "cannot create %s: %s\n", path, strerror(errno));
        return 1;
    }
    for (size_t index = 0; index < size; index++) {
        unsigned char value = (unsigned char)('A' + (index % 26u));
        if (fwrite(&value, 1, 1, file) != 1) {
            fprintf(stderr, "short write for %s\n", path);
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}

static unsigned read_u16_le(const uint8_t *bytes)
{
    return (unsigned)bytes[0] | ((unsigned)bytes[1] << 8);
}

static unsigned read_u16_be(const uint8_t *bytes)
{
    return ((unsigned)bytes[0] << 8) | (unsigned)bytes[1];
}

static int expect_bytes(const uint8_t *actual, const char *expected, size_t size,
                        const char *label)
{
    if (memcmp(actual, expected, size) != 0) {
        fprintf(stderr, "unexpected bytes for %s\n", label);
        return 1;
    }
    return 0;
}

static int expect_unsigned(unsigned actual, unsigned expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %u, got %u\n", label, expected, actual);
        return 1;
    }
    return 0;
}

static int manifest_contains(const char *manifest, const char *needle)
{
    if (!strstr(manifest, needle)) {
        fprintf(stderr, "manifest is missing %s\n", needle);
        return 1;
    }
    return 0;
}

static void cleanup_test_dir(const char *dir_path)
{
    char path[256];
    const char *files[] = {
        "ALPHA.BS",
        "BOX.DR/INNER.BS",
        "BOX.DR",
        "HELLO.SM",
        "HELLO.SM.meta.json",
    };

    for (size_t index = 0; index < sizeof(files) / sizeof(files[0]); index++) {
        snprintf(path, sizeof(path), "%s/%s", dir_path, files[index]);
        remove(path);
    }
    TEST_RMDIR(dir_path);
}

int main(void)
{
    static const unsigned char alpha_data[] = { 'A', 'B', 'C' };
    static const char *const sidecar_json =
        "{\n"
        "  \"type\": \"SM\",\n"
        "  \"flags\": 4660,\n"
        "  \"load\": 17921,\n"
        "  \"entry\": 22136,\n"
        "  \"date_month\": 12,\n"
        "  \"date_year\": 82\n"
        "}\n";
    const char *dir_path = "virtual_floppy_test_tmp";
    char alpha_path[256];
    char box_path[256];
    char inner_path[256];
    char hello_path[256];
    char sidecar_path[256];
    char error[256];
    uint8_t *image = NULL;
    size_t image_size = 0;
    FILE *manifest_file = NULL;
    long manifest_size_long;
    char *manifest = NULL;
    const uint8_t *entry0;
    const uint8_t *entry1;
    const uint8_t *entry2;
    const uint8_t *box_entry0;
    int result = 1;

    cleanup_test_dir(dir_path);
    if (TEST_MKDIR(dir_path) != 0) {
        fprintf(stderr, "cannot create %s: %s\n", dir_path, strerror(errno));
        return 1;
    }

    snprintf(alpha_path, sizeof(alpha_path), "%s/ALPHA.BS", dir_path);
    snprintf(box_path, sizeof(box_path), "%s/BOX.DR", dir_path);
    snprintf(inner_path, sizeof(inner_path), "%s/BOX.DR/INNER.BS", dir_path);
    snprintf(hello_path, sizeof(hello_path), "%s/HELLO.SM", dir_path);
    snprintf(sidecar_path, sizeof(sidecar_path), "%s/HELLO.SM.meta.json", dir_path);

    if (write_file_bytes(alpha_path, alpha_data, sizeof(alpha_data)) != 0) {
        goto cleanup;
    }
    if (TEST_MKDIR(box_path) != 0) {
        fprintf(stderr, "cannot create %s: %s\n", box_path, strerror(errno));
        goto cleanup;
    }
    if (write_file_bytes(inner_path, "DIR!", 4u) != 0) {
        goto cleanup;
    }
    if (write_pattern_file(hello_path, 257u) != 0) {
        goto cleanup;
    }
    if (write_file_bytes(sidecar_path, sidecar_json, strlen(sidecar_json)) != 0) {
        goto cleanup;
    }

    if (virtual_floppy_build_from_hostdir(dir_path, &image, &image_size,
                                          error, sizeof(error)) != 0) {
        fprintf(stderr, "build failed: %s\n", error);
        goto cleanup;
    }

    if (expect_unsigned((unsigned)image_size, FLOPPY_IMAGE_40, "image size") != 0) {
        goto cleanup;
    }

    entry0 = image;
    entry1 = image + 24u;
    entry2 = image + 48u;

    if (expect_bytes(entry0, "ALPHA   BS", 10u, "entry0 name/type") != 0 ||
        expect_unsigned(read_u16_le(entry0 + 10), 3u, "entry0 start") != 0 ||
        expect_unsigned(read_u16_le(entry0 + 12), 4u, "entry0 end") != 0 ||
        expect_unsigned(read_u16_le(entry0 + 14), 0u, "entry0 flags") != 0 ||
        expect_unsigned(read_u16_le(entry0 + 16), 3u, "entry0 last bytes") != 0) {
        goto cleanup;
    }

    if (expect_bytes(entry1, "BOX     DR", 10u, "entry1 name/type") != 0 ||
        expect_unsigned(read_u16_le(entry1 + 10), 4u, "entry1 start") != 0 ||
        expect_unsigned(read_u16_le(entry1 + 12), 8u, "entry1 end") != 0 ||
        expect_unsigned(read_u16_le(entry1 + 14), 0u, "entry1 flags") != 0 ||
        expect_unsigned(read_u16_le(entry1 + 16), 0u, "entry1 last bytes") != 0) {
        goto cleanup;
    }

    if (expect_bytes(entry2, "HELLO   SM", 10u, "entry2 name/type") != 0 ||
        expect_unsigned(read_u16_le(entry2 + 10), 8u, "entry2 start") != 0 ||
        expect_unsigned(read_u16_le(entry2 + 12), 10u, "entry2 end") != 0 ||
        expect_unsigned(read_u16_le(entry2 + 14), 4660u, "entry2 flags") != 0 ||
        expect_unsigned(read_u16_le(entry2 + 16), 1u, "entry2 last bytes") != 0 ||
        expect_unsigned(read_u16_be(entry2 + 18), 17921u, "entry2 load") != 0 ||
        expect_unsigned(read_u16_be(entry2 + 20), 22136u, "entry2 entry") != 0 ||
        expect_unsigned(entry2[22], 0x12u, "entry2 month BCD") != 0 ||
        expect_unsigned(entry2[23], 0x82u, "entry2 year BCD") != 0) {
        goto cleanup;
    }

    if (expect_bytes(image + (3u * FLOPPY_SECTOR_BYTES), "ABC", 3u, "entry0 data") != 0) {
        goto cleanup;
    }
    box_entry0 = image + (4u * FLOPPY_SECTOR_BYTES);
    if (expect_bytes(box_entry0, "INNER   BS", 10u, "box entry0 name/type") != 0 ||
        expect_unsigned(read_u16_le(box_entry0 + 10), 3u, "box entry0 start") != 0 ||
        expect_unsigned(read_u16_le(box_entry0 + 12), 4u, "box entry0 end") != 0 ||
        expect_unsigned(read_u16_le(box_entry0 + 16), 4u, "box entry0 last bytes") != 0) {
        goto cleanup;
    }
    if (expect_bytes(image + (7u * FLOPPY_SECTOR_BYTES), "DIR!", 4u, "box payload") != 0) {
        goto cleanup;
    }
    if (image[8u * FLOPPY_SECTOR_BYTES] != 'A' ||
        image[(8u * FLOPPY_SECTOR_BYTES) + 1u] != 'B' ||
        image[(9u * FLOPPY_SECTOR_BYTES)] != 'W') {
        fprintf(stderr, "unexpected HELLO.SM payload bytes\n");
        goto cleanup;
    }

    manifest_file = tmpfile();
    if (!manifest_file) {
        fprintf(stderr, "tmpfile failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (virtual_floppy_dump_manifest(dir_path, manifest_file, error, sizeof(error)) != 0) {
        fprintf(stderr, "manifest failed: %s\n", error);
        goto cleanup;
    }
    if (fseek(manifest_file, 0, SEEK_END) != 0) {
        fprintf(stderr, "fseek failed\n");
        goto cleanup;
    }
    manifest_size_long = ftell(manifest_file);
    if (manifest_size_long < 0) {
        fprintf(stderr, "ftell failed\n");
        goto cleanup;
    }
    if (fseek(manifest_file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "fseek failed\n");
        goto cleanup;
    }
    manifest = malloc((size_t)manifest_size_long + 1u);
    if (!manifest) {
        fprintf(stderr, "out of memory\n");
        goto cleanup;
    }
    if (fread(manifest, 1, (size_t)manifest_size_long, manifest_file)
        != (size_t)manifest_size_long) {
        fprintf(stderr, "short read for manifest\n");
        goto cleanup;
    }
    manifest[manifest_size_long] = '\0';

    if (manifest_contains(manifest, "\"tracks\": 40") != 0 ||
        manifest_contains(manifest, "\"host_name\": \"ALPHA.BS\"") != 0 ||
        manifest_contains(manifest, "\"host_name\": \"BOX.DR\"") != 0 ||
        manifest_contains(manifest, "\"host_name\": \"BOX.DR/INNER.BS\"") != 0 ||
        manifest_contains(manifest, "\"host_name\": \"HELLO.SM\"") != 0 ||
        manifest_contains(manifest, "\"start_sector\": 7") != 0 ||
        manifest_contains(manifest, "\"encoded_start_sector\": 3") != 0 ||
        manifest_contains(manifest, "\"flags\": 4660") != 0 ||
        manifest_contains(manifest, "\"load\": 17921") != 0 ||
        manifest_contains(manifest, "\"entry\": 22136") != 0 ||
        manifest_contains(manifest, "\"date_month\": 12") != 0 ||
        manifest_contains(manifest, "\"date_year\": 82") != 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    free(manifest);
    if (manifest_file) {
        fclose(manifest_file);
    }
    free(image);
    cleanup_test_dir(dir_path);
    return result;
}