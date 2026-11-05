// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "stair_collision.h and stair_collision.c"
// ============================================================================
#ifndef STAIR_COLLISION_H
#define STAIR_COLLISION_H

#include <stdbool.h>

#include "raylib.h"

// The flight of stairs as solid geometry, with no drawing in it: boxes the player walks on, a ray and a sphere test so shots
// stop at the stairs and spiders crawl over them, and the route a walking monster follows up and down. Coordinates are
// the floor's own (y = 0 is this floor), the same on every floor.
//
// Staircase_GroundHeight and Staircase_Blocked (the player's queries) are declared in staircase.h and defined here.

typedef struct { float x0, x1, z0, z1, y0, y1; unsigned char kind; } StairBox;   // kind 0 = solid step or landing, 1 = railing
#define STAIR_BOX_SOLID 0
#define STAIR_BOX_RAIL  1

// The i-th box, or NULL past the last one (for drawing).
const StairBox *Stair_Box(int i);

// Nearest hit of a ray on the solid flight (railings are bars, shots pass between them). dir must be a unit vector. A ray
// that starts inside a box ignores that box, like a ray starting inside a wall tile. On a hit *t is the distance and
// *normal the unit normal of the face, pointing out.
bool Stair_Raycast(Vector3 origin, Vector3 dir, float maxDist, float *t, Vector3 *normal);

// Pushes a sphere out of the solid flight. Returns true if it moved.
bool Stair_PushOutSphere(Vector3 *center, float radius);

// Distance from a point to the nearest face of the solid flight (0 inside). Large (1e9) when the flight is far.
float Stair_SurfaceDistance(Vector3 p);

// True when the point is on the flight itself: inside the flight's footprint and above the floor.
bool Stair_OnFlight(float x, float z, float y);

// Where a walking monster should head to reach a target, when the stairs are involved: a waypoint on the way to the foot of
// the flight, along the switchback, or back down. False when they are not (neither stands on the flight), and the monster
// should simply walk at the target.
bool Stair_ChaseWaypoint(Vector3 from, Vector3 to, float *waypointX, float *waypointZ);

#endif
