// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "sound_bank.h and sound_bank.c"
// ============================================================================
#include "sound_bank.h"

#include <stdio.h>
#include <string.h>

static const char *const SOUND_NAMES[SND_COUNT] = {
    "footstep", "jump", "land", "slide", "pipe_swing", "pipe_hit", "pistol", "shotgun", "shotgun_pump",
    "rifle", "dry_fire", "pickup", "use_item", "flash_click", "battery", "hurt", "death", "hit_confirm",
    "kill_confirm", "skitter", "alert", "door", "heartbeat", "ambient", "ui_click",
};

// The .mdix file writes these enums out by value (Cat, Wave); stop the build if a C enum is renumbered.
typedef char sound_enums_match_the_mdix_file[(AUD_CAT_SFX == 0 && AUD_CAT_UI == 1 && AUD_CAT_AMBIENT == 2 && AUD_CAT_VOICE == 3
    && SYN_SINE == 0 && SYN_SQUARE == 1 && SYN_SAW == 2 && SYN_TRIANGLE == 3 && SYN_NOISE == 4) ? 1 : -1];

static SoundDef g_bank[SND_COUNT];
static bool     g_loaded = false;
static unsigned g_revision = 0;

typedef struct { const MdixDoc *d; char err[160]; } Rd;

static void Fail(Rd *r, const char *path, const char *why)
{
    if (!r->err[0]) snprintf(r->err, sizeof r->err, "%.80s: %.70s", path, why);
}

static int RInt(Rd *r, int row, const char *key, int lo, int hi)
{
    char p[48]; int v = 0;
    snprintf(p, sizeof p, "sounds[%d].%s", row, key);
    if (!MdixDoc_Int(r->d, p, &v)) { Fail(r, p, "missing or not an int"); return lo; }
    if (v < lo || v > hi) { char w[48]; snprintf(w, sizeof w, "must be %d to %d", lo, hi); Fail(r, p, w); return lo; }
    return v;
}

static float RFloat(Rd *r, int row, const char *key, float lo, float hi)
{
    char p[48]; float v = 0.0f;
    snprintf(p, sizeof p, "sounds[%d].%s", row, key);
    if (!MdixDoc_Float(r->d, p, &v)) { Fail(r, p, "missing or not a number"); return lo; }
    if (!(v >= lo && v <= hi)) { char w[56]; snprintf(w, sizeof w, "must be %g to %g", (double)lo, (double)hi); Fail(r, p, w); return lo; }
    return v;
}

static bool RBool(Rd *r, int row, const char *key)
{
    char p[48]; bool v = false;
    snprintf(p, sizeof p, "sounds[%d].%s", row, key);
    if (!MdixDoc_Bool(r->d, p, &v)) Fail(r, p, "missing or not true/false");
    return v;
}

