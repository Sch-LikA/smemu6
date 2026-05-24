// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* psg.h – Smaky 6 PSG expansion-card wrapper */
#ifndef PSG_H
#define PSG_H

#include <stdint.h>

#define SMAKY6_PSG_CHIP_COUNT 4

typedef struct __PSG PSG;

struct Smaky6Psg {
    PSG *chip[SMAKY6_PSG_CHIP_COUNT];
    uint32_t chip_clock_hz;
    uint32_t sample_rate;
};

int smaky6_psg_init(struct Smaky6Psg *psg, uint32_t chip_clock_hz, uint32_t sample_rate);
void smaky6_psg_fini(struct Smaky6Psg *psg);
void smaky6_psg_reset(struct Smaky6Psg *psg);

void smaky6_psg_write_select(struct Smaky6Psg *psg, unsigned chip, uint8_t reg);
void smaky6_psg_write_data(struct Smaky6Psg *psg, unsigned chip, uint8_t value);
uint8_t smaky6_psg_read_data(const struct Smaky6Psg *psg, unsigned chip);

int16_t smaky6_psg_mix_sample(struct Smaky6Psg *psg);

#endif /* PSG_H */