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
 * that this module plays back: recorded 5.25" samples (sound/floppy/, CC0
 * MAME team recordings) when the files are present, with the procedural
 * synthesis below as the fallback (web build, stripped installs):
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
 * Public entry points:
 *   sound_floppy_motor() – called by the port handlers when the hardware
 *                          MOTOR bit changes (Phantom ROM: port 0x19 bit 3;
 *                          Plan F4: port 0x1A bit 0).
 *   sound_floppy_step()  – called by floppy_write_cont() when the emulated
 *                          track value actually changes.
 *
 * The sector index tick (procedural mode) is a free-running 80 Hz timer
 * (300 RPM x 16 holes, HARDWARE.md 7.1), decoupled from the 50 Hz floppy
 * data tick.
 */
#include "sound.h"
#include "machine.h"
#include "machine_internal.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define AUDIO_SAMPLE_RATE   SMAKY6_AUDIO_HZ
#define TSTATES_PER_FRAME   SMAKY6_TSTATES_PER_FRAME
#define SAMPLES_PER_FRAME   SMAKY6_SAMPLES_PER_FRAME
#define AUDIO_AMPLITUDE     10000   /* +-10000 out of +-32767 -- comfortable volume */
#define PSG_MIX_DIVISOR     2       /* four AY chips sum quickly; attenuate before mixing */

/* Maximum queued audio bytes before we start skipping frames to avoid
 * unbounded latency build-up (e.g. when the emulator runs faster than
 * real time).  4 frames of audio = ~71 ms. */
#define MAX_QUEUE_BYTES     (SAMPLES_PER_FRAME * sizeof(int16_t) * 4)

/* -audio-dump: capture the final mixed frame buffer as a WAV file (debug) */
static FILE     *g_dump       = NULL;
static uint32_t  g_dump_bytes = 0;

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void sound_set_audio_dump(const char *path)
{
    if (!path) return;
    g_dump = fopen(path, "wb");
    if (!g_dump) {
        fprintf(stderr, "[sound] cannot open audio dump %s: %s\n", path, strerror(errno));
        return;
    }
    /* 44-byte canonical WAV header; RIFF/data sizes patched at sound_fini. */
    uint8_t hdr[44];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    put_le32(hdr + 16, 16);            /* fmt chunk size */
    hdr[20] = 1; hdr[22] = 1;          /* PCM, mono */
    put_le32(hdr + 24, AUDIO_SAMPLE_RATE);
    put_le32(hdr + 28, AUDIO_SAMPLE_RATE * 2u);
    hdr[32] = 2;                        /* block align */
    hdr[34] = 16;                       /* bits per sample */
    memcpy(hdr + 36, "data", 4);
    fwrite(hdr, 1, sizeof(hdr), g_dump);
}

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

static uint32_t g_lcg              = 0x12345678u; /* noise PRNG state */
static int      g_motor_on         = 0;            /* 1 = motor envelope target on */
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

/* Free-running 80 Hz index-hole tick phase (procedural fallback only).
 * HARDWARE.md 7.1: 300 RPM x 16 holes = 80 ticks/s; 44100/80 = 551.25
 * samples per tick.  Decoupled from the 50 Hz data tick by design. */
static uint32_t g_index_phase = 0u;
#define INDEX_TICK_DIV 551u

/* ── Sample-based drive engine ──────────────────────────────────────────── */
/*
 * Preferred drive sound source: recorded samples of a 5.25" drive (CC0
 * 1.0 Universal, MAME team recordings; see sound/floppy/LICENSE.txt).
 * Files are mono 44 100 Hz 16-bit PCM - identical to the audio buffer, so
 * no resampling is needed.  When all four files load at sound_init() the
 * sample engine replaces the procedural synthesis above; otherwise the
 * procedural engine runs (web build, stripped installs).
 *
 * The rotation loop plays at its recorded speed (one 300 RPM revolution
 * per 200 ms, index ticks at 80 Hz), independent of the 50 Hz floppy data
 * tick - the data model is untouched.
 */
#define SAMPLE_DIR "sound/floppy"

typedef struct {
    int16_t *data;
    int      n;
} SampleBuf;

static SampleBuf s_spin_loop;    /* 525_spin_loaded.wav       steady loop (1 rev) */
static SampleBuf s_spin_start;   /* 525_spin_start_loaded.wav spin-up sweep       */
static SampleBuf s_spin_end;     /* 525_spin_end.wav          spin-down tail      */
static SampleBuf s_step_click;   /* 525_step_1_1.wav          head-step click     */
static int       samples_ready   = 0;

/* Recorded samples are full-scale 16-bit; scale into the same amplitude
 * range as the procedural constants (loop peak lands well below the
 * buzzer's +/-10000 ceiling). */
