/* sound.h – Smaky 6 buzzer / loudspeaker */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

struct Smaky6;

void sound_init(struct Smaky6 *m);
void sound_fini(struct Smaky6 *m);

/* Produce a square-wave beep (freq in Hz, duration in ms) */
void sound_beep(struct Smaky6 *m, unsigned freq_hz, unsigned duration_ms);

/* Called from the I/O port write when the software toggles the buzzer bit */
void sound_set_bit(struct Smaky6 *m, int level);

#endif /* SOUND_H */
