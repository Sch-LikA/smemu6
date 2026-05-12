// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
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
 *
 * ── Floppy drive acoustic simulation ────────────────────────────────────
 *
 * The Micropolis 5.25" hard-sectored drive produces three distinct sounds
 * that this module synthesizes entirely procedurally (no sample files):
 *
 *  1. MOTOR WHIR  (while CONT bit 0 = 1)
 *     A spindle motor spinning at 300 RPM creates broadband bearing noise
 *     and a periodic hum at the pole-pass frequency.  Modelled as:
 *       - White noise → 1st-order LP at ~700 Hz → 1st-order HP at ~150 Hz
 *         (isolates the bearing-noise band and removes DC drift)
 *       - 200 Hz sine added for the motor-pole hum (6-pole motor × 2 rps)
 *       - Amplitude shaped by a 300 ms linear attack / 500 ms linear decay
 *         envelope so the motor "spins up" and "spins down" audibly.
 *
 *  2. HEAD-STEP CLICK  (on every STEP_PULSE rising edge)
 *     The voice-coil or stepper carriage mechanism produces a sharp snap
 *     followed by a low-frequency resonant thump as the carriage settles.
 *     Modelled as a 25 ms burst of:
 *       - 40% high-freq noise × exp(-18t)  → the initial mechanical snap
 *       - 60% 55 Hz sine    × exp(-7t)     → the settling thump
 *     The step sound restarts from the beginning on every step pulse, which
 *     means rapid multi-track seeks produce a rapid-fire chattering effect.
 *
 *  3. SECTOR-HOLE CLICK  (every floppy_tick(), i.e. once per sector, ~2 ms)
 *     The hard-sectored Micropolis disk has 16 physical index holes.  As
 *     each hole passes the optical sensor a soft tick is produced.  At 300
 *     RPM (5 rev/s) and 16 sectors/rev the holes pass at 80 Hz, giving a
 *     characteristic regular ticking while the drive is spinning.  Modelled
 *     as a 5 ms noise burst × exp(-30t).  Suppressed until the motor envelope
 *     reaches 10% to avoid spurious ticks during the spin-up transient.
 *
 * All three sounds are mixed into the same int16 frame buffer that carries
 * the buzzer signal, with hard saturation at ±32767.  Amplitude constants
 * are chosen so that the step click is clearly audible over the motor whir,
 * yet both remain below the buzzer's ±10 000 ceiling to avoid clipping on
 * simultaneous buzzer + floppy activity.
 *
 * The three public entry points are:
 *   sound_floppy_motor()  – optionally called by floppy.c when CONT motor bit changes;
 *                            also implicitly triggered by sound_floppy_step() so the
 *                            motor whir starts even if the OS never sets the motor bit.
 *   sound_floppy_step()   – called by floppy_write_cont() on every step pulse
 *   sound_floppy_sector() – called by floppy_tick() on every sector advance
 *
 * Motor keepalive:
 *   The Micropolis controller's MOTOR bit (CONT bit 0) is not always set by the
 *   SAMOS OS before seeking.  Rather than relying on the bit, the motor sound is
 *   activated implicitly by sound_floppy_step() and sustained by the sector ticks
 *   via a 250-frame (5 s at 50 Hz) keepalive counter.  When the counter expires
 *   the motor envelope decays naturally over 500 ms.
 */
#include "sound.h"
#include "machine.h"
#include "machine_internal.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define AUDIO_SAMPLE_RATE   SMAKY6_AUDIO_HZ
#define TSTATES_PER_FRAME   SMAKY6_TSTATES_PER_FRAME
#define SAMPLES_PER_FRAME   SMAKY6_SAMPLES_PER_FRAME
#define AUDIO_AMPLITUDE     10000   /* +-10000 out of +-32767 -- comfortable volume */

/* Maximum queued audio bytes before we start skipping frames to avoid
 * unbounded latency build-up (e.g. when the emulator runs faster than
 * real time).  4 frames of audio = ~71 ms. */
#define MAX_QUEUE_BYTES     (SAMPLES_PER_FRAME * sizeof(int16_t) * 4)

static SDL_AudioDeviceID g_audio_dev = 0;
static int16_t           g_frame_buf[SAMPLES_PER_FRAME];
static int               g_last_sample = 0;  /* next sample index to fill */
static int               g_level = 0;        /* current buzzer level (0 or 1) */
static int               g_beeper_enabled      = 1; /* beeper on by default; -no-beeper disables */
static int               g_drive_sound_enabled = 0; /* drive sounds off by default; -drive-sound enables */

