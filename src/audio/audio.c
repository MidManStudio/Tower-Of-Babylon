// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "audio.h and audio.c"
// ============================================================================
#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../config/game_config.h"
#include "../platform/platform.h"
#include "audio_core.h"
#include "synth.h"

extern const MdixBaked g_bakedSounds;                          // config_baked.c, written by tools/mdix_bake.mjs

#define MAX_VARIANTS   6
#define DEFAULT_NEAR   4.0f        // metres, for a sound with no falloff of its own that is played at a place
#define DEFAULT_FAR    40.0f
#define BUILD_BUDGET_S 0.003       // render sounds for at most this long each frame (at least one)
#define LOOP_FADE_PER_SEC 2.0f     // volume change per second when a loop starts or stops
#define DEVICE_RATE    44100

typedef struct {
    bool     built;
    float    lengthSecs;
    Sound    src[MAX_VARIANTS];
    Sound    voice[MAX_VARIANTS][VOICES_MAX];       // voice[v][0] is src[v]; the others are aliases of it (they share the samples)
    int      slotVariant[VOICES_MAX];               // which variant each slot last played, so a stolen voice can be cut
    VoicePool pool;
    int      lastVariant;
    // loops (a SoundDef.loop sound is a raylib Music stream over a WAV kept in memory)
    Music    music;
    unsigned char *wav;
    bool     musicLoaded, loopOn, loopPlaying;
    float    loopCur, loopTarget, loopPitch;
} Slot;

static Slot        g_slot[SND_COUNT];
static AudioMix    g_mix;
static Limiter     g_limiter;
static Schedule    g_sched;
static AudioStatus g_status;
static bool        g_inited = false;
static unsigned    g_builtRevision = 0;
static int         g_next = 0;                          // next sound to render
static uint32_t    g_rng = 0x2545f491u;
static Vector3     g_listener = { 0 };
static float       g_lfx = 0.0f, g_lfz = 1.0f;
static double      g_now = 0.0;

static float Rand01(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return (float)(g_rng >> 8) / 16777216.0f;
}

static void LimiterCallback(void *buffer, unsigned int frames)
{
    Limiter_Process(&g_limiter, (float *)buffer, (int)frames, 2, DEVICE_RATE);      // raylib hands over stereo float
}

// ---- building ------------------------------------------------------------------------------------------------------
static void FreeSlot(Slot *s)
{
    if (s->musicLoaded) { UnloadMusicStream(s->music); s->musicLoaded = false; }
    if (s->wav) { MemFree(s->wav); s->wav = NULL; }
    for (int v = 0; v < MAX_VARIANTS; ++v) {
        if (s->src[v].stream.buffer == NULL) continue;
        for (int k = 1; k < VOICES_MAX; ++k)
            if (s->voice[v][k].stream.buffer != NULL) UnloadSoundAlias(s->voice[v][k]);
        UnloadSound(s->src[v]);
    }
    memset(s, 0, sizeof *s);
}

static void ResetAll(void)
{
    for (int i = 0; i < SND_COUNT; ++i) FreeSlot(&g_slot[i]);
    Sched_Init(&g_sched);
    g_next = 0;
    g_status.built = 0;
}

static void PutLE32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }

