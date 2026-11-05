// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "floor_run.h and floor_run.c"
// ============================================================================
#include "floor_run.h"

#include <math.h>
#include <string.h>

void FloorRun_DefaultConfig(FloorRunConfig *c)
{
    c->holdSecs = 10.0f; c->drainPerSec = 0.15f; c->reach = 2.4f;
    c->monstersOnStart = 3; c->monsterGapSecs = 1.5f;
    c->countdownSecs = 30.0f; c->collapseSecs = 40.0f; c->collapseEase = TWEEN_LINEAR; c->rageSpeed = 1.35f;
}

static void Post(FloorRun *r, RunEventKind kind, float value)
{
    if (r->evCount == RUN_EVENT_CAP) { r->evHead = (r->evHead + 1) % RUN_EVENT_CAP; --r->evCount; }     // full: the oldest goes
    r->ev[(r->evHead + r->evCount) % RUN_EVENT_CAP] = (RunEvent){ kind, value };
    ++r->evCount;
}

bool FloorRun_PopEvent(FloorRun *r, RunEvent *out)
{
    if (r->evCount == 0) return false;
    *out = r->ev[r->evHead];
    r->evHead = (r->evHead + 1) % RUN_EVENT_CAP;
    --r->evCount;
    return true;
}

void FloorRun_Begin(FloorRun *r, const FloorRunConfig *cfg, float consoleX, float consoleZ, float doorX, float doorZ, float maxRadius)
{
    memset(r, 0, sizeof *r);
    if (cfg) r->cfg = *cfg; else FloorRun_DefaultConfig(&r->cfg);
    r->phase = RUN_EXPLORING;
    r->consoleX = consoleX; r->consoleZ = consoleZ; r->doorX = doorX; r->doorZ = doorZ;
    r->maxRadius = maxRadius;
    r->waveRadius = maxRadius;
    Countdown_Init(&r->countdown, r->cfg.countdownSecs);
}

void FloorRun_Step(FloorRun *r, float dt, bool reachable, bool useHeld)
{
    if (r->phase == RUN_EXPLORING) {
        if (reachable && useHeld) {
            if (!r->holding) {
                r->holding = true;
                if (r->progress <= 0.0f) Post(r, RUN_EV_CONSOLE_START, 0.0f);
            }
            if (!r->called) { r->called = true; r->monstersLeft = r->cfg.monstersOnStart; r->monsterTimer = 0.5f; }
            r->progress += dt / r->cfg.holdSecs;
            if (r->progress >= 1.0f) {
                r->progress = 1.0f;
                r->holding = false;
                r->phase = RUN_COUNTDOWN;
                Countdown_Start(&r->countdown);
                r->lastWhole = (int)ceilf(r->cfg.countdownSecs) + 1;
                Post(r, RUN_EV_UNLOCKED, r->cfg.countdownSecs);
            }
        } else {
            if (r->holding) { r->holding = false; Post(r, RUN_EV_CONSOLE_ABORT, r->progress); }
            r->progress -= r->cfg.drainPerSec * dt;
            if (r->progress < 0.0f) r->progress = 0.0f;
        }
        if (r->monstersLeft > 0) {                      // the console's calls carry on even if the player has walked away
            r->monsterTimer -= dt;
            if (r->monsterTimer <= 0.0f) { --r->monstersLeft; Post(r, RUN_EV_MONSTER, (float)r->monstersLeft); r->monsterTimer = r->cfg.monsterGapSecs; }
        }
    } else if (r->phase == RUN_COUNTDOWN) {
        const bool done = Countdown_Tick(&r->countdown, dt);
        const int whole = (int)ceilf(r->countdown.time);
        if (!done && whole <= 10 && whole < r->lastWhole) Post(r, RUN_EV_COUNT, (float)whole);
        r->lastWhole = whole;
        if (done) {
            r->phase = RUN_COLLAPSING;
            Post(r, RUN_EV_DOOR_OPEN, 0.0f);
            Post(r, RUN_EV_COLLAPSE_BEGIN, r->cfg.collapseSecs);
            Tween_Start(&r->wave, r->maxRadius, 0.0f, r->cfg.collapseSecs, r->cfg.collapseEase);
            r->waveRadius = r->maxRadius;
        }
    } else {
        Tween_Tick(&r->wave, dt);
        r->waveRadius = Tween_Value(&r->wave);
    }
}

bool FloorRun_DoorOpen(const FloorRun *r) { return r->phase == RUN_COLLAPSING; }

bool FloorRun_Gone(const FloorRun *r, float x, float z)
{
    if (r->phase != RUN_COLLAPSING) return false;
    return hypotf(x - r->doorX, z - r->doorZ) > r->waveRadius;
}

float FloorRun_CountdownLeft(const FloorRun *r)
{
    if (r->phase == RUN_EXPLORING) return r->cfg.countdownSecs;
    return r->phase == RUN_COUNTDOWN ? r->countdown.time : 0.0f;
}