/* ── Floppy drive sound state ───────────────────────────────────────────── */

/* Head-step click: ~8 ms sharp crack */
#define STEP_CLICK_SAMPLES    (( 8 * AUDIO_SAMPLE_RATE) / 1000)   /* 353 */
/* Sector-hole sensor click: ~6 ms */
#define SECTOR_CLICK_SAMPLES  (( 6 * AUDIO_SAMPLE_RATE) / 1000)   /* 265 */

/* Peak amplitudes (int16 range ±32767; buzzer uses ±10000) */
#define FLOPPY_MOTOR_AMP   1800
#define FLOPPY_STEP_AMP    14000
#define FLOPPY_SECTOR_AMP  4000

/* Motor volume envelope: 400 ms attack, 800 ms decay (spindle inertia) */
#define MOTOR_ATTACK_RATE      (1.0f / (0.40f * AUDIO_SAMPLE_RATE))
#define MOTOR_DECAY_RATE       (1.0f / (0.80f * AUDIO_SAMPLE_RATE))
/* Motor keepalive: step pulses and active I/O set this countdown (frames).
 * Allows the motor to stay on for a few seconds after activity ceases.
 * ~100 frames = 2 s at 50 Hz. */
#define MOTOR_KEEPALIVE_FRAMES  100

static uint32_t g_lcg              = 0x12345678u; /* noise PRNG state */
static int      g_motor_on         = 0;            /* 1 = motor envelope target on */
static int      g_motor_frames     = 0;            /* keepalive countdown */
static float    g_motor_vol        = 0.0f;         /* 0..1 envelope */
static float    g_motor_lp1        = 0.0f;         /* LP filter stage 1 */
static float    g_motor_lp2        = 0.0f;         /* LP filter stage 2 */
static float    g_motor_hp         = 0.0f;         /* HP filter state */
static float    g_motor_prev_lp    = 0.0f;         /* previous LP2 value for HP */
static int      g_step_left        = 0;            /* remaining samples for step click */
static int      g_step_pos         = 0;            /* position within step sound */
static float    g_step_lp          = 0.0f;         /* LP state for step crack filter */
static float    g_step_hp          = 0.0f;         /* HP state for step crack filter */
static float    g_step_prev_lp     = 0.0f;
static int      g_sector_left      = 0;            /* remaining samples for sector click */
static int      g_sector_pos       = 0;

static inline float lcg_noise(void)
{
    g_lcg = g_lcg * 1664525u + 1013904223u;
    return (float)(int32_t)g_lcg * (1.0f / 2147483648.0f);  /* -1..+1 */
}

/* Mix synthesized floppy sounds into the already-filled g_frame_buf[].
 * Called once per 50 Hz frame from sound_end_frame(), after the buzzer fill. */
