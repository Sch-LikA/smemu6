// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* psg.c – Smaky 6 PSG expansion-card wrapper */
#include "psg.h"

#include <emu2149.h>
#include <stddef.h>
#include <string.h>

/* Return one AY core pointer when the wrapper and chip index are valid. */
static PSG *smaky6_psg_chip(const struct Smaky6Psg *psg, unsigned chip)
{
    if (!psg || chip >= SMAKY6_PSG_CHIP_COUNT) {
        return NULL;
    }
    return psg->chip[chip];
}

/* Allocate all four AY-compatible cores, cache the requested clock/sample-rate,
 * and reset the wrapper to a clean power-on state. */
int smaky6_psg_init(struct Smaky6Psg *psg, uint32_t chip_clock_hz, uint32_t sample_rate)
{
    unsigned chip;

    if (!psg || chip_clock_hz == 0 || sample_rate == 0) {
        return -1;
    }

    memset(psg, 0, sizeof(*psg));
    psg->chip_clock_hz = chip_clock_hz;
    psg->sample_rate = sample_rate;

    for (chip = 0; chip < SMAKY6_PSG_CHIP_COUNT; chip++) {
        psg->chip[chip] = PSG_new(chip_clock_hz, sample_rate);
        if (!psg->chip[chip]) {
            smaky6_psg_fini(psg);
            return -1;
        }
        PSG_setVolumeMode(psg->chip[chip], 2);
        PSG_reset(psg->chip[chip]);
    }

    return 0;
}

/* Delete all allocated AY cores and clear the cached configuration fields. */
void smaky6_psg_fini(struct Smaky6Psg *psg)
{
    unsigned chip;

    if (!psg) {
        return;
    }

    for (chip = 0; chip < SMAKY6_PSG_CHIP_COUNT; chip++) {
        if (psg->chip[chip]) {
            PSG_delete(psg->chip[chip]);
            psg->chip[chip] = NULL;
        }
    }

    psg->chip_clock_hz = 0;
    psg->sample_rate = 0;
}

/* Reset every allocated AY core back to its default register state. */
void smaky6_psg_reset(struct Smaky6Psg *psg)
{
    unsigned chip;

    if (!psg) {
        return;
    }

    for (chip = 0; chip < SMAKY6_PSG_CHIP_COUNT; chip++) {
        if (psg->chip[chip]) {
            PSG_reset(psg->chip[chip]);
        }
    }
}

/* Write one register-select byte to the addressed AY chip. */
void smaky6_psg_write_select(struct Smaky6Psg *psg, unsigned chip, uint8_t reg)
{
    PSG *ay = smaky6_psg_chip(psg, chip);

    if (!ay) {
        return;
    }

    PSG_writeIO(ay, 0, reg);
}

/* Write one data byte to the currently selected register of the addressed AY chip. */
void smaky6_psg_write_data(struct Smaky6Psg *psg, unsigned chip, uint8_t value)
{
    PSG *ay = smaky6_psg_chip(psg, chip);

    if (!ay) {
        return;
    }

    PSG_writeIO(ay, 1, value);
}

/* Read back the currently selected register value from the addressed AY chip. */
uint8_t smaky6_psg_read_data(const struct Smaky6Psg *psg, unsigned chip)
{
    PSG *ay = smaky6_psg_chip(psg, chip);

    if (!ay) {
        return 0xFF;
    }

    return PSG_readIO(ay);
}

/* Mix one signed sample from all installed AY chips and clamp to int16. */
int16_t smaky6_psg_mix_sample(struct Smaky6Psg *psg)
{
    int mix = 0;
    unsigned chip;

    if (!psg) {
        return 0;
    }

    for (chip = 0; chip < SMAKY6_PSG_CHIP_COUNT; chip++) {
        if (psg->chip[chip]) {
            mix += PSG_calc(psg->chip[chip]);
        }
    }

    if (mix > 32767) {
        return 32767;
    }
    if (mix < -32768) {
        return -32768;
    }
    return (int16_t)mix;
}