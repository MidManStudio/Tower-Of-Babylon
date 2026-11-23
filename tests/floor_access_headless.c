// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "floor_access_headless.c"
// ============================================================================
// The accessor layer (src/dungeon/floor_access.h) against an oracle written in this file straight from the storage, on 40 generated
// floors and on tiles outside the floor; and a guard that keeps the files that were moved onto the accessors from reading the grid,
// TILE_WALL or WALL_HEIGHT directly again. The guard reads the source files, so run this from the project root. The detector is
// also tried on lines it must catch and lines it must not, so it cannot go quiet unnoticed. Inside a macro so mray's build, which
// compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DFLOOR_ACCESS_HEADLESS_TEST -Isrc -Iraylib/src tests/floor_access_headless.c src/dungeon/dungeon_raycast.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o floor_access_headless
// Run:
//   ./floor_access_headless
#ifdef FLOOR_ACCESS_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dungeon/dungeon_generator.h"
#include "dungeon/dungeon_raycast.h"
#include "dungeon/floor_access.h"

static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static FloorMap g_floor;

// ---- the oracle: the old direct reads, written out here -------------------------------------------------------------------------
static int OracleSolid(const FloorMap *f, int x, int z) { return x < 0 || x >= MAP_WIDTH || z < 0 || z >= MAP_HEIGHT || f->grid[z][x].type == TILE_WALL; }
static int OracleCeiling(int x, int z)
{
    if (x < 0 || x >= MAP_WIDTH || z < 0 || z >= MAP_HEIGHT) return 0;
    return !(x >= STAIR_WALL_X && (z <= UP_ALCOVE_Z1 || z >= DOWN_ALCOVE_Z0));
}

// ---- the migration guard -------------------------------------------------------------------------------------------------------
// The files that were moved onto the accessors (docs/design/08, section 6.3). None of them may read these again.
static const char *const MOVED[] = {
    "src/dungeon/dungeon_renderer.c", "src/dungeon/dungeon_raycast.c", "src/dungeon/dungeon_probes.c",
    "src/enemies/spider_ai.c", "src/enemies/enemy_manager.c", "src/swarm/swarm.c", "src/weapons/pickups.c",
    "src/floor/floor_game.c", "src/player/player_controller.c", "src/lighting/lighting.c"
};
static const char *const FORBIDDEN[] = { "grid[", "->grid", ".grid", "Tile_IsSolid", "TILE_WALL", "WALL_HEIGHT" };

// 1 when the code part of the line (the part before "//") holds one of the forbidden tokens.
static int ScanLine(const char *line, const char **found)
{
    char code[1024];
    size_t n = 0;
    const char *comment = strstr(line, "//");
    for (const char *p = line; *p && p != comment && n + 1 < sizeof code; ++p) code[n++] = *p;
    code[n] = 0;
    for (size_t k = 0; k < sizeof FORBIDDEN / sizeof FORBIDDEN[0]; ++k)
        if (strstr(code, FORBIDDEN[k])) { *found = FORBIDDEN[k]; return 1; }
    return 0;
}

// Violations in a file, or -1 when it cannot be read.
static int ScanFile(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) { printf("      cannot read %s (run from the project root)\n", path); return -1; }
    char line[2048];
    const char *found = NULL;
    int bad = 0, lineNo = 0;
    while (fgets(line, sizeof line, fp)) {
        ++lineNo;
        if (ScanLine(line, &found)) { printf("      %s:%d reads %s directly\n", path, lineNo, found); ++bad; }
    }
    fclose(fp);
    return bad;
}

