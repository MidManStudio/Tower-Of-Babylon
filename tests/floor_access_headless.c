// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "floor_access_headless.c"
// ============================================================================
// The accessor layer (src/dungeon/floor_access.h) and the stair shell (FloorShell) against an oracle written in this file straight
// from the storage, on 40 generated floors of five sizes (45 by 45, 64 by 64, 100 by 70, 128 by 128, 45 by 128) and on tiles outside
// them; and a guard that keeps the files that were moved onto the accessors from reading the grid, TILE_WALL, WALL_HEIGHT or the
// 45 by 45 constants (MAP_WIDTH, STAIR_WALL_X, ...) directly again. The guard reads the source files, so run this from the project
// root. The detector is also tried on lines it must catch and lines it must not, so it cannot go quiet unnoticed. Inside a macro so
// mray's build, which compiles every .c file, does not pick up a second main().
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
static int OracleSolid(const FloorMap *f, int x, int z) { return x < 0 || x >= f->w || z < 0 || z >= f->h || f->grid[z][x].type == TILE_WALL; }
// No ceiling over the two stair alcoves (the east 9 columns, the north 9 rows and the south 9 rows) or outside the floor.
static int OracleCeiling(const FloorMap *f, int x, int z)
{
    if (x < 0 || x >= f->w || z < 0 || z >= f->h) return 0;
    return !(x >= f->w - 9 && (z <= 8 || z >= f->h - 9));
}