#define FLOPPY_SAMPLE_GAIN  0.35f

/* Loop playback state (16.16 fixed-point phase, rate 1.0) */
static int       g_loop_on   = 0;
static float     g_loop_gain = 0.0f;
static uint32_t  g_loop_phase = 0u;

/* Spin-up fade-in 300 ms / spin-down fade-out 150 ms (per-sample slopes) */
#define LOOP_ATTACK  (1.0f / (0.300f * AUDIO_SAMPLE_RATE))
#define LOOP_RELEASE (1.0f / (0.150f * AUDIO_SAMPLE_RATE))

/* Concurrent one-shot voices (step clicks overlap during rapid seeks) */
#define MAX_ONESHOTS 8
typedef struct {
    const int16_t *data;
    int            n;
    uint32_t       phase;   /* 16.16 */
    float          rate;
    int            active;
} OneShot;
static OneShot g_oneshot[MAX_ONESHOTS];

/* Minimal RIFF/WAVE reader: mono, 16-bit PCM, 44 100 Hz only. */
static int wav_load_44k_mono(const char *path, SampleBuf *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 44) { fclose(f); return 0; }

    unsigned char *raw = malloc((size_t)sz);
    if (!raw) { fclose(f); return 0; }
    if (fread(raw, 1, (size_t)sz, f) != (size_t)sz) { free(raw); fclose(f); return 0; }
    fclose(f);

    if (memcmp(raw, "RIFF", 4) != 0 || memcmp(raw + 8, "WAVE", 4) != 0) {
        free(raw);
        return 0;
    }

    uint32_t fmt_rate = 0, fmt_ch = 0, fmt_bits = 0, fmt_fmt = 0;
    const unsigned char *data_p = NULL;
    uint32_t data_len = 0;
    uint32_t off = 12;
    while (off + 8 <= (uint32_t)sz) {
        uint32_t cid   = (uint32_t)raw[off]     | ((uint32_t)raw[off+1] << 8) |
                         ((uint32_t)raw[off+2] << 16) | ((uint32_t)raw[off+3] << 24);
        uint32_t csize = (uint32_t)raw[off+4]   | ((uint32_t)raw[off+5] << 8) |
                         ((uint32_t)raw[off+6] << 16) | ((uint32_t)raw[off+7] << 24);
        (void)cid;
        if (memcmp(raw + off, "fmt ", 4) == 0 && csize >= 16) {
            fmt_fmt  = (uint32_t)raw[off+8]  | ((uint32_t)raw[off+9] << 8);
            fmt_ch   = (uint32_t)raw[off+10] | ((uint32_t)raw[off+11] << 8);
            fmt_rate = (uint32_t)raw[off+12] | ((uint32_t)raw[off+13] << 8) |
                       ((uint32_t)raw[off+14] << 16) | ((uint32_t)raw[off+15] << 24);
            /* +8 = byte rate (skipped); +12 = block align (skipped) */
            fmt_bits = (uint32_t)raw[off+22] | ((uint32_t)raw[off+23] << 8);
        } else if (memcmp(raw + off, "data", 4) == 0) {
            data_p   = raw + off + 8;
            data_len = csize;
        }
        off += 8 + csize + (csize & 1u);
    }

    int ok = (fmt_fmt == 1u && fmt_ch == 1u && fmt_bits == 16u &&
              fmt_rate == AUDIO_SAMPLE_RATE && data_p && data_len >= 4u);
    if (ok) {
        out->n = (int)(data_len / 2u);
        out->data = malloc((size_t)out->n * sizeof(int16_t));
        if (out->data) {
            for (int i = 0; i < out->n; i++)
                out->data[i] = (int16_t)((uint16_t)data_p[i*2] |
                                          ((uint16_t)data_p[i*2+1] << 8));
        } else {
            ok = 0;
        }
    }
    free(raw);
    return ok;
}

static void oneshot_trigger(const SampleBuf *buf, float rate)
{
    int slot = 0;
    for (int i = 0; i < MAX_ONESHOTS; i++)
        if (!g_oneshot[i].active) { slot = i; break; }
    g_oneshot[slot].data   = buf->data;
    g_oneshot[slot].n      = buf->n;
    g_oneshot[slot].phase  = 0u;
    g_oneshot[slot].rate   = rate;
    g_oneshot[slot].active = 1;
}

/* Generate deterministic pseudo-random noise for the synthesized drive sounds. */
static inline float lcg_noise(void)
{
    g_lcg = g_lcg * 1664525u + 1013904223u;
    return (float)(int32_t)g_lcg * (1.0f / 2147483648.0f);  /* -1..+1 */
}

