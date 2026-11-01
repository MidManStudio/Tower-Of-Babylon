// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_headless.c"
// ============================================================================
// Everything is inside the macro so mray's build, which compiles every .c file,
// does not pick up a second main().
// Build (one line):
//   cc -O2 -std=c99 -DDUNGEON_HEADLESS_TEST -Isrc tests/dungeon_headless.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c src/dungeon/floor_window.c -lm -o dungeon_headless
// Run:
//   ./dungeon_headless [tower_seed] [map_floor]
#ifdef DUNGEON_HEADLESS_TEST
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dungeon/dungeon_generator.h"
#include "dungeon/dungeon_types.h"
#include "dungeon/floor_window.h"

static FloorMap g_a, g_b;
static FloorWindow g_window;
static int g_failures = 0;

static double NowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static int SameGrid(const FloorMap *a, const FloorMap *b)
{
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x)
            if (a->grid[z][x].type != b->grid[z][x].type || a->grid[z][x].variant != b->grid[z][x].variant) return 0;
    return 1;
}

static int DiffTiles(const FloorMap *a, const FloorMap *b)
{
    int n = 0;
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x)
            if (a->grid[z][x].type != b->grid[z][x].type || a->grid[z][x].variant != b->grid[z][x].variant) ++n;
    return n;
}

static void PrintMap(const FloorMap *f)
{
    printf("floor %d  seed %08x  attempts %d  fallback %d  partitions %d  pillars %d  route %d  open %.0f%%\n",
           f->floorNumber, f->seed, f->attempts, f->usedFallback, f->partitions, f->pillars, f->pathLength,
           f->walkableFraction * 100.0f);
    for (int z = 0; z < MAP_HEIGHT; ++z) {
        for (int x = 0; x < MAP_WIDTH; ++x) {
            const MapCell *c = &f->grid[z][x];
            char ch = ' ';
            if (c->type == TILE_WALL) ch = c->variant == WALL_PILLAR ? 'o' : '#';
            else if (c->type == TILE_STAIRS_UP) ch = 'U';
            else if (c->type == TILE_STAIRS_DOWN) ch = 'D';
            else if (x == ARRIVE_BELOW_X && z == ARRIVE_BELOW_Z) ch = '@';
            else if (x == ARRIVE_ABOVE_X && z == ARRIVE_ABOVE_Z) ch = '@';
            putchar(ch);
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    const uint32_t seeds[] = { 1u, 42u, 2026u, 0xC0FFEEu, 0xDEADBEEFu, 7u };
    const int seedCount = (int)(sizeof seeds / sizeof seeds[0]);
    const int floorsPerSeed = 60;

    int total = 0, firstTry = 0, fallbacks = 0, invalid = 0;
    int pathMin = 1 << 30, pathMax = 0; long pathSum = 0;
    float openMin = 1.0f, openMax = 0.0f; double openSum = 0.0;
    long partSum = 0, pillarSum = 0;
    double genMs = 0.0, worstMs = 0.0;

    for (int s = 0; s < seedCount; ++s)
        for (int fl = 1; fl <= floorsPerSeed; ++fl) {
            const double t0 = NowMs();
            GenerateFloorMap(&g_a, fl, seeds[s]);
            const double dt = NowMs() - t0;
            genMs += dt;
            if (dt > worstMs) worstMs = dt;
            ++total;
            if (g_a.attempts == 1 && !g_a.usedFallback) ++firstTry;
            if (g_a.usedFallback) ++fallbacks;
            char why[96] = "";
            if (!ValidateFloorMap(&g_a, why, sizeof why)) {
                ++invalid;
                if (!g_a.usedFallback) printf("  invalid floor %d seed %u: %s\n", fl, seeds[s], why);
            }
            if (g_a.pathLength < pathMin) pathMin = g_a.pathLength;
            if (g_a.pathLength > pathMax) pathMax = g_a.pathLength;
            pathSum += g_a.pathLength;
            if (g_a.walkableFraction < openMin) openMin = g_a.walkableFraction;
            if (g_a.walkableFraction > openMax) openMax = g_a.walkableFraction;
            openSum += g_a.walkableFraction;
            partSum += g_a.partitions;
            pillarSum += g_a.pillars;
        }

    printf("generated %d floors: %.2f ms average, %.2f ms worst\n", total, genMs / total, worstMs);
    printf("first attempt accepted %d/%d, fallback layouts %d, failing validation %d\n", firstTry, total, fallbacks, invalid);
    printf("route to stairs: min %d avg %.1f max %d tiles\n", pathMin, (double)pathSum / total, pathMax);
    printf("open space: min %.0f%% avg %.0f%% max %.0f%%, partitions avg %.1f, pillars avg %.1f\n",
           openMin * 100.0, openSum / total * 100.0, openMax * 100.0, (double)partSum / total, (double)pillarSum / total);

    printf("checks:\n");
    Check(invalid == 0, "every floor passes validation, reserved shell untouched");
    Check(fallbacks * 20 <= total, "fallback layout used on at most 5% of floors");
    Check(firstTry * 10 >= total * 6, "at least 60% of floors accepted on the first attempt");

    GenerateFloorMap(&g_a, 7, 2026u);
    GenerateFloorMap(&g_b, 7, 2026u);
    Check(SameGrid(&g_a, &g_b), "same seed and floor give the same layout");
    GenerateFloorMap(&g_b, 8, 2026u);
    Check(DiffTiles(&g_a, &g_b) > 150, "neighbouring floors differ clearly");
    GenerateFloorMap(&g_b, 7, 2027u);
    Check(DiffTiles(&g_a, &g_b) > 150, "different tower seeds differ clearly");

    // Sliding window.
    FloorWindow_Init(&g_window, 2026u, 1);
    Check(g_window.generations == 2 && FloorWindow_Get(&g_window, 0) == NULL, "start on floor 1 builds floors 1 and 2 only");
    GenerateFloorMap(&g_a, 1, 2026u);
    FloorMap keep1 = *FloorWindow_Get(&g_window, 1);
    Check(SameGrid(&keep1, &g_a), "window floor 1 matches a direct generation");
    Check(!FloorWindow_Advance(&g_window, -1) && g_window.currentFloor == 1, "cannot descend below floor 1");

    int perStepOk = 1;
    for (int i = 0; i < 6; ++i) {
        const int before = g_window.generations;
        FloorWindow_Advance(&g_window, +1);
        if (g_window.generations - before != 1) perStepOk = 0;
    }
    Check(perStepOk, "each climb generates exactly one new floor");
    Check(g_window.currentFloor == 7 && FloorWindow_Get(&g_window, 6) && FloorWindow_Get(&g_window, 8) && !FloorWindow_Get(&g_window, 5),
          "window holds floors 6, 7 and 8 after climbing to 7");

    const FloorMap *cur = FloorWindow_Current(&g_window);
    FloorMap keep7 = *cur;
    FloorWindow_Advance(&g_window, +1);
    Check(SameGrid(FloorWindow_Get(&g_window, 7), &keep7), "the floor left behind is kept, not regenerated");
    for (int i = 0; i < 7; ++i) FloorWindow_Advance(&g_window, -1);
    Check(g_window.currentFloor == 1 && SameGrid(FloorWindow_Get(&g_window, 1), &keep1), "coming back down rebuilds floor 1 identically");

    printf("\n");
    const uint32_t mapSeed = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 2026u;
    const int mapFloor = argc > 2 ? atoi(argv[2]) : 1;
    GenerateFloorMap(&g_a, mapFloor, mapSeed);
    PrintMap(&g_a);

    printf("\n%s\n", g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
#endif
