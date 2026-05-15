// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
#ifndef VIRTUAL_FLOPPY_H
#define VIRTUAL_FLOPPY_H

#include <stddef.h>
#include <stdint.h>

int virtual_floppy_build_from_hostdir(const char *path,
                                      uint8_t **out_image,
                                      size_t *out_size,
                                      char *error,
                                      size_t error_size);

#endif /* VIRTUAL_FLOPPY_H */