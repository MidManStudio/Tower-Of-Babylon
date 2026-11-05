// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "stair_layout.h"
// ============================================================================
#ifndef STAIR_LAYOUT_H
#define STAIR_LAYOUT_H

#include "dungeon_types.h"
#include "staircase.h"

// Where the up flight stands inside its alcove, in world units. Shared by the drawing (staircase.c) and the collision
// (stair_collision.c) so the two cannot drift apart. Nothing else should include this file.

// ---- Alcove interiors in world units ---------------------------------------
#define ALC_X0   ((float)STAIR_WALL_X * TILE_SIZE + TILE_SIZE * 0.5f)    // 73, inside face of the west wall
#define ALC_X1   ((float)(MAP_WIDTH - 1) * TILE_SIZE - TILE_SIZE * 0.5f) // 87, inside face of the east wall
#define UP_Z0    (TILE_SIZE * 0.5f)                                      // 1
#define UP_Z1    ((float)UP_ALCOVE_Z1 * TILE_SIZE - TILE_SIZE * 0.5f)    // 15

// ---- The flight ------------------------------------------------------------
// Two runs of 16 steps joined by a landing, 8 m of rise in all, in a switchback:
//   run A  climbs east  along z 6.5..9.5,  x 76..84, y 0 -> 4
//   landing                 x 84..87, z 6.5..15,     y 4
//   run B  climbs west  along z 12..15,    x 84..76, y 4 -> 8
//   top landing             x 73..76, z 12..15,      y 8, door in the west wall
// Every step is a solid block from the floor up, so the flight is one solid mass.
#define RUN_STEPS    16
#define STEP_TREAD   0.5f
#define RUN_X_LOW    76.0f
#define RUN_X_HIGH   (RUN_X_LOW + (float)RUN_STEPS * STEP_TREAD)   // 84
#define LANE_A_Z0    6.5f
#define LANE_A_Z1    9.5f
#define LANE_B_Z0    12.0f
#define LANE_B_Z1    15.0f
#define RUN_RISE     ((float)RUN_STEPS * STEP_RISE)                // 4
#define RAIL_HALF    0.08f
#define RAIL_HEIGHT  0.95f

#endif
