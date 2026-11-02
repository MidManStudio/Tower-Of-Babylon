// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "audio_core.h and audio_core.c"
// ============================================================================
#ifndef AUDIO_CORE_H
#define AUDIO_CORE_H

#include <stdbool.h>

// The parts of the audio system that are plain arithmetic: the peak limiter, voice slots, where a sound sits in the room,
// the category volumes, footstep spacing and the "play this a bit later" queue. No raylib and no sound card, so
// tests/audio_headless.c runs every one of them. audio.c puts them to work.

// ---- categories and the volume mix --------------------------------------------------------------------------------
typedef enum { AUD_CAT_SFX = 0, AUD_CAT_UI, AUD_CAT_AMBIENT, AUD_CAT_VOICE, AUD_CAT_COUNT } AudioCategory;

typedef struct {
    float master;                        // 0..1
    float volume[AUD_CAT_COUNT];         // 0..1 each
    bool  muted[AUD_CAT_COUNT];
} AudioMix;

void  Mix_Init(AudioMix *m);                                    // everything at 1, nothing muted
float Mix_Gain(const AudioMix *m, int category, float soundVolume);   // sound x category x master; 0 when muted

// ---- limiter on the final mix (the Unity package's MID_AudioLimiter, same idea) ------------------------------------
typedef struct { float gain; float threshold; float releasePerSec; } Limiter;
void Limiter_Init(Limiter *l, float threshold, float releasePerSec);
// Interleaved float samples in -1..1. The peak of this buffer is measured first, so a loud buffer is brought under the
// threshold within the same buffer (the drop is immediate, so nothing in it overshoots); the gain then creeps back up by
// releasePerSec, smoothed over about 1.5 ms at the start of each buffer.
void Limiter_Process(Limiter *l, float *samples, int frames, int channels, int sampleRate);

// ---- voice slots: which of a sound's N voices to use --------------------------------------------------------------
#define VOICES_MAX 8
typedef struct { double start, end; } VoiceSlot;
typedef struct { VoiceSlot slot[VOICES_MAX]; int size; } VoicePool;
void Voices_Init(VoicePool *p, int size);                       // size 1..VOICES_MAX
// A free slot if there is one (the lowest), otherwise the one that started longest ago (*stolen = true). The slot is
// marked busy until now + length.
int  Voices_Acquire(VoicePool *p, double now, double length, bool *stolen);
int  Voices_Active(const VoicePool *p, double now);

// ---- where a sound is in the room ----------------------------------------------------------------------------------
typedef struct { float gain, pan; } Spatial;
// Listener at (lx, ly, lz) facing the unit floor-plane direction (fx, fz); source at (sx, sy, sz). Full volume out to
// nearDist, falling off with the square of the remaining distance to silence at farDist. pan is -1 (left) .. 1 (right),
// 0 straight ahead, behind or when the source is almost on top of the listener.
Spatial Spatial_Compute(float lx, float ly, float lz, float fx, float fz, float sx, float sy, float sz, float nearDist, float farDist);

// ---- footsteps -----------------------------------------------------------------------------------------------------
typedef struct { float dist; int foot; } Footsteps;             // foot alternates 0, 1 on every step
// Adds the metres walked since the last call; true when a step lands now (foot has just flipped).
bool Footsteps_Advance(Footsteps *f, float metres, float stride);

// ---- "play this in a moment" ---------------------------------------------------------------------------------------
#define SCHED_MAX 16
typedef struct { double at; int id; float volume, pitch; bool used; } SchedItem;
typedef struct { SchedItem item[SCHED_MAX]; } Schedule;
void Sched_Init(Schedule *s);
bool Sched_Add(Schedule *s, double at, int id, float volume, float pitch);     // false when the queue is full
bool Sched_PopDue(Schedule *s, double now, SchedItem *out);                    // the earliest due item, removed; false when none

#endif
