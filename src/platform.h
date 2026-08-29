/* platform.h - smemu6 backend abstraction (see decision log / MEMORY.md).
 *
 * The core renders and mixes in a platform-neutral way and hands every
 * cross-boundary I/O to a "backend" through the smemu6_backend vtable below:
 * display frames, audio samples, and (later) input.  Every target supplies one
 * backend; the native and Emscripten builds use the SDL backend
 * (smemu6_sdl_backend) by default.  Embedded targets define SMEMU6_HAVE_BACKEND
 * and link their own backend via smemu6_set_backend().
 *
 * When SMEMU6_HAVE_BACKEND is NOT defined, the SDL backend is compiled in and
 * selected automatically: the core is then byte-for-byte identical to the
 * pre-abstraction build (all SDL code simply relocated into backends/sdl/).
 */
#ifndef SMEMU6_PLATFORM_H
#define SMEMU6_PLATFORM_H

#include <stdint.h>
#include "smemu6_scancode.h"

struct Smaky6;   /* forward; the full type lives only in the core */

/* Per-frame framebuffer handed to the backend for display.
 * Format: 32-bit ARGB8888 (matches the historic SDL_PIXELFORMAT_ARGB8888).
 * width/height are the visible pixels; stride_bytes is one row in bytes. */
struct smemu6_frame {
    const uint32_t *pixels;
    int width;
    int height;
    int stride_bytes;
};

/* Backend vtable.  Every field is optional (the core NULL-checks before use),
 * so a backend may leave the methods it does not implement unset.  ctx is an
 * opaque, backend-owned pointer echoed back on every callback. */
struct smemu6_backend {
    void *ctx;

    /* Audio device lifecycle + stream.  audio_open returns 0 on success / -1
     * on failure; audio_queued_bytes reports bytes currently queued (backpressure). */
    int  (*audio_open)(void *ctx, int rate, int channels, int block_frames);
    int  (*audio_queued_bytes)(void *ctx);
    void (*audio_push)(void *ctx, const int16_t *samples, int frames);
    void (*audio_close)(void *ctx);

    /* Display: hand one rendered framebuffer to the backend. */
    /* Display: hand one rendered framebuffer to the backend.  The machine
     * pointer is passed too so the backend can draw overlays that reflect
     * live machine state (drive LEDs, function keys, RESET/BREAK buttons). */
    void (*present)(void *ctx, struct Smaky6 *m, const struct smemu6_frame *frame);

    /* End-of-emulation-frame hook (optional vsync / teardown). */
    void (*frame_done)(void *ctx, struct Smaky6 *m);

    /* Input: one portable scancode (or key release) into the core.  `m` is
     * passed so the handler can feed the event via keyboard_event(); `repeat`
     * distinguishes a fresh press from a held-key autorepeat. */
    void (*key)(void *ctx, struct Smaky6 *m, smemu6_scancode scan, int down, int repeat);
    /* Input: one decoded unicode codepoint (from an SDL_TEXTINPUT event) into
     * the core via keyboard_text(). */
    void (*text)(void *ctx, struct Smaky6 *m, uint32_t codepoint);
};

/* Install a backend. NULL selects the compiled-in default:
 *   - SDL backend (smemu6_sdl_backend) when SMEMU6_HAVE_BACKEND is unset,
 *   - a no-op       when SMEMU6_HAVE_BACKEND is set (the target must call this).
 * Call once before machine start. */
void smemu6_set_backend(struct smemu6_backend *be);

/* Dispatch entry points the core calls. All are NULL-safe. */
int  platform_audio_open(int rate, int channels, int block_frames);
int  platform_audio_queued_bytes(void);
void platform_audio_push(const int16_t *samples, int frames);
void platform_audio_close(void);
void platform_present(struct Smaky6 *m, const struct smemu6_frame *frame);
void platform_frame_done(struct Smaky6 *m);
void platform_key(struct Smaky6 *m, smemu6_scancode scan, int down, int repeat);
void platform_text(struct Smaky6 *m, uint32_t codepoint);

#endif /* SMEMU6_PLATFORM_H */