int main(void)
{
    printf("Size\n");
    Check(Floor_Width(&g_floor) == MAP_WIDTH && Floor_Depth(&g_floor) == MAP_HEIGHT, "the floor is MAP_WIDTH by MAP_HEIGHT tiles for now");
    Check(Floor_InBounds(&g_floor, 0, 0) && Floor_InBounds(&g_floor, MAP_WIDTH - 1, MAP_HEIGHT - 1), "the first and last tile are in the floor");
    Check(!Floor_InBounds(&g_floor, -1, 0) && !Floor_InBounds(&g_floor, 0, -1) && !Floor_InBounds(&g_floor, MAP_WIDTH, 0) && !Floor_InBounds(&g_floor, 0, MAP_HEIGHT), "one step outside on any side is not");

    printf("World to tile\n");
    {
        int ok = 1;
        for (int t = -3; t <= MAP_WIDTH + 3; ++t) {
            const float c = (float)t * TILE_SIZE;
            if (Floor_TileOfWorld(c) != t) ok = 0;
            if (Floor_TileOfWorld(c + TILE_SIZE * 0.5f - 0.01f) != t) ok = 0;
            if (Floor_TileOfWorld(c - TILE_SIZE * 0.5f + 0.01f) != t) ok = 0;
            if (Floor_TileOfWorld(c + TILE_SIZE * 0.5f + 0.01f) != t + 1) ok = 0;
            if (Floor_TileOfWorld(c - TILE_SIZE * 0.5f - 0.01f) != t - 1) ok = 0;
        }
        Check(ok, "a tile is centred on t * TILE_SIZE and reaches half a tile each way (negative coordinates included)");
    }

    printf("Tiles, on 40 floors and three tiles beyond every side\n");
    long tiles = 0, walls = 0, bad = 0, badClass = 0, badTop = 0, badType = 0, badVariant = 0, badFlags = 0, badReserved = 0, badOld = 0;
    long badCeil = 0, badCeilOld = 0, badCeilY = 0, badLevel = 0;
    for (int k = 0; k < 40; ++k) {
        GenerateFloorMap(&g_floor, 1 + k % 25, 0x9E3779B1u * (unsigned)(k + 1));
        const FloorMap *f = &g_floor;
        for (int z = -3; z < MAP_HEIGHT + 3; ++z)
            for (int x = -3; x < MAP_WIDTH + 3; ++x) {
                ++tiles;
                const int solid = OracleSolid(f, x, z);
                walls += solid;
                if ((int)Floor_Solid(f, 0, x, z) != solid) ++bad;
                if ((int)Tile_IsSolid(f, x, z) != solid) ++badOld;
                if (Floor_WallClass(f, 0, x, z) != (solid ? WALL_CLASS_FULL : WALL_CLASS_OPEN)) ++badClass;
                if (Floor_WallTopY(f, 0, x, z) != (solid ? WALL_HEIGHT : 0.0f)) ++badTop;
                const int in = x >= 0 && x < MAP_WIDTH && z >= 0 && z < MAP_HEIGHT;
                if (Floor_Type(f, 0, x, z) != (in ? f->grid[z][x].type : TILE_WALL)) ++badType;
                if (Floor_Variant(f, 0, x, z) != (in ? f->grid[z][x].variant : (uint8_t)WALL_SHELL)) ++badVariant;
                if (Floor_Flags(f, x, z) != (in ? f->grid[z][x].flags : (uint8_t)0)) ++badFlags;
                if ((int)Floor_Reserved(f, x, z) != (in && (f->grid[z][x].flags & CELL_RESERVED) != 0)) ++badReserved;
                const int ceil = OracleCeiling(x, z);
                if ((int)Floor_HasCeiling(f, x, z) != ceil) ++badCeil;
                if ((int)Dungeon_HasCeiling(x, z) != ceil) ++badCeilOld;
                // The ceiling over a point: the same answer at the middle of the tile, near each of its corners and at any height.
                const float cx = (float)x * TILE_SIZE, cz = (float)z * TILE_SIZE, e = TILE_SIZE * 0.5f - 0.01f;
                const float want = ceil ? WALL_HEIGHT : FLOOR_NO_CEILING;
                const float ys[4] = { -1.0f, 0.0f, 3.0f, 7.0f };
                for (int yi = 0; yi < 4; ++yi)
                    if (Floor_CeilY(f, cx, cz, ys[yi]) != want || Floor_CeilY(f, cx - e, cz - e, ys[yi]) != want || Floor_CeilY(f, cx + e, cz - e, ys[yi]) != want ||
                        Floor_CeilY(f, cx - e, cz + e, ys[yi]) != want || Floor_CeilY(f, cx + e, cz + e, ys[yi]) != want) ++badCeilY;
                // Until stacked levels, the level is not read.
                if (Floor_Solid(f, 0, x, z) != Floor_Solid(f, 1, x, z) || Floor_Type(f, 0, x, z) != Floor_Type(f, 2, x, z)) ++badLevel;
            }
    }
    Check(tiles == 40L * (MAP_WIDTH + 6) * (MAP_HEIGHT + 6) && walls > 0 && walls < tiles, "the sweep covers every tile of 40 floors and the ring around them, walls and open ground");
    Check(bad == 0, "Floor_Solid is the old answer on every tile, and a tile outside the floor is solid");
    Check(badOld == 0, "and Tile_IsSolid (kept for the generator and the tests) agrees with it");
    Check(badClass == 0, "the wall class is open or full, and full outside the floor");
    Check(badTop == 0, "the top of a wall is WALL_HEIGHT, and 0 where there is no wall");
    Check(badType == 0, "Floor_Type is the tile's type (a wall outside the floor)");
    Check(badVariant == 0, "Floor_Variant is the wall style (the shell outside the floor)");
    Check(badFlags == 0 && badReserved == 0, "Floor_Flags and Floor_Reserved are the cell flags (none outside the floor)");
    Check(badCeil == 0, "Floor_HasCeiling is the ceiling rule: none over the two stair alcoves, none outside the floor");
    Check(badCeilOld == 0, "and Dungeon_HasCeiling (kept for the tests) agrees with it on every tile");
    Check(badCeilY == 0, "Floor_CeilY is the roof height over a column with a ceiling and FLOOR_NO_CEILING over one without, at any height and anywhere in the tile");
    Check(badLevel == 0, "the level argument is not read until stacked levels");

    printf("The roof\n");
    Check(Floor_RoofY(&g_floor) == WALL_HEIGHT, "the roof plane is WALL_HEIGHT high");
    Check(Floor_RoofY(NULL) == WALL_HEIGHT, "and asking without a floor works (the swarm clamps under the roof with no floor at all)");
    Check(FLOOR_NO_CEILING > 100.0f * WALL_HEIGHT, "no ceiling is a height no sphere or ray reaches");

    printf("The migration guard\n");
    {
        const char *found = NULL;
        Check(ScanLine("    if (f->grid[tz][tx].type != TILE_WALL) continue;", &found), "the detector catches a grid read");
        Check(ScanLine("    return Tile_IsSolid(f, x, z);", &found), "the detector catches Tile_IsSolid");
        Check(ScanLine("    const float top = WALL_HEIGHT;", &found), "the detector catches WALL_HEIGHT");
        Check(ScanLine("    if (c->type == TILE_WALL) continue;", &found), "the detector catches TILE_WALL");
        Check(ScanLine("    const MapCell *c = map->grid + 3;", &found), "the detector catches an arrow to the grid");
        Check(!ScanLine("    return Floor_Solid(f, 0, x, z);   // used to be Tile_IsSolid(f, x, z) and WALL_HEIGHT", &found), "the detector leaves a comment alone");
        Check(!ScanLine("    const float top = Floor_WallTopY(f, 0, x, z);", &found), "the detector leaves the accessors alone");
        Check(!ScanLine("    if (!Tile_InBounds(ax, az)) continue;", &found), "the detector leaves Tile_InBounds alone (swarm's own flow array, listed for round R1)");
        int total = 0, unreadable = 0;
        for (size_t i = 0; i < sizeof MOVED / sizeof MOVED[0]; ++i) {
            const int n = ScanFile(MOVED[i]);
            if (n < 0) ++unreadable; else total += n;
        }
        Check(unreadable == 0, "every moved file can be read");
        Check(total == 0, "none of the moved files reads the grid, TILE_WALL, Tile_IsSolid or WALL_HEIGHT directly");
    }

    if (g_failures) { printf("\nFAILED (%d failure%s)\n", g_failures, g_failures == 1 ? "" : "s"); return 1; }
    printf("\nALL PASSED (0 failures)\n");
    return 0;
}

#endif
