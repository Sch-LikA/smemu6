// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* usart.c – Generic 8251-style USART stub for Smaky 6 */
#include "machine_internal.h"
#include "usart.h"
#include <string.h>

/* Direct field access: struct Smaky6 fully visible via machine_internal.h */

void usart_init(struct Smaky6 *m)
{
    for (int i = 0; i < USART_COUNT; i++) {
        struct Usart *u = &m->usart[i];
        memset(u, 0, sizeof(*u));
        u->status = 0x06u;
    }
}

void usart_fini(struct Smaky6 *m) { (void)m; }

uint8_t usart_read_data(struct Smaky6 *m, UsartId id)
{
    return m->usart[id].data_reg;
}

void usart_write_data(struct Smaky6 *m, UsartId id, uint8_t val)
{
    m->usart[id].data_reg = val;
}

uint8_t usart_read_status(struct Smaky6 *m, UsartId id)
{
    return m->usart[id].status;
}

void usart_write_command(struct Smaky6 *m, UsartId id, uint8_t val)
{
    m->usart[id].command = val;
}
