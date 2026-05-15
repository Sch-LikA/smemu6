// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
#include "virtual_floppy.h"

#include "floppy.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define VFD_DIR_SECTORS 3u
#define VFD_MAX_ENTRIES ((VFD_DIR_SECTORS * FLOPPY_SECTOR_BYTES) / 24u)

struct VirtualFile {
    char name[9];
    char type[3];
    char *host_name;
    uint8_t *data;
    size_t size;
    uint16_t start_sector;
    uint16_t end_sector;
    uint16_t last_bytes;
    uint8_t month_bcd;
    uint8_t year_bcd;
};

static void set_error(char *error, size_t error_size, const char *fmt, const char *value)
{
    if (!error || error_size == 0) {
        return;
    }
    if (value) {
        snprintf(error, error_size, fmt, value);
    } else {
        snprintf(error, error_size, "%s", fmt);
    }
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (name_len < suffix_len) {
        return 0;
    }
    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static uint8_t encode_bcd(unsigned value)
{
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static int is_supported_type(const char *type)
{
    static const char *const types[] = {
        "SY", "SM", "ST", "SR", "LS", "FH",
        "BS", "IM", "KS", "HP", "RF",
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (strcmp(type, types[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_visible_name(const char *filename, char out_name[9], char out_type[3])
{
    const char *dot = strrchr(filename, '.');
    size_t name_len;

    if (!dot || dot == filename || dot[1] == '\0' || dot[2] == '\0' || dot[3] != '\0') {
        return -1;
    }

    name_len = (size_t)(dot - filename);
    if (name_len == 0 || name_len > 8) {
        return -1;
    }

    memset(out_name, ' ', 8);
    out_name[8] = '\0';
    out_type[0] = (char)toupper((unsigned char)dot[1]);
    out_type[1] = (char)toupper((unsigned char)dot[2]);
    out_type[2] = '\0';

    if (!is_supported_type(out_type)) {
        return -1;
    }

    for (size_t i = 0; i < name_len; i++) {
        unsigned char ch = (unsigned char)filename[i];
        if (!(isalnum(ch) || ch == '_')) {
            return -1;
        }
        out_name[i] = (char)toupper(ch);
    }
    return 0;
}

static int read_host_file(const char *dir_path, const char *filename,
                          uint8_t **out_data, size_t *out_size,
                          uint8_t *out_month_bcd, uint8_t *out_year_bcd,
                          char *error, size_t error_size)
{
    size_t path_len = strlen(dir_path);
    size_t file_len = strlen(filename);
    char *full_path = malloc(path_len + 1 + file_len + 1);
    struct stat st;
    FILE *file;
    uint8_t *data;
    size_t nread;
    struct tm *tm_info;

    if (!full_path) {
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }

    memcpy(full_path, dir_path, path_len);
    if (path_len > 0 && dir_path[path_len - 1] != '/') {
        full_path[path_len++] = '/';
    }
    memcpy(full_path + path_len, filename, file_len + 1);

    if (stat(full_path, &st) != 0) {
        set_error(error, error_size, "stat failed for %s", filename);
        free(full_path);
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        set_error(error, error_size, "%s is not a regular file", filename);
        free(full_path);
        return -1;
    }
    if (st.st_size <= 0) {
        set_error(error, error_size, "%s is empty or unreadable", filename);
        free(full_path);
        return -1;
    }

    file = fopen(full_path, "rb");
    if (!file) {
        set_error(error, error_size, "cannot open %s", filename);
        free(full_path);
        return -1;
    }

    data = malloc((size_t)st.st_size);
    if (!data) {
        fclose(file);
        free(full_path);
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }

    nread = fread(data, 1, (size_t)st.st_size, file);
    fclose(file);
    if (nread != (size_t)st.st_size) {
        free(data);
        free(full_path);
        set_error(error, error_size, "short read for %s", filename);
        return -1;
    }

    tm_info = localtime(&st.st_mtime);
    if (tm_info) {
        *out_month_bcd = encode_bcd((unsigned)(tm_info->tm_mon + 1));
        *out_year_bcd = encode_bcd((unsigned)((tm_info->tm_year + 1900) % 100));
    } else {
        *out_month_bcd = 0;
        *out_year_bcd = 0;
    }

    *out_data = data;
    *out_size = (size_t)st.st_size;
    free(full_path);
    return 0;
}

static int compare_virtual_files(const void *lhs, const void *rhs)
{
    const struct VirtualFile *a = lhs;
    const struct VirtualFile *b = rhs;
    int cmp = memcmp(a->name, b->name, 8);
    if (cmp != 0) {
        return cmp;
    }
    return memcmp(a->type, b->type, 2);
}

static void free_virtual_files(struct VirtualFile *files, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        free(files[i].host_name);
        free(files[i].data);
    }
    free(files);
}

int virtual_floppy_build_from_hostdir(const char *path,
                                      uint8_t **out_image,
                                      size_t *out_size,
                                      char *error,
                                      size_t error_size)
{
    DIR *dir;
    struct dirent *entry;
    struct VirtualFile *files = NULL;
    size_t file_count = 0;
    size_t file_capacity = 0;
    size_t total_sectors = VFD_DIR_SECTORS;
    size_t image_size;
    uint8_t *image;

    if (!path || !out_image || !out_size) {
        set_error(error, error_size, "invalid arguments", NULL);
        return -1;
    }

    *out_image = NULL;
    *out_size = 0;

    dir = opendir(path);
    if (!dir) {
        set_error(error, error_size, "cannot open directory %s", path);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        struct VirtualFile file;
        size_t sectors;

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (has_suffix(entry->d_name, ".meta.json")) {
            continue;
        }
        if (parse_visible_name(entry->d_name, file.name, file.type) != 0) {
            fprintf(stderr,
                    "virtual-floppy: ignoring unsupported host entry '%s' (use NAME.TT with 1-8 chars and known 2-char type)\n",
                    entry->d_name);
            continue;
        }
        if (file_count >= VFD_MAX_ENTRIES) {
            closedir(dir);
            free_virtual_files(files, file_count);
            set_error(error, error_size, "too many host files for one floppy directory", NULL);
            return -1;
        }

        memset(&file, 0, sizeof(file));
        if (parse_visible_name(entry->d_name, file.name, file.type) != 0) {
            continue;
        }
        if (read_host_file(path, entry->d_name, &file.data, &file.size,
                           &file.month_bcd, &file.year_bcd,
                           error, error_size) != 0) {
            closedir(dir);
            free_virtual_files(files, file_count);
            return -1;
        }
        file.host_name = malloc(strlen(entry->d_name) + 1);
        if (!file.host_name) {
            closedir(dir);
            free(file.data);
            free_virtual_files(files, file_count);
            set_error(error, error_size, "out of memory", NULL);
            return -1;
        }
        strcpy(file.host_name, entry->d_name);

        sectors = (file.size + FLOPPY_SECTOR_BYTES - 1u) / FLOPPY_SECTOR_BYTES;
        file.start_sector = (uint16_t)total_sectors;
        file.end_sector = (uint16_t)(total_sectors + sectors);
        file.last_bytes = (uint16_t)(file.size % FLOPPY_SECTOR_BYTES);
        total_sectors += sectors;

        if (file_count == file_capacity) {
            size_t next_capacity = file_capacity == 0 ? 8 : file_capacity * 2;
            struct VirtualFile *next_files = realloc(files, next_capacity * sizeof(*next_files));
            if (!next_files) {
                closedir(dir);
                free(file.host_name);
                free(file.data);
                free_virtual_files(files, file_count);
                set_error(error, error_size, "out of memory", NULL);
                return -1;
            }
            files = next_files;
            file_capacity = next_capacity;
        }

        files[file_count++] = file;
    }
    closedir(dir);

    qsort(files, file_count, sizeof(*files), compare_virtual_files);

    total_sectors = VFD_DIR_SECTORS;
    for (size_t i = 0; i < file_count; i++) {
        size_t sectors = (files[i].size + FLOPPY_SECTOR_BYTES - 1u) / FLOPPY_SECTOR_BYTES;
        files[i].start_sector = (uint16_t)total_sectors;
        files[i].end_sector = (uint16_t)(total_sectors + sectors);
        files[i].last_bytes = (uint16_t)(files[i].size % FLOPPY_SECTOR_BYTES);
        total_sectors += sectors;
    }

    if (total_sectors > FLOPPY_TRACKS_77 * FLOPPY_SECTORS) {
        free_virtual_files(files, file_count);
        set_error(error, error_size, "host directory does not fit on a 77-track floppy", NULL);
        return -1;
    }

    image_size = total_sectors <= FLOPPY_TRACKS_40 * FLOPPY_SECTORS
               ? FLOPPY_IMAGE_40
               : FLOPPY_IMAGE_77;
    image = calloc(1, image_size);
    if (!image) {
        free_virtual_files(files, file_count);
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }

    for (size_t i = 0; i < file_count; i++) {
        uint8_t *dir_entry = image + i * 24u;
        size_t offset = (size_t)files[i].start_sector * FLOPPY_SECTOR_BYTES;

        memcpy(dir_entry, files[i].name, 8);
        memcpy(dir_entry + 8, files[i].type, 2);
        dir_entry[10] = (uint8_t)(files[i].start_sector & 0xFFu);
        dir_entry[11] = (uint8_t)(files[i].start_sector >> 8);
        dir_entry[12] = (uint8_t)(files[i].end_sector & 0xFFu);
        dir_entry[13] = (uint8_t)(files[i].end_sector >> 8);
        dir_entry[16] = (uint8_t)(files[i].last_bytes & 0xFFu);
        dir_entry[17] = (uint8_t)(files[i].last_bytes >> 8);
        dir_entry[22] = files[i].month_bcd;
        dir_entry[23] = files[i].year_bcd;

        memcpy(image + offset, files[i].data, files[i].size);
    }

    free_virtual_files(files, file_count);
    *out_image = image;
    *out_size = image_size;
    return 0;
}