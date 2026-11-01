// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/flashlight.md, section "tests/flashlight_headless.c"
// ============================================================================
// The flashlight's rules without a window: charge draining only while on, batteries (loaded, refused when nearly full,
// bringing a flat light back), the flicker as it runs low (steady above the low mark, worse and worse below it, dim between
// flickers, always the same pattern), dying at zero, the beam following the camera with a lag, and the beam's shape.
// Inside a macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DFLASHLIGHT_HEADLESS_TEST -Isrc -Iraylib/src tests/flashlight_headless.c src/flashlight/flashlight.c src/config/game_config.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c src/weapons/weapons.c src/projectiles/projectile_config.c src/projectiles/projectile_patterns.c -lm -o flashlight_headless
// Run:
//   ./flashlight_headless
#ifdef FLASHLIGHT_HEADLESS_TEST

#include <math.h>
#include <stdio.h>

#include "config/game_config.h"
#include "flashlight/flashlight.h"

static int g_failed = 0;
static void Check(int ok, const char *what) { printf("  %s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++g_failed; }
static int Near(float a, float b, float eps) { return fabsf(a - b) < eps; }
static const float DT = 1.0f / 60.0f;
static const Vector3 FWD = { 0, 0, 1 };

static void Run(Flashlight *f, float secs, Vector3 fwd) { for (float t = 0; t < secs - 1e-6f; t += DT) Flashlight_Update(f, DT, fwd); }

// Of the frames in the next `secs` seconds, how many are a dropout (nearly dark) and how many are dimmer than steady.
static void Watch(Flashlight *f, float secs, int *frames, int *dropouts, float *brightest)
{
    *frames = *dropouts = 0; *brightest = 0;
    for (float t = 0; t < secs - 1e-6f; t += DT) {
        Flashlight_Update(f, DT, FWD);
        ++*frames;
        if (f->level < 0.25f) ++*dropouts;
        if (f->level > *brightest) *brightest = f->level;
    }
}

int main(void)
{
    GameConfig_LoadBaked();          // links and loads the same way the game does
    // The rules are tested against a fixed set of numbers, not the ones in flashlight.mdix, so tuning the file
    // (how long a battery lasts) never breaks them. tests/config_headless.c checks what the file says.
    const FlashlightConfig rules = { 300.0f, 120.0f, 150.0f, 0.20f, 14.0f, 30.0f, 20.0f, 0.8f, 14.0f, 0xFFE9B0u };
    Flashlight_SetConfig(&rules);
    const FlashlightConfig *c = Flashlight_Config();
    Flashlight f;

    printf("Switching and charge\n");
    Flashlight_Init(&f);
    Check(!f.on && f.charge == 0.0f && Flashlight_Toggle(&f) == false && !f.on, "a new flashlight is off, empty, and will not switch on with no charge");
    Flashlight_Give(&f, c->startSecs);
    Check(Near(f.charge, 120.0f, 1e-4f) && Near(Flashlight_Fraction(&f), 0.4f, 1e-4f), "one found on the floor holds 120 s: 40% of the 300 s capacity");
    Flashlight_Give(&f, 9999.0f);
    Check(Near(f.charge, 300.0f, 1e-4f), "a charge above the capacity is clamped to it");
    Flashlight_Give(&f, 120.0f);
    Check(Flashlight_Toggle(&f) == true && f.on, "with some charge it switches on");
    Run(&f, 10.0f, FWD);
    Check(Near(f.charge, 110.0f, 0.05f) && Near(f.level, 1.0f, 1e-4f), "ten seconds on costs ten seconds of charge, at full brightness");
    Check(Flashlight_Toggle(&f) == false && !f.on, "toggling again switches it off");
    Run(&f, 10.0f, FWD);
    Check(Near(f.charge, 110.0f, 0.05f) && f.level == 0.0f, "off, it spends nothing and gives no light");

    printf("Batteries\n");
    Flashlight_Init(&f); Flashlight_Give(&f, 100.0f);
    Check(Near(Flashlight_AddBattery(&f), 150.0f, 1e-4f) && Near(f.charge, 250.0f, 1e-4f), "a battery adds 150 s");
    Check(Near(Flashlight_AddBattery(&f), 0.0f, 1e-4f) && Near(f.charge, 250.0f, 1e-4f), "and is refused at 250 s, above capacity - half a battery (225 s): it would be half wasted");
    Flashlight_Give(&f, 200.0f);
    Check(Near(Flashlight_AddBattery(&f), 100.0f, 1e-4f) && Near(f.charge, 300.0f, 1e-4f), "at 200 s it is taken but fills only to the capacity (it added 100 s)");
    Flashlight_Init(&f); Flashlight_Give(&f, 3.0f); Flashlight_Toggle(&f);
    Run(&f, 4.0f, FWD);
    Check(!f.on && f.charge == 0.0f && f.level == 0.0f, "a flashlight that runs out goes off and stays at 0");
    Flashlight_Init(&f); Flashlight_Give(&f, 1.0f); Flashlight_Toggle(&f);
    int diedFrames = 0;
    for (int i = 0; i < 90; ++i) { Flashlight_Update(&f, DT, FWD); diedFrames += f.died ? 1 : 0; }
    Check(diedFrames == 1, "it is reported dead on exactly one frame, so the game can say so once");
    Check(Flashlight_Toggle(&f) == false && !f.on, "flat, it will not switch on");
    Check(Near(Flashlight_AddBattery(&f), 150.0f, 1e-4f) && f.on, "a battery into a flat flashlight brings the light back on");

    printf("Failing\n");
    {
        // Above the low mark (60 s of 300) it is steady.
        Flashlight_Init(&f); Flashlight_Give(&f, 80.0f); Flashlight_Toggle(&f);
        int frames, drops; float best;
        Watch(&f, 15.0f, &frames, &drops, &best);
        Check(drops == 0 && best == 1.0f, "above the low mark it never flickers");
        // Then four windows of 15 s each through the low zone: the dropouts must increase window to window.
        Flashlight_Init(&f); Flashlight_Give(&f, 60.0f); Flashlight_Toggle(&f);
        int d[4], fr[4]; float b[4];
        for (int w = 0; w < 4; ++w) Watch(&f, 14.0f, &fr[w], &d[w], &b[w]);
        printf("    dropout frames in each 14 s window as it empties: %d, %d, %d, %d of %d\n", d[0], d[1], d[2], d[3], fr[0]);
        Check(d[0] > 0, "as soon as it is below the low mark it starts to cut out");
        Check(d[0] < d[1] && d[1] < d[2] && d[2] < d[3], "and cuts out more and more as the charge falls");
        Check(d[3] > fr[3] / 2, "in the last stretch it is dark more than half the time");
        Check(d[0] < fr[0] / 4, "just below the mark it is mostly lit");
        Check(b[0] < 1.0f && b[0] > 0.7f && b[3] < b[0], "between flickers it is dimmer than steady, and dimmer the emptier it is");
        // Same clock, same pattern.
        Flashlight a, b2;
        Flashlight_Init(&a); Flashlight_Init(&b2); Flashlight_Give(&a, 20.0f); Flashlight_Give(&b2, 20.0f); Flashlight_Toggle(&a); Flashlight_Toggle(&b2);
        int same = 1;
        for (int i = 0; i < 300; ++i) { Flashlight_Update(&a, DT, FWD); Flashlight_Update(&b2, DT, FWD); if (a.level != b2.level) same = 0; }
        Check(same, "the flicker is the same pattern for the same charge and clock (no random state)");
        int runs = 0, last = 1, minLevel = 1;
        Flashlight_Init(&a); Flashlight_Give(&a, 10.0f); Flashlight_Toggle(&a);
        for (int i = 0; i < 300; ++i) { Flashlight_Update(&a, DT, FWD); const int lit = a.level >= 0.25f; if (lit != last) ++runs; last = lit; if (a.level < 0.25f) minLevel = 0; }
        Check(runs >= 8 && !minLevel, "with 10 s left it switches off and on many times in 5 s (it glitches, it does not just dim)");
    }

    printf("The beam follows the camera, a little behind\n");
    {
        Flashlight_Init(&f); Flashlight_Give(&f, 120.0f); Flashlight_Toggle(&f);
        Flashlight_Update(&f, DT, FWD);
        Check(Near(f.dir.z, 1.0f, 1e-5f), "it starts out pointing where the camera does");
        const Vector3 right = { 1, 0, 0 };
        Flashlight_Update(&f, DT, right);
        const float after1 = acosf(f.dir.z * 0 + (f.dir.x * 0 + f.dir.z * 1.0f)) * 57.29578f;   // angle from the old direction
        Check(after1 > 10.0f && after1 < 30.0f && Near(sqrtf(f.dir.x * f.dir.x + f.dir.y * f.dir.y + f.dir.z * f.dir.z), 1.0f, 1e-4f), "a sudden quarter turn moves the beam only part of the way in one frame (about 21%), and it stays unit length");
        Run(&f, 0.5f, right);
        Check(f.dir.x > 0.999f, "half a second later it has caught up");
        // Looking straight up and down does not break it.
        Run(&f, 1.0f, (Vector3){ 0, 1, 0 });
        Check(f.dir.y > 0.999f, "looking straight up works");
        Flashlight_Update(&f, DT, (Vector3){ 0, 0, 0 });
        Check(f.dir.y > 0.999f, "a zero look direction (should never happen) leaves the beam where it was");
    }

    printf("The beam's shape (the same maths as the shader)\n");
    {
        Flashlight_Init(&f); Flashlight_Give(&f, 120.0f); Flashlight_Toggle(&f);
        Run(&f, 0.1f, FWD);
        const Vector3 eye = { 0, 1.6f, 0 };
        const float exp5 = (1.0f - 5.0f / 20.0f) * (1.0f - 5.0f / 20.0f) * c->strength;
        Check(Near(Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 5.0f }), exp5, 1e-4f), "straight ahead at 5 m: strength (0.8) times (1 - 5/20) squared = 0.45");
        Check(Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 1.0f }) > Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 5.0f })
              && Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 5.0f }) > Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 12.0f }), "it fades with distance");
        Check(Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 20.0f }) == 0.0f && Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 25.0f }) == 0.0f, "and is gone at its 20 m range");
        // Sideways: 14 degrees is full, 30 is nothing.
        float prev = 2.0f; int mono = 1;
        for (int deg = 0; deg <= 40; deg += 2) {
            const float r = deg * 3.14159265f / 180.0f;
            const float v = Flashlight_BeamAt(&f, eye, (Vector3){ 6.0f * sinf(r), 1.6f, 6.0f * cosf(r) });
            if (v > prev + 1e-6f) mono = 0;
            prev = v;
        }
        Check(mono, "off to the side it only ever gets dimmer");
        const float fullEdge = Flashlight_BeamAt(&f, eye, (Vector3){ 6.0f * sinf(13.0f * 0.0174533f), 1.6f, 6.0f * cosf(13.0f * 0.0174533f) });
        const float centre = Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 6.0f });
        Check(Near(fullEdge, centre, 1e-3f), "inside the inner 14 degrees it is at full strength");
        Check(Flashlight_BeamAt(&f, eye, (Vector3){ 6.0f * sinf(31.0f * 0.0174533f), 1.6f, 6.0f * cosf(31.0f * 0.0174533f) }) == 0.0f, "outside the outer 30 degrees it is nothing");
        Check(Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, -5.0f }) == 0.0f, "behind the player it is nothing");
        // Vertical angles count the same.
        Check(Near(Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f + 6.0f * sinf(20.0f * 0.0174533f), 6.0f * cosf(20.0f * 0.0174533f) }),
                   Flashlight_BeamAt(&f, eye, (Vector3){ 6.0f * sinf(20.0f * 0.0174533f), 1.6f, 6.0f * cosf(20.0f * 0.0174533f) }), 1e-4f), "up and down are the same as left and right");
        // Off, or a dropout, gives nothing; a dimmer level scales it.
        f.level = 0.5f;
        Check(Near(Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 5.0f }), exp5 * 0.5f, 1e-4f), "half brightness gives half the beam");
        Flashlight_Toggle(&f); Flashlight_Update(&f, DT, FWD);
        Check(Flashlight_BeamAt(&f, eye, (Vector3){ 0, 1.6f, 5.0f }) == 0.0f && Flashlight_GlowAt(&f, eye, (Vector3){ 0, 1.6f, 2.0f }) == 0.0f, "switched off, neither the beam nor the glow gives light");
        // The glow fallback.
        Flashlight_Toggle(&f); Run(&f, 0.1f, FWD);
        const float g1 = Flashlight_GlowAt(&f, eye, (Vector3){ 0, 1.6f, 1.0f }), g3 = Flashlight_GlowAt(&f, eye, (Vector3){ 0, 1.6f, 3.0f });
        Check(g1 > g3 && g3 > 0.0f && Flashlight_GlowAt(&f, eye, (Vector3){ 0, 1.6f, 6.5f }) == 0.0f && g1 <= 0.6f + 1e-4f, "the glow fades out by 6 m and never exceeds 0.6");
        Check(Flashlight_GlowAt(&f, eye, (Vector3){ 0, 1.6f, -3.0f }) > 0.0f && Flashlight_GlowAt(&f, eye, (Vector3){ 0, 1.6f, -3.0f }) < g3, "behind the player there is a dimmer glow, not darkness");
    }

    printf(g_failed ? "\n%d FAILED\n" : "\nall passed\n", g_failed);
    return g_failed ? 1 : 0;
}

#endif
