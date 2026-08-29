/* platform.c - smemu6 backend dispatch (see platform.h). */
#include "platform.h"

#include <stddef.h>  /* NULL */

/* Backend installed by the target. NULL => use the compiled-in default. */
static struct smemu6_backend *g_backend = NULL;

void smemu6_set_backend(struct smemu6_backend *be)
{
    g_backend = be;
}

#ifndef SMEMU6_HAVE_BACKEND
/* Compiled-in default backend: the SDL one (defined in backends/sdl/). */
extern struct smemu6_backend smemu6_sdl_backend;
#endif

static struct smemu6_backend *cur(void)
{
#ifndef SMEMU6_HAVE_BACKEND
    return g_backend ? g_backend : &smemu6_sdl_backend;
#else
    return g_backend;   /* embedded: no default; NULL => dispatch no-ops */
#endif
}

int platform_audio_open(int rate, int channels, int block_frames)
{
    struct smemu6_backend *b = cur();
    if (!b || !b->audio_open) return -1;
    return b->audio_open(b->ctx, rate, channels, block_frames);
}

int platform_audio_queued_bytes(void)
{
    struct smemu6_backend *b = cur();
    if (!b || !b->audio_queued_bytes) return 0;
    return b->audio_queued_bytes(b->ctx);
}

void platform_audio_push(const int16_t *samples, int frames)
{
    struct smemu6_backend *b = cur();
    if (!b || !b->audio_push) return;
    b->audio_push(b->ctx, samples, frames);
}

void platform_audio_close(void)
{
    struct smemu6_backend *b = cur();
    if (!b || !b->audio_close) return;
    b->audio_close(b->ctx);
}

void platform_present(struct Smaky6 *m, const struct smemu6_frame *frame)
{
    struct smemu6_backend *b = cur();
    if (!b || !b->present) return;
    b->present(b->ctx, m, frame);
}

void platform_frame_done(struct Smaky6 *m)
{
    struct smemu6_backend *b = cur();
    if (!b || !b->frame_done) return;
    b->frame_done(b->ctx, m);
}

void platform_key(struct Smaky6 *m, smemu6_scancode scan, int down, int repeat)
{
    struct smemu6_backend *b = cur();
    if (!b || !b->key) return;
    b->key(b->ctx, m, scan, down, repeat);
}

void platform_text(struct Smaky6 *m, uint32_t codepoint)
{
    struct smemu6_backend *b = cur();
    if (!b || !b->text) return;
    b->text(b->ctx, m, codepoint);
}
