// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "floor_window.c"
// ============================================================================
#include "floor_window.h"

#include <string.h>

#include "dungeon_generator.h"

static int SlotOf(int floorNumber)
{
    return ((floorNumber % ACTIVE_FLOOR_COUNT) + ACTIVE_FLOOR_COUNT) % ACTIVE_FLOOR_COUNT;
}

static void Refresh(FloorWindow *w)
{
    for (int f = w->currentFloor - 1; f <= w->currentFloor + 1; ++f) {
        const int slot = SlotOf(f);
        if (f < 1) {
            w->loaded[slot] = 0;
        } else if (w->loaded[slot] != f) {
            GenerateFloorMapSized(&w->floors[slot], f, w->towerSeed, w->tiles, w->tiles);
            w->loaded[slot] = f;
            ++w->generations;
        }
    }
}

void FloorWindow_Init(FloorWindow *w, uint32_t towerSeed, int startFloor)
{
    FloorWindow_InitSized(w, towerSeed, startFloor, MAP_WIDTH);
}

void FloorWindow_InitSized(FloorWindow *w, uint32_t towerSeed, int startFloor, int tiles)
{
    memset(w->loaded, 0, sizeof w->loaded);
    w->tiles = tiles < FLOOR_TILES_MIN ? FLOOR_TILES_MIN : (tiles > FLOOR_TILES_MAX ? FLOOR_TILES_MAX : tiles);
    w->towerSeed = towerSeed;
    w->currentFloor = startFloor < 1 ? 1 : startFloor;
    w->generations = 0;
    Refresh(w);
}

FloorMap *FloorWindow_Current(FloorWindow *w) { return FloorWindow_Get(w, w->currentFloor); }

FloorMap *FloorWindow_Get(FloorWindow *w, int floorNumber)
{
    if (floorNumber < 1 || floorNumber < w->currentFloor - 1 || floorNumber > w->currentFloor + 1) return NULL;
    const int slot = SlotOf(floorNumber);
    return w->loaded[slot] == floorNumber ? &w->floors[slot] : NULL;
}

bool FloorWindow_Advance(FloorWindow *w, int direction)
{
    const int next = w->currentFloor + (direction >= 0 ? 1 : -1);
    if (next < 1) return false;
    w->currentFloor = next;
    Refresh(w);
    return true;
}