static void floppy_mix_frame(void)
{
    /* Motor noise filter: two cascaded LP stages + HP stage.
     *   LP α = 0.19  → ~1500 Hz; two poles keep hiss below ~1.5 kHz
     *   HP α = 0.957 →  ~300 Hz; removes DC and sub-bass rumble
     * Step crack filter: narrow bandpass around 600–3000 Hz.
     *   LP α = 0.40  → ~3000 Hz (passes most of the crack)
     *   HP α = 0.92  →  ~600 Hz (removes the low-frequency thud) */
    static const float m_lp  = 0.19f;
    static const float m_hp  = 0.957f;
    static const float s_lp  = 0.40f;
    static const float s_hp  = 0.92f;

    /* Update motor on/off from the keepalive counter once per frame. */
    if (g_motor_frames > 0) {
        g_motor_frames--;
        g_motor_on = 1;
    } else {
        g_motor_on = 0;
    }

    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
        float mix = 0.0f;

        /* ── Motor whir ────────────────────────────────────────────────── */
        if (g_motor_on) {
            g_motor_vol += MOTOR_ATTACK_RATE;
            if (g_motor_vol > 1.0f) g_motor_vol = 1.0f;
        } else {
            g_motor_vol -= MOTOR_DECAY_RATE;
            if (g_motor_vol < 0.0f) g_motor_vol = 0.0f;
        }

        if (g_motor_vol > 0.001f) {
            float n      = lcg_noise();
            g_motor_lp1  += m_lp * (n            - g_motor_lp1);
            g_motor_lp2  += m_lp * (g_motor_lp1  - g_motor_lp2);
            g_motor_hp    = m_hp * (g_motor_hp + g_motor_lp2 - g_motor_prev_lp);
            g_motor_prev_lp = g_motor_lp2;
            mix += g_motor_hp * g_motor_vol * FLOPPY_MOTOR_AMP;
        }

        /* ── Head-step click ───────────────────────────────────────────── */
        if (g_step_left > 0) {
            /* Bandpass-filtered noise crack: LP at ~3 kHz removes extreme hiss,
             * HP at ~600 Hz removes the low thud, leaving a crisp mechanical crack.
             * Decays with exp(-70t) — almost all energy in first 2 ms. */
            float t  = (float)g_step_pos * (1.0f / STEP_CLICK_SAMPLES);
            float n  = lcg_noise();
            g_step_lp     += s_lp * (n          - g_step_lp);
            g_step_hp      = s_hp * (g_step_hp + g_step_lp - g_step_prev_lp);
            g_step_prev_lp = g_step_lp;
            mix += g_step_hp * expf(-t * 70.0f) * FLOPPY_STEP_AMP;
            g_step_pos++;
            g_step_left--;
        }

        /* ── Sector-hole sensor click ──────────────────────────────────── */
        if (g_sector_left > 0) {
            /* Soft tick: fast noise burst with a short tail. */
            float t    = (float)g_sector_pos * (1.0f / SECTOR_CLICK_SAMPLES);
            mix += lcg_noise() * expf(-t * 35.0f) * FLOPPY_SECTOR_AMP;
            g_sector_pos++;
            g_sector_left--;
        }

        /* Add to buzzer buffer with saturation */
        int32_t s = (int32_t)g_frame_buf[i] + (int32_t)mix;
        if      (s >  32767) s =  32767;
        else if (s < -32768) s = -32768;
        g_frame_buf[i] = (int16_t)s;
    }
}

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

void sound_set_beeper_enabled(int enabled)
{
    g_beeper_enabled = enabled ? 1 : 0;
}

void sound_set_drive_sound_enabled(int enabled)
{
    g_drive_sound_enabled = enabled ? 1 : 0;
}

