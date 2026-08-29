/* backends/sdl/sdl_backend.c - SDL2 backend for smemu6 (see platform.h).
 *
 * Default backend for the native and Emscripten builds.  It owns every SDL
 * resource (audio device here; window/renderer/texture + input in later steps).
 * The core never references SDL directly: it calls the dispatch functions in
 * platform.c, which forward here through the smemu6_backend vtable.
 *
 * Embedded targets do NOT compile this file (gated in CMake / PlatformIO) and
 * supply their own backend instead.
 */
#include "../src/platform.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Accumulates all SDL-owned state for this backend.  Grows as more subsystems
 * (video, keyboard) register their methods onto smemu6_sdl_backend below. */
struct sdl_backend_state {
    SDL_AudioDeviceID audio_dev;   /* 0 == device not open */
    /* video + keyboard state added in later steps */
};

static struct sdl_backend_state g_sdl_state = {0};

/* ---- audio -------------------------------------------------------------- */

static int SDLCALL sdl_audio_close_thread(void *arg)
{
    SDL_CloseAudioDevice((SDL_AudioDeviceID)(uintptr_t)arg);
    return 0;
}

static int sdl_audio_open(void *ctx, int rate, int channels, int block_frames)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    SDL_AudioSpec want, got;
    (void)block_frames;

    SDL_memset(&want, 0, sizeof(want));
    want.freq     = rate;
    want.format   = AUDIO_S16SYS;
    want.channels = channels;
    /* Emscripten SDL2 requires a power-of-two buffer size; 1024 is the
     * smallest power of two above our 882-sample frame (44100 / 50 Hz). */
    want.samples  = 1024;
    want.callback = NULL;   /* push mode -- no callback thread */

    /* SDL_OpenAudioDevice can transiently fail on PulseAudio/PipeWire if the
     * server is not yet ready.  Retry up to 5 times with a short delay. */
    for (int attempt = 0; attempt < 5 && s->audio_dev == 0; attempt++) {
        if (attempt > 0) SDL_Delay(20);
        s->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    }
    if (!s->audio_dev) {
        fprintf(stderr, "[sound] DISABLED: SDL_OpenAudioDevice failed: %s\n",
                SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(s->audio_dev, 0);

    /* Smoke-test: queue a silent frame and verify the driver accepted it.
     * On a broken PipeWire/PulseAudio session the device opens successfully
     * but silently drops all data (GetQueuedAudioSize stays 0). */
    {
        int16_t silence[1024];
        memset(silence, 0, sizeof(silence));
        SDL_QueueAudio(s->audio_dev, silence, sizeof(silence));
        SDL_Delay(2);
        Uint32 queued = SDL_GetQueuedAudioSize(s->audio_dev);
        if (queued == 0) {
            fprintf(stderr, "[sound] WARNING: audio device opened but queue stays empty "
                    "-- PipeWire/PulseAudio session may be broken; sound will be silent\n");
        } else {
            fprintf(stderr, "[sound] OK: device ready, %u bytes queued (driver: %s)\n",
                    (unsigned)queued, SDL_GetCurrentAudioDriver());
            SDL_ClearQueuedAudio(s->audio_dev);   /* discard the test frame */
        }
    }
    return 0;
}

static int sdl_audio_queued_bytes(void *ctx)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    if (!s->audio_dev) return 0;
    return (int)SDL_GetQueuedAudioSize(s->audio_dev);
}

static void sdl_audio_push(void *ctx, const int16_t *samples, int frames)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    if (!s->audio_dev || !samples || frames <= 0) return;
    SDL_QueueAudio(s->audio_dev, samples, (Uint32)(frames * (int)sizeof(int16_t)));
}

static void sdl_audio_close(void *ctx)
{
    struct sdl_backend_state *s = (struct sdl_backend_state *)ctx;
    if (!s->audio_dev) return;
    /* SDL_CloseAudioDevice can block indefinitely on a broken session; run the
     * close in a detached thread so shutdown never hangs. */
    SDL_AudioDeviceID dev = s->audio_dev;
    s->audio_dev = 0;
    SDL_PauseAudioDevice(dev, 1);
    SDL_ClearQueuedAudio(dev);
    SDL_Thread *t = SDL_CreateThread(sdl_audio_close_thread, "audio_close",
                                     (void *)(uintptr_t)dev);
    if (t)
        SDL_DetachThread(t);   /* let it finish on its own; never join */
    else
        SDL_CloseAudioDevice(dev);   /* thread creation failed: risk the block */
}

/* The compiled-in default backend.  Only audio is wired in Step 1; present /
 * frame_done / key / text stay NULL until their subsystems are relocated here. */
struct smemu6_backend smemu6_sdl_backend = {
    .ctx                = &g_sdl_state,
    .audio_open         = sdl_audio_open,
    .audio_queued_bytes = sdl_audio_queued_bytes,
    .audio_push         = sdl_audio_push,
    .audio_close        = sdl_audio_close,
};
