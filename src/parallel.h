/* parallel.h – Smaky 6 parallel interface (ports 0x02/0x03) */
#ifndef PARALLEL_H
#define PARALLEL_H

#include <stdint.h>

struct Smaky6;

void parallel_init(struct Smaky6 *m);
void parallel_fini(struct Smaky6 *m);

/*
 * Port 0x02 (PAR):  bidirectional data
 * Port 0x03 (SPAR): status
 *   bit 0 = RDYP  (receiver ready)
 *   bit 1 = FULP  (transmitter full)
 *   bit 6 = S6
 *   bit 7 = S7
 */
uint8_t parallel_read_data(struct Smaky6 *m);
void    parallel_write_data(struct Smaky6 *m, uint8_t val);
uint8_t parallel_read_status(struct Smaky6 *m);

#endif /* PARALLEL_H */