void sound_init(struct Smaky6 *m)
{
    (void)m;
    if (!g_beeper_enabled && !g_drive_sound_enabled) return;  /* all sound disabled */

    SDL_AudioSpec want, got;
    SDL_memset(&want, 0, sizeof(want));
    want.freq     = AUDIO_SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    /* Emscripten SDL2 requires a power-of-two buffer size; 1024 is the
     * smallest power of two above our 882-sample frame (44100 / 50 Hz). */
#ifdef __EMSCRIPTEN__
    want.samples  = 1024;
#else
    want.samples  = SAMPLES_PER_FRAME;
#endif
    want.callback = NULL;   /* push mode -- no callback thread */

    /* SDL_OpenAudioDevice can transiently fail on PulseAudio/PipeWire if the
     * server is not yet ready.  Retry up to 5 times with a short delay. */
    for (int attempt = 0; attempt < 5 && g_audio_dev == 0; attempt++) {
        if (attempt > 0)
            SDL_Delay(20);
        g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    }
    if (g_audio_dev == 0) {
        fprintf(stderr, "[sound] DISABLED: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(g_audio_dev, 0);

    /* Smoke-test: queue a silent frame and verify the driver accepted it.
     * On a broken PipeWire/PulseAudio session the device opens successfully
     * but silently drops all data (GetQueuedAudioSize stays 0). */
    {
        int16_t silence[SAMPLES_PER_FRAME];
        memset(silence, 0, sizeof(silence));
        SDL_QueueAudio(g_audio_dev, silence, sizeof(silence));
        SDL_Delay(2);  /* give the driver a moment to register the queue */
        Uint32 queued = SDL_GetQueuedAudioSize(g_audio_dev);
        if (queued == 0) {
            fprintf(stderr, "[sound] WARNING: audio device opened but queue stays empty "
                    "-- PipeWire/PulseAudio session may be broken; sound will be silent\n");
        } else {
            fprintf(stderr, "[sound] OK: device ready, %u bytes queued (driver: %s)\n",
                    (unsigned)queued, SDL_GetCurrentAudioDriver());
            SDL_ClearQueuedAudio(g_audio_dev);  /* discard the test frame */
        }
    }

    memset(g_frame_buf, 0, sizeof(g_frame_buf));
    g_last_sample = 0;
    g_level       = 0;
}

/* SDL_CloseAudioDevice can block indefinitely on a broken PipeWire/PulseAudio
 * session.  Run it in a detached thread so shutdown never hangs.  The process
 * will exit (and the OS will reap the thread) before the close completes in
 * the worst case. */
static int SDLCALL s_audio_close_thread(void *arg)
{
    SDL_CloseAudioDevice((SDL_AudioDeviceID)(uintptr_t)arg);
    return 0;
}

void sound_fini(struct Smaky6 *m)
{
    (void)m;
    if (g_audio_dev) {
        SDL_AudioDeviceID dev = g_audio_dev;
        g_audio_dev = 0;
        SDL_PauseAudioDevice(dev, 1);
        SDL_ClearQueuedAudio(dev);
        SDL_Thread *t = SDL_CreateThread(s_audio_close_thread, "audio_close",
                                         (void *)(uintptr_t)dev);
        if (t)
            SDL_DetachThread(t);  /* let it finish on its own; never join */
        else
            SDL_CloseAudioDevice(dev);  /* thread creation failed: risk the block */
    }
}

/* Called from port 0x03 write handler (machine.c) on every write to the port.
 * The hardware generates a pulse on each write strobe (the data value is
 * irrelevant for sound), so we toggle the internal level on every call.
 * Uses m->snd.frame_base + m->cpu.cycles to determine the sample position. */
void sound_set_bit(struct Smaky6 *m, int level)
{
    (void)level;   /* data value unused – hardware is pulse-triggered */

    if (!g_audio_dev || !g_beeper_enabled) return;

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

    /* Fill remainder with current buzzer level */
    if (g_beeper_enabled)
        fill_buf(g_last_sample, SAMPLES_PER_FRAME, g_level);
    else
        memset(g_frame_buf, 0, sizeof(g_frame_buf));
    g_last_sample = 0;

    /* Mix floppy drive sounds on top of the buzzer signal */
    if (g_drive_sound_enabled)
        floppy_mix_frame();

    /* Skip frame if queue is already backed up (emulator running too fast) */
    if (SDL_GetQueuedAudioSize(g_audio_dev) < MAX_QUEUE_BYTES)
        SDL_QueueAudio(g_audio_dev, g_frame_buf, sizeof(g_frame_buf));
}

/* sound_beep: kept for API completeness (not currently called). */
void sound_beep(struct Smaky6 *m, unsigned freq_hz, unsigned duration_ms)
{
    (void)m; (void)freq_hz; (void)duration_ms;
}

/* ── Floppy drive sound event triggers ─────────────────────────────────── */

/* Called on every head-step pulse.  Restarts the step-click envelope and
 * arms the motor keepalive — step pulses definitively imply spindle running. */
void sound_floppy_step(struct Smaky6 *m, int drive)
{
    (void)m; (void)drive;
    g_motor_frames = MOTOR_KEEPALIVE_FRAMES;
    /* Reset step filter state so each click starts clean */
    g_step_lp      = 0.0f;
    g_step_hp      = 0.0f;
    g_step_prev_lp = 0.0f;
    g_step_left    = STEP_CLICK_SAMPLES;
    g_step_pos     = 0;
}

/* Called from floppy_tick() once per sector advance.
 * Arms the motor keepalive only when real I/O is occurring (disk_active > 0),
 * which avoids false motor-on during the RAM test before any floppy access.
 * In post-ROM mode also accepts the explicit MOTOR bit (ctrl bit 0). */
void sound_floppy_sector(struct Smaky6 *m)
{
    int io_active = (m->fdc.disk_active[0] > 0) || (m->fdc.disk_active[1] > 0);
    int motor_bit = (m->fdc.ctrl >> 0) & 1;

    if (io_active || motor_bit)
        g_motor_frames = MOTOR_KEEPALIVE_FRAMES;

    /* Emit a sector-hole click only when the motor is audibly spinning. */
    if (g_motor_vol < 0.01f) return;
    g_sector_left = SECTOR_CLICK_SAMPLES;
    g_sector_pos  = 0;
}
