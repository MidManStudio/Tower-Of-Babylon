// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "floor_window.h"
// ============================================================================
#ifndef FLOOR_WINDOW_H
#define FLOOR_WINDOW_H

#include "dungeon_types.h"

#define ACTIVE_FLOOR_COUNT 3

// Three floors live in memory: the one below, the current one and the one above.
// Floor n is always held in slot n % 3, so a move up or down reuses exactly the
// slot of the floor that fell out of range and regenerates only that one.
typedef struct {
    FloorMap floors[ACTIVE_FLOOR_COUNT];
    int      loaded[ACTIVE_FLOOR_COUNT];  // floor number held in each slot, 0 = empty
    int      currentFloor;
    uint32_t towerSeed;
    int      generations;                 // floors generated so far
    int      tiles;                       // every floor of the window is this many tiles on a side
} FloorWindow;

void      FloorWindow_Init(FloorWindow *w, uint32_t towerSeed, int startFloor);       // 45 by 45 floors

// The same with floors of tiles by tiles (FLOOR_TILES_MIN to FLOOR_TILES_MAX). The stair code has to be told the size of the floor in
// play as well (Staircase_SetFrame); GameSim_Init does both.
void      FloorWindow_InitSized(FloorWindow *w, uint32_t towerSeed, int startFloor, int tiles);
FloorMap *FloorWindow_Current(FloorWindow *w);

// NULL when floorNumber is outside the window or below floor 1.
FloorMap *FloorWindow_Get(FloorWindow *w, int floorNumber);

// direction +1 climbs, -1 descends. Returns false (and changes nothing) when it
// would go below floor 1.
bool FloorWindow_Advance(FloorWindow *w, int direction);

// Height of a floor relative to the current one, for drawing.
static inline float FloorWindow_RelativeY(const FloorWindow *w, int floorNumber)
{
    return (float)(floorNumber - w->currentFloor) * FLOOR_HEIGHT;
}

#endif
