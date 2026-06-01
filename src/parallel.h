// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* parallel.h – Smaky 6 parallel interface (ports 0x02/0x03) */
#ifndef PARALLEL_H
#define PARALLEL_H

#include <stdint.h>

struct Smaky6;

/* Reset the parallel interface data and status latches. */
void parallel_init(struct Smaky6 *m);
/* No dynamic parallel-interface resources are owned today; kept for symmetry. */
void parallel_fini(struct Smaky6 *m);

/*
 * Port 0x02 (PAR):  bidirectional data
 * Port 0x03 (SPAR): status
 *   bit 0 = RDYP  (receiver ready)
 *   bit 1 = FULP  (transmitter full)
 *   bit 6 = S6
 *   bit 7 = S7
 */
/* Read the bidirectional data latch exposed on port 0x02. */
uint8_t parallel_read_data(struct Smaky6 *m);
/* Update the bidirectional data latch from a guest write to port 0x02. */
void    parallel_write_data(struct Smaky6 *m, uint8_t val);
/* Read the status bits exposed on port 0x03. */
uint8_t parallel_read_status(struct Smaky6 *m);

#endif /* PARALLEL_H */
