// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/gvex_session.md, section "gvex_world.h and gvex_world.c"
// ============================================================================
#ifndef GVEX_WORLD_H
#define GVEX_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "../dungeon/dungeon_types.h"

// The creatures of a session (docs/design/10-gvex-dossier.md): one Flecs world per game, run only by the server, which tells the clients where its
// creatures are in every snapshot. Slice S1 has one kind, the Ghoul (G.V.E.X.-X0-01), in the three mobility grades of the dossier; the rest come
// as the slices of docs/gvex_session.md. The world is an instance (no file-scope state), so a host that runs the server and its own client in one
// process can hold this and anything else the same way. Compiled only with TOWER_GVEX (platform.h).

#define GVEX_MAX_CREATURES 32       // the most one world holds (and one snapshot carries: NET_MAX_MONSTERS)
#define GVEX_MAX_TARGETS   4        // players the creatures may sense: NET_MAX_PLAYERS

typedef enum { GVEX_KIND_GHOUL = 0, GVEX_KIND_COUNT } GvexKind;                // the wire byte; the order of the dossier's registry will grow it
typedef enum { GVEX_STATE_IDLE = 0, GVEX_STATE_CHASE, GVEX_STATE_ATTACK, GVEX_STATE_COUNT } GvexState;
#define GVEX_GRADES 3               // the Ghoul's mobility grades: 0 slow, 1 medium-velocity, 2 high-velocity runner

// A player the creatures can sense this tick: where, and how far the noise they make carries (PlayerNoise, player.md).
typedef struct { bool present; float x, y, z, noise; } GvexTarget;

// What the server tells the clients about one creature.
typedef struct {
    uint16_t id;                    // the spawn index: the same on every machine, and the seed of its looks (enemy_plan.h)
    uint8_t  kind, variant, state;  // GvexKind, the grade, GvexState
    float    x, y, z, yaw;          // metres; yaw as the players' (forward is sin, cos)
    float    health, maxHealth;
} GvexRecord;

typedef struct GvexWorld GvexWorld;

const char *Gvex_Designation(uint8_t kind);      // "G.V.E.X.-X0-01", or "?" for a byte that is no kind
const char *Gvex_Name(uint8_t kind);             // "Ghouls"
float Gvex_GradeSpeedFactor(int grade);          // times the floor's chase speed (a placeholder to be tuned, gvex_world.c)

// The world of a floor: the creatures spawn where the single-player game would put its cylinder enemies (enemy_plan.h: the same floor seed gives
// the same places and looks). `map` must outlive the world. NULL when `map` is NULL or Flecs cannot start.
GvexWorld *GvexWorld_Create(const FloorMap *map, int tier);
void GvexWorld_Destroy(GvexWorld *w);

// One fixed step: every creature senses the targets and moves. dt is NET_TICK_SECS.
void GvexWorld_Step(GvexWorld *w, const GvexTarget *targets, int nTargets, float dt);

int  GvexWorld_Count(const GvexWorld *w);                            // creatures alive
int  GvexWorld_Records(GvexWorld *w, GvexRecord *out, int max);      // the living, in id order; returns how many were written
// Hurts creature `id`; true when that killed it (it is gone from then on). False for an unknown or dead id. (Slice S3 calls this.)
bool GvexWorld_Damage(GvexWorld *w, int id, float amount);

#endif
