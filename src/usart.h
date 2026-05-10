// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* usart.h – Generic 8251-compatible USART model (Smaky 6 has two) */
#ifndef USART_H
#define USART_H

#include <stdint.h>

/*
 * Two USART instances:
 *   USART_PAPER (0): Port 0x04 data / 0x05 status  (paper reader, near ESC)
 *   USART_CASS  (1): Port 0x06 data / 0x07 status  (cassette/modem, near CR)
 */
typedef enum { USART_PAPER = 0, USART_CASS = 1, USART_COUNT = 2 } UsartId;

struct Usart {
    uint8_t data_reg;   /* last byte read/written */
    uint8_t status;     /* bit0 = RXRDY, bit1 = TXRDY, bit2 = TXEMPTY */
    uint8_t command;    /* last command byte written */
    uint8_t mode;       /* mode word */
};

struct Smaky6;

void usart_init(struct Smaky6 *m);
void usart_fini(struct Smaky6 *m);

/* Port read/write dispatchers for each USART */
uint8_t usart_read_data(struct Smaky6 *m, UsartId id);
void    usart_write_data(struct Smaky6 *m, UsartId id, uint8_t val);
uint8_t usart_read_status(struct Smaky6 *m, UsartId id);
void    usart_write_command(struct Smaky6 *m, UsartId id, uint8_t val);

#endif /* USART_H */
