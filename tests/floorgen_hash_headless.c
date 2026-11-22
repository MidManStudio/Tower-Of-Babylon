// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "floorgen_hash_headless.c"
// ============================================================================
// The floors the generator makes, pinned down. 40 golden floors (4 tower seeds by floors 1 to 10) each keep their hash, attempt
// count, partition and pillar counts and route length; the same floor must come out whatever else was generated before it; the hash
// must see a change to any tile and must not see the things that are not the layout; the generation report must add up; and 500
// floors must stay inside the envelope measured on the generator it replaced (5,000 floors, see docs/design/09, section 5b).
// The golden values were taken with plain scans and checked with fast scans, with -O0 to -O3, with -ffast-math and fused
// multiply-add, under the address and undefined behaviour sanitizers, and in a WebAssembly build: all give the same 40 lines.
// With "dump" as the first argument the program prints those 40 lines and exits, which tests/floorgen_wasm_check.sh compares
// between builds. Inside a macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DFLOORGEN_HASH_HEADLESS_TEST -Isrc tests/floorgen_hash_headless.c src/dungeon/dungeon_generator.c -lm -o floorgen_hash_headless
// Run:
//   ./floorgen_hash_headless          (or ./floorgen_hash_headless dump)
#ifdef FLOORGEN_HASH_HEADLESS_TEST

#include <stdio.h>
#include <string.h>

#include "dungeon/dungeon_generator.h"
#include "floorgen/fg_hash.h"

static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

typedef struct { uint32_t tower; int floor; uint32_t hash; int attempts, fallback, partitions, pillars, route; } Golden;
static const Golden GOLDEN[40] = {
    { 0x00000000u,  1, 0xa76714bau, 2, 0, 33, 29, 54 },
    { 0x00000000u,  2, 0x78d02e49u, 1, 0, 27, 37, 54 },
    { 0x00000000u,  3, 0x5e3aa9e1u, 1, 0, 30, 30, 54 },
    { 0x00000000u,  4, 0x197adff9u, 1, 0, 32, 36, 58 },
    { 0x00000000u,  5, 0x9de58fe4u, 2, 0, 37, 22, 66 },
    { 0x00000000u,  6, 0x71906a54u, 3, 0, 30, 36, 52 },
    { 0x00000000u,  7, 0x282f0453u, 2, 0, 27, 35, 54 },
    { 0x00000000u,  8, 0x8400000eu, 1, 0, 31, 29, 78 },
    { 0x00000000u,  9, 0x4f4b0891u, 1, 0, 31, 20, 56 },
    { 0x00000000u, 10, 0xe7a70019u, 1, 0, 26, 33, 56 },
    { 0x00000001u,  1, 0x4c8c116cu, 3, 0, 37, 29, 64 },
    { 0x00000001u,  2, 0x70cca822u, 4, 0, 35, 29, 56 },
    { 0x00000001u,  3, 0xc651d9beu, 3, 0, 28, 28, 60 },
    { 0x00000001u,  4, 0x380c62b7u, 1, 0, 29, 38, 52 },
    { 0x00000001u,  5, 0xa6312807u, 1, 0, 34, 21, 82 },
    { 0x00000001u,  6, 0x3ca05b4fu, 3, 0, 38, 29, 76 },
    { 0x00000001u,  7, 0xef21bce0u, 1, 0, 34, 23, 56 },
    { 0x00000001u,  8, 0xec37444du, 1, 0, 27, 32, 54 },
    { 0x00000001u,  9, 0x38f0109cu, 1, 0, 34, 21, 58 },
    { 0x00000001u, 10, 0x06f3fc49u, 1, 0, 34, 38, 58 },
    { 0x12345678u,  1, 0xf52f9f6bu, 2, 0, 28, 37, 60 },
    { 0x12345678u,  2, 0x758795ffu, 2, 0, 35, 19, 54 },
    { 0x12345678u,  3, 0x0596267cu, 1, 0, 32, 19, 74 },
    { 0x12345678u,  4, 0xc23b75cbu, 1, 0, 29, 39, 54 },
    { 0x12345678u,  5, 0x42410527u, 1, 0, 31, 25, 52 },
    { 0x12345678u,  6, 0x4dc63c1au, 3, 0, 30, 37, 52 },
    { 0x12345678u,  7, 0x8d7b51eeu, 1, 0, 30, 36, 58 },
    { 0x12345678u,  8, 0x514a905du, 1, 0, 33, 27, 54 },
    { 0x12345678u,  9, 0x3af320eeu, 1, 0, 29, 39, 58 },
    { 0x12345678u, 10, 0x9856ed55u, 1, 0, 38, 38, 56 },
    { 0xdeadbeefu,  1, 0x5a12e832u, 4, 0, 35, 27, 90 },
    { 0xdeadbeefu,  2, 0xc0be1865u, 2, 0, 27, 37, 54 },
    { 0xdeadbeefu,  3, 0x628c7728u, 1, 0, 30, 36, 56 },
    { 0xdeadbeefu,  4, 0x1c2c10d0u, 1, 0, 36, 20, 52 },
    { 0xdeadbeefu,  5, 0xddd3ec6eu, 1, 0, 34, 30, 54 },
    { 0xdeadbeefu,  6, 0xfb258b4cu, 1, 0, 34, 21, 52 },
    { 0xdeadbeefu,  7, 0x82677e5cu, 1, 0, 31, 33, 66 },
    { 0xdeadbeefu,  8, 0x9e9263c5u, 1, 0, 31, 21, 74 },
    { 0xdeadbeefu,  9, 0x7291a091u, 1, 0, 31, 36, 56 },
    { 0xdeadbeefu, 10, 0xbb797500u, 1, 0, 35, 28, 58 },
};

