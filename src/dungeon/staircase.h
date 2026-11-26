// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "staircase.h and staircase.c"
// ============================================================================
#ifndef STAIRCASE_H
#define STAIRCASE_H

#include <stdbool.h>

#include "raylib.h"
#include "dungeon_types.h"

// Largest ledge the player walks onto without jumping. One stair step is STEP_RISE.
#define STEP_RISE    0.25f
#define STEP_UP_MAX  0.30f

typedef enum {
    DOOR_UP = 0,    // top of the flight in the up alcove, leads to the next floor
    DOOR_DOWN,      // floor level in the down alcove, leads to the floor below
    DOOR_COUNT
} DoorId;

// ---- Physics ---------------------------------------------------------------
// Height of the highest standable surface under (x, z) that is no more than
// STEP_UP_MAX above y. Never below 0 (the floor).
// Tells the stair code how big the floor in use is. The shell stands 9 tiles in from the east edge and the down alcove 9 from the
// south edge, so on a floor bigger than 45 by 45 everything here moves by (size - 45) tiles. Call it whenever the size of the floor
// in play changes (FloorWindow_Init does); the default is 45 by 45, which is what every older test builds. One size at a time.
void Staircase_SetFrame(int floorW, int floorH);

float Staircase_GroundHeight(float x, float z, float y);

// True when a body of the given radius and height standing at (x, z, y) would be
// inside a stair block or railing that it cannot step onto.
bool Staircase_Blocked(float x, float z, float radius, float y, float height);

// ---- Doors -----------------------------------------------------------------
// True when the player is close enough to the door to use it.
bool Staircase_DoorInRange(DoorId id, Vector3 playerPos);

// The down door of floor 1 is locked. The up door is never locked.
bool Staircase_DoorLocked(DoorId id, const FloorMap *floor);

// The up door is shut until the stair clock opens it (src/floor): the game sets this each frame. Off by default, so a test or a
// floor with no clock has an up door that is never locked.
void Staircase_SetUpDoorLocked(bool locked);

// Where a door is in the floor (the middle of its opening on the wall), for the clock's wave to run toward.
void Staircase_DoorPosition(DoorId id, float *x, float *z);

// Where the player stands, and which way they face, after coming out of a door.
// Below: came up from the floor beneath, stands in front of this floor's down door.
// Above: came down from the floor above, stands on the top landing by the up door.
void Staircase_SpawnBelow(Vector3 *pos, float *yaw);
void Staircase_SpawnAbove(Vector3 *pos, float *yaw);

// ---- Drawing ---------------------------------------------------------------
// Draws the stair shaft above the up alcove, the flight, both doors and the
// railings. doorOpen[id] runs from 0 (closed) to 1 (open).
// Only what is within 34 m of viewer is drawn.
void DrawStairs(const FloorMap *floor, const float doorOpen[DOOR_COUNT], Vector3 viewer);

#endif
