/* sound.c – Smaky 6 1-bit buzzer emulation via SDL2 push-audio (QueueAudio).
 *
 * Hardware: the Smaky 6 buzzer is driven by an active-high pulse.  The ROM
 * beep routine writes the SAME byte (e.g. 0x83) to port 0x03 repeatedly in a
 * tight software loop; the buzzer hardware produces a brief transient on each
 * write strobe, not on the data bit value.  The emulator models this by
 * TOGGLING the internal buzzer level on every port 0x03 write, which gives a
 * 50% duty-cycle square wave at the loop's repetition frequency.
 *
 * Emulation strategy:
 *   The Z80 runs at 2.5 MHz, 50 Hz → 50 000 T-states per frame.
 *   The audio device runs at 44 100 Hz → 882 samples per 20 ms frame.
 *
 *   Each time port 0x03 is written, sound_set_bit() maps the current frame
 *   T-state position (m->snd.frame_base + m->cpu.cycles) to a sample index
 *   and fills the per-frame buffer with the previous level up to that point.
 *   At end of frame, sound_end_frame() fills the remainder and pushes the
 *   buffer to the audio device with SDL_QueueAudio.
 *
 *   SDL_QueueAudio is used in push (callback-free) mode so there is no data
 *   race between the emulation thread and an audio callback thread.
 */
#include "sound.h"
#include "machine_internal.h"

#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE   44100
#define TSTATES_PER_FRAME   50000
#define SAMPLES_PER_FRAME   (AUDIO_SAMPLE_RATE / 50)   /* 882 */
#define AUDIO_AMPLITUDE     10000   /* +-10000 out of +-32767 -- comfortable volume */

/* Maximum queued audio bytes before we start skipping frames to avoid
 * unbounded latency build-up (e.g. when the emulator runs faster than
 * real time).  4 frames of audio = ~71 ms. */
#define MAX_QUEUE_BYTES     (SAMPLES_PER_FRAME * sizeof(int16_t) * 4)

static SDL_AudioDeviceID g_audio_dev = 0;
static int16_t           g_frame_buf[SAMPLES_PER_FRAME];
static int               g_last_sample = 0;  /* next sample index to fill */
static int               g_level = 0;        /* current buzzer level (0 or 1) */

/* -- helpers --------------------------------------------------------------- */

static inline int cycles_to_sample(zusize frame_pos)
{
    /* Map T-state position within frame to sample index [0, SAMPLES_PER_FRAME] */
    int s = (int)((frame_pos * (zusize)SAMPLES_PER_FRAME) / (zusize)TSTATES_PER_FRAME);
    if (s < 0)                 s = 0;
    if (s > SAMPLES_PER_FRAME) s = SAMPLES_PER_FRAME;
    return s;
}

static inline void fill_buf(int from, int to, int level)
{
    /* level=1: speaker deflected → +AUDIO_AMPLITUDE
     * level=0: speaker at rest  → 0 (silence)
     * Unipolar model: matches a transistor-driven buzzer where the speaker
     * cone is pushed only when the bit is set, resting silently otherwise. */
    int16_t val = level ? (int16_t)AUDIO_AMPLITUDE : (int16_t)0;
    for (int i = from; i < to; i++)
        g_frame_buf[i] = val;
}

/* -- public API ------------------------------------------------------------ */

void sound_init(struct Smaky6 *m)
{
    (void)m;

    SDL_AudioSpec want, got;
    SDL_memset(&want, 0, sizeof(want));
    want.freq     = AUDIO_SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = SAMPLES_PER_FRAME;
    want.callback = NULL;   /* push mode -- no callback thread */

    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (g_audio_dev == 0) {
        fprintf(stderr, "sound: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(g_audio_dev, 0);

    memset(g_frame_buf, 0, sizeof(g_frame_buf));
    g_last_sample = 0;
    g_level       = 0;
}

void sound_fini(struct Smaky6 *m)
{
    (void)m;
    if (g_audio_dev) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
}

/* Called from port 0x03 write handler (machine.c) on every write to the port.
 * The hardware generates a pulse on each write strobe (the data value is
 * irrelevant for sound), so we toggle the internal level on every call.
 * Uses m->snd.frame_base + m->cpu.cycles to determine the sample position. */
void sound_set_bit(struct Smaky6 *m, int level)
{
    (void)level;   /* data value unused – hardware is pulse-triggered */

    if (!g_audio_dev) return;

    /* Compute absolute frame position and map to sample index */
    zusize frame_pos = m->snd.frame_base + (zusize)m->cpu.cycles;
    int    sample_to = cycles_to_sample(frame_pos);

    /* Fill from last position to now with the previous level */
    if (sample_to > g_last_sample)
        fill_buf(g_last_sample, sample_to, g_level);

    g_last_sample = sample_to;
    g_level       ^= 1;   /* toggle: each write produces a transition */
    m->snd.buzzer_bit = g_level;
}

/* Called by machine_run_frame() after all Z80 cycles for the frame.
 * Fills the remainder of the frame buffer and submits it to the audio device. */
void sound_end_frame(struct Smaky6 *m)
{
    (void)m;
    if (!g_audio_dev) return;

    /* Fill remainder with current level */
    fill_buf(g_last_sample, SAMPLES_PER_FRAME, g_level);
    g_last_sample = 0;

    /* Skip frame if queue is already backed up (emulator running too fast) */
    if (SDL_GetQueuedAudioSize(g_audio_dev) < MAX_QUEUE_BYTES)
        SDL_QueueAudio(g_audio_dev, g_frame_buf, sizeof(g_frame_buf));
}

/* sound_beep: kept for API completeness (not currently called). */
void sound_beep(struct Smaky6 *m, unsigned freq_hz, unsigned duration_ms)
{
    (void)m; (void)freq_hz; (void)duration_ms;
}
