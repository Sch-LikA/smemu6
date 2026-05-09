/* sound.h – Smaky 6 buzzer / loudspeaker */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

struct Smaky6;

void sound_init(struct Smaky6 *m);
void sound_fini(struct Smaky6 *m);

/* Configure sound output.  Must be called BEFORE sound_init().
 * g_beeper_enabled      defaults to 1 (on);  pass 0 to disable (-no-beeper).
 * g_drive_sound_enabled defaults to 0 (off); pass 1 to enable (-drive-sound). */
void sound_set_beeper_enabled(int enabled);
void sound_set_drive_sound_enabled(int enabled);

/* Produce a square-wave beep (freq in Hz, duration in ms) */
void sound_beep(struct Smaky6 *m, unsigned freq_hz, unsigned duration_ms);

/* Called from the I/O port write when the software toggles the buzzer bit */
void sound_set_bit(struct Smaky6 *m, int level);

/* Called at end of each 50 Hz frame to flush the per-frame sample buffer
 * to the SDL audio queue.  Must be called once per machine_run_frame(). */
void sound_end_frame(struct Smaky6 *m);

/* Floppy drive acoustic simulation.
 *
 * These three functions are called by floppy.c to trigger synthesized
 * mechanical sounds.  All sounds are mixed into the same audio frame buffer
 * as the buzzer signal.  See sound.c for the full synthesis description.
 *
 * sound_floppy_motor():
 *   Explicitly sets the motor on or off.  Optional: sound_floppy_sector()
 *   reads the hardware MOTOR bit (m->fdc.ctrl bit 0) every floppy_tick()
 *   and is the primary mechanism for keeping g_motor_on in sync.
 *   Call this only when you need an immediate response on the same frame
 *   as the CONT write (before the next floppy_tick()).
 *
 * sound_floppy_step():
 *   Triggers one head-step click: a 25 ms decaying burst composed of a
 *   high-frequency snap (noise × exp(-18t)) and a low-frequency thump
 *   (55 Hz sine × exp(-7t)).  Calling this repeatedly in quick succession
 *   produces the characteristic rapid-fire chattering of a multi-track seek.
 *   Also arms the motor keepalive so the motor whir starts automatically.
 *   Call on every rising edge of the STEP_PULSE bit (Phantom ROM mode) or
 *   on every port 0x1A write (post-ROM SYS.SY mode).
 *
 * sound_floppy_sector():
 *   Triggers one sector-hole sensor click: a 5 ms noise burst that models
 *   the optical index-hole sensor firing as each of the 16 physical holes
 *   passes.  At 300 RPM this fires at 80 Hz, giving the drive's characteristic
 *   ticking sound.  Call once per floppy_tick() sector advance.
 *   The click is suppressed while the motor envelope is below 10% so that no
 *   spurious ticks are emitted during the spin-up transient.               */
void sound_floppy_motor(struct Smaky6 *m, int drive, int on);
void sound_floppy_step(struct Smaky6 *m, int drive);
void sound_floppy_sector(struct Smaky6 *m);

#endif /* SOUND_H */
