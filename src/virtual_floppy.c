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
    uint16_t flags;
    uint16_t last_bytes;
    uint16_t load_addr;
    uint16_t entry_addr;
    uint8_t month_bcd;
    uint8_t year_bcd;
    struct VirtualFile *children;
    size_t child_count;
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

static char *build_path(const char *dir_path, const char *filename)
{
    size_t path_len = strlen(dir_path);
    size_t file_len = strlen(filename);
    char *full_path = malloc(path_len + 1 + file_len + 1);

    if (!full_path) {
        return NULL;
    }

    memcpy(full_path, dir_path, path_len);
    if (path_len > 0 && dir_path[path_len - 1] != '/') {
        full_path[path_len++] = '/';
    }
    memcpy(full_path + path_len, filename, file_len + 1);
    return full_path;
}

static uint8_t encode_bcd(unsigned value)
{
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static void encode_date_from_mtime(time_t mtime,
                                   uint8_t *out_month_bcd,
                                   uint8_t *out_year_bcd)
{
    struct tm *tm_info = localtime(&mtime);

    if (tm_info) {
        *out_month_bcd = encode_bcd((unsigned)(tm_info->tm_mon + 1));
        *out_year_bcd = encode_bcd((unsigned)((tm_info->tm_year + 1900) % 100));
    } else {
        *out_month_bcd = 0;
        *out_year_bcd = 0;
    }
}

static int is_container_type(const char *type)
{
    return strcmp(type, "DR") == 0;
}

static char *build_host_name(const char *prefix, const char *filename)
{
    size_t prefix_len;
    size_t file_len;
    char *host_name;

    if (!prefix || prefix[0] == '\0') {
        host_name = malloc(strlen(filename) + 1u);
        if (host_name) {
            strcpy(host_name, filename);
        }
        return host_name;
    }

    prefix_len = strlen(prefix);
    file_len = strlen(filename);
    host_name = malloc(prefix_len + 1u + file_len + 1u);
    if (!host_name) {
        return NULL;
    }

    memcpy(host_name, prefix, prefix_len);
    host_name[prefix_len] = '/';
    memcpy(host_name + prefix_len + 1u, filename, file_len + 1u);
    return host_name;
}

static int is_supported_type(const char *type)
{
    static const char *const types[] = {
        "SY", "SM", "ST", "SR", "LS", "FH",
        "BS", "IM", "KS", "DR", "HP", "RF",
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

static const char *find_json_value(const char *json, const char *key)
{
    char pattern[64];
    const char *cursor;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    cursor = strstr(json, pattern);
    if (!cursor) {
        return NULL;
    }

    cursor += strlen(pattern);
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != ':') {
        return NULL;
    }
    cursor++;
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

static int parse_json_integer(const char *json, const char *key,
                              unsigned max_value, unsigned *out_value,
                              int *out_present, const char *filename,
                              char *error, size_t error_size)
{
    const char *value = find_json_value(json, key);
    char *end = NULL;
    unsigned long parsed;

    if (!value) {
        *out_present = 0;
        return 0;
    }

    if (*value == '"') {
        char number[32];
        size_t len = 0;

        value++;
        while (value[len] && value[len] != '"') {
            if (len + 1 >= sizeof(number)) {
                set_error(error, error_size, "metadata value too long in %s", filename);
                return -1;
            }
            number[len] = value[len];
            len++;
        }
        if (value[len] != '"') {
            set_error(error, error_size, "unterminated metadata string in %s", filename);
            return -1;
        }
        number[len] = '\0';
        parsed = strtoul(number, &end, 0);
        if (!end || *end != '\0') {
            set_error(error, error_size, "invalid metadata integer in %s", filename);
            return -1;
        }
    } else {
        parsed = strtoul(value, &end, 0);
        if (end == value) {
            set_error(error, error_size, "invalid metadata integer in %s", filename);
            return -1;
        }
    }

    if (parsed > max_value) {
        set_error(error, error_size, "metadata value out of range in %s", filename);
        return -1;
    }

    *out_present = 1;
    *out_value = (unsigned)parsed;
    return 0;
}

static int parse_json_type(const char *json, char out_type[3], int *out_present,
                           const char *filename, char *error, size_t error_size)
{
    const char *value = find_json_value(json, "type");

    if (!value) {
        *out_present = 0;
        return 0;
    }
    if (value[0] != '"' || value[1] == '\0' || value[2] == '\0' || value[3] != '"') {
        set_error(error, error_size, "invalid metadata type in %s", filename);
        return -1;
    }

    out_type[0] = (char)toupper((unsigned char)value[1]);
    out_type[1] = (char)toupper((unsigned char)value[2]);
    out_type[2] = '\0';
    if (!is_supported_type(out_type)) {
        set_error(error, error_size, "unsupported metadata type in %s", filename);
        return -1;
    }

    *out_present = 1;
    return 0;
}

static int apply_sidecar_metadata(const char *dir_path, const char *filename,
                                  struct VirtualFile *file,
                                  char *error, size_t error_size)
{
    char *sidecar_name;
    char *sidecar_path;
    struct stat st;
    FILE *meta_file;
    char *json = NULL;
    size_t read_size;
    int type_present;
    char metadata_type[3];
    unsigned value;
    int present;

    sidecar_name = malloc(strlen(filename) + strlen(".meta.json") + 1u);
    if (!sidecar_name) {
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }
    snprintf(sidecar_name, strlen(filename) + strlen(".meta.json") + 1u,
             "%s.meta.json", filename);
    sidecar_path = build_path(dir_path, sidecar_name);
    if (!sidecar_path) {
        free(sidecar_name);
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }

    if (stat(sidecar_path, &st) != 0) {
        if (errno == ENOENT) {
            free(sidecar_name);
            free(sidecar_path);
            return 0;
        }
        set_error(error, error_size, "stat failed for %s", sidecar_name);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        set_error(error, error_size, "%s is not a regular file", sidecar_name);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    if (st.st_size <= 0 || st.st_size > 65536) {
        set_error(error, error_size, "%s is empty or too large", sidecar_name);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }

    meta_file = fopen(sidecar_path, "rb");
    if (!meta_file) {
        set_error(error, error_size, "cannot open %s", sidecar_name);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }

    json = malloc((size_t)st.st_size + 1u);
    if (!json) {
        fclose(meta_file);
        free(sidecar_name);
        free(sidecar_path);
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }

    read_size = fread(json, 1, (size_t)st.st_size, meta_file);
    fclose(meta_file);
    if (read_size != (size_t)st.st_size) {
        free(json);
        set_error(error, error_size, "short read for %s", sidecar_name);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    json[read_size] = '\0';

    if (parse_json_type(json, metadata_type, &type_present,
                        sidecar_name, error, error_size) != 0) {
        free(json);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    if (type_present && strcmp(metadata_type, file->type) != 0) {
        free(json);
        set_error(error, error_size, "metadata type mismatch in %s", sidecar_name);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }

    if (parse_json_integer(json, "flags", 0xFFFFu, &value, &present,
                           sidecar_name, error, error_size) != 0) {
        free(json);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    if (present) {
        file->flags = (uint16_t)value;
    }

    if (parse_json_integer(json, "load", 0xFFFFu, &value, &present,
                           sidecar_name, error, error_size) != 0) {
        free(json);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    if (present) {
        file->load_addr = (uint16_t)value;
    }

    if (parse_json_integer(json, "entry", 0xFFFFu, &value, &present,
                           sidecar_name, error, error_size) != 0) {
        free(json);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    if (present) {
        file->entry_addr = (uint16_t)value;
    }

    if (parse_json_integer(json, "date_month", 12u, &value, &present,
                           sidecar_name, error, error_size) != 0) {
        free(json);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    if (present) {
        file->month_bcd = value == 0 ? 0 : encode_bcd(value);
    }

    if (parse_json_integer(json, "date_year", 99u, &value, &present,
                           sidecar_name, error, error_size) != 0) {
        free(json);
        free(sidecar_name);
        free(sidecar_path);
        return -1;
    }
    if (present) {
        file->year_bcd = value == 0 ? 0 : encode_bcd(value);
    }

    free(json);
    free(sidecar_name);
    free(sidecar_path);
    return 0;
}

static int read_host_file(const char *dir_path, const char *filename,
                          uint8_t **out_data, size_t *out_size,
                          uint8_t *out_month_bcd, uint8_t *out_year_bcd,
                          char *error, size_t error_size)
{
    char *full_path = build_path(dir_path, filename);
    struct stat st;
    FILE *file;
    uint8_t *data;
    size_t nread;
    struct tm *tm_info;

    if (!full_path) {
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }

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
        free_virtual_files(files[i].children, files[i].child_count);
        free(files[i].host_name);
        free(files[i].data);
    }
    free(files);
}

static size_t virtual_file_sector_count(const struct VirtualFile *file)
{
    return (file->size + FLOPPY_SECTOR_BYTES - 1u) / FLOPPY_SECTOR_BYTES;
}

static void write_virtual_files_to_image(uint8_t *image,
                                         const struct VirtualFile *files,
                                         size_t file_count)
{
    for (size_t i = 0; i < file_count; i++) {
        uint8_t *dir_entry = image + i * 24u;
        size_t offset = (size_t)files[i].start_sector * FLOPPY_SECTOR_BYTES;

        memcpy(dir_entry, files[i].name, 8);
        memcpy(dir_entry + 8, files[i].type, 2);
        dir_entry[10] = (uint8_t)(files[i].start_sector & 0xFFu);
        dir_entry[11] = (uint8_t)(files[i].start_sector >> 8);
        dir_entry[12] = (uint8_t)(files[i].end_sector & 0xFFu);
        dir_entry[13] = (uint8_t)(files[i].end_sector >> 8);
        dir_entry[14] = (uint8_t)(files[i].flags & 0xFFu);
        dir_entry[15] = (uint8_t)(files[i].flags >> 8);
        dir_entry[16] = (uint8_t)(files[i].last_bytes & 0xFFu);
        dir_entry[17] = (uint8_t)(files[i].last_bytes >> 8);
        dir_entry[18] = (uint8_t)(files[i].load_addr >> 8);
        dir_entry[19] = (uint8_t)(files[i].load_addr & 0xFFu);
        dir_entry[20] = (uint8_t)(files[i].entry_addr >> 8);
        dir_entry[21] = (uint8_t)(files[i].entry_addr & 0xFFu);
        dir_entry[22] = files[i].month_bcd;
        dir_entry[23] = files[i].year_bcd;

        memcpy(image + offset, files[i].data, files[i].size);
    }
}

static int build_virtual_directory_image(struct VirtualFile *files,
                                         size_t file_count,
                                         size_t total_sectors,
                                         uint8_t **out_data,
                                         size_t *out_size,
                                         char *error,
                                         size_t error_size)
{
    uint8_t *data;
    size_t size;

    size = total_sectors * FLOPPY_SECTOR_BYTES;
    data = calloc(1, size);
    if (!data) {
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }

    write_virtual_files_to_image(data, files, file_count);
    *out_data = data;
    *out_size = size;
    return 0;
}

static unsigned decode_bcd(uint8_t value);
static void json_print_string(FILE *out, const char *text);

static int collect_virtual_files_internal(const char *path,
                                          const char *host_prefix,
                                          struct VirtualFile **out_files,
                                          size_t *out_file_count,
                                          size_t *out_total_sectors,
                                          char *error,
                                          size_t error_size)
{
    DIR *dir;
    struct dirent *entry;
    struct VirtualFile *files = NULL;
    size_t file_count = 0;
    size_t file_capacity = 0;
    size_t total_sectors = VFD_DIR_SECTORS;

    *out_files = NULL;
    *out_file_count = 0;
    *out_total_sectors = 0;

    dir = opendir(path);
    if (!dir) {
        set_error(error, error_size, "cannot open directory %s", path);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        struct VirtualFile file;
        char *entry_path;
        struct stat st;
        size_t sectors;

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (has_suffix(entry->d_name, ".meta.json")) {
            continue;
        }

        memset(&file, 0, sizeof(file));
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

        entry_path = build_path(path, entry->d_name);
        if (!entry_path) {
            closedir(dir);
            free_virtual_files(files, file_count);
            set_error(error, error_size, "out of memory", NULL);
            return -1;
        }
        if (stat(entry_path, &st) != 0) {
            free(entry_path);
            closedir(dir);
            free_virtual_files(files, file_count);
            set_error(error, error_size, "stat failed for %s", entry->d_name);
            return -1;
        }

        file.host_name = build_host_name(host_prefix, entry->d_name);
        if (!file.host_name) {
            free(entry_path);
            closedir(dir);
            free_virtual_files(files, file_count);
            set_error(error, error_size, "out of memory", NULL);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            size_t child_total_sectors = 0;

            if (!is_container_type(file.type)) {
                fprintf(stderr,
                        "virtual-floppy: ignoring directory host entry '%s' (directories must use NAME.DR)\n",
                        entry->d_name);
                free(file.host_name);
                free(entry_path);
                continue;
            }

            encode_date_from_mtime(st.st_mtime, &file.month_bcd, &file.year_bcd);
            if (collect_virtual_files_internal(entry_path, file.host_name,
                                               &file.children, &file.child_count,
                                               &child_total_sectors,
                                               error, error_size) != 0) {
                free(file.host_name);
                free(entry_path);
                closedir(dir);
                free_virtual_files(files, file_count);
                return -1;
            }
            if (build_virtual_directory_image(file.children, file.child_count,
                                              child_total_sectors,
                                              &file.data, &file.size,
                                              error, error_size) != 0) {
                free(file.host_name);
                free(entry_path);
                closedir(dir);
                free_virtual_files(file.children, file.child_count);
                free_virtual_files(files, file_count);
                return -1;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (read_host_file(path, entry->d_name, &file.data, &file.size,
                               &file.month_bcd, &file.year_bcd,
                               error, error_size) != 0) {
                free(file.host_name);
                free(entry_path);
                closedir(dir);
                free_virtual_files(files, file_count);
                return -1;
            }
        } else {
            fprintf(stderr,
                    "virtual-floppy: ignoring unsupported host entry '%s' (not a regular file or NAME.DR directory)\n",
                    entry->d_name);
            free(file.host_name);
            free(entry_path);
            continue;
        }

        if (apply_sidecar_metadata(path, entry->d_name, &file,
                                   error, error_size) != 0) {
            free(entry_path);
            closedir(dir);
            free(file.host_name);
            free(file.data);
            free_virtual_files(file.children, file.child_count);
            free_virtual_files(files, file_count);
            return -1;
        }

        free(entry_path);

        sectors = virtual_file_sector_count(&file);
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
                free_virtual_files(file.children, file.child_count);
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
        size_t sectors = virtual_file_sector_count(&files[i]);
        files[i].start_sector = (uint16_t)total_sectors;
        files[i].end_sector = (uint16_t)(total_sectors + sectors);
        files[i].last_bytes = (uint16_t)(files[i].size % FLOPPY_SECTOR_BYTES);
        total_sectors += sectors;
    }

    *out_files = files;
    *out_file_count = file_count;
    *out_total_sectors = total_sectors;
    return 0;
}

static int collect_virtual_files(const char *path,
                                 struct VirtualFile **out_files,
                                 size_t *out_file_count,
                                 size_t *out_image_size,
                                 char *error,
                                 size_t error_size)
{
    size_t total_sectors = 0;

    *out_files = NULL;
    *out_file_count = 0;
    *out_image_size = 0;

    if (collect_virtual_files_internal(path, NULL,
                                      out_files, out_file_count,
                                      &total_sectors,
                                      error, error_size) != 0) {
        return -1;
    }

    if (total_sectors > FLOPPY_TRACKS_77 * FLOPPY_SECTORS) {
        free_virtual_files(*out_files, *out_file_count);
        *out_files = NULL;
        *out_file_count = 0;
        set_error(error, error_size, "host directory does not fit on a 77-track floppy", NULL);
        return -1;
    }

    *out_image_size = total_sectors <= FLOPPY_TRACKS_40 * FLOPPY_SECTORS
                    ? FLOPPY_IMAGE_40
                    : FLOPPY_IMAGE_77;
    return 0;
}

static void json_print_manifest_entries(FILE *out,
                                        const struct VirtualFile *files,
                                        size_t file_count,
                                        uint16_t base_sector,
                                        size_t *printed_count)
{
    for (size_t i = 0; i < file_count; i++) {
        uint16_t absolute_start = (uint16_t)(base_sector + files[i].start_sector);
        uint16_t absolute_end = (uint16_t)(base_sector + files[i].end_sector);

        if (*printed_count > 0) {
            fprintf(out, ",\n");
        }
        fprintf(out, "    {\n");
        fprintf(out, "      \"host_name\": ");
        json_print_string(out, files[i].host_name);
        fprintf(out, ",\n      \"name\": ");
        json_print_string(out, files[i].name);
        fprintf(out, ",\n      \"type\": ");
        json_print_string(out, files[i].type);
        fprintf(out, ",\n      \"size_bytes\": %zu,\n", files[i].size);
        fprintf(out, "      \"start_sector\": %u,\n", absolute_start);
        fprintf(out, "      \"end_sector\": %u,\n", absolute_end);
        fprintf(out, "      \"encoded_start_sector\": %u,\n", files[i].start_sector);
        fprintf(out, "      \"encoded_end_sector\": %u,\n", files[i].end_sector);
        fprintf(out, "      \"flags\": %u,\n", files[i].flags);
        fprintf(out, "      \"load\": %u,\n", files[i].load_addr);
        fprintf(out, "      \"entry\": %u,\n", files[i].entry_addr);
        fprintf(out, "      \"is_container\": %s,\n",
                is_container_type(files[i].type) ? "true" : "false");
        fprintf(out, "      \"date_month\": %u,\n", decode_bcd(files[i].month_bcd));
        fprintf(out, "      \"date_year\": %u\n", decode_bcd(files[i].year_bcd));
        fprintf(out, "    }");
        (*printed_count)++;

        if (files[i].child_count > 0) {
            json_print_manifest_entries(out, files[i].children, files[i].child_count,
                                        absolute_start, printed_count);
        }
    }
}

static unsigned decode_bcd(uint8_t value)
{
    return (unsigned)((value >> 4) * 10u + (value & 0x0Fu));
}

static void json_print_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
        case '\\': fputs("\\\\", out); break;
        case '"':  fputs("\\\"", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (*p < 0x20) {
                fprintf(out, "\\u%04x", *p);
            } else {
                fputc(*p, out);
            }
            break;
        }
    }
    fputc('"', out);
}

int virtual_floppy_build_from_hostdir(const char *path,
                                      uint8_t **out_image,
                                      size_t *out_size,
                                      char *error,
                                      size_t error_size)
{
    struct VirtualFile *files = NULL;
    size_t file_count = 0;
    size_t image_size;
    uint8_t *image;

    if (!path || !out_image || !out_size) {
        set_error(error, error_size, "invalid arguments", NULL);
        return -1;
    }

    *out_image = NULL;
    *out_size = 0;

    if (collect_virtual_files(path, &files, &file_count, &image_size,
                              error, error_size) != 0) {
        return -1;
    }

    image = calloc(1, image_size);
    if (!image) {
        free_virtual_files(files, file_count);
        set_error(error, error_size, "out of memory", NULL);
        return -1;
    }

    write_virtual_files_to_image(image, files, file_count);

    free_virtual_files(files, file_count);
    *out_image = image;
    *out_size = image_size;
    return 0;
}

int virtual_floppy_dump_manifest(const char *path,
                                 FILE *out,
                                 char *error,
                                 size_t error_size)
{
    struct VirtualFile *files = NULL;
    size_t file_count = 0;
    size_t image_size = 0;

    if (!path || !out) {
        set_error(error, error_size, "invalid arguments", NULL);
        return -1;
    }

    if (collect_virtual_files(path, &files, &file_count, &image_size,
                              error, error_size) != 0) {
        return -1;
    }

    fprintf(out, "{\n");
    fprintf(out, "  \"source\": ");
    json_print_string(out, path);
    fprintf(out, ",\n  \"image_size\": %zu,\n", image_size);
    fprintf(out, "  \"tracks\": %u,\n",
            image_size == FLOPPY_IMAGE_77 ? FLOPPY_TRACKS_77 : FLOPPY_TRACKS_40);
    fprintf(out, "  \"files\": [\n");

    {
        size_t printed_count = 0;
        json_print_manifest_entries(out, files, file_count, 0, &printed_count);
        if (printed_count > 0) {
            fprintf(out, "\n");
        }
    }

    fprintf(out, "  ]\n}\n");
    free_virtual_files(files, file_count);
    return 0;
}