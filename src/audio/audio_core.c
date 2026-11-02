// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "audio_core.h and audio_core.c"
// ============================================================================
#include "audio_core.h"

#include <math.h>
#include <string.h>

// ---- mix ----------------------------------------------------------------------------------------------------------
static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void Mix_Init(AudioMix *m)
{
    m->master = 1.0f;
    for (int i = 0; i < AUD_CAT_COUNT; ++i) { m->volume[i] = 1.0f; m->muted[i] = false; }
}

float Mix_Gain(const AudioMix *m, int category, float soundVolume)
{
    if (category < 0 || category >= AUD_CAT_COUNT || m->muted[category]) return 0.0f;
    return soundVolume * Clamp01(m->volume[category]) * Clamp01(m->master);
}

// ---- limiter ------------------------------------------------------------------------------------------------------
void Limiter_Init(Limiter *l, float threshold, float releasePerSec)
{
    l->gain = 1.0f;
    l->threshold = threshold < 0.1f ? 0.1f : (threshold > 1.0f ? 1.0f : threshold);
    l->releasePerSec = releasePerSec < 0.01f ? 0.01f : releasePerSec;
}

void Limiter_Process(Limiter *l, float *samples, int frames, int channels, int sampleRate)
{
    if (!samples || frames <= 0 || channels <= 0 || sampleRate <= 0) return;
    const int n = frames * channels;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) { const float a = fabsf(samples[i]); if (a > peak) peak = a; }

    const float need = peak > l->threshold ? l->threshold / peak : 1.0f;     // the gain that would put this buffer exactly at the threshold
    const float from = l->gain;
    float to;
    if (need < from) to = need;                                              // too loud: down at once
    else {                                                                   // room to spare: back up slowly, never past what this buffer allows
        to = from + l->releasePerSec * (float)frames / (float)sampleRate;
        if (to > need) to = need;
        if (to > 1.0f) to = 1.0f;
    }
    int ramp = sampleRate / 650;                                             // about 1.5 ms
    if (ramp < 1) ramp = 1;
    if (ramp > frames) ramp = frames;
    for (int f = 0; f < frames; ++f) {
        const float g = (to > from && f < ramp) ? from + (to - from) * ((float)(f + 1) / (float)ramp) : to;   // only a rise is smoothed
        for (int c = 0; c < channels; ++c) samples[f * channels + c] *= g;
    }
    l->gain = to;
}

// ---- voices -------------------------------------------------------------------------------------------------------
void Voices_Init(VoicePool *p, int size)
{
    memset(p, 0, sizeof *p);
    p->size = size < 1 ? 1 : (size > VOICES_MAX ? VOICES_MAX : size);
}

int Voices_Acquire(VoicePool *p, double now, double length, bool *stolen)
{
    int pick = -1;
    for (int i = 0; i < p->size; ++i) if (p->slot[i].end <= now) { pick = i; break; }
    bool steal = false;
    if (pick < 0) {
        pick = 0;
        for (int i = 1; i < p->size; ++i) if (p->slot[i].start < p->slot[pick].start) pick = i;
        steal = true;
    }
    p->slot[pick].start = now;
    p->slot[pick].end = now + (length > 0.0 ? length : 0.0);
    if (stolen) *stolen = steal;
    return pick;
}

int Voices_Active(const VoicePool *p, double now)
{
    int n = 0;
    for (int i = 0; i < p->size; ++i) if (p->slot[i].end > now) ++n;
    return n;
}

// ---- spatial ------------------------------------------------------------------------------------------------------
Spatial Spatial_Compute(float lx, float ly, float lz, float fx, float fz, float sx, float sy, float sz, float nearDist, float farDist)
{
    Spatial s;
    const float dx = sx - lx, dy = sy - ly, dz = sz - lz;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d <= nearDist) s.gain = 1.0f;
    else if (d >= farDist || farDist <= nearDist) s.gain = 0.0f;
    else { const float t = (farDist - d) / (farDist - nearDist); s.gain = t * t; }

    const float hd = sqrtf(dx * dx + dz * dz);
    if (hd < 0.05f) { s.pan = 0.0f; return s; }
    const float rx = -fz, rz = fx;                          // right of a listener facing (fx, fz), +Y up: raylib is right-handed
    float pan = (dx * rx + dz * rz) / hd;
    if (hd < 2.0f) pan *= hd / 2.0f;                        // nearly on top of you: no hard swing from side to side
    s.pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    return s;
}

// ---- footsteps ----------------------------------------------------------------------------------------------------
bool Footsteps_Advance(Footsteps *f, float metres, float stride)
{
    if (stride <= 0.01f || metres <= 0.0f) return false;
    f->dist += metres;
    if (f->dist < stride) return false;
    f->dist -= stride;
    if (f->dist > stride) f->dist = 0.0f;                   // a huge jump (a teleport) is one step, not a burst
    f->foot ^= 1;
    return true;
}

// ---- schedule -----------------------------------------------------------------------------------------------------
void Sched_Init(Schedule *s) { memset(s, 0, sizeof *s); }

bool Sched_Add(Schedule *s, double at, int id, float volume, float pitch)
{
    for (int i = 0; i < SCHED_MAX; ++i) {
        if (!s->item[i].used) { s->item[i] = (SchedItem){ at, id, volume, pitch, true }; return true; }
    }
    return false;
}

bool Sched_PopDue(Schedule *s, double now, SchedItem *out)
{
    int best = -1;
    for (int i = 0; i < SCHED_MAX; ++i)
        if (s->item[i].used && s->item[i].at <= now && (best < 0 || s->item[i].at < s->item[best].at)) best = i;
    if (best < 0) return false;
    *out = s->item[best];
    s->item[best].used = false;
    return true;
}
