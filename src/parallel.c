// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* parallel.c – Smaky 6 parallel interface (ports 0x02/0x03) */
#include "machine_internal.h"
#include "parallel.h"

/* Direct field access: struct Smaky6 fully visible via machine_internal.h */

/* Reset the simple parallel-interface latches to their idle state. */
void parallel_init(struct Smaky6 *m)
{
    m->par.data   = 0x00u;
    m->par.status = 0x01u;  /* RDYP=1 */
}

/* No dynamic resources are owned by the parallel stub. */
void parallel_fini(struct Smaky6 *m) { (void)m; }

/* Return the current parallel data latch. */
uint8_t parallel_read_data(struct Smaky6 *m)   { return m->par.data; }

/* Update the parallel data latch from a guest write. */
void    parallel_write_data(struct Smaky6 *m, uint8_t val) { m->par.data = val; }

/* Return the current parallel status bits. */
uint8_t parallel_read_status(struct Smaky6 *m) { return m->par.status; }
