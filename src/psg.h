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

/* Allocate and reset the four AY-compatible cores that back the optional PSG card. */
int smaky6_psg_init(struct Smaky6Psg *psg, uint32_t chip_clock_hz, uint32_t sample_rate);
/* Release every AY core owned by the wrapper and clear its cached configuration. */
void smaky6_psg_fini(struct Smaky6Psg *psg);
/* Reset all four AY cores back to their power-on register state. */
void smaky6_psg_reset(struct Smaky6Psg *psg);

/* Write one register-select byte to the chosen AY chip. */
void smaky6_psg_write_select(struct Smaky6Psg *psg, unsigned chip, uint8_t reg);
/* Write one data byte to the currently selected register of the chosen AY chip. */
void smaky6_psg_write_data(struct Smaky6Psg *psg, unsigned chip, uint8_t value);
/* Read the currently selected register value back from the chosen AY chip. */
uint8_t smaky6_psg_read_data(const struct Smaky6Psg *psg, unsigned chip);

/* Mix one signed audio sample from all installed AY chips, clamping to int16. */
int16_t smaky6_psg_mix_sample(struct Smaky6Psg *psg);

#endif /* PSG_H */