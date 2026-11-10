// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/sim.md, section "sim_events.h and sim_events.c"
// ============================================================================
#ifndef SIM_EVENTS_H
#define SIM_EVENTS_H

#include <stdbool.h>

// What the rules tell the outside world. The simulation (src/sim/game_sim.h) never plays a sound, never shows text and never
// touches the screen: when something happens that a player should see or hear, it posts an event here, and whoever runs the
// simulation (the game's main loop; a server throws them away, or sends the ones a client needs) takes them out afterwards.
typedef enum {
    SIMEV_TOAST = 0,      // text: a short message for the bottom of the screen
    SIMEV_SOUND,          // sound: which one; arg: its one number, if it has one
    SIMEV_HIT,            // a shot or a blow of the player landed (the hit marker)
    SIMEV_KILL,           // ...and killed (the kill marker)
    SIMEV_FLOOR,          // a floor was built and the player is on it (arg: the floor number): lights and the like are made for it
    SIMEV_FADE_IN,        // the player was put somewhere new: the screen fades in
    SIMEV_RESET_AUDIO,    // a new life: the sound system forgets its edges and timers
    SIMEV_HEAL_FLASH      // a medkit was used: the green flash
} SimEventKind;

typedef enum {
    SIMSND_HURT = 0, SIMSND_DOOR, SIMSND_SHOT /* arg: WeaponId */, SIMSND_DRYFIRE, SIMSND_MELEE /* arg: 1 when it hit something */,
    SIMSND_HITMARKER /* arg: 1 for a kill */, SIMSND_PICKUP, SIMSND_USEITEM, SIMSND_BATTERY, SIMSND_FLASHLIGHT /* arg: 1 on, 0 off */,
    SIMSND_CONSOLE_START, SIMSND_UNLOCKED, SIMSND_COUNT_TICK /* arg: seconds left */, SIMSND_COLLAPSE
} SimSound;

#define SIM_EVENT_TEXT 80          // longest text kept; a longer one is cut
#define SIM_EVENT_MAX  64          // a queue holds this many; when it is full the NEWEST are dropped and counted

typedef struct { unsigned char kind, sound; int arg; char text[SIM_EVENT_TEXT]; } SimEvent;
typedef struct { SimEvent ev[SIM_EVENT_MAX]; int head, count; unsigned dropped; } SimEventQueue;

void SimEvents_Clear(SimEventQueue *q);
void SimEvents_Push(SimEventQueue *q, SimEventKind kind, int arg);
void SimEvents_PushSound(SimEventQueue *q, SimSound sound, int arg);
void SimEvents_PushToast(SimEventQueue *q, const char *text);
bool SimEvents_Pop(SimEventQueue *q, SimEvent *out);       // oldest first; false when empty

// For code that runs inside a step but knows nothing of the simulation that owns it (the stair clock, floor/floor_game.c): it calls
// these, and they post to whichever queue is bound (the simulation binds its own at the start of every call and unbinds at the end).
// With nothing bound they do nothing, so a test or a tool can run that code with no queue at all. One queue is bound at a time
// (module-global, like the monsters and the shots: one simulation per process).
void SimOut_Bind(SimEventQueue *q);
void SimOut_Toast(const char *text);
void SimOut_Sound(SimSound sound, int arg);

#endif
