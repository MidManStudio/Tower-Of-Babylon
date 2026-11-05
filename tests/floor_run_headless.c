// Headless test of the stair clock: the ported timers and fixed step, the unlock console, the countdown, the collapse, the events,
// and the config file. No window, no raylib calls. Run from the project root:
//   cc -O2 -std=c99 -DFLOOR_RUN_HEADLESS_TEST -Isrc -Iraylib/src tests/floor_run_headless.c src/core/timers.c src/core/fixed_step.c src/floor/floor_run.c src/floor/floor_config.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c -lm -o floor_run_headless
#ifdef FLOOR_RUN_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/fixed_step.h"
#include "core/timers.h"
#include "floor/floor_config.h"
#include "floor/floor_run.h"

extern const MdixBaked g_bakedFloor;

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }
static int Near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

#define TICK (1.0f / 30.0f)

static int CountKind(FloorRun *r, RunEventKind k, float *firstValue, float *lastValue)
{
    RunEvent e; int n = 0;
    while (FloorRun_PopEvent(r, &e)) if (e.kind == k) { if (n == 0 && firstValue) *firstValue = e.value; if (lastValue) *lastValue = e.value; ++n; }
    return n;
}

static void Run(FloorRun *r, float secs, bool reachable, bool held) { for (float t = 0.0f; t < secs - 0.0001f; t += TICK) FloorRun_Step(r, TICK, reachable, held); }

