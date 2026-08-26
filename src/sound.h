// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* sound.h – Smaky 6 buzzer / loudspeaker */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

struct Smaky6;

/* Open the SDL audio path and reset the per-frame mixer state. */
void sound_init(struct Smaky6 *m);
/* Drain and close the SDL audio path plus any owned mixer state. */
void sound_fini(struct Smaky6 *m);

/* Configure sound output.  Must be called BEFORE sound_init().
 * g_beeper_enabled      defaults to 1 (on);  pass 0 to disable (-no-beeper).
 * g_drive_sound_enabled defaults to 0 (off); pass 1 to enable (-drive-sound). */
void sound_set_beeper_enabled(int enabled);
void sound_set_drive_sound_enabled(int enabled);

/* Dump the final mixed audio (buzzer + drive sounds + PSG) to a WAV file.
 * Call before sound_init(); the file is finalized at sound_fini().  Debug
 * aid for verifying drive-sound events without listening. */
void sound_set_audio_dump(const char *path);

/* Queue a standalone square-wave beep into the shared audio mixer. */
void sound_beep(struct Smaky6 *m, unsigned freq_hz, unsigned duration_ms);

/* Update the live beeper latch from port 0x03 writes during CPU execution. */
void sound_set_bit(struct Smaky6 *m, int level);

/* Called at end of each 50 Hz frame to flush the per-frame sample buffer
 * to the SDL audio queue.  Must be called once per machine_run_frame(). */
void sound_end_frame(struct Smaky6 *m);

/* Floppy drive acoustic simulation.
 *
 * These functions are called by the FDC port handlers to trigger drive
 * sounds.  All sounds are mixed into the same audio frame buffer as the
 * buzzer signal.  When the recorded sample set (sound/floppy/) is loaded,
 * the sample engine plays it; otherwise a procedural synthesis is used.
 * See sound.c for details.
 *
 * sound_floppy_motor():
 *   Sets the spindle motor state from the hardware MOTOR bit.  Call when
 *   the bit changes: Phantom ROM mode uses bit 3 (MOTORON) of port 0x19,
 *   Plan F4 (SYS.SY) mode uses bit 0 (MOTOR) of port 0x1A.
 *
 * sound_floppy_step():
 *   Triggers one head-step click.  Call only when the emulated track value
 *   actually changed (a step pulse that hits the track boundary produces
 *   no sound on real hardware).  Repeated calls in quick succession
 *   produce the rapid-fire chattering of a multi-track seek.
 *
 * sound_floppy_samples_active():
 *   1 when the recorded sample engine is active, 0 when the procedural
 *   fallback is in use.                                                      */
void sound_floppy_motor(struct Smaky6 *m, int drive, int on);
void sound_floppy_step(struct Smaky6 *m, int drive);
int  sound_floppy_samples_active(void);

#endif /* SOUND_H */
