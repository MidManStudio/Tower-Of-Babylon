// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_types.h"
// ============================================================================
#ifndef DUNGEON_TYPES_H
#define DUNGEON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// A floor is 45 to 128 tiles on a side (docs/design/08, section 2). Every floor array is MAP_MAX by MAP_MAX and a floor uses the
// w by h corner of it that is its own size (FloorMap.w and .h; Floor_Width and Floor_Depth in floor_access.h). Anything that
// walks, bounds or strides over a floor reads the floor's size, never MAP_WIDTH.
#define MAP_MAX         128
#define FLOOR_TILES_MIN 45
#define FLOOR_TILES_MAX MAP_MAX

// The size of the floors a single-player game builds, in tiles on a side (GameSim_Init reads it), FLOOR_TILES_MIN to FLOOR_TILES_MAX.
// 45 is the solo floor; 64 is the size planned for two teams. Online play builds 45 by 45 floors whatever this says: src/net makes
// them with GenerateFloorMap until the size travels in the join message (docs/design/08, section 12).
#ifndef TOWER_FLOOR_TILES
#define TOWER_FLOOR_TILES 45
#endif

// The solo floor, 45 by 45: what GenerateFloorMap builds and the size most tests build by hand. Code that has to work on floors of
// any size must not use these two (tests/floor_access_headless.c keeps the moved source files off them).
#define MAP_WIDTH    45
#define MAP_HEIGHT   45
#define TILE_SIZE    2.0f
#define WALL_HEIGHT  6.0f
#define FLOOR_HEIGHT 8.0f   // vertical distance between two floors

// Narrowest corridor the generator may create, in tiles.
#define MIN_CORRIDOR_TILES 3

// ---- Fixed layout, identical on every floor of one size ---------------------
// The hall is everything inside the border ring. Two stair alcoves sit in the
// east corners. The generator is never allowed to change them. The numbers below
// are those of the 45 by 45 floor; a floor of w by h tiles has the same shell
// anchored to its east and south edges, which FloorShell_Make gives (below).
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

// The shell of a floor of w by h tiles. Its east strip starts 9 tiles in from the east edge, the up alcove is always the north 9
// rows and the down alcove the south 9, and every door, pad, arrival and approach point keeps its offset from the east and the
// north or south edge. For w = h = 45 every field equals the macro above (tests/floor_access_headless.c checks that).
typedef struct {
    int w, h;
    int hallX0, hallX1, hallZ0, hallZ1;
    int wallX;                                   // the west wall of both alcoves
    int upAlcoveZ0, upAlcoveZ1, downAlcoveZ0, downAlcoveZ1;
    int upDoorZ0, upDoorZ1, downDoorZ0, downDoorZ1;
    int upPadX, upPadZ, downPadX, downPadZ;
    int arriveBelowX, arriveBelowZ, arriveAboveX, arriveAboveZ;
    int upApproachX, upApproachZ, downApproachX, downApproachZ;
} FloorShell;

static inline FloorShell FloorShell_Make(int w, int h)
{
    FloorShell s;
    s.w = w; s.h = h;
    s.hallX0 = 1;           s.hallX1 = w - 2;     s.hallZ0 = 1;            s.hallZ1 = h - 2;
    s.wallX = w - 9;
    s.upAlcoveZ0 = 0;       s.upAlcoveZ1 = 8;     s.downAlcoveZ0 = h - 9;  s.downAlcoveZ1 = h - 1;
    s.upDoorZ0 = 3;         s.upDoorZ1 = 5;       s.downDoorZ0 = h - 6;    s.downDoorZ1 = h - 4;
    s.upPadX = w - 5;       s.upPadZ = 4;         s.downPadX = w - 5;      s.downPadZ = h - 5;
    s.arriveBelowX = w - 7; s.arriveBelowZ = h - 5; s.arriveAboveX = w - 7; s.arriveAboveZ = 4;
    s.upApproachX = w - 10; s.upApproachZ = 4;    s.downApproachX = w - 10; s.downApproachZ = h - 5;
    return s;
}

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

// The stages of one generation, for the timings in the report (FloorMap.stageMicros).
typedef enum {
    GEN_STAGE_SHELL = 0,    // the fixed shell and the bit grid
    GEN_STAGE_PROTECT,      // the protect mask
    GEN_STAGE_HALLS,        // choosing the pillar halls
    GEN_STAGE_PARTITIONS,   // the probe-driven partition pass
    GEN_STAGE_PILLARS,      // the pillar pass
    GEN_STAGE_POCKETS,      // connecting or filling the pockets
    GEN_STAGE_MEASURE,      // measuring and judging the layout
    GEN_STAGE_COUNT
} GenStage;

typedef struct {
    TileType type;
    bool     discovered;
    uint8_t  variant;
    uint8_t  flags;
} MapCell;

typedef struct {
    int      floorNumber;
    uint32_t seed;
    int      w, h;                          // this floor's size in tiles, FLOOR_TILES_MIN to FLOOR_TILES_MAX; set by the generator
    MapCell  grid[MAP_MAX][MAP_MAX];        // indexed [z][x]; only the w by h corner is the floor

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
    int   openTiles;                    // tiles that are not walls, on the whole grid. The layout targets compare this
    float walkableFraction;             // openTiles over the hall area, for display. The layout never reads it
    uint32_t stageMicros[GEN_STAGE_COUNT]; // microseconds spent in each generation stage, summed over the attempts
    uint32_t genMicros;                 // microseconds for the whole GenerateFloorMap call
} FloorMap;

// Bounds of the 45 by 45 solo floor only. For a floor of any size use Floor_InBounds (floor_access.h).
static inline bool Tile_InBounds(int x, int z)
{
    return x >= 0 && x < MAP_WIDTH && z >= 0 && z < MAP_HEIGHT;
}

static inline bool Tile_IsSolid(const FloorMap *f, int x, int z)
{
    return x < 0 || z < 0 || x >= f->w || z >= f->h || f->grid[z][x].type == TILE_WALL;
}

static inline float Tile_ToWorld(int t) { return (float)t * TILE_SIZE; }

#endif
