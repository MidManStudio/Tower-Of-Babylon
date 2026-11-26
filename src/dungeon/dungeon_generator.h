// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_generator.h"
// ============================================================================
#ifndef DUNGEON_GENERATOR_H
#define DUNGEON_GENERATOR_H

#include <stddef.h>

#include "dungeon_types.h"

// Seed for one floor, derived from the tower seed. Deterministic.
uint32_t DungeonFloorSeed(uint32_t towerSeed, int floorNum);

// Builds floor floorNum (1 or higher) into outFloor. The same tower seed and
// floor number always give the same floor.
void GenerateFloorMap(FloorMap *outFloor, int floorNum, uint32_t baseSeed);

// The same for a floor of w by h tiles, each clamped to FLOOR_TILES_MIN..FLOOR_TILES_MAX. GenerateFloorMap is this with 45 by 45
// (and every 45 by 45 floor is exactly the floor it was before sizes existed). The size is not mixed into the seed.
void GenerateFloorMapSized(FloorMap *outFloor, int floorNum, uint32_t baseSeed, int w, int h);

// Checks the rules every floor must satisfy. On failure writes a short reason
// into why (when why is not NULL) and returns false.
bool ValidateFloorMap(const FloorMap *floor, char *why, size_t whySize);

// Name of a generation stage (GEN_STAGE_*) for reports, or "?" when out of range.
const char *GenStageName(int stage);

#endif
