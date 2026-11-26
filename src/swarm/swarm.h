// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/swarm.md, section "swarm.h and swarm.c"
// ============================================================================
#ifndef SWARM_H
#define SWARM_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "swarm_config.h"
#include "../dungeon/dungeon_types.h"

// The worm swarm: many worms, each a chain of round dots, crawling at the player as one pile. A dot has no body of its own, only a
// position (and where it was one step ago); the chain keeps neighbours a link apart, a grid keeps every dot out of its neighbours, the
// tile map keeps it out of the walls, and one goal moves every dot: toward the player, by the way a walker would go. Its health is a
// single box in the middle of the swarm. No raylib calls in swarm.c: the swarm is a plain struct (no module globals), stepped at the
// game's fixed step, so a server can run several. Drawing is swarm_draw.c.
#define SWARM_HASH 2048        // a power of two, at least twice SWARM_MAX_DOTS

typedef enum { SWARM_NONE = 0, SWARM_ASLEEP, SWARM_HUNTING, SWARM_DEAD } SwarmState;

typedef struct { Vector3 pos; float radius, height; bool alive; } SwarmTarget;    // the player: feet, body radius and height

typedef struct {
    SwarmConfig cfg;                       // the numbers it was spawned with
    SwarmState  state;
    int         worms, dotsPer, count;     // count = worms x dotsPer; dot i is dot (i % dotsPer) of worm (i / dotsPer), 0 the head
    Vector3     p[SWARM_MAX_DOTS];         // where each dot is
    Vector3     q[SWARM_MAX_DOTS];         // where it was a step ago (the motion)
    Vector3     r[SWARM_MAX_DOTS];         // where it was when the last step began (drawing blends from here: Swarm_BeginTick)
    float       wormSpeed[SWARM_MAX_WORMS], wormPhase[SWARM_MAX_WORMS];
    int         head[SWARM_HASH], next[SWARM_MAX_DOTS];      // the grid that finds a dot's neighbours
    int16_t     flow[MAP_MAX][MAP_MAX];                      // steps to the player's tile by open tiles, -1 for wall or unreachable (the floor's w by h corner)
    int         flowTx, flowTz, flowFloor;
    int         flowW, flowH;                                // the size of the floor the field was made for; 0 until the first field
    uint32_t    flowSeed;
    Vector3     core;                      // the middle of the swarm: the centre of its health box
    float       coreHalf;
    float       health, hurtT, deathT, biteT, time;
    uint32_t    rng;
    unsigned    hits, kills;
} Swarm;

// Empties the swarm (a new floor).
void Swarm_Clear(Swarm *s);

// A swarm with these numbers, asleep, lying in a heap round (x, z), the worms' layout drawn from seed. False for numbers that do not fit.
bool Swarm_SpawnAt(Swarm *s, const SwarmConfig *cfg, float x, float z, uint32_t seed);

// A swarm for this floor: in an open place (not an alcove, one tile of clearance all round) at least cfg->spawnMinDist from both
// arrivals, chosen from seed. When no place is far enough the farthest half is allowed. False when the floor has no open place.
bool Swarm_Spawn(Swarm *s, const SwarmConfig *cfg, const FloorMap *map, uint32_t seed);

// For drawing between fixed steps: remembers where every dot is. Call before each simulation step.
void Swarm_BeginTick(Swarm *s);

// One fixed step. map may be NULL (no walls, the floor and ceiling only). player may be NULL (nothing to hunt).
void Swarm_Step(Swarm *s, float dt, const FloorMap *map, const SwarmTarget *player);

// The swarm's blow on the player: dots touching them bite, after bite_windup seconds of contact and then every bite_interval. True when a
// bite lands this call, with its damage (capped) and where it came from.
bool Swarm_Bite(Swarm *s, float dt, const SwarmTarget *player, float *outDamage, Vector3 *outFrom);

// A shot: true (the shot is spent) when the sphere touches the health box. Damage above zero is taken from the box and counted;
// a hit wakes a sleeping swarm and the last of the health kills it.
bool Swarm_Hit(Swarm *s, Vector3 pos, float radius, float damage);

// A melee blow from eye along the floor-plane direction (fx, fz): hits when the box is within reach, inside the arc and level with the
// eye. Returns the number of hits (0 or 1).
int Swarm_MeleeHit(Swarm *s, Vector3 eye, float fx, float fz, float reach, float arcDeg, float damage);

bool     Swarm_Alive(const Swarm *s);            // asleep or hunting
bool     Swarm_Active(const Swarm *s);           // anything left to draw (the dead lie there a while)
unsigned Swarm_HitsLanded(const Swarm *s);       // running totals, like the enemies', for the hit marker
unsigned Swarm_KillsLanded(const Swarm *s);

// Drawn by swarm_draw.c: between BeginMode3D and EndMode3D. alpha blends from the last step's positions (0) to the newest (1).
void Swarm_Draw(const Swarm *s, Vector3 viewer, float alpha);

#endif