static FloorMap g_a, g_b;

static int CountOpen(const FloorMap *f)
{
    int n = 0;
    for (int z = 0; z < MAP_HEIGHT; ++z) for (int x = 0; x < MAP_WIDTH; ++x) if (f->grid[z][x].type != TILE_WALL) ++n;
    return n;
}

// The same hash with the flags left out: what a hash that forgot a field would give.
static uint32_t HashWithoutFlags(const FloorMap *f)
{
    uint32_t h = 2166136261u;
    h = FgHashByte(h, 'F'); h = FgHashByte(h, 'G'); h = FgHashByte(h, FG_GEN_VERSION);
    h = FgHashByte(h, MAP_WIDTH); h = FgHashByte(h, MAP_HEIGHT);
    h = FgHashU32(h, (uint32_t)f->floorNumber); h = FgHashU32(h, f->seed);
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) { h = FgHashByte(h, (uint32_t)f->grid[z][x].type); h = FgHashByte(h, f->grid[z][x].variant); }
    h = FgHashU32(h, (uint32_t)f->partitions); h = FgHashU32(h, (uint32_t)f->pillars); h = FgHashU32(h, (uint32_t)f->pathLength);
    h = FgHashU32(h, (uint32_t)f->attempts); h = FgHashByte(h, f->usedFallback ? 1u : 0u);
    return h;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "dump") == 0) {
        for (int i = 0; i < 40; ++i) {
            GenerateFloorMap(&g_a, GOLDEN[i].floor, GOLDEN[i].tower);
            printf("%08x %2d %08x att=%d fb=%d part=%d pil=%d route=%d\n", GOLDEN[i].tower, GOLDEN[i].floor, FgFloorHash(&g_a),
                   g_a.attempts, g_a.usedFallback ? 1 : 0, g_a.partitions, g_a.pillars, g_a.pathLength);
        }
        return 0;
    }

#ifdef FG_PLAIN_SCANS
    printf("Built with plain scans (-DFG_PLAIN_SCANS): the goldens must come out the same as with the bit scans\n\n");
