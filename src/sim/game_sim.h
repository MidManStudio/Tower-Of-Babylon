// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/sim.md, section "game_sim.h and game_sim.c"
// ============================================================================
#ifndef GAME_SIM_H
#define GAME_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"
#include "../dungeon/dungeon_types.h"
#include "../dungeon/floor_window.h"
#include "../dungeon/staircase.h"
#include "../player/player_controller.h"
#include "../inventory/inventory.h"
#include "../weapons/weapons.h"
#include "../flashlight/flashlight.h"
#include "../swarm/swarm.h"
#include "../input/input_latch.h"
#include "sim_events.h"

// The rules of the game with no window: one player on the tower, with what that player carries, the monsters and the swarm of the
// floor, the shots, the pickups, the stair clock. It was SimTick and its state in main.c; main.c now owns one of these and calls
// GameSim_Step 30 times a second (docs/sim.md). It draws nothing, plays nothing and reads no clock: commands go in (SimCmd, plus
// where the player is looking, which the host sets on the player before the step), events come out (SimEvent).
//
// The monsters, shots, pickups, props and the stair clock still keep their state at file scope in their own modules, so there is
// ONE simulation per process until those become part of this struct (docs/sim.md, "Not verified" and "For multiplayer").

#define SIM_STEP_SECS (1.0f / 30.0f)
#define SIM_MAX_STEPS_PER_FRAME 5

#define DOOR_OPEN_SECS 0.7f          // a door slides open this long, then the screen fades out...
#define DOOR_FADE_SECS 0.4f          // ...this long, then the floor changes
#define DEATH_FADE_SECS 1.2f         // after dying the screen fades out over this long...
#define DEATH_TOTAL_SECS 2.6f        // ...and the player comes back at the start of the floor after this long

typedef enum { SEQ_NONE = 0, SEQ_OPENING, SEQ_FADING } DoorSeq;

typedef struct GameSim {
    FloorWindow window;              // three floors live here, in this struct
    FloorMap *map;                   // the floor the player is on (one of the three)
    PlayerController player;
    Inventory inv;
    bool invOpen;                    // the inventory screen is open: only walking is live (set by the host's UI)
    Arsenal arsenal;                 // the combat state of whatever is in hand, kept in step with the inventory
    Flashlight torch;
    bool torchLowWarned;
    Swarm swarm;                     // the worm swarm of this floor (swarm/swarm.h)
    unsigned swarmHitsBase, swarmKillsBase;     // its hit and kill totals carried across floors, so a new floor does not look like a hit
    bool floorCleared;               // the player came down into this floor: its stairs are open and the clock is not run
    Vector3 prevEye;                 // where the camera was at the start of the last step (the picture blends from here)
    float prevAim, prevLean;         // the aim-down-sights and lean blends at the start of the last step (the view blends from them too)
    uint32_t shotRng;                // pellet spread and speed
    float gunNoiseT, gunNoiseM;      // the last shot can be heard for this long, out to this many metres
    float blockToastT;               // keeps "AMMO FULL" from repeating at every step
    unsigned lastHitCount, lastKillCount;
    DoorSeq seq;                     // a door opening, then the screen fading, then the floor changing
    DoorId seqDoor;
    float doorOpen[DOOR_COUNT];      // 0..1, for drawing
    float fadeOut;                   // 0..1 while leaving through a door, for drawing
    float deathT;                    // seconds since the player died
    unsigned long steps;             // how many steps have run
    SimEventQueue events;
} GameSim;

// Sets everything to its start and builds the tower's floor window from the seed. Call before the module inits below.
void GameSim_Init(GameSim *s, uint32_t towerSeed);        // floors of TOWER_FLOOR_TILES by TOWER_FLOOR_TILES (45 unless changed)
void GameSim_InitSized(GameSim *s, uint32_t towerSeed, int tiles);   // floors of tiles by tiles, FLOOR_TILES_MIN to FLOOR_TILES_MAX
// The first floor and the player on it: monsters, swarm, pickups, the stair clock. Call after InitObstaclesSystem, InitProjectilesSystem,
// InitEnemiesSystem and GameConfig_Init (the same order the game always used). Posts SIMEV_FLOOR.
void GameSim_Begin(GameSim *s);

// One step of the rules, SIM_STEP_SECS long. cmd is what the player wanted since the last step.
void GameSim_Step(GameSim *s, SimCmd cmd);

// What a shot fired now would scatter by, in degrees (the held gun's spread, narrowed by aiming down the sights).
float GameSim_SpreadDeg(const GameSim *s);
// Whether the DIVE button exists: in the air after a sprinting jump, and not yet diving (it takes the SLIDE button's place).
bool GameSim_DiveButtonShown(const GameSim *s);

// What the host's UI can do between steps: they take effect at once, not at the next step.
void GameSim_DropSlot(GameSim *s, int slot);
void GameSim_UseSlot(GameSim *s, int slot);
void GameSim_EquipSlot(GameSim *s, int slot);

// The flashlight drains by time and its beam follows the view, so it runs every frame, not every step (docs/sim.md).
void GameSim_TorchFrame(GameSim *s, float dt);

// Which door, pickup or console is within reach now (-1 for none).
void GameSim_FindNear(const GameSim *s, int *nearDoor, int *nearPickup, bool *nearConsole);
bool GameSim_SlideButtonShown(const GameSim *s);      // a forward sprint, or a slide in progress
void GameSim_FlatForward(const GameSim *s, float *fx, float *fz);   // forward on the floor plane, from the camera

bool GameSim_PopEvent(GameSim *s, SimEvent *out);

#endif