/* Mix one full frame of PSG samples into the shared audio output buffer. */
static void psg_mix_frame(struct Smaky6 *m)
{
    for (int i = 0; i < (int)SAMPLES_PER_FRAME; i++) {
        int32_t sample = smaky6_psg_mix_sample(&m->psg.card) / PSG_MIX_DIVISOR;
        int32_t mixed = (int32_t)g_frame_buf[i] + sample;
        if (mixed > 32767) {
            mixed = 32767;
        } else if (mixed < -32768) {
            mixed = -32768;
        }
        g_frame_buf[i] = (int16_t)mixed;
    }
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

    /* Sample engine: the recorded rotation loop + one-shot voices replace
     * the procedural synthesis entirely. */
    if (samples_ready) {
        for (int i = 0; i < (int)SAMPLES_PER_FRAME; i++) {
            float mix = 0.0f;

            if (g_loop_on) {
                if (g_loop_gain < 1.0f) {
                    g_loop_gain += LOOP_ATTACK;
                    if (g_loop_gain > 1.0f) g_loop_gain = 1.0f;
                }
            } else {
                if (g_loop_gain > 0.0f) {
                    g_loop_gain -= LOOP_RELEASE;
                    if (g_loop_gain < 0.0f) g_loop_gain = 0.0f;
                }
            }

            if (g_loop_gain > 0.0f && s_spin_loop.n > 0) {
                uint32_t n = (uint32_t)s_spin_loop.n;
                uint32_t idx  = g_loop_phase >> 16;
                if (idx >= n) idx -= n;
                uint32_t next = (idx + 1u) % n;
                float fr = (float)(g_loop_phase & 0xFFFFu) / 65536.0f;
                float s  = (float)s_spin_loop.data[idx]  * (1.0f - fr)
                         + (float)s_spin_loop.data[next] * fr;
                mix += s * g_loop_gain * FLOPPY_SAMPLE_GAIN;
                g_loop_phase += 0x10000u;   /* rate 1.0 */
            }

            for (int v = 0; v < MAX_ONESHOTS; v++) {
                OneShot *os = &g_oneshot[v];
                if (!os->active) continue;
                if (os->n > 0) {
                    uint32_t idx = os->phase >> 16;
                    if (idx >= (uint32_t)os->n) {
                        os->active = 0;
                    } else {
                        mix += (float)os->data[idx] * FLOPPY_SAMPLE_GAIN;
                        os->phase += (uint32_t)(os->rate * 65536.0f);
                    }
                } else {
                    os->active = 0;
                }
            }

            int32_t s = (int32_t)g_frame_buf[i] + (int32_t)mix;
            if      (s >  32767) s =  32767;
            else if (s < -32768) s = -32768;
            g_frame_buf[i] = (int16_t)s;
        }
        return;
    }

    /* Procedural fallback.  Motor on/off is driven explicitly by the FDC
     * control bits (sound_floppy_motor), no keepalive needed. */
    for (int i = 0; i < (int)SAMPLES_PER_FRAME; i++) {
        float mix = 0.0f;

        /* Free-running 80 Hz index-hole tick (300 RPM x 16 holes),
         * decoupled from the 50 Hz data tick. */
        if (g_motor_vol > 0.01f) {
            g_index_phase++;
            if (g_index_phase >= INDEX_TICK_DIV) {
                g_index_phase -= INDEX_TICK_DIV;
                g_sector_left = SECTOR_CLICK_SAMPLES;
                g_sector_pos  = 0;
            }
        }

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

/* Convert a Z80 T-state offset within the current frame into an audio sample index. */
static inline int cycles_to_sample(zusize frame_pos)
{
    /* Map T-state position within frame to sample index [0, SAMPLES_PER_FRAME] */
    int s = (int)((frame_pos * (zusize)SAMPLES_PER_FRAME) / (zusize)TSTATES_PER_FRAME);
    if (s < 0)                 s = 0;
    if (s > (int)SAMPLES_PER_FRAME) s = (int)SAMPLES_PER_FRAME;
    return s;
}

/* Fill a sample range with the current unipolar buzzer level. */
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

/* Enable or disable the simple beeper source before audio initialisation. */
void sound_set_beeper_enabled(int enabled)
{
    g_beeper_enabled = enabled ? 1 : 0;
}

/* Enable or disable synthesized drive sounds before audio initialisation. */
void sound_set_drive_sound_enabled(int enabled)
{
    g_drive_sound_enabled = enabled ? 1 : 0;
}

/* Open the SDL audio device on demand and initialize the per-frame mixer state. */
void sound_init(struct Smaky6 *m)
{
    if (g_audio_dev) return;
    if (!g_beeper_enabled && !g_drive_sound_enabled && !(m && m->psg.enabled))
        return;  /* all sound sources disabled */

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

    if (g_drive_sound_enabled) {
        if (wav_load_44k_mono(SAMPLE_DIR "/525_spin_loaded.wav", &s_spin_loop) &&
            wav_load_44k_mono(SAMPLE_DIR "/525_spin_start_loaded.wav", &s_spin_start) &&
            wav_load_44k_mono(SAMPLE_DIR "/525_spin_end.wav", &s_spin_end) &&
            wav_load_44k_mono(SAMPLE_DIR "/525_step_1_1.wav", &s_step_click)) {
            samples_ready = 1;
            fprintf(stderr, "[sound] drive samples loaded from %s/\n", SAMPLE_DIR);
        } else {
            fprintf(stderr, "[sound] drive samples not found in %s/ -- "
                            "using procedural drive sounds\n", SAMPLE_DIR);
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

/* Stop and close the SDL audio device, deferring the potentially blocking close
 * call to a detached helper thread when possible. */
void sound_fini(struct Smaky6 *m)
{
    (void)m;
    if (g_dump) {
        uint8_t b[4];
        put_le32(b, g_dump_bytes + 36u);
        fseek(g_dump, 4, SEEK_SET);
        fwrite(b, 1, 4, g_dump);
        put_le32(b, g_dump_bytes);
        fseek(g_dump, 40, SEEK_SET);
        fwrite(b, 1, 4, g_dump);
        fclose(g_dump);
        g_dump = NULL;
    }
    free(s_spin_loop.data);   s_spin_loop.data   = NULL; s_spin_loop.n   = 0;
    free(s_spin_start.data);  s_spin_start.data  = NULL; s_spin_start.n  = 0;
    free(s_spin_end.data);    s_spin_end.data    = NULL; s_spin_end.n    = 0;
    free(s_step_click.data);  s_step_click.data  = NULL; s_step_click.n  = 0;
    samples_ready = 0;
    g_loop_on = 0;
    g_loop_gain = 0.0f;
    g_loop_phase = 0u;
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

    if (m->psg.enabled)
        psg_mix_frame(m);

    if (g_dump) {
        fwrite(g_frame_buf, sizeof(int16_t), SAMPLES_PER_FRAME, g_dump);
        g_dump_bytes += (uint32_t)sizeof(g_frame_buf);
    }

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
/* Set the spindle motor state from the FDC control bits.
 *
 * Called by the port handlers when the hardware MOTOR bit changes:
 *   - Phantom ROM mode: bit 3 (MOTORON) of port 0x19  (machine.c)
 *   - Plan F4 (SYS.SY) mode: bit 0 (MOTOR) of port 0x1A (floppy.c)
 *
 * Sample engine: ON starts the spin-up sweep and fades the recorded
 * rotation loop in; OFF fades the loop out and plays the spin-down tail.
 * Procedural engine: sets the motor envelope target (400 ms attack /
 * 800 ms decay shape the spin-up and spin-down).
 */
void sound_floppy_motor(struct Smaky6 *m, int drive, int on)
{
    (void)m; (void)drive;
    if (samples_ready) {
        if (on && !g_loop_on) {
            g_loop_on = 1;
            g_loop_phase = 0u;
            oneshot_trigger(&s_spin_start, 1.0f);
        } else if (!on && g_loop_on) {
            g_loop_on = 0;
            oneshot_trigger(&s_spin_end, 1.0f);
        }
        return;
    }
    g_motor_on = on;
}

/* Head-step click.  Call only when the emulated track value actually
 * changed (see floppy_write_cont).
 *
 * Sample engine: plays the recorded click at a slightly randomized rate
 * (0.95-1.05x) so multi-track seeks don't sound like a machine gun.
 * Procedural engine: restarts the synthesized crack from its beginning.
 */
void sound_floppy_step(struct Smaky6 *m, int drive)
{
    (void)m; (void)drive;
    if (samples_ready) {
        float rate = 0.95f + (float)((g_lcg >> 8) & 0xFFFFu) / 65536.0f * 0.10f;
        oneshot_trigger(&s_step_click, rate);
        return;
    }
    /* Reset step filter state so each click starts clean */
    g_step_lp      = 0.0f;
    g_step_hp      = 0.0f;
    g_step_prev_lp = 0.0f;
    g_step_left    = STEP_CLICK_SAMPLES;
    g_step_pos     = 0;
}

/* 1 when the recorded sample engine is active (sound/floppy/ loaded at
 * sound_init), 0 when the procedural fallback is in use. */
int sound_floppy_samples_active(void)
{
    return samples_ready;
}