#endif
    printf("Golden floors\n");
    {
        int hashOk = 1, reportOk = 1;
        for (int i = 0; i < 40; ++i) {
            GenerateFloorMap(&g_a, GOLDEN[i].floor, GOLDEN[i].tower);
            if (FgFloorHash(&g_a) != GOLDEN[i].hash) { hashOk = 0; printf("      floor %d of tower %08x: hash %08x, expected %08x\n", GOLDEN[i].floor, GOLDEN[i].tower, FgFloorHash(&g_a), GOLDEN[i].hash); }
            if (g_a.attempts != GOLDEN[i].attempts || (g_a.usedFallback ? 1 : 0) != GOLDEN[i].fallback || g_a.partitions != GOLDEN[i].partitions ||
                g_a.pillars != GOLDEN[i].pillars || g_a.pathLength != GOLDEN[i].route) reportOk = 0;
        }
        Check(hashOk, "all 40 floors have their golden hash");
        Check(reportOk, "all 40 floors have their golden attempts, partitions, pillars and route length");
    }

    printf("\nThe same floor every time\n");
    {
        GenerateFloorMap(&g_a, 3, 0x12345678u);
        const uint32_t first = FgFloorHash(&g_a);
        GenerateFloorMap(&g_b, 7, 0xDEADBEEFu);                 // another floor in between: nothing may leak through the statics
        GenerateFloorMap(&g_b, 4, 0x12345678u);
        GenerateFloorMap(&g_a, 3, 0x12345678u);
        Check(FgFloorHash(&g_a) == first, "a floor made again after other floors is the same floor");
        int distinct = 1;
        for (int i = 0; i < 40 && distinct; ++i) for (int j = i + 1; j < 40; ++j) if (GOLDEN[i].hash == GOLDEN[j].hash) { distinct = 0; break; }
        Check(distinct, "the 40 golden floors all have different hashes");
        Check(GOLDEN[0].hash != GOLDEN[10].hash && GOLDEN[0].hash != GOLDEN[1].hash, "neighbouring floors and neighbouring towers differ");
    }

    printf("\nWhat the hash sees and what it does not\n");
    {
        GenerateFloorMap(&g_a, 5, 0x12345678u);
        const uint32_t base = FgFloorHash(&g_a);
        g_b = g_a; g_b.grid[10][10].type = g_b.grid[10][10].type == TILE_WALL ? TILE_FLOOR : TILE_WALL;
        Check(FgFloorHash(&g_b) != base, "a changed tile type changes the hash");
        g_b = g_a; g_b.grid[20][5].variant ^= 1;
        Check(FgFloorHash(&g_b) != base, "a changed wall variant changes the hash");
        g_b = g_a; g_b.grid[30][30].flags ^= CELL_RESERVED;
        Check(FgFloorHash(&g_b) != base, "a changed flag changes the hash");
        g_b = g_a; g_b.floorNumber += 1;
        Check(FgFloorHash(&g_b) != base, "the floor number is in the hash");
        g_b = g_a; g_b.seed ^= 1u;
        Check(FgFloorHash(&g_b) != base, "the seed is in the hash");
        g_b = g_a; g_b.pillars += 1;
        Check(FgFloorHash(&g_b) != base, "the pillar count is in the hash");
        g_b = g_a;
        for (int z = 0; z < MAP_HEIGHT; ++z) for (int x = 0; x < MAP_WIDTH; ++x) g_b.grid[z][x].discovered = true;
        g_b.genMicros += 1000; g_b.stageMicros[GEN_STAGE_PILLARS] += 77; g_b.walkableFraction += 0.25f; g_b.openTiles += 5;
        Check(FgFloorHash(&g_b) == base, "the discovered bit, the timings, the display fraction and the open count are not in the hash");
        int caught = 0;
        for (int i = 0; i < 40; ++i) {
            GenerateFloorMap(&g_a, GOLDEN[i].floor, GOLDEN[i].tower);
            if (HashWithoutFlags(&g_a) != GOLDEN[i].hash) { caught = 1; break; }
        }
        Check(caught, "broken on purpose: a hash that forgets the flags no longer matches the goldens");
    }

    printf("\nThe generation report\n");
    {
        int addsUp = 1, attemptsOk = 1, openOk = 1, namesOk = 1, shellOk = 1;
        for (int i = 0; i < 200; ++i) {
            GenerateFloorMap(&g_a, 1 + i % 50, 0x2000u + (uint32_t)i * 31u);
            uint32_t sum = 0;
            for (int s = 0; s < GEN_STAGE_COUNT; ++s) sum += g_a.stageMicros[s];
            if (sum > g_a.genMicros) addsUp = 0;
            if (g_a.attempts < 1 || g_a.attempts > 8) attemptsOk = 0;
            if (CountOpen(&g_a) != g_a.openTiles) openOk = 0;
            const float want = (float)g_a.openTiles / 1849.0f;
            if (g_a.walkableFraction < want - 0.0001f || g_a.walkableFraction > want + 0.0001f) openOk = 0;
            if (g_a.pathLength < 1) shellOk = 0;
        }
        for (int s = 0; s < GEN_STAGE_COUNT; ++s) if (strcmp(GenStageName(s), "?") == 0 || GenStageName(s)[0] == 0) namesOk = 0;
        Check(addsUp, "the microseconds of the stages never add up to more than the whole call (200 floors)");
        Check(attemptsOk, "the number of attempts is between 1 and 8");
        Check(openOk, "openTiles is the count of tiles that are not walls, and the display fraction is that over the hall area");
        Check(shellOk, "every floor has a route to the stairs");
        Check(namesOk && strcmp(GenStageName(-1), "?") == 0 && strcmp(GenStageName(GEN_STAGE_COUNT), "?") == 0, "every stage has a name, and an out of range stage is a question mark");
    }

    printf("\nThe envelope of the generator it replaced (500 floors)\n");
    {
        double routeSum = 0, openSum = 0, partSum = 0, pilSum = 0;
        int first = 0, fallbacks = 0, n = 0, rangeOk = 1;
        for (int t = 0; t < 10; ++t)
            for (int floor = 1; floor <= 50; ++floor) {
                GenerateFloorMap(&g_a, floor, 0x1000u + (uint32_t)t * 7919u);
                ++n;
                if (g_a.usedFallback) { ++fallbacks; continue; }
                if (g_a.attempts == 1) ++first;
                routeSum += g_a.pathLength; openSum += g_a.walkableFraction; partSum += g_a.partitions; pilSum += g_a.pillars;
                if (g_a.pathLength < 52 || g_a.pathLength > 260) rangeOk = 0;
                if (g_a.openTiles * 100 < 55 * 1849 || g_a.openTiles * 100 > 90 * 1849) rangeOk = 0;
            }
        const int m = n - fallbacks;
        const double route = routeSum / m, open = openSum / m, part = partSum / m, pil = pilSum / m;
        printf("      (route %.2f, open %.3f, partitions %.2f, pillars %.2f, first attempt %.1f%%, fallbacks %d of %d)\n", route, open, part, pil, 100.0 * first / m, fallbacks, n);
        Check(rangeOk, "every floor that is not the fallback layout meets the targets: a route of 52 to 260 steps and 55 to 90 percent open");
        Check(route > 57.0 && route < 62.0, "the mean route to the stairs is within 2.5 of the old generator's 59.5");
        Check(open > 0.832 && open < 0.844, "the mean open space is within 0.006 of the old generator's 0.838");
        Check(part > 30.3 && part < 32.3, "the mean number of partitions is within 1.0 of the old generator's 31.3");
        Check(pil > 27.4 && pil < 31.0, "the mean number of pillars is within 1.8 of the old generator's 29.2");
        Check(100.0 * first / m > 58.0 && 100.0 * first / m < 72.0, "58 to 72 percent of floors are accepted on the first attempt (the old generator: about 65)");
        Check(fallbacks <= 5, "at most 1 floor in 100 needs the fallback layout (the old generator: about 1 in 10,000)");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
