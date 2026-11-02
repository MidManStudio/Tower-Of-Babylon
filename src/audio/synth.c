// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "synth.h and synth.c"
// ============================================================================
#include "synth.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692f

static float LayerSecs(const SynLayer *l)
{
    const float own = l->length > 0.0f ? l->length : l->attack + 6.9f * l->decay;
    const float s = l->delay + own;
    return s < SYNTH_MAX_SECS ? s : SYNTH_MAX_SECS;
}

static float TotalSecs(const SynRecipe *r)
{
    float t = r->duration;
    if (t <= 0.0f) {
        for (int i = 0; i < r->layers && i < SYNTH_MAX_LAYERS; ++i) {
            const float s = LayerSecs(&r->layer[i]);
            if (s > t) t = s;
        }
    }
    if (t > SYNTH_MAX_SECS) t = SYNTH_MAX_SECS;
    return t;
}

int Synth_Frames(const SynRecipe *r, int rate)
{
    const int n = (int)(TotalSecs(r) * (float)rate);
    return n < 1 ? 1 : n;
}

static unsigned Xorshift(unsigned *s)
{
    unsigned x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

// Adds one layer into acc (n floats, the whole sound).
static void AddLayer(const SynLayer *l, int index, unsigned seed, int rate, float *acc, int n)
{
    const float sr = (float)rate;
    const int start = (int)(l->delay * sr);
    float own = l->length > 0.0f ? l->length : l->attack + 6.9f * l->decay;
    if (own * sr < 1.0f) own = 1.0f / sr;
    int len = (int)(own * sr);
    if (start >= n) return;
    if (start + len > n) len = n - start;

    unsigned rng = seed ^ (0x9e3779b9u * (unsigned)(index + 1));
    if (rng == 0) rng = 1;

    const bool glide = l->f0 > 0.0f && l->f1 > 0.0f;
    float freq = l->f0;
    const float freqMul = glide ? powf(l->f1 / l->f0, 1.0f / (float)len) : 1.0f;
    const bool filtered = l->lp0 > 0.0f && l->lp1 > 0.0f;
    float cut = l->lp0;
    const float cutMul = filtered ? powf(l->lp1 / l->lp0, 1.0f / (float)len) : 1.0f;
    float phase = 0.0f, y1 = 0.0f, y2 = 0.0f;
    const float driveGain = 1.0f + l->drive, driveNorm = l->drive > 0.0f ? 1.0f / tanhf(driveGain) : 1.0f;
    const float decayK = l->decay > 0.0f ? 1.0f / l->decay : 1000000.0f;

    for (int i = 0; i < len; ++i) {
        const float t = (float)i / sr;
        float x;
        switch (l->wave) {
            case SYN_SQUARE:   x = phase < 0.5f ? 1.0f : -1.0f; break;
            case SYN_SAW:      x = 2.0f * phase - 1.0f; break;
            case SYN_TRIANGLE: x = 4.0f * fabsf(phase - 0.5f) - 1.0f; break;
            case SYN_NOISE:    x = (float)(int)(Xorshift(&rng) >> 8) / 8388608.0f - 1.0f; break;   // 24 bits -> -1..1
            default:           x = sinf(TWO_PI * phase); break;
        }
        phase += freq / sr;
        phase -= floorf(phase);
        freq *= freqMul;

        if (filtered) {                                  // two one-pole low-passes in a row: 12 dB per octave
            const float a = 1.0f - expf(-TWO_PI * cut / sr);
            y1 += a * (x - y1);
            y2 += a * (y1 - y2);
            x = y2;
            cut *= cutMul;
        }
        if (l->drive > 0.0f) x = tanhf(x * driveGain) * driveNorm;

        const float env = t < l->attack ? t / l->attack : expf(-(t - l->attack) * decayK);
        acc[start + i] += l->level * env * x;
    }
}

int Synth_Render(const SynRecipe *r, unsigned seed, int rate, short *out, int cap)
{
    const int frames = Synth_Frames(r, rate);
    const int extra = r->loopFade > 0.0f ? (int)(r->loopFade * (float)rate) : 0;   // rendered past the end, folded back onto the start
    int total = frames + extra;
    if (extra * 2 > frames) return 0;                                              // a fade longer than half the sound makes no sense
    if (frames > cap) return 0;
    float *acc = (float *)calloc((size_t)total, sizeof(float));
    if (!acc) return 0;

    for (int i = 0; i < r->layers && i < SYNTH_MAX_LAYERS; ++i) AddLayer(&r->layer[i], i, seed, rate, acc, total);

    if (extra > 0) {                                     // linear cross-fade: the overshoot (what comes after the end) fades out as the start fades in
        for (int i = 0; i < extra; ++i) {
            const float w = (float)i / (float)extra;
            acc[i] = acc[i] * w + acc[frames + i] * (1.0f - w);
        }
    } else {                                             // 4 ms fade-out so the last sample is zero (no click when it stops)
        int fade = (int)(0.004f * (float)rate);
        if (fade > frames) fade = frames;
        for (int i = 0; i < fade; ++i) acc[frames - 1 - i] *= (float)i / (float)fade;
    }

    float peak = 0.0f;
    for (int i = 0; i < frames; ++i) { const float a = fabsf(acc[i]); if (a > peak) peak = a; }
    const float scale = peak > 0.98f ? 0.98f / peak : 1.0f;
    for (int i = 0; i < frames; ++i) out[i] = (short)lrintf(acc[i] * scale * 32767.0f);
    free(acc);
    return frames;
}