static void BuildOne(int id)
{
    const SoundDef *def = SoundBank_Get(id);
    Slot *s = &g_slot[id];
    if (!def) return;
    const int frames = Synth_Frames(&def->recipe, SYNTH_RATE);
    s->lengthSecs = (float)frames / (float)SYNTH_RATE;
    Voices_Init(&s->pool, def->voices);
    s->lastVariant = -1;

    if (def->loop) {                                            // one take, as a looping stream
        short *pcm = (short *)MemAlloc((unsigned)frames * 2u);
        if (!pcm) return;
        const int n = Synth_Render(&def->recipe, 0x51ed270bu + (unsigned)id, SYNTH_RATE, pcm, frames);
        if (n > 0) {
            const unsigned bytes = (unsigned)n * 2u;
            unsigned char *w = (unsigned char *)MemAlloc(44u + bytes);
            if (w) {
                memcpy(w, "RIFF", 4); PutLE32(w + 4, 36u + bytes); memcpy(w + 8, "WAVEfmt ", 8); PutLE32(w + 16, 16u);
                w[20] = 1; w[21] = 0; w[22] = 1; w[23] = 0;                                    // PCM, mono
                PutLE32(w + 24, SYNTH_RATE); PutLE32(w + 28, SYNTH_RATE * 2u); w[32] = 2; w[33] = 0; w[34] = 16; w[35] = 0;
                memcpy(w + 36, "data", 4); PutLE32(w + 40, bytes);
                memcpy(w + 44, pcm, bytes);
                s->music = LoadMusicStreamFromMemory(".wav", w, (int)(44u + bytes));          // keeps pointing at w: it stays allocated
                if (s->music.stream.buffer != NULL) { s->music.looping = true; s->wav = w; s->musicLoaded = true; }
                else MemFree(w);
            }
        }
        MemFree(pcm);
        s->built = s->musicLoaded;
        return;
    }

    for (int v = 0; v < def->variants && v < MAX_VARIANTS; ++v) {
        short *pcm = (short *)MemAlloc((unsigned)frames * 2u);
        if (!pcm) return;
        const int n = Synth_Render(&def->recipe, 0x9e3779b1u * (unsigned)(id + 1) + 0x7f4a7c15u * (unsigned)v, SYNTH_RATE, pcm, frames);
        if (n <= 0) { MemFree(pcm); continue; }
        Wave w = { 0 };
        w.frameCount = (unsigned)n; w.sampleRate = SYNTH_RATE; w.sampleSize = 16; w.channels = 1; w.data = pcm;
        s->src[v] = LoadSoundFromWave(w);
        UnloadWave(w);                                          // LoadSoundFromWave copied what it needs
        if (s->src[v].stream.buffer == NULL) continue;
        s->voice[v][0] = s->src[v];
        for (int k = 1; k < def->voices; ++k) s->voice[v][k] = LoadSoundAlias(s->src[v]);
    }
    s->built = s->src[0].stream.buffer != NULL;
}

// ---- public: setup -------------------------------------------------------------------------------------------------
void Audio_Init(void)
{
    if (g_inited) return;
    g_inited = true;
    Mix_Init(&g_mix);
    Limiter_Init(&g_limiter, 0.92f, 1.0f);
    Sched_Init(&g_sched);
    GameConfig_RegisterExtra("assets/config/sounds.mdix", &g_bakedSounds, SoundBank_LoadFrom);
}

static void OpenDevice(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) { g_status.deviceFailed = true; return; }
    g_status.deviceReady = true;
    AttachAudioMixedProcessor(LimiterCallback);
}

void Audio_Shutdown(void)
{
    if (g_status.deviceReady) {
        DetachAudioMixedProcessor(LimiterCallback);
        ResetAll();
        CloseAudioDevice();
    }
    memset(&g_status, 0, sizeof g_status);
}

// ---- public: playing -----------------------------------------------------------------------------------------------
static bool PlaySlot(int id, float volume, float pitch, float pan, float spatialGain)
{
    if (!g_status.deviceReady || id < 0 || id >= SND_COUNT) return false;
    const SoundDef *def = SoundBank_Get(id);
    Slot *s = &g_slot[id];
    if (!def || !s->built || def->loop) return false;
    const float vol = Mix_Gain(&g_mix, def->category, def->volume * volume * spatialGain);
    if (vol <= 0.002f) return false;

    int variant = 0;
    if (def->variants > 1) {                                    // a different take from last time
        variant = (int)(Rand01() * (float)def->variants);
        if (variant >= def->variants) variant = def->variants - 1;
        if (variant == s->lastVariant) variant = (variant + 1) % def->variants;
    }
    if (s->src[variant].stream.buffer == NULL) variant = 0;
    s->lastVariant = variant;

    float p = pitch * (1.0f + def->pitchJitter * (Rand01() * 2.0f - 1.0f));
    if (p < 0.25f) p = 0.25f;
    if (p > 3.0f) p = 3.0f;

    bool stolen = false;
    const int k = Voices_Acquire(&s->pool, g_now, (double)(s->lengthSecs / p), &stolen);
    if (stolen && s->voice[s->slotVariant[k]][k].stream.buffer != NULL) StopSound(s->voice[s->slotVariant[k]][k]);
    s->slotVariant[k] = variant;
    Sound snd = s->voice[variant][k];
    if (snd.stream.buffer == NULL) return false;
    SetSoundVolume(snd, vol > 1.0f ? 1.0f : vol);
    SetSoundPitch(snd, p);
    SetSoundPan(snd, pan);
    PlaySound(snd);
    return true;
}

bool Audio_PlayEx(int id, float volume, float pitch, float pan) { return PlaySlot(id, volume, pitch, pan, 1.0f); }
bool Audio_Play(int id, float volume) { return PlaySlot(id, volume, 1.0f, 0.0f, 1.0f); }

