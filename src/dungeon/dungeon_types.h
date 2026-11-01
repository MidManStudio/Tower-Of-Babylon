// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_types.h"
// ============================================================================
#ifndef DUNGEON_TYPES_H
#define DUNGEON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define MAP_WIDTH    45
#define MAP_HEIGHT   45
#define TILE_SIZE    2.0f
#define WALL_HEIGHT  6.0f
#define FLOOR_HEIGHT 8.0f   // vertical distance between two floors

// Narrowest corridor the generator may create, in tiles.
#define MIN_CORRIDOR_TILES 3

// ---- Fixed layout, identical on every floor --------------------------------
// The hall is everything inside the border ring. Two stair alcoves sit in the
// east corners. The generator is never allowed to change them.
//   up alcove   : x 36..43, z 0..8   (door in the west wall at z 3..5)
//   down alcove : x 36..43, z 36..44 (door in the west wall at z 39..41)
#define HALL_X0 1
#define HALL_X1 43
#define HALL_Z0 1
#define HALL_Z1 43

#define STAIR_WALL_X   36
#define UP_ALCOVE_Z0   0
#define UP_ALCOVE_Z1   8
#define DOWN_ALCOVE_Z0 36
#define DOWN_ALCOVE_Z1 44
#define UP_DOOR_Z0     3
#define UP_DOOR_Z1     5
#define DOWN_DOOR_Z0   39
#define DOWN_DOOR_Z1   41

#define UP_PAD_X   40
#define UP_PAD_Z   4
#define DOWN_PAD_X 40
#define DOWN_PAD_Z 40

// Where the player appears. Arriving from below puts you in the down alcove,
// arriving from above puts you in the up alcove, so every floor has to be crossed.
#define ARRIVE_BELOW_X 38
#define ARRIVE_BELOW_Z 40
#define ARRIVE_ABOVE_X 38
#define ARRIVE_ABOVE_Z 4

// Open ground just outside each alcove door.
#define UP_APPROACH_X   35
#define UP_APPROACH_Z   4
#define DOWN_APPROACH_X 35
#define DOWN_APPROACH_Z 40

// Tiles within this distance of an approach point are kept free of walls and pillars.
#define OPEN_RADIUS_TILES 6

typedef enum {
    TILE_WALL = 0,
    TILE_FLOOR,
    TILE_CORRIDOR,
    TILE_STAIRS_UP,
    TILE_ELEVATOR_UP,
    TILE_HAZARD_ZONE,
    TILE_STAIRS_DOWN
} TileType;

// MapCell.variant for TILE_WALL
#define WALL_SHELL     0
#define WALL_PARTITION 1
#define WALL_PILLAR    2

// MapCell.flags
#define CELL_RESERVED 0x01

typedef struct {
    TileType type;
    bool     discovered;
    uint8_t  variant;
    uint8_t  flags;
} MapCell;

typedef struct {
    int      floorNumber;
    uint32_t seed;
    MapCell  grid[MAP_HEIGHT][MAP_WIDTH];  // indexed [z][x]

    float startX, startZ;               // arrival from below (world units)
    float stairsX, stairsZ;             // up pad
    float elevatorX, elevatorZ;         // unused, kept for existing code
    float downX, downZ;                 // down pad
    float arriveAboveX, arriveAboveZ;   // arrival from above

    // Generation report
    int   attempts;
    bool  usedFallback;
    int   partitions;
    int   pillars;
    int   pathLength;                   // steps from arrival-below to the up pad
    float walkableFraction;
} FloorMap;

static inline bool Tile_InBounds(int x, int z)
{
    return x >= 0 && x < MAP_WIDTH && z >= 0 && z < MAP_HEIGHT;
}

static inline bool Tile_IsSolid(const FloorMap *f, int x, int z)
{
    return !Tile_InBounds(x, z) || f->grid[z][x].type == TILE_WALL;
}

static inline float Tile_ToWorld(int t) { return (float)t * TILE_SIZE; }

#endif
