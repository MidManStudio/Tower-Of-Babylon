// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "tests/audio_headless.c"
// ============================================================================
// The audio system without a sound card: the synthesiser's numbers (pitch, envelope, filter, loop, clipping), the mixer
// arithmetic (volumes, limiter, voices, pan and distance, footsteps, the delay queue), and the sound bank as it comes out
// of assets/config/sounds.mdix, including that a bad document is refused. Every shipped recipe is rendered and measured.
// Set AUDIO_DUMP=some/folder to also write each sound as a WAV file you can play on any phone.
// Inside a macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DAUDIO_HEADLESS_TEST -Isrc -Iraylib/src tests/audio_headless.c src/audio/synth.c src/audio/audio_core.c src/audio/sound_bank.c src/config/game_config.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c src/weapons/weapons.c src/flashlight/flashlight.c src/projectiles/projectile_config.c src/projectiles/projectile_patterns.c -lm -o audio_headless
// Run from the project root (it reads assets/config/sounds.mdix):
//   ./audio_headless
#ifdef AUDIO_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio_core.h"
#include "audio/sound_bank.h"
#include "audio/synth.h"
#include "config/game_config.h"

extern const MdixBaked g_bakedSounds;

static int g_failed = 0;
static void Check(int ok, const char *what) { printf("  %s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++g_failed; }
static int Near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

static SynLayer Tone(int wave, float f0, float f1, float level, float attack, float decay, float length)
{
    SynLayer l = { 0 };
    l.wave = wave; l.f0 = f0; l.f1 = f1; l.level = level; l.attack = attack; l.decay = decay; l.length = length;
    return l;
}
static SynRecipe One(SynLayer l, float duration) { SynRecipe r = { 0 }; r.layers = 1; r.layer[0] = l; r.duration = duration; return r; }

static short *Render(const SynRecipe *r, unsigned seed, int *n)
{
    const int cap = Synth_Frames(r, SYNTH_RATE);
    short *out = (short *)malloc((size_t)cap * sizeof(short));
    *n = Synth_Render(r, seed, SYNTH_RATE, out, cap);
    return out;
}
static int Peak(const short *s, int n) { int p = 0; for (int i = 0; i < n; ++i) { const int a = abs(s[i]); if (a > p) p = a; } return p; }
static float Rms(const short *s, int from, int to) { double e = 0; for (int i = from; i < to; ++i) e += (double)s[i] * s[i]; return (float)sqrt(e / (to - from)); }
static int Crossings(const short *s, int n) { int c = 0; for (int i = 1; i < n; ++i) if ((s[i - 1] < 0) != (s[i] < 0)) ++c; return c; }

static int Sorted(const MdixBaked *b)
{
    for (int i = 1; i < b->count; ++i) if (strcmp(b->entries[i - 1].path, b->entries[i].path) >= 0) return 0;
    return b->count > 0;
}

static MdixBaked Doctored(const MdixBaked *src, MdixBakedEntry *store, const char *path, const char *newText, const char *newPath)
{
    for (int i = 0; i < src->count; ++i) {
        store[i] = src->entries[i];
        if (strcmp(store[i].path, path) == 0) { if (newText) store[i].text = newText; if (newPath) store[i].path = newPath; }
    }
    MdixBaked b = *src;
    b.entries = store;
    return b;
}

static int Rejects(const MdixBaked *t, const char *needle)
{
    MdixDoc *d = MdixDoc_FromBaked(t);
    char err[160];
    const unsigned rev = SoundBank_Revision();
    const int refused = !SoundBank_LoadFrom(d, err, (int)sizeof err);
    const int named = strstr(err, needle) != NULL;
    const int unchanged = SoundBank_Revision() == rev;
    if (refused && !named) printf("      (error was: %s)\n", err);
    MdixDoc_Free(d);
    return refused && named && unchanged;
}

static void WriteWav(const char *path, const short *s, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const unsigned bytes = (unsigned)n * 2u, rate = SYNTH_RATE, byteRate = rate * 2u, chunk = 36u + bytes;
    const unsigned short fmt = 1, ch = 1, align = 2, bits = 16;
    const unsigned fmtLen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&chunk, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(s, 2, (size_t)n, f);
    fclose(f);
}

int main(void)
{
    int n = 0;
    short *a, *b;

    printf("Synthesiser: pitch, envelope, filter, timing\n");
    {   // a constant 200 Hz sine for half a second crosses zero 200 times (2 per cycle, 100 cycles)
        SynRecipe r = One(Tone(SYN_SINE, 200.0f, 200.0f, 1.0f, 0.0f, 1000.0f, 0.5f), 0.0f);
        a = Render(&r, 1, &n);
        Check(n == SYNTH_RATE / 2, "half a second is 11025 samples at 22050 Hz");
        Check(abs(Crossings(a, n) - 200) <= 2, "a 200 Hz sine crosses zero about 200 times in half a second");
        Check(Peak(a, n) > 30000 && Peak(a, n) <= 32767, "full level lands just under full scale (0.98 of it at most)");
        free(a);
    }
    {   // an exponential glide 100 -> 400 Hz in one second: crossings = 2 * integral of f = 2 * 300 / ln 4
        SynRecipe r = One(Tone(SYN_SINE, 100.0f, 400.0f, 1.0f, 0.0f, 1000.0f, 1.0f), 0.0f);
        a = Render(&r, 1, &n);
        const float want = 2.0f * 300.0f / logf(4.0f);
        Check(fabsf((float)Crossings(a, n) - want) < 6.0f, "a 100 to 400 Hz glide has the zero crossings the maths says (about 433)");
        free(a);
    }
    {   // attack 0.1 s then e-fold every 0.1 s: loudest at 0.1 s, then 1/e per 0.1 s
        SynRecipe r = One(Tone(SYN_SINE, 1000.0f, 1000.0f, 1.0f, 0.1f, 0.1f, 0.0f), 0.0f);
        a = Render(&r, 1, &n);
        const int w = SYNTH_RATE / 100;                                                  // 10 ms windows
        const float atPeak = Rms(a, SYNTH_RATE / 10 - w / 2, SYNTH_RATE / 10 + w / 2);
        const float later = Rms(a, SYNTH_RATE / 5 - w / 2, SYNTH_RATE / 5 + w / 2);
        const float early = Rms(a, SYNTH_RATE / 20 - w / 2, SYNTH_RATE / 20 + w / 2);
        Check(early < atPeak * 0.65f && early > atPeak * 0.35f, "halfway through the attack it is about half as loud as at the top");
        Check(fabsf(later / atPeak - expf(-1.0f)) < 0.06f, "0.1 s after the top it has fallen to 1/e");
        free(a);
    }
    {   // a low-pass takes the top off noise: much less change from sample to sample
        SynRecipe raw = One(Tone(SYN_NOISE, 0, 0, 1.0f, 0.0f, 1000.0f, 0.5f), 0.0f);
        SynRecipe lp = raw; lp.layer[0].lp0 = 400.0f; lp.layer[0].lp1 = 400.0f;
        a = Render(&raw, 7, &n); b = Render(&lp, 7, &n);
        double da = 0, db = 0, ma = 0, mb = 0;
        for (int i = 1; i < n; ++i) { da += abs(a[i] - a[i - 1]); db += abs(b[i] - b[i - 1]); ma += abs(a[i]); mb += abs(b[i]); }
        Check((db / mb) < (da / ma) * 0.25, "a 400 Hz low-pass leaves noise at a quarter of its sample-to-sample jitter or less");
        free(a); free(b);
    }
    {   // delay: nothing before it starts
        SynLayer l = Tone(SYN_SINE, 300.0f, 300.0f, 1.0f, 0.001f, 1000.0f, 0.3f); l.delay = 0.2f;
        SynRecipe r = One(l, 0.0f);
        a = Render(&r, 1, &n);
        int silent = 1;
        for (int i = 0; i < (int)(0.19f * SYNTH_RATE); ++i) if (a[i] != 0) silent = 0;
        Check(silent && n == (int)(0.5f * SYNTH_RATE), "a layer with a 0.2 s delay is silent until then and the sound is delay + length long");
        free(a);
    }
    {   // never clipped: three layers adding up to 3x are scaled down to 0.98, a quiet sound is left alone
        SynRecipe r = One(Tone(SYN_SINE, 220.0f, 220.0f, 3.0f, 0.0f, 1000.0f, 0.3f), 0.0f);
        a = Render(&r, 1, &n);
        Check(Near((float)Peak(a, n), 0.98f * 32767.0f, 40.0f), "a layer at level 3 is scaled so the peak is 0.98 of full scale");
        free(a);
        r = One(Tone(SYN_SINE, 220.0f, 220.0f, 0.25f, 0.0f, 1000.0f, 0.3f), 0.0f);
        a = Render(&r, 1, &n);
        Check(Near((float)Peak(a, n), 0.25f * 32767.0f, 60.0f), "a quiet sound is never scaled up");
        Check(a[n - 1] == 0, "the last sample is zero (a 4 ms fade, so stopping makes no click)");
        free(a);
    }
    {   // same recipe, same seed: same samples; another seed: another noise
        SynRecipe r = One(Tone(SYN_NOISE, 0, 0, 1.0f, 0.001f, 0.05f, 0.2f), 0.0f);
        short *x, *y, *z; int n1, n2, n3;
        x = Render(&r, 5, &n1); y = Render(&r, 5, &n2); z = Render(&r, 6, &n3);
        Check(n1 == n2 && memcmp(x, y, (size_t)n1 * 2) == 0, "the same seed gives the identical samples");
        Check(memcmp(x, z, (size_t)n1 * 2) != 0, "another seed gives another take");
        free(x); free(y); free(z);
    }
    {   // a too-small buffer or a fade longer than half the sound is refused, not overrun
        SynRecipe r = One(Tone(SYN_SINE, 200.0f, 200.0f, 1.0f, 0.0f, 1000.0f, 0.5f), 0.0f);
        short tiny[10];
        Check(Synth_Render(&r, 1, SYNTH_RATE, tiny, 10) == 0, "a buffer that is too small writes nothing and returns 0");
        r.loopFade = 0.4f;
        a = (short *)malloc(20000 * 2);
        Check(Synth_Render(&r, 1, SYNTH_RATE, a, 20000) == 0, "a loop fade of more than half the sound is refused");
        free(a);
    }

    printf("Loops\n");
    {   // a sound that repeats: the last sample runs on into the first as smoothly as any two neighbours
        SynRecipe r = { 0 };
        r.layers = 2; r.duration = 2.0f; r.loopFade = 0.2f;
        r.layer[0] = Tone(SYN_SINE, 55.0f, 55.0f, 0.5f, 0.0f, 1000.0f, 0.0f);
        r.layer[1] = Tone(SYN_SINE, 82.5f, 82.5f, 0.3f, 0.0f, 1000.0f, 0.0f);
        a = Render(&r, 1, &n);
        double step = 0; for (int i = 1; i < n; ++i) step += abs(a[i] - a[i - 1]);
        step /= (n - 1);
        Check(n == 2 * SYNTH_RATE, "a looped sound is exactly its duration long");
        Check(fabs((double)a[0] - (double)a[n - 1]) < step * 3.0 + 4.0, "from its last sample to its first there is no more of a jump than between any two neighbours");
        free(a);
    }

    printf("Mixer arithmetic\n");
    {
        AudioMix m; Mix_Init(&m);
        m.master = 0.5f; m.volume[AUD_CAT_SFX] = 0.8f;
        Check(Near(Mix_Gain(&m, AUD_CAT_SFX, 0.5f), 0.2f, 1e-5f), "sound x category x master: 0.5 x 0.8 x 0.5 is 0.2");
        m.muted[AUD_CAT_SFX] = true;
        Check(Mix_Gain(&m, AUD_CAT_SFX, 1.0f) == 0.0f && Mix_Gain(&m, AUD_CAT_UI, 1.0f) > 0.0f, "muting one category silences only it");
        Check(Mix_Gain(&m, 99, 1.0f) == 0.0f, "an unknown category is silent");
    }
    {   // the limiter: a loud buffer is under the threshold at once, and the gain comes back
        Limiter l; Limiter_Init(&l, 0.9f, 1.0f);
        float buf[2 * 1024];
        for (int i = 0; i < 1024; ++i) { buf[2 * i] = 1.8f * sinf(0.05f * (float)i); buf[2 * i + 1] = buf[2 * i]; }
        Limiter_Process(&l, buf, 1024, 2, 44100);
        float pk = 0; for (int i = 0; i < 2048; ++i) if (fabsf(buf[i]) > pk) pk = fabsf(buf[i]);
        Check(pk <= 0.9f + 1e-4f, "a buffer peaking at 1.8 comes out at no more than the 0.9 threshold");
        Check(l.gain < 0.55f && l.gain > 0.45f, "the gain dropped to about 0.5");
        int buffers = 0;
        for (; buffers < 200 && l.gain < 0.999f; ++buffers) { for (int i = 0; i < 2048; ++i) buf[i] = 0.2f; Limiter_Process(&l, buf, 1024, 2, 44100); }
        Check(buffers > 20 && buffers < 80, "on quiet input the gain recovers to 1 in about half a second (not instantly, not never)");
        for (int i = 0; i < 2048; ++i) buf[i] = 0.3f;
        Limiter_Process(&l, buf, 1024, 2, 44100);
        Check(Near(buf[1000], 0.3f, 1e-4f), "a quiet signal passes untouched once the gain is back");
    }
    {   // voices: free ones first, then the oldest is stolen
        VoicePool p; Voices_Init(&p, 3);
        bool st = true;
        const int s0 = Voices_Acquire(&p, 10.0, 1.0, &st); const bool st0 = st;
        const int s1 = Voices_Acquire(&p, 10.1, 1.0, &st); const bool st1 = st;
        const int s2 = Voices_Acquire(&p, 10.2, 1.0, &st); const bool st2 = st;
        Check(s0 == 0 && s1 == 1 && s2 == 2 && !st0 && !st1 && !st2, "three plays take three free voices in order");
        Check(Voices_Active(&p, 10.3) == 3, "three are sounding");
        const int s3 = Voices_Acquire(&p, 10.3, 1.0, &st);
        Check(s3 == 0 && st, "a fourth at the same moment steals the voice that started longest ago");
        const int s4 = Voices_Acquire(&p, 11.15, 1.0, &st);
        Check(s4 == 1 && !st, "later, a voice that has finished is free again (the lowest one)");
        Voices_Init(&p, 99);
        Check(p.size == VOICES_MAX, "a pool is never bigger than VOICES_MAX");
    }
    {   // pan and distance, listener facing -Z like raylib's default camera (right is +X)
        Spatial s;
        s = Spatial_Compute(0, 0, 0, 0, -1, 10, 0, 0, 4, 40);
        Check(s.pan > 0.95f, "a sound to the right pans right");
        s = Spatial_Compute(0, 0, 0, 0, -1, -10, 0, 0, 4, 40);
        Check(s.pan < -0.95f, "a sound to the left pans left");
        s = Spatial_Compute(0, 0, 0, 0, -1, 0, 0, -10, 4, 40);
        Check(Near(s.pan, 0.0f, 1e-4f), "straight ahead is centred");
        s = Spatial_Compute(0, 0, 0, 0, -1, 0, 0, 10, 4, 40);
        Check(Near(s.pan, 0.0f, 1e-4f), "straight behind is centred too (no front-back cue from a pan alone)");
        s = Spatial_Compute(0, 0, 0, 1, 0, 0, 0, 10, 4, 40);
        Check(s.pan > 0.95f || s.pan < -0.95f, "turning the listener turns the sound's side: facing +X, a sound at +Z is hard to one side");
        s = Spatial_Compute(0, 0, 0, 0, -1, 1, 0, 0, 4, 40);
        Check(s.pan > 0.0f && s.pan < 0.6f, "a sound one metre away does not swing hard to the side");
        Check(Near(Spatial_Compute(0, 0, 0, 0, -1, 4, 0, 0, 4, 40).gain, 1.0f, 1e-5f), "full volume out to the near distance");
        Check(Near(Spatial_Compute(0, 0, 0, 0, -1, 22, 0, 0, 4, 40).gain, 0.25f, 1e-4f), "halfway to the far distance it is a quarter (the square of the half that is left)");
        Check(Spatial_Compute(0, 0, 0, 0, -1, 40, 0, 0, 4, 40).gain == 0.0f && Spatial_Compute(0, 0, 0, 0, -1, 400, 0, 0, 4, 40).gain == 0.0f, "silent at and beyond the far distance");
    }
    {   // footsteps
        Footsteps f = { 0 };
        int steps = 0, feet[8] = { 0 };
        for (int i = 0; i < 16; ++i) if (Footsteps_Advance(&f, 0.5f, 2.0f)) { feet[steps < 8 ? steps : 7] = f.foot; ++steps; }
        Check(steps == 4, "16 half-metre moves with a 2 m stride are 4 steps");
        Check(feet[0] == 1 && feet[1] == 0 && feet[2] == 1 && feet[3] == 0, "the feet alternate");
        Check(!Footsteps_Advance(&f, 0.0f, 2.0f) && !Footsteps_Advance(&f, -1.0f, 2.0f), "standing still or going backwards in time makes no step");
        Footsteps t = { 0 };
        Check(Footsteps_Advance(&t, 100.0f, 2.0f) && !Footsteps_Advance(&t, 0.5f, 2.0f), "a huge jump (a teleport) is one step, not a burst");
    }
    {   // the delay queue
        Schedule q; Sched_Init(&q);
        SchedItem it;
        Sched_Add(&q, 5.0, 7, 1.0f, 1.0f); Sched_Add(&q, 3.0, 8, 1.0f, 1.0f); Sched_Add(&q, 4.0, 9, 1.0f, 1.0f);
        Check(!Sched_PopDue(&q, 2.9, &it), "nothing is due before its time");
        Check(Sched_PopDue(&q, 10.0, &it) && it.id == 8, "the earliest due item comes first");
        Check(Sched_PopDue(&q, 10.0, &it) && it.id == 9 && Sched_PopDue(&q, 10.0, &it) && it.id == 7 && !Sched_PopDue(&q, 10.0, &it), "then the rest in order, then none");
        int added = 0; for (int i = 0; i < SCHED_MAX + 4; ++i) added += Sched_Add(&q, 1.0, i, 1.0f, 1.0f);
        Check(added == SCHED_MAX, "the queue holds SCHED_MAX items and refuses the rest");
    }

    printf("Sound bank (assets/config/sounds.mdix)\n");
    Check(Sorted(&g_bakedSounds), "the baked table is in strcmp order (the lookup is a binary search)");
    {
        FILE *f = fopen("assets/config/sounds.mdix", "rb");
        Check(f != NULL, "assets/config/sounds.mdix is there (run from the project root)");
        if (f) {
            fseek(f, 0, SEEK_END); const long len = ftell(f); fseek(f, 0, SEEK_SET);
            char *txt = (char *)malloc((size_t)len + 1);
            const size_t got = fread(txt, 1, (size_t)len, f); fclose(f); txt[got] = 0;
            Check(Mdix_Hash(txt, (int)got) == g_bakedSounds.sourceHash, "the baked table is the one this file makes (else run: node tools/mdix_bake.mjs)");
            free(txt);
        }
    }
    {
        MdixDoc *d = MdixDoc_FromBaked(&g_bakedSounds);
        char err[160] = "";
        Check(!SoundBank_Loaded(), "nothing is loaded before the first load");
        Check(SoundBank_LoadFrom(d, err, (int)sizeof err), "the baked sounds load");
        if (err[0]) printf("      (%s)\n", err);
        MdixDoc_Free(d);
    }
    Check(SoundBank_Loaded() && SoundBank_Revision() == 1, "the bank is loaded and counts one revision");
    {
        int ok = 1, loops = 0;
        for (int i = 0; i < SND_COUNT; ++i) {
            const SoundDef *s = SoundBank_Get(i);
            if (!s || strcmp(s->id, SoundBank_Name(i)) != 0 || s->recipe.layers < 1 || s->recipe.layers > SYNTH_MAX_LAYERS) ok = 0;
            if (s && s->loop) ++loops;
        }
        Check(ok, "every SoundId has its row, in the right order, with 1 to 4 layers");
        Check(loops == 2 && SoundBank_Get(SND_HEARTBEAT)->loop && SoundBank_Get(SND_AMBIENT)->loop, "two sounds loop: the heartbeat and the ambience");
        Check(SoundBank_Get(-1) == NULL && SoundBank_Get(SND_COUNT) == NULL, "an id outside the list gives NULL, not a wild pointer");
    }

    printf("Every shipped recipe, rendered and measured\n");
    {
        const char *dump = getenv("AUDIO_DUMP");
        int bad = 0;
        double totalKb = 0;
        for (int i = 0; i < SND_COUNT; ++i) {
            const SoundDef *s = SoundBank_Get(i);
            const int cap = Synth_Frames(&s->recipe, SYNTH_RATE);
            for (int v = 0; v < s->variants; ++v) {
                short *pcm = (short *)malloc((size_t)cap * 2);
                const int got = Synth_Render(&s->recipe, 0x9e3779b1u * (unsigned)(i + 1) + 0x7f4a7c15u * (unsigned)v, SYNTH_RATE, pcm, cap);
                const int pk = got > 0 ? Peak(pcm, got) : 0;
                const float secs = (float)got / SYNTH_RATE;
                const int endOk = s->loop || pcm[got - 1] == 0;
                if (got != cap || pk < 1500 || pk > 32112 || secs > SYNTH_MAX_SECS || !endOk) {
                    printf("      %s take %d: %d samples (want %d), peak %d, last sample %d\n", s->id, v, got, cap, pk, got > 0 ? pcm[got - 1] : 0);
                    bad = 1;
                }
                if (v == 0) {
                    printf("      %-13s %5.2f s  peak %5.3f  rms %5.3f  %d layer%s%s\n", s->id, secs, (double)pk / 32767.0, (double)Rms(pcm, 0, got) / 32767.0,
                           s->recipe.layers, s->recipe.layers == 1 ? "" : "s", s->loop ? "  (loop)" : "");
                    if (dump) { char path[512]; snprintf(path, sizeof path, "%s/%02d_%s.wav", dump, i, s->id); WriteWav(path, pcm, got); }
                }
                totalKb += got * 2.0 / 1024.0;
                free(pcm);
            }
        }
        Check(!bad, "every take of every sound renders to its full length, is audible (peak above 5%), is not clipped, and ends on zero");
        Check(totalKb < 1500.0, "all the takes together are under 1.5 MB of 16-bit samples");
        printf("      all takes: %.0f KB at 22050 Hz\n", totalKb);
    }

    printf("Bad documents are refused and change nothing\n");
    {
        MdixBakedEntry store[4000];
        MdixBaked bad;
        if (g_bakedSounds.count > 4000) { Check(0, "the test's scratch table is big enough"); }
        bad = Doctored(&g_bakedSounds, store, "sounds[0].id", "\"pistol\"", NULL);
        Check(Rejects(&bad, "sounds[0].id"), "a sound in the wrong place is refused and named");
        bad = Doctored(&g_bakedSounds, store, "sounds[0].voices", NULL, "sounds[0].voicez");
        Check(Rejects(&bad, "sounds[0].voices"), "a missing value is refused and named");
        bad = Doctored(&g_bakedSounds, store, "sounds[0].voices", "9", NULL);
        Check(Rejects(&bad, "sounds[0].voices"), "9 voices is out of range");
        bad = Doctored(&g_bakedSounds, store, "sounds[0].volume", "5.0", NULL);
        Check(Rejects(&bad, "sounds[0].volume"), "a volume of 5 is out of range");
        bad = Doctored(&g_bakedSounds, store, "sounds[2].wave", "9", NULL);
        Check(Rejects(&bad, "sounds[2].wave"), "an unknown wave is refused");
        bad = Doctored(&g_bakedSounds, store, "sounds[2].kind", "7", NULL);
        Check(Rejects(&bad, "sounds[2].kind"), "a row that is neither a sound nor a layer is refused");
    }

    printf("The config layer hands sounds.mdix to the bank\n");
    {
        GameConfig_LoadBaked();                                        // no extra is registered yet, so the bank is not touched
        const unsigned before = SoundBank_Revision();
        Check(GameConfig_RegisterExtra("assets/config/sounds.mdix", &g_bakedSounds, SoundBank_LoadFrom), "a system can register its file");
        Check(SoundBank_Revision() == before + 1, "registering after the config is loaded installs the baked copy at once");
        GameConfig_LoadBaked();
        Check(SoundBank_Revision() == before + 2, "loading the baked config again loads the registered file again");
        int more = 0;
        for (int i = 0; i < GAME_CONFIG_MAX_EXTRAS; ++i) more += GameConfig_RegisterExtra("x", &g_bakedSounds, SoundBank_LoadFrom);
        Check(more == GAME_CONFIG_MAX_EXTRAS - 1 && !GameConfig_RegisterExtra("y", &g_bakedSounds, SoundBank_LoadFrom), "the list holds GAME_CONFIG_MAX_EXTRAS files and refuses the next");
        Check(!GameConfig_RegisterExtra(NULL, &g_bakedSounds, SoundBank_LoadFrom), "a registration with no path is refused");
    }

    printf("\n%s (%d failed)\n", g_failed ? "FAILED" : "all passed", g_failed);
    return g_failed ? 1 : 0;
}

#endif // AUDIO_HEADLESS_TEST