bool Audio_PlayAt(int id, Vector3 where, float volume)
{
    const SoundDef *def = SoundBank_Get(id);
    if (!def) return false;
    const float nearD = def->falloffEnd > 0.0f ? def->falloffStart : DEFAULT_NEAR;
    const float farD = def->falloffEnd > 0.0f ? def->falloffEnd : DEFAULT_FAR;
    const Spatial sp = Spatial_Compute(g_listener.x, g_listener.y, g_listener.z, g_lfx, g_lfz, where.x, where.y, where.z, nearD, farD);
    if (sp.gain <= 0.0f) return false;
    return PlaySlot(id, volume, 1.0f, sp.pan, sp.gain);
}

void Audio_PlayIn(int id, float seconds, float volume)
{
    Sched_Add(&g_sched, g_now + (double)(seconds > 0.0f ? seconds : 0.0f), id, volume, 1.0f);
}

void Audio_LoopSet(int id, bool on, float volume, float pitch)
{
    if (id < 0 || id >= SND_COUNT) return;
    Slot *s = &g_slot[id];
    const SoundDef *def = SoundBank_Get(id);
    if (!def || !def->loop) return;
    s->loopOn = on;
    s->loopTarget = on ? Mix_Gain(&g_mix, def->category, def->volume * volume) : 0.0f;
    s->loopPitch = pitch < 0.25f ? 0.25f : (pitch > 3.0f ? 3.0f : pitch);
}

void Audio_SetMaster(float v) { g_mix.master = v; }
void Audio_SetCategoryVolume(int category, float v) { if (category >= 0 && category < AUD_CAT_COUNT) g_mix.volume[category] = v; }
void Audio_SetCategoryMuted(int category, bool muted) { if (category >= 0 && category < AUD_CAT_COUNT) g_mix.muted[category] = muted; }

// ---- public: the frame ---------------------------------------------------------------------------------------------
static void UpdateLoops(float dt)
{
    for (int i = 0; i < SND_COUNT; ++i) {
        Slot *s = &g_slot[i];
        if (!s->musicLoaded) continue;
        const float step = LOOP_FADE_PER_SEC * dt;
        if (s->loopCur < s->loopTarget) { s->loopCur += step; if (s->loopCur > s->loopTarget) s->loopCur = s->loopTarget; }
        else if (s->loopCur > s->loopTarget) { s->loopCur -= step; if (s->loopCur < s->loopTarget) s->loopCur = s->loopTarget; }
        if (s->loopCur > 0.002f) {
            if (!s->loopPlaying) { PlayMusicStream(s->music); s->loopPlaying = true; }
            SetMusicVolume(s->music, s->loopCur > 1.0f ? 1.0f : s->loopCur);
            SetMusicPitch(s->music, s->loopPitch > 0.0f ? s->loopPitch : 1.0f);
            UpdateMusicStream(s->music);
        } else if (s->loopPlaying) {
            StopMusicStream(s->music);
            s->loopPlaying = false;
        }
    }
}

void Audio_Update(float dt, Vector3 listener, float listenerFx, float listenerFz, bool userGesture)
{
    if (!g_inited) return;
    g_now = GetTime();
    g_listener = listener; g_lfx = listenerFx; g_lfz = listenerFz;
    g_status.bankLoaded = SoundBank_Loaded();
    g_status.revision = SoundBank_Revision();

    if (!g_status.deviceReady && !g_status.deviceFailed) {
#if TOWER_AUDIO_GESTURE_UNLOCK
        if (userGesture) OpenDevice();                          // a browser keeps the sound card shut until the player touches the page
#else
        (void)userGesture;
        OpenDevice();
#endif
    }
    if (!g_status.deviceReady) return;

    if (g_status.bankLoaded && g_builtRevision != g_status.revision) {      // first load, or a new sounds.mdix came in (web): render again
        ResetAll();
        g_builtRevision = g_status.revision;
    }
    g_status.total = g_status.bankLoaded ? SND_COUNT : 0;
    const double t0 = GetTime();
    int builtNow = 0;                                               // always at least one, then as many as fit in the budget
    while (g_status.bankLoaded && g_next < SND_COUNT && (builtNow == 0 || GetTime() - t0 < BUILD_BUDGET_S)) {
        BuildOne(g_next++);
        ++g_status.built;
        ++builtNow;
    }

    SchedItem due;
    while (Sched_PopDue(&g_sched, g_now, &due)) Audio_PlayEx(due.id, due.volume, due.pitch, 0.0f);
    UpdateLoops(dt);

    int active = 0;
    for (int i = 0; i < SND_COUNT; ++i) if (g_slot[i].built) active += Voices_Active(&g_slot[i].pool, g_now) + (g_slot[i].loopPlaying ? 1 : 0);
    g_status.voicesActive = active;
    g_status.limiterGain = g_limiter.gain;
}

const AudioStatus *Audio_Status(void) { return &g_status; }