int main(void)
{
    printf("CountdownTimer (ported from Unity)\n");
    {
        CountdownTimer t; Countdown_Init(&t, 1.0f);
        Check(!t.running && !Countdown_Tick(&t, 5.0f), "a timer that was not started does not run");
        Countdown_Start(&t);
        Check(!Countdown_Tick(&t, 0.4f) && !Countdown_Tick(&t, 0.4f) && Near(Countdown_Progress(&t), 0.2f, 0.001f), "it counts down: 0.4 s twice leaves a fifth");
        Check(Countdown_Tick(&t, 0.4f) && !t.running && t.time == 0.0f, "the tick that reaches zero says so, stops it and holds it at zero");
        Check(!Countdown_Tick(&t, 0.4f), "and says so only once");
        Countdown_Start(&t);
        Check(Near(t.time, 1.0f, 0.0001f) && t.running, "Start runs it again from the full time");
        Countdown_Tick(&t, 0.5f); Countdown_Start(&t);
        Check(Near(t.time, 1.0f, 0.0001f), "and restarts it when it is already running");
        Countdown_Stop(&t);
        Check(!Countdown_Tick(&t, 5.0f) && Near(t.time, 1.0f, 0.0001f), "Stop freezes it");
        Countdown_Init(&t, 0.0f); Countdown_Start(&t);
        Check(Countdown_Tick(&t, 0.01f) && Countdown_Progress(&t) == 0.0f, "a zero-length timer finishes on its first tick (and its progress is 0, not a division by zero)");
    }

    printf("Tween (ValueInterpolationTimer, one way)\n");
    {
        Tween t; Tween_Start(&t, 10.0f, 20.0f, 4.0f, TWEEN_LINEAR);
        Tween_Tick(&t, 2.0f);
        Check(Near(Tween_Value(&t), 15.0f, 0.001f), "linear: halfway in time is halfway in value");
        Tween_Start(&t, 0.0f, 1.0f, 2.0f, TWEEN_EASE_IN); Tween_Tick(&t, 1.0f);
        Check(Near(Tween_Value(&t), 0.25f, 0.001f), "ease in: slow at first (a quarter at half time)");
        Tween_Start(&t, 0.0f, 1.0f, 2.0f, TWEEN_EASE_OUT); Tween_Tick(&t, 1.0f);
        Check(Near(Tween_Value(&t), 0.75f, 0.001f), "ease out: fast at first (three quarters at half time)");
        Tween_Start(&t, 0.0f, 1.0f, 2.0f, TWEEN_EASE_IN_OUT); Tween_Tick(&t, 1.0f);
        const float mid = Tween_Value(&t);
        Tween_Start(&t, 0.0f, 1.0f, 2.0f, TWEEN_EASE_IN_OUT); Tween_Tick(&t, 0.5f);
        Check(Near(mid, 0.5f, 0.001f) && Tween_Value(&t) < 0.25f, "ease in and out: the middle is the middle and the start is slow");
        Tween_Start(&t, 5.0f, 0.0f, 1.0f, TWEEN_LINEAR);
        int done = 0; float last = 5.0f; int monotone = 1;
        for (int i = 0; i < 40; ++i) { done += Tween_Tick(&t, 0.05f); const float v = Tween_Value(&t); if (v > last + 0.0001f) monotone = 0; last = v; }
        Check(done == 1 && last == 0.0f && !t.running && monotone, "running down: finishes exactly once, lands exactly on the end value, never goes back up, and stays");
    }

    printf("FixedStep (from FixedStepLoop)\n");
    {
        FixedStep f; FixedStep_Init(&f, 1.0f / 30.0f, 5);
        Check(FixedStep_Advance(&f, 0.1f) == 3, "0.1 s at 30 steps a second is 3 steps");
        FixedStep g; FixedStep_Init(&g, 1.0f / 30.0f, 5);
        int n = FixedStep_Advance(&g, 0.01f) + FixedStep_Advance(&g, 0.01f) + FixedStep_Advance(&g, 0.01f);
        Check(n == 0, "three short frames that add up to less than a step make none");
        Check(FixedStep_Advance(&g, 0.01f) == 1, "and the fourth, which takes them past it, makes one: no time is lost");
        FixedStep h; FixedStep_Init(&h, 1.0f / 30.0f, 5);
        const int big = FixedStep_Advance(&h, 1.0f);
        Check(big == 5 && h.dropped >= 24 && h.dropped <= 25, "a one second stall runs 5 steps, not 30, and counts the 24 or 25 it gave up (float rounding decides which)");
        Check(FixedStep_Advance(&h, 0.0f) == 0 && FixedStep_Alpha(&h) < 1.0f && FixedStep_Alpha(&h) >= 0.0f, "afterwards it is not still trying to catch up, and the blend factor is between 0 and 1");
        FixedStep a, b; FixedStep_Init(&a, 1.0f / 30.0f, 8); FixedStep_Init(&b, 1.0f / 30.0f, 8);
        long sa = 0, sb = 0;
        for (int i = 0; i < 144; ++i) sa += FixedStep_Advance(&a, 1.0f / 144.0f);
        for (int i = 0; i < 30; ++i) sb += FixedStep_Advance(&b, 1.0f / 30.0f);
        Check(labs(sa - 30) <= 1 && labs(sb - 30) <= 1, "one second makes 30 steps (give or take one) whether the screen runs at 144 or at 30 frames a second");
        FixedStep neg; FixedStep_Init(&neg, 0.0f, 0);
        Check(FixedStep_Advance(&neg, -1.0f) == 0 && neg.step > 0.0f && neg.maxSteps >= 1, "a negative frame time and a zero step are survived");
    }

    printf("The unlock console\n");
    FloorRunConfig cfg; FloorRun_DefaultConfig(&cfg);
    {
        FloorRun r; FloorRun_Begin(&r, &cfg, 30.0f, 30.0f, 73.0f, 13.5f, 90.0f);
        Run(&r, 5.0f, true, false);
        Check(r.phase == RUN_EXPLORING && r.progress == 0.0f && r.evCount == 0, "standing at the console without USE does nothing");
        Run(&r, 5.0f, false, true);
        Check(r.progress == 0.0f && r.evCount == 0, "USE held out of reach does nothing either");
        Run(&r, 9.8f, true, true);
        Check(r.phase == RUN_EXPLORING && r.progress > 0.95f && r.progress < 1.0f, "held for 9.8 of the 10 seconds: still working on it");
        Run(&r, 0.4f, true, true);
        Check(r.phase == RUN_COUNTDOWN, "held for 10: unlocked, the countdown has begun");
        float a = 0, b = 0;
        RunEvent e; int sawStart = 0, sawUnlocked = 0, monsters = 0, order = 1; RunEventKind prev = RUN_EV_NONE;
        while (FloorRun_PopEvent(&r, &e)) {
            if (e.kind == RUN_EV_CONSOLE_START) ++sawStart;
            if (e.kind == RUN_EV_UNLOCKED) { ++sawUnlocked; if (!Near(e.value, 30.0f, 0.001f)) order = 0; }
            if (e.kind == RUN_EV_MONSTER) ++monsters;
            if (prev == RUN_EV_NONE && e.kind != RUN_EV_CONSOLE_START) order = 0;
            prev = e.kind;
        }
        (void)a; (void)b;
        Check(sawStart == 1 && sawUnlocked == 1 && monsters == 3 && order && prev == RUN_EV_UNLOCKED, "events: one start first, the console's three monsters, then one unlocked (carrying the countdown's 30 s) last");
        Check(!FloorRun_DoorOpen(&r) && FloorRun_CountdownLeft(&r) > 29.0f && FloorRun_CountdownLeft(&r) <= 30.0f, "the door is shut and the countdown shows its 30 seconds");
    }
    {
        FloorRun r; FloorRun_Begin(&r, &cfg, 30.0f, 30.0f, 73.0f, 13.5f, 90.0f);
        Run(&r, 5.0f, true, true);
        Run(&r, 2.0f, true, false);
        Check(Near(r.progress, 0.5f - 0.15f * 2.0f, 0.02f), "let go after 5 s of the 10 and waiting 2: half, less the drain (0.15 a second) is left");
        RunEvent e; int aborts = 0, starts = 0;
        while (FloorRun_PopEvent(&r, &e)) { if (e.kind == RUN_EV_CONSOLE_ABORT) ++aborts; if (e.kind == RUN_EV_CONSOLE_START) ++starts; }
        Check(aborts == 1 && starts == 1, "letting go says so, once");
        Run(&r, 0.5f, true, true);
        starts = 0; while (FloorRun_PopEvent(&r, &e)) if (e.kind == RUN_EV_CONSOLE_START) ++starts;
        Check(starts == 0 && r.progress > 0.2f, "taking it up again where it was is not a new start, and carries on from what was left");
        Run(&r, 10.0f, true, false);
        Check(r.progress == 0.0f, "left alone it drains to nothing and stops there");
        Run(&r, 0.2f, true, true);
        starts = 0; while (FloorRun_PopEvent(&r, &e)) if (e.kind == RUN_EV_CONSOLE_START) ++starts;
        Check(starts == 1, "and starting again from nothing is a new start");
        FloorRun r2; FloorRunConfig keep = cfg; keep.drainPerSec = 0.0f;
        FloorRun_Begin(&r2, &keep, 30.0f, 30.0f, 73.0f, 13.5f, 90.0f);
        Run(&r2, 4.0f, true, true); const float before = r2.progress; Run(&r2, 20.0f, true, false);
        Check(Near(r2.progress, before, 0.0001f), "with a drain of 0 the progress is kept");
    }
    {
        FloorRun r; FloorRun_Begin(&r, &cfg, 30.0f, 30.0f, 73.0f, 13.5f, 90.0f);
        Run(&r, 0.1f, true, true);
        Run(&r, 6.0f, false, false);                                      // walked away straight after starting
        int monsters = 0; RunEvent e; float times = 0; (void)times;
        while (FloorRun_PopEvent(&r, &e)) if (e.kind == RUN_EV_MONSTER) ++monsters;
        Check(monsters == 3, "the console's monsters come whether or not the player stays");
        Run(&r, 0.1f, true, true); Run(&r, 6.0f, false, false);
        monsters = 0; while (FloorRun_PopEvent(&r, &e)) if (e.kind == RUN_EV_MONSTER) ++monsters;
        Check(monsters == 0, "and only once per floor: working it again calls no more");
        FloorRun q; FloorRun_Begin(&q, &cfg, 30, 30, 73, 13.5f, 90);
        Run(&q, 0.1f, true, true);
        int n = 0; float tFirst = -1, tSecond = -1;
        for (float t = 0.0f; t < 6.0f; t += TICK) { FloorRun_Step(&q, TICK, false, false); while (FloorRun_PopEvent(&q, &e)) if (e.kind == RUN_EV_MONSTER) { if (n == 0) tFirst = t; if (n == 1) tSecond = t; ++n; } }
        Check(n >= 2 && Near(tSecond - tFirst, 1.5f, 0.1f), "they are 1.5 s apart (monster_gap_secs)");
    }

    printf("The countdown and the collapse\n");
    {
        FloorRun r; FloorRun_Begin(&r, &cfg, 30.0f, 30.0f, 73.0f, 13.5f, 90.0f);
        Run(&r, 10.1f, true, true);
        RunEvent e; while (FloorRun_PopEvent(&r, &e)) {}
        float t = 0.0f; int counts[11] = { 0 }; int orderOk = 1, lastCount = 99, doorOpenAt = -1; float doorTime = 0.0f;
        int framesBeforeOpen = 0;
        while (r.phase == RUN_COUNTDOWN && t < 40.0f) {
            FloorRun_Step(&r, TICK, false, false); t += TICK;
            if (r.phase == RUN_COUNTDOWN) ++framesBeforeOpen;
            while (FloorRun_PopEvent(&r, &e)) {
                if (e.kind == RUN_EV_COUNT) { const int v = (int)e.value; if (v >= 1 && v <= 10) ++counts[v]; if (v >= lastCount) orderOk = 0; lastCount = v; }
                if (e.kind == RUN_EV_DOOR_OPEN) { doorOpenAt = 1; doorTime = t; }
            }
            if (t < 29.5f && FloorRun_DoorOpen(&r)) orderOk = 0;
        }
        int each = 1; for (int v = 1; v <= 10; ++v) if (counts[v] != 1) each = 0;
        Check(doorOpenAt == 1 && Near(doorTime, 30.0f, 0.1f), "the door opens 30 seconds after unlocking");
        Check(each && orderOk, "the last ten seconds are announced once each, 10 down to 1, and the door stays shut until the end");
        Check(r.phase == RUN_COLLAPSING && FloorRun_DoorOpen(&r) && FloorRun_CountdownLeft(&r) == 0.0f, "then the floor is collapsing and the door is open");
        Check(!FloorRun_Gone(&r, 20.0f, 70.0f) && !FloorRun_Gone(&r, 73.0f, 13.5f) && !FloorRun_Gone(&r, 73.0f, 13.5f + 89.0f), "at the first moment none of the floor within the radius has gone, not even 89 m from the door");
        float prev = r.waveRadius; int mono = 1, steps = 0;
        const float R0 = r.waveRadius;
        for (float s = 0.0f; s < 20.0f; s += TICK) { FloorRun_Step(&r, TICK, false, false); if (r.waveRadius > prev + 0.0001f) mono = 0; prev = r.waveRadius; ++steps; }
        Check(mono && Near(r.waveRadius, R0 / 2.0f, R0 * 0.03f), "the front moves only inward, and is halfway in half the collapse time (20 of 40 s)");
        const float dMid = hypotf(60.0f - 73.0f, 40.0f - 13.5f);
        Check(FloorRun_Gone(&r, 73.0f + 0.7f * R0, 13.5f) && !FloorRun_Gone(&r, 73.0f + 0.3f * R0, 13.5f) && dMid > 0.0f, "halfway: the floor 70% of the way out is gone, 30% of the way out still stands");
        for (float s = 0.0f; s < 21.0f; s += TICK) FloorRun_Step(&r, TICK, false, false);
        Check(r.waveRadius == 0.0f && FloorRun_Gone(&r, 73.5f, 13.5f) && FloorRun_Gone(&r, 80.0f, 13.5f), "after the 40 s it has all gone, right up to the door");
        (void)steps; (void)framesBeforeOpen;
    }
    {
        FloorRun r; FloorRun_Begin(&r, &cfg, 30.0f, 30.0f, 73.0f, 13.5f, 90.0f);
        Check(!FloorRun_Gone(&r, 0.0f, 90.0f), "before the unlock, nothing is gone");
        Run(&r, 10.4f, true, true); Run(&r, 10.0f, false, false);
        Check(r.phase == RUN_COUNTDOWN && !FloorRun_Gone(&r, 0.0f, 90.0f), "during the countdown nothing is gone either");
        FloorRunConfig ease = cfg; ease.collapseEase = TWEEN_EASE_IN;
        FloorRun e1; FloorRun_Begin(&e1, &ease, 30, 30, 73, 13.5f, 90);
        Run(&e1, 10.4f, true, true); Run(&e1, 30.5f, false, false); Run(&e1, 20.0f, false, false);
        Check(e1.waveRadius > 90.0f * 0.5f, "ease in (config collapse_ease = 1): at half the time the front is still past halfway, the collapse speeds up");
    }
    {
        FloorRun r; FloorRun_Begin(&r, &cfg, 30.0f, 30.0f, 73.0f, 13.5f, 90.0f);
        for (int i = 0; i < 20; ++i) { Run(&r, 0.1f, true, true); Run(&r, 0.1f, true, false); }      // 40 events: more than the queue holds
        RunEvent e; int n = 0; while (FloorRun_PopEvent(&r, &e)) ++n;
        Check(n == RUN_EVENT_CAP && r.evCount == 0, "the event queue holds 16: more than that drops the oldest and never overruns");
        Check(!FloorRun_PopEvent(&r, &e), "an empty queue says so");
    }

    printf("assets/config/floor.mdix\n");
    {
        char err[200];
        MdixDoc *doc = MdixDoc_FromBaked(&g_bakedFloor);
        const int ok = FloorConfig_LoadFrom(doc, err, sizeof err);
        const FloorRunConfig *c = FloorConfig_Get();
        Check(ok, "the shipped file loads");
        Check(c->countdownSecs == 30.0f, "the stair countdown is 30 seconds");
        Check(c->holdSecs == 10.0f && c->monstersOnStart == 3 && c->collapseSecs == 40.0f && c->rageSpeed > 1.0f, "and the other numbers are what the file says");
        FloorRunConfig def; FloorRun_DefaultConfig(&def);
        Check(memcmp(&def, c, sizeof def) == 0, "the built-in defaults are the same as the file (so a failed load changes nothing the player can feel)");
        MdixDoc_Free(doc);

        // A handmade table: every key, then the same with one value broken.
        static MdixBakedEntry good[] = {
            { "stair_clock.collapse_ease", 'i', "0" }, { "stair_clock.collapse_secs", 'f', "40" }, { "stair_clock.console_drain_per_sec", 'f', "0.15" },
            { "stair_clock.console_hold_secs", 'f', "10" }, { "stair_clock.console_reach", 'f', "2.4" }, { "stair_clock.countdown_secs", 'f', "45" },
            { "stair_clock.monster_gap_secs", 'f', "1.5" }, { "stair_clock.monsters_on_start", 'i', "3" }, { "stair_clock.rage_speed", 'f', "1.35" },
        };
        MdixBaked tab = { "test.mdix", 0u, good, 9 };
        doc = MdixDoc_FromBaked(&tab);
        Check(FloorConfig_LoadFrom(doc, err, sizeof err) && FloorConfig_Get()->countdownSecs == 45.0f, "a document with another countdown is taken: 45 s");
        MdixDoc_Free(doc);

        good[5].text = "1";                                                                   // countdown_secs below its minimum
        doc = MdixDoc_FromBaked(&tab);
        Check(!FloorConfig_LoadFrom(doc, err, sizeof err) && strstr(err, "countdown_secs") && FloorConfig_Get()->countdownSecs == 45.0f, "a countdown of 1 s is refused, the message names it, and the last good numbers stay");
        MdixDoc_Free(doc); good[5].text = "45";

        good[7].type = 'f'; good[7].text = "3.5";                                              // monsters_on_start as a float
        doc = MdixDoc_FromBaked(&tab);
        Check(!FloorConfig_LoadFrom(doc, err, sizeof err) && strstr(err, "monsters_on_start"), "a fractional number of monsters is refused");
        MdixDoc_Free(doc); good[7].type = 'i'; good[7].text = "3";

        MdixBaked missing = { "test.mdix", 0u, good, 8 };                                       // rage_speed is the last, and gone
        doc = MdixDoc_FromBaked(&missing);
        Check(!FloorConfig_LoadFrom(doc, err, sizeof err) && strstr(err, "rage_speed"), "a missing value is refused and named");
        MdixDoc_Free(doc);
        Check(!FloorConfig_LoadFrom(NULL, err, sizeof err), "no document is refused");

        FloorConfig_LoadFrom(MdixDoc_FromBaked(&g_bakedFloor), err, sizeof err);                // leave the shipped numbers in place
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
