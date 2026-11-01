// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_raycast.h"
// ============================================================================
#ifndef DUNGEON_RAYCAST_H
#define DUNGEON_RAYCAST_H

#include <stdbool.h>

#include "raylib.h"
#include "dungeon_types.h"

// 3D queries against the floor as solid geometry: the floor plane at y = 0, a
// ceiling plane at y = WALL_HEIGHT, and every solid tile as a box from the
// floor to the ceiling. Units are world units, tile (x, z) is centred on
// (x * TILE_SIZE, z * TILE_SIZE).

typedef struct {
    bool    hit;
    float   t;        // distance along the ray
    Vector3 point;
    Vector3 normal;   // unit, pointing out of the surface
} DungeonHit;

// The two stair alcoves are open to the shaft above, so they have no ceiling.
bool Dungeon_HasCeiling(int tileX, int tileZ);

// Nearest surface along the ray, up to maxDist. dir must be a unit vector. A ray
// that starts inside a solid tile reports no hit for that tile.
bool Dungeon_Raycast(const FloorMap *f, Vector3 origin, Vector3 dir, float maxDist, DungeonHit *hit);

// Pushes a sphere out of walls, pillars, floor and ceiling. Returns true if it moved.
bool Dungeon_PushOutSphere(const FloorMap *f, Vector3 *center, float radius);

// Distance from a point to the nearest surface. Used to check that a foot tip really rests on geometry.
float Dungeon_SurfaceDistance(const FloorMap *f, Vector3 p);

#endif