bool SoundBank_LoadFrom(const MdixDoc *doc, char *err, int errCap)
{
    char local[160];
    if (!err) { err = local; errCap = (int)sizeof local; }
    err[0] = 0;
    if (!doc) { snprintf(err, (size_t)errCap, "no document"); return false; }

    static SoundDef next[SND_COUNT];                    // built aside, installed only if everything is right
    memset(next, 0, sizeof next);
    Rd r = { doc, "" };
    const int rows = MdixDoc_ArrayLen(doc, "sounds");
    if (rows < 1) { snprintf(err, (size_t)errCap, "sounds: no rows"); return false; }

    int cur = -1;                                       // index of the sound the next layer row belongs to
    for (int i = 0; i < rows && !r.err[0]; ++i) {
        const int kind = RInt(&r, i, "kind", 0, 1);     // 0 = a sound, 1 = a layer of the sound above it
        if (r.err[0]) break;
        if (kind == 0) {
            ++cur;
            if (cur >= SND_COUNT) { Fail(&r, "sounds", "more sounds than the game has ids for"); break; }
            SoundDef *s = &next[cur];
            char id[32] = "";
            char p[48];
            snprintf(p, sizeof p, "sounds[%d].id", i);
            if (!MdixDoc_Str(doc, p, id, (int)sizeof id) || id[0] == 0) { Fail(&r, p, "missing, empty or too long"); break; }
            if (strcmp(id, SOUND_NAMES[cur]) != 0) {
                char w[96]; snprintf(w, sizeof w, "is \"%s\" but sound %d must be \"%s\"", id, cur, SOUND_NAMES[cur]);
                Fail(&r, p, w); break;
            }
            snprintf(s->id, sizeof s->id, "%s", id);
            s->category     = RInt(&r, i, "category", 0, AUD_CAT_COUNT - 1);
            s->volume       = RFloat(&r, i, "volume", 0.0f, 2.0f);
            s->pitchJitter  = RFloat(&r, i, "pitch_jitter", 0.0f, 0.5f);
            s->voices       = RInt(&r, i, "voices", 1, VOICES_MAX);
            s->variants     = RInt(&r, i, "variants", 1, 6);
            s->loop         = RBool(&r, i, "loop");
            s->recipe.loopFade = RFloat(&r, i, "loop_fade", 0.0f, 0.5f);
            s->falloffStart = RFloat(&r, i, "falloff_start", 0.0f, 100.0f);
            s->falloffEnd   = RFloat(&r, i, "falloff_end", 0.0f, 200.0f);
            s->recipe.duration = RFloat(&r, i, "duration", 0.0f, SYNTH_MAX_SECS);
            if (s->falloffEnd > 0.0f && s->falloffEnd <= s->falloffStart) Fail(&r, id, "falloff_end must be above falloff_start");
            if (s->loop && s->recipe.loopFade <= 0.0f) Fail(&r, id, "a loop needs loop_fade above 0");
            if (s->loop && s->recipe.duration <= 0.0f) Fail(&r, id, "a loop needs a duration");
        } else {
            if (cur < 0) { Fail(&r, "sounds[0]", "the first row must be a sound (kind 0), not a layer"); break; }
            SoundDef *s = &next[cur];
            if (s->recipe.layers >= SYNTH_MAX_LAYERS) { Fail(&r, s->id, "more than 4 layers"); break; }
            SynLayer *l = &s->recipe.layer[s->recipe.layers++];
            l->wave   = RInt(&r, i, "wave", 0, SYN_NOISE);
            l->f0     = RFloat(&r, i, "f0", 0.0f, 12000.0f);
            l->f1     = RFloat(&r, i, "f1", 0.0f, 12000.0f);
            l->level  = RFloat(&r, i, "level", 0.0f, 4.0f);
            l->delay  = RFloat(&r, i, "delay", 0.0f, SYNTH_MAX_SECS);
            l->attack = RFloat(&r, i, "attack", 0.0f, 2.0f);
            l->decay  = RFloat(&r, i, "decay", 0.0005f, 1000.0f);
            l->length = RFloat(&r, i, "length", 0.0f, SYNTH_MAX_SECS);
            l->lp0    = RFloat(&r, i, "lp0", 0.0f, 11000.0f);
            l->lp1    = RFloat(&r, i, "lp1", 0.0f, 11000.0f);
            l->drive  = RFloat(&r, i, "drive", 0.0f, 8.0f);
            if (l->wave != SYN_NOISE && l->f0 <= 0.0f) Fail(&r, s->id, "a tone layer needs f0 above 0");
            if ((l->lp0 > 0.0f) != (l->lp1 > 0.0f)) Fail(&r, s->id, "lp0 and lp1 must both be 0 (no filter) or both above 0");
            if (l->f0 > 0.0f && l->f1 <= 0.0f && l->wave != SYN_NOISE) Fail(&r, s->id, "f1 must be above 0 when f0 is (use the same number for no glide)");
        }
    }
    if (!r.err[0] && cur != SND_COUNT - 1) {
        char w[80]; snprintf(w, sizeof w, "%d sounds, the game has %d", cur + 1, SND_COUNT);
        Fail(&r, "sounds", w);
    }
    for (int i = 0; i <= cur && i < SND_COUNT && !r.err[0]; ++i)
        if (next[i].recipe.layers < 1) Fail(&r, next[i].id, "has no layers");
    if (r.err[0]) { snprintf(err, (size_t)errCap, "%s", r.err); return false; }

    memcpy(g_bank, next, sizeof g_bank);
    g_loaded = true;
    ++g_revision;
    return true;
}

bool SoundBank_Loaded(void) { return g_loaded; }
unsigned SoundBank_Revision(void) { return g_revision; }
const SoundDef *SoundBank_Get(int id) { return (g_loaded && id >= 0 && id < SND_COUNT) ? &g_bank[id] : NULL; }
const char *SoundBank_Name(int id) { return (id >= 0 && id < SND_COUNT) ? SOUND_NAMES[id] : ""; }
