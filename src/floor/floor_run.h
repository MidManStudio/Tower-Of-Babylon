// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "floor_run.h and floor_run.c"
// ============================================================================
#ifndef FLOOR_RUN_H
#define FLOOR_RUN_H

#include <stdbool.h>

#include "../core/timers.h"

// The stair clock of one floor (docs/design/01-game-loop.md): work the unlock console, a countdown starts, at zero the top door
// opens and the floor collapses in a wave from the far edge toward the stairs. Pure rules, no drawing and no raylib, stepped in
// fixed ticks, so a server can run the same code. Everything the game has to *do* about it (spawn a monster, play a sound, kill
// the player) comes out as events; main.c / floor_game.c does those.

typedef enum { RUN_EXPLORING = 0, RUN_COUNTDOWN, RUN_COLLAPSING } RunPhase;

typedef enum {
    RUN_EV_NONE = 0,
    RUN_EV_CONSOLE_START,        // the hold began from nothing
    RUN_EV_CONSOLE_ABORT,        // the hold was let go before it finished
    RUN_EV_MONSTER,              // the console calls one monster (value: how many are still to come)
    RUN_EV_UNLOCKED,             // the hold finished: the countdown starts
    RUN_EV_COUNT,                // a second of the last ten of the countdown (value: whole seconds left)
    RUN_EV_DOOR_OPEN,            // the countdown ended
    RUN_EV_COLLAPSE_BEGIN        // and the floor starts to go
} RunEventKind;

typedef struct { RunEventKind kind; float value; } RunEvent;
#define RUN_EVENT_CAP 16

// All of it comes from assets/config/floor.mdix (floor_config.h); these are the numbers used before it is read.
typedef struct {
    float holdSecs;              // USE held this long at the console unlocks the stairs
    float drainPerSec;           // when let go, the progress falls by this much of the whole per second (0 = it is kept)
    float reach;                 // metres from the console's middle within which USE works
    int   monstersOnStart;       // how many monsters the console calls once, from the first moment it is worked
    float monsterGapSecs;        // between one call and the next
    float countdownSecs;         // from unlocked to the door opening
    float collapseSecs;          // from the door opening to the last of the floor being gone
    int   collapseEase;          // TweenEase of the wave front
    float rageSpeed;             // monsters walk this many times faster once the door has opened
} FloorRunConfig;

typedef struct {
    FloorRunConfig cfg;
    RunPhase phase;
    float consoleX, consoleZ, doorX, doorZ;
    float maxRadius;             // from the door to the farthest bit of floor
    float progress;              // the console, 0..1
    bool  holding, called;       // called: the console has summoned its monsters (once per floor)
    int   monstersLeft; float monsterTimer;
    CountdownTimer countdown; int lastWhole;
    Tween wave; float waveRadius;
    RunEvent ev[RUN_EVENT_CAP]; int evHead, evCount;
} FloorRun;

void FloorRun_DefaultConfig(FloorRunConfig *out);

// Starts a floor: the console where it stands, the door the wave runs toward, how far the floor reaches from that door.
void FloorRun_Begin(FloorRun *r, const FloorRunConfig *cfg, float consoleX, float consoleZ, float doorX, float doorZ, float maxRadius);

// One fixed tick. reachable: the player is within reach of the console (and able to use it); useHeld: USE is down.
void FloorRun_Step(FloorRun *r, float dt, bool reachable, bool useHeld);

bool FloorRun_PopEvent(FloorRun *r, RunEvent *out);       // oldest first; false when there is none

bool  FloorRun_DoorOpen(const FloorRun *r);               // the countdown has ended
bool  FloorRun_Gone(const FloorRun *r, float x, float z); // the floor at this point has fallen away
float FloorRun_CountdownLeft(const FloorRun *r);          // seconds; the full countdown before it starts, 0 after it ends

#endif
