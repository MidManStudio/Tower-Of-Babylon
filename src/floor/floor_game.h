// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "floor_game.h and floor_game.c"
// ============================================================================
#ifndef FLOOR_GAME_H
#define FLOOR_GAME_H

#include <stdbool.h>

#include "raylib.h"
#include "../dungeon/dungeon_types.h"

// The stair clock in the game (floor_run.c has the rules): where the console stands, drawing it, the fixed-step ticking, and
// doing what the rules ask for (call a monster, play a sound, lock or open the top door, make the floor fall, kill whoever is
// still on it). The simulation (src/sim) calls these; it knows nothing else about the clock. The clock never plays a sound, shows text
// or touches the renderer: its sounds and messages go out through SimOut_Sound and SimOut_Toast (sim/sim_events.h), and the collapse
// wave is read back with FloorGame_CollapseWave.

typedef struct { float dt; const FloorMap *map; Vector3 player; bool alive; bool useHeld; int floorNumber; } FloorGameIn;
typedef struct { bool killPlayer; } FloorGameOut;

// Hands assets/config/floor.mdix to the config layer. Before GameConfig_Init, like Audio_Init.
void FloorGame_Register(void);

// A floor begins. cleared: the player came down from above, the stairs are already open and there is no clock on this floor.
void FloorGame_NewFloor(const FloorMap *map, int floorNumber, bool cleared);

// The player is within reach of the console and it can still be worked (it is not once the stairs are unlocked).
bool FloorGame_NearConsole(Vector3 player);

// Where the console stands. False when this floor has no clock.
bool FloorGame_ConsolePosition(float *x, float *z);

void FloorGame_Tick(const FloorGameIn *in, FloorGameOut *out);    // once per simulation step (30 a second): the game's own fixed step
void FloorGame_Frame(const FloorGameIn *in, FloorGameOut *out);   // or once per frame, when the caller has no steps: it keeps its own accumulator
void FloorGame_Draw(Vector3 viewer, float time);                  // in the 3D view
void FloorGame_CollapseWave(float *cx, float *cz, float *radius); // where the collapse has got to (radius < 0: no wave); the caller gives it to DrawFloorMapCollapse

// For the HUD.
int   FloorGame_Phase(void);              // 0 no clock on this floor, 1 find the console, 2 countdown, 3 collapsing
float FloorGame_ConsoleProgress(void);    // 0..1
bool  FloorGame_ConsoleHeld(void);
float FloorGame_CountdownLeft(void);      // seconds
float FloorGame_CollapseLeft01(void);     // 1 nothing gone .. 0 all gone

#endif
