// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "floor_access.h"
// ============================================================================
#ifndef FLOOR_ACCESS_H
#define FLOOR_ACCESS_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "dungeon_types.h"

// The one way the rest of the game reads a floor (docs/design/08, section 6.3). Nothing outside the generator and this file
// touches FloorMap.grid[][], TILE_WALL or WALL_HEIGHT any more, so the storage can change behind these functions (a floor of its
// own size in round R1, wall classes in R2, stacked levels in R4) without moving a call site again.
//
// Until those rounds every answer is the one the old direct reads gave:
//  - there is one level, so `level` is accepted and not read (callers pass 0);
//  - a wall is a full wall, so the wall class is open (0) or full (3);
//  - there is one ceiling height, so the `y` of Floor_CeilY is accepted and not read;
//  - every tile outside the floor is a full wall.

// Wall classes (design/08 section 6.1). Only OPEN and FULL exist until round R2.
#define WALL_CLASS_OPEN 0
#define WALL_CLASS_LOW  1
#define WALL_CLASS_MID  2
#define WALL_CLASS_FULL 3

// What Floor_CeilY answers over a column with no ceiling (the two stair alcoves are open to the shaft above).
#define FLOOR_NO_CEILING 1.0e9f

// ---- size -----------------------------------------------------------------------------------------------------------------
static inline int Floor_Width(const FloorMap *f) { (void)f; return MAP_WIDTH; }    // tiles along x
static inline int Floor_Depth(const FloorMap *f) { (void)f; return MAP_HEIGHT; }   // tiles along z (not a height in metres)

static inline bool Floor_InBounds(const FloorMap *f, int x, int z)
{
    return x >= 0 && x < Floor_Width(f) && z >= 0 && z < Floor_Depth(f);
}

// The tile that world coordinate w (x or z) falls in. Tile t is centred on t * TILE_SIZE.
static inline int Floor_TileOfWorld(float w) { return (int)floorf((w + TILE_SIZE * 0.5f) / TILE_SIZE); }

// ---- walls ----------------------------------------------------------------------------------------------------------------
// Any wall class. A tile outside the floor is solid.
static inline bool Floor_Solid(const FloorMap *f, int level, int x, int z)
{
    (void)level;
    return !Floor_InBounds(f, x, z) || f->grid[z][x].type == TILE_WALL;
}

// WALL_CLASS_OPEN to WALL_CLASS_FULL.
static inline int Floor_WallClass(const FloorMap *f, int level, int x, int z)
{
    return Floor_Solid(f, level, x, z) ? WALL_CLASS_FULL : WALL_CLASS_OPEN;
}

// World height of the top of the wall on this tile, measured from the surface of the level; 0 where there is no wall.
static inline float Floor_WallTopY(const FloorMap *f, int level, int x, int z)
{
    return Floor_Solid(f, level, x, z) ? WALL_HEIGHT : 0.0f;
}

// ---- what a tile is ---------------------------------------------------------------------------------------------------------
// The TileType (TILE_WALL outside the floor).
static inline TileType Floor_Type(const FloorMap *f, int level, int x, int z)
{
    (void)level;
    return Floor_InBounds(f, x, z) ? f->grid[z][x].type : TILE_WALL;
}

// The wall style (WALL_SHELL, WALL_PARTITION, WALL_PILLAR) of a wall tile; WALL_SHELL outside the floor.
static inline uint8_t Floor_Variant(const FloorMap *f, int level, int x, int z)
{
    (void)level;
    return Floor_InBounds(f, x, z) ? f->grid[z][x].variant : (uint8_t)WALL_SHELL;
}

// The CELL_* flag bits of a tile (0 outside the floor).
static inline uint8_t Floor_Flags(const FloorMap *f, int x, int z)
{
    return Floor_InBounds(f, x, z) ? f->grid[z][x].flags : (uint8_t)0;
}

// The stair alcoves and what the generator must not touch (CELL_RESERVED).
static inline bool Floor_Reserved(const FloorMap *f, int x, int z) { return (Floor_Flags(f, x, z) & CELL_RESERVED) != 0; }

// ---- ceilings -------------------------------------------------------------------------------------------------------------
// Whether the column over tile (x, z) has a ceiling. The two stair alcoves (east of STAIR_WALL_X, north of UP_ALCOVE_Z1 or south
// of DOWN_ALCOVE_Z0) are open to the shaft above, and so is everything outside the floor.
static inline bool Floor_HasCeiling(const FloorMap *f, int x, int z)
{
    return Floor_InBounds(f, x, z) && !(x >= STAIR_WALL_X && (z <= UP_ALCOVE_Z1 || z >= DOWN_ALCOVE_Z0));
}

// The ceiling over world point (x, z): its height, or FLOOR_NO_CEILING where the column has none. `y` is where the caller is; it
// is not read until stacked levels (round R4) make "the lowest ceiling above y" a different thing from "the ceiling".
static inline float Floor_CeilY(const FloorMap *f, float x, float z, float y)
{
    (void)y;
    return Floor_HasCeiling(f, Floor_TileOfWorld(x), Floor_TileOfWorld(z)) ? WALL_HEIGHT : FLOOR_NO_CEILING;
}

// The height of the roof plane: the ceiling of every column that has one. For the callers that clamp or cast against the roof
// without asking whether the column under them has one (a ray that hits the roof, a swarm that stays under it). Round R4 replaces
// these calls with a walk over the decks of a column; until then the roof is one plane.
static inline float Floor_RoofY(const FloorMap *f) { (void)f; return WALL_HEIGHT; }

#endif
