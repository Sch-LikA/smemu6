/* sound.c – Smaky 6 buzzer / loudspeaker via SDL2 audio */
#include "sound.h"
#include "machine.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS    1
#define AUDIO_SAMPLES     512   /* buffer size in frames */

/* Simple square wave generator state */
typedef struct {
    unsigned freq_hz;
    unsigned duration_samples; /* remaining samples to play */
    unsigned phase;            /* half-period counter */
    int      level;            /* current output level (0 or 1) */
} SqWave;

static SqWave   g_sqwave;
static SDL_AudioDeviceID g_audio_dev = 0;

static void audio_callback(void *userdata, uint8_t *stream, int len)
{
    SqWave *sq = (SqWave *)userdata;
    int16_t *out = (int16_t *)(void *)stream;
    int n = len / 2;

    for (int i = 0; i < n; i++) {
        if (sq->duration_samples == 0 || sq->freq_hz == 0) {
            out[i] = 0;
            continue;
        }
        unsigned half_period = AUDIO_SAMPLE_RATE / (sq->freq_hz * 2);
        if (half_period == 0) half_period = 1;
        out[i] = sq->level ? 16384 : -16384;
        sq->phase++;
        if (sq->phase >= half_period) {
            sq->phase = 0;
            sq->level = !sq->level;
        }
        if (sq->duration_samples > 0) sq->duration_samples--;
    }
}

void sound_init(struct Smaky6 *m)
{
    (void)m;
    memset(&g_sqwave, 0, sizeof(g_sqwave));

    SDL_AudioSpec want, got;
    SDL_memset(&want, 0, sizeof(want));
    want.freq     = AUDIO_SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = AUDIO_CHANNELS;
    want.samples  = AUDIO_SAMPLES;
    want.callback = audio_callback;
    want.userdata = &g_sqwave;

    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (g_audio_dev == 0) {
        fprintf(stderr, "sound: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(g_audio_dev, 0);
}

void sound_fini(struct Smaky6 *m)
{
    (void)m;
    if (g_audio_dev) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
}

void sound_beep(struct Smaky6 *m, unsigned freq_hz, unsigned duration_ms)
{
    (void)m;
    if (!g_audio_dev) return;
    SDL_LockAudioDevice(g_audio_dev);
    g_sqwave.freq_hz          = freq_hz;
    g_sqwave.duration_samples = (unsigned)(AUDIO_SAMPLE_RATE * duration_ms / 1000);
    g_sqwave.phase            = 0;
    g_sqwave.level            = 1;
    SDL_UnlockAudioDevice(g_audio_dev);
}

void sound_set_bit(struct Smaky6 *m, int level)
{
    (void)m;
    /* Bit-bang mode: update output level directly */
    if (!g_audio_dev) return;
    SDL_LockAudioDevice(g_audio_dev);
    g_sqwave.level            = level & 1;
    g_sqwave.freq_hz          = 0;   /* manual control */
    g_sqwave.duration_samples = 100; /* keep for a short burst */
    SDL_UnlockAudioDevice(g_audio_dev);
}