// ---- the migration guard -------------------------------------------------------------------------------------------------------
// The files that were moved onto the accessors (docs/design/08, section 6.3) and onto the floor's own size (round R1). None of them
// may read these again.
static const char *const MOVED[] = {
    "src/dungeon/dungeon_renderer.c", "src/dungeon/dungeon_raycast.c", "src/dungeon/dungeon_probes.c",
    "src/enemies/spider_ai.c", "src/enemies/enemy_manager.c", "src/swarm/swarm.c", "src/weapons/pickups.c",
    "src/floor/floor_game.c", "src/player/player_controller.c", "src/lighting/lighting.c"
};
static const char *const FORBIDDEN[] = {
    "grid[", "->grid", ".grid", "Tile_IsSolid", "Tile_InBounds", "TILE_WALL", "WALL_HEIGHT",
    "MAP_WIDTH", "MAP_HEIGHT", "STAIR_WALL_X", "ALCOVE_Z", "DOOR_Z", "_PAD_X", "_PAD_Z", "ARRIVE_BELOW_", "ARRIVE_ABOVE_", "APPROACH_X", "APPROACH_Z", "HALL_X", "HALL_Z"
};

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
    printf("The shell of the 45 by 45 floor is the old constants\n");
    {
        const FloorShell s = FloorShell_Make(MAP_WIDTH, MAP_HEIGHT);
        Check(s.hallX0 == HALL_X0 && s.hallX1 == HALL_X1 && s.hallZ0 == HALL_Z0 && s.hallZ1 == HALL_Z1, "the hall");
        Check(s.wallX == STAIR_WALL_X && s.upAlcoveZ0 == UP_ALCOVE_Z0 && s.upAlcoveZ1 == UP_ALCOVE_Z1 && s.downAlcoveZ0 == DOWN_ALCOVE_Z0 && s.downAlcoveZ1 == DOWN_ALCOVE_Z1, "the alcoves");
        Check(s.upDoorZ0 == UP_DOOR_Z0 && s.upDoorZ1 == UP_DOOR_Z1 && s.downDoorZ0 == DOWN_DOOR_Z0 && s.downDoorZ1 == DOWN_DOOR_Z1, "the doors");
        Check(s.upPadX == UP_PAD_X && s.upPadZ == UP_PAD_Z && s.downPadX == DOWN_PAD_X && s.downPadZ == DOWN_PAD_Z, "the pads");
        Check(s.arriveBelowX == ARRIVE_BELOW_X && s.arriveBelowZ == ARRIVE_BELOW_Z && s.arriveAboveX == ARRIVE_ABOVE_X && s.arriveAboveZ == ARRIVE_ABOVE_Z, "the arrival points");
        Check(s.upApproachX == UP_APPROACH_X && s.upApproachZ == UP_APPROACH_Z && s.downApproachX == DOWN_APPROACH_X && s.downApproachZ == DOWN_APPROACH_Z, "the approach points");
    }

    printf("World to tile\n");
    {
        int ok = 1;
        for (int t = -3; t <= 131; ++t) {
            const float c = (float)t * TILE_SIZE;
            if (Floor_TileOfWorld(c) != t) ok = 0;
            if (Floor_TileOfWorld(c + TILE_SIZE * 0.5f - 0.01f) != t) ok = 0;
            if (Floor_TileOfWorld(c - TILE_SIZE * 0.5f + 0.01f) != t) ok = 0;
            if (Floor_TileOfWorld(c + TILE_SIZE * 0.5f + 0.01f) != t + 1) ok = 0;
            if (Floor_TileOfWorld(c - TILE_SIZE * 0.5f - 0.01f) != t - 1) ok = 0;
        }
        Check(ok, "a tile is centred on t * TILE_SIZE and reaches half a tile each way (negative coordinates included)");
    }

    printf("Floors of five sizes, three tiles beyond every side\n");
    static const int SIZES[5][3] = { { 45, 45, 16 }, { 64, 64, 8 }, { 100, 70, 6 }, { 128, 128, 6 }, { 45, 128, 4 } };   // w, h, how many floors
    long floors = 0, tiles = 0, walls = 0;
    long badSize = 0, badValid = 0, badBounds = 0, bad = 0, badOld = 0, badClass = 0, badTop = 0, badType = 0, badVariant = 0, badFlags = 0, badReserved = 0;
    long badCeil = 0, badCeilY = 0, badLevel = 0, badShell = 0, badPads = 0;
    for (int sz = 0; sz < 5; ++sz)
        for (int k = 0; k < SIZES[sz][2]; ++k) {
            const int w = SIZES[sz][0], h = SIZES[sz][1];
            char why[96] = "";
            GenerateFloorMapSized(&g_floor, 1 + (k * 7) % 25, 0x9E3779B1u * (unsigned)(sz * 100 + k + 1), w, h);
            const FloorMap *f = &g_floor;
            ++floors;
            if (f->w != w || f->h != h || Floor_Width(f) != w || Floor_Depth(f) != h) ++badSize;
            if (!ValidateFloorMap(f, why, sizeof why)) { ++badValid; printf("      %dx%d floor %d: %s\n", w, h, k, why); }
            if (!Floor_InBounds(f, 0, 0) || !Floor_InBounds(f, w - 1, h - 1) || Floor_InBounds(f, -1, 0) || Floor_InBounds(f, 0, -1) || Floor_InBounds(f, w, 0) || Floor_InBounds(f, 0, h)) ++badBounds;

            // The shell of this size, from the numbers in the design: east strip 9 in from the east edge, alcoves 9 rows at both ends.
            const FloorShell s = Floor_Shell(f);
            if (s.wallX != w - 9 || s.upAlcoveZ0 != 0 || s.upAlcoveZ1 != 8 || s.downAlcoveZ0 != h - 9 || s.downAlcoveZ1 != h - 1 ||
                s.upDoorZ0 != 3 || s.upDoorZ1 != 5 || s.downDoorZ0 != h - 6 || s.downDoorZ1 != h - 4 || s.upPadX != w - 5 || s.downPadZ != h - 5 ||
                s.arriveBelowX != w - 7 || s.arriveBelowZ != h - 5 || s.arriveAboveX != w - 7 || s.arriveAboveZ != 4 || s.hallX1 != w - 2 || s.hallZ1 != h - 2) ++badShell;
            // And the generator built that shell: the pads, doors and alcove wall.
            if (f->grid[s.upPadZ][s.upPadX].type != TILE_STAIRS_UP || f->grid[s.downPadZ][s.downPadX].type != TILE_STAIRS_DOWN) ++badPads;
            for (int z = s.upDoorZ0; z <= s.upDoorZ1; ++z) if (f->grid[z][s.wallX].type == TILE_WALL) ++badPads;
            for (int z = s.downDoorZ0; z <= s.downDoorZ1; ++z) if (f->grid[z][s.wallX].type == TILE_WALL) ++badPads;
            if (f->grid[s.upAlcoveZ1][s.wallX + 1].type != TILE_WALL || f->grid[s.downAlcoveZ0][s.wallX + 1].type != TILE_WALL) ++badPads;
            if (fabsf(f->startX - Tile_ToWorld(s.arriveBelowX)) > 0.001f || fabsf(f->startZ - Tile_ToWorld(s.arriveBelowZ)) > 0.001f) ++badPads;

            for (int z = -3; z < h + 3; ++z)
                for (int x = -3; x < w + 3; ++x) {
                    ++tiles;
                    const int solid = OracleSolid(f, x, z);
                    walls += solid;
                    if ((int)Floor_Solid(f, 0, x, z) != solid) ++bad;
                    if ((int)Tile_IsSolid(f, x, z) != solid) ++badOld;
                    if (Floor_WallClass(f, 0, x, z) != (solid ? WALL_CLASS_FULL : WALL_CLASS_OPEN)) ++badClass;
                    if (Floor_WallTopY(f, 0, x, z) != (solid ? WALL_HEIGHT : 0.0f)) ++badTop;
                    const int in = x >= 0 && x < w && z >= 0 && z < h;
                    if (Floor_Type(f, 0, x, z) != (in ? f->grid[z][x].type : TILE_WALL)) ++badType;
                    if (Floor_Variant(f, 0, x, z) != (in ? f->grid[z][x].variant : (uint8_t)WALL_SHELL)) ++badVariant;
                    if (Floor_Flags(f, x, z) != (in ? f->grid[z][x].flags : (uint8_t)0)) ++badFlags;
                    if ((int)Floor_Reserved(f, x, z) != (in && (f->grid[z][x].flags & CELL_RESERVED) != 0)) ++badReserved;
                    const int ceil = OracleCeiling(f, x, z);
                    if ((int)Floor_HasCeiling(f, x, z) != ceil) ++badCeil;
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
    Check(floors == 40 && tiles > 0 && walls > 0 && walls < tiles, "the sweep covers 40 floors of five sizes and the ring around them, walls and open ground");
    Check(badSize == 0, "every floor has the size it was asked for, and Floor_Width and Floor_Depth say so");
    Check(badValid == 0, "every floor passes ValidateFloorMap (shell intact, stairs reachable, nothing walled in)");
    Check(badBounds == 0, "the first and last tile are in the floor and one step outside on any side is not");
    Check(badShell == 0, "the shell of every size is the east strip 9 in from the edge and the alcoves at both ends");
    Check(badPads == 0, "and the generator built it: pads, doors, the alcove wall, the arrival point");
    Check(bad == 0, "Floor_Solid is the old answer on every tile, and a tile outside the floor is solid");
    Check(badOld == 0, "and Tile_IsSolid (kept for the generator and the tests) agrees with it");
    Check(badClass == 0, "the wall class is open or full, and full outside the floor");
    Check(badTop == 0, "the top of a wall is WALL_HEIGHT, and 0 where there is no wall");
    Check(badType == 0, "Floor_Type is the tile's type (a wall outside the floor)");
    Check(badVariant == 0, "Floor_Variant is the wall style (the shell outside the floor)");
    Check(badFlags == 0 && badReserved == 0, "Floor_Flags and Floor_Reserved are the cell flags (none outside the floor)");
    Check(badCeil == 0, "Floor_HasCeiling is the ceiling rule: none over the two stair alcoves of this size, none outside the floor");
    Check(badCeilY == 0, "Floor_CeilY is the roof height over a column with a ceiling and FLOOR_NO_CEILING over one without, at any height and anywhere in the tile");
    Check(badLevel == 0, "the level argument is not read until stacked levels");

    printf("The roof\n");
    Check(Floor_RoofY(&g_floor) == WALL_HEIGHT, "the roof plane is WALL_HEIGHT high");
    Check(Floor_RoofY(NULL) == WALL_HEIGHT, "and asking without a floor works (the swarm clamps under the roof with no floor at all)");
    Check(FLOOR_NO_CEILING > 100.0f * WALL_HEIGHT, "no ceiling is a height no sphere or ray reaches");

    printf("Sizes are clamped\n");
    {
        GenerateFloorMapSized(&g_floor, 1, 7u, 10, 999);
        Check(g_floor.w == FLOOR_TILES_MIN && g_floor.h == FLOOR_TILES_MAX, "a size below the least is the least and one above the most is the most");
        GenerateFloorMap(&g_floor, 1, 7u);
        Check(g_floor.w == MAP_WIDTH && g_floor.h == MAP_HEIGHT, "GenerateFloorMap is the 45 by 45 floor");
    }

    printf("The migration guard\n");
    {
        const char *found = NULL;
        Check(ScanLine("    if (f->grid[tz][tx].type != TILE_WALL) continue;", &found), "the detector catches a grid read");
        Check(ScanLine("    return Tile_IsSolid(f, x, z);", &found), "the detector catches Tile_IsSolid");
        Check(ScanLine("    const float top = WALL_HEIGHT;", &found), "the detector catches WALL_HEIGHT");
        Check(ScanLine("    if (c->type == TILE_WALL) continue;", &found), "the detector catches TILE_WALL");
        Check(ScanLine("    const MapCell *c = map->grid + 3;", &found), "the detector catches an arrow to the grid");
        Check(ScanLine("    for (int x = 0; x < MAP_WIDTH; ++x) {", &found), "the detector catches MAP_WIDTH");
        Check(ScanLine("    if (x >= STAIR_WALL_X && z <= UP_ALCOVE_Z1) continue;", &found), "the detector catches the 45 by 45 shell constants");
        Check(ScanLine("    if (!Tile_InBounds(ax, az)) continue;", &found), "the detector catches Tile_InBounds");
        Check(!ScanLine("    return Floor_Solid(f, 0, x, z);   // used to be Tile_IsSolid(f, x, z) and WALL_HEIGHT and MAP_WIDTH", &found), "the detector leaves a comment alone");
        Check(!ScanLine("    const float top = Floor_WallTopY(f, 0, x, z);", &found), "the detector leaves the accessors alone");
        Check(!ScanLine("    const int d = abs(tx - sh.upApproachX) + abs(tz - sh.upApproachZ);", &found), "the detector leaves a shell field alone");
        int total = 0, unreadable = 0;
        for (size_t i = 0; i < sizeof MOVED / sizeof MOVED[0]; ++i) {
            const int n = ScanFile(MOVED[i]);
            if (n < 0) ++unreadable; else total += n;
        }
        Check(unreadable == 0, "every moved file can be read");
        Check(total == 0, "none of the moved files reads the grid, TILE_WALL, Tile_IsSolid, WALL_HEIGHT or the 45 by 45 constants directly");
    }

    if (g_failures) { printf("\nFAILED (%d failure%s)\n", g_failures, g_failures == 1 ? "" : "s"); return 1; }
    printf("\nALL PASSED (0 failures)\n");
    return 0;
}

#endif
