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

// Checks the rules every floor must satisfy. On failure writes a short reason
// into why (when why is not NULL) and returns false.
bool ValidateFloorMap(const FloorMap *floor, char *why, size_t whySize);

#endif
