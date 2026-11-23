// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "bench_floorgen.c"
// ============================================================================
// The floor generator's benchmark: builds N floors and prints, per generation stage, how many microseconds a floor takes (mean,
// median, 95th percentile, worst), how big a share of the total each stage is, how many attempts a floor needed, and a checksum of
// every floor's hash. Run it on the phone in Termux to see where the time goes; the checksum must be the same on the phone and on a
// computer (that is the determinism contract, docs/design/08 section 12).
//
// Build and run (Termux on the phone, or any computer with gcc or clang), from the project root:
//   cc -O2 -std=c99 -DFLOORGEN_BENCH -Isrc tools/bench_floorgen.c src/dungeon/dungeon_generator.c -lm -o /tmp/bench_floorgen && /tmp/bench_floorgen
// Options:   /tmp/bench_floorgen [floors] [tower_seed] [rows]
//   floors      how many floors to build (default 300, at most 100000). Floor numbers go 1 to 25 and round again.
//   tower_seed  the tower seed, decimal or 0x hex (default 0x12345678)
//   rows        the word "rows" prints one line per floor as well (CSV: floor, seed, attempts, fallback, open, hash, total, stages)
// A build without optimisation is not a benchmark and the program says so. The times come from clock() (processor time, plain C99, so
// the same code runs on the phone, the server and the browser); in a browser its resolution is the browser's, and the line "smallest
// time seen" says what the clock can resolve. Nothing here is read by the game.
// The whole file is inside #ifdef FLOORGEN_BENCH so that mray's build, which compiles every .c file in the project, does not pick up
// a second main() (the same rule as the headless tests, see docs/testing.md).
#ifdef FLOORGEN_BENCH

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dungeon/dungeon_generator.h"
#include "floorgen/fg_hash.h"

#define BENCH_DEFAULT_FLOORS 300
#define BENCH_MAX_FLOORS 100000
#define BENCH_WARMUP 3

static FloorMap g_floor;

static int CompareU32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

typedef struct { double mean; uint32_t p50, p95, max, min; } Stats;

// Sorts v (n values) and reads the statistics from it.
static Stats Summarise(uint32_t *v, int n)
{
    Stats s = { 0.0, 0, 0, 0, 0 };
    if (n <= 0) return s;
    qsort(v, (size_t)n, sizeof v[0], CompareU32);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += (double)v[i];
    s.mean = sum / (double)n;
    s.p50 = v[n / 2];
    int k95 = (int)((long)n * 95 / 100);
    if (k95 >= n) k95 = n - 1;
    s.p95 = v[k95];
    s.max = v[n - 1];
    s.min = v[0];
    return s;
}

static void PrintBanner(int floors, uint32_t tower)
{
    printf("bench_floorgen: compiler %s, ", __VERSION__);
#if defined(__aarch64__)
    printf("arch aarch64, ");
#elif defined(__arm__)
    printf("arch arm32, ");
#elif defined(__x86_64__)
    printf("arch x86_64, ");
#elif defined(__i386__)
    printf("arch x86, ");
#elif defined(__wasm__)
    printf("arch wasm, ");
#elif defined(__riscv)
    printf("arch riscv, ");
#else
    printf("arch other, ");
#endif
    printf("%d-bit pointers, ", (int)(sizeof(void *) * 8));
#if defined(__OPTIMIZE__)
    printf("optimised build\n");
#else
    printf("NOT OPTIMISED: these numbers are not a benchmark\n");
#endif
    printf("generator version %d, floor %d by %d tiles, FloorMap %d bytes, CLOCKS_PER_SEC %ld\n", FG_GEN_VERSION, MAP_WIDTH, MAP_HEIGHT, (int)sizeof(FloorMap), (long)CLOCKS_PER_SEC);
    printf("%d floors from tower seed %08x (floor numbers 1 to 25, round and round), %d warm-up floors first\n", floors, (unsigned)tower, BENCH_WARMUP);
}

int main(int argc, char **argv)
{
    int floors = BENCH_DEFAULT_FLOORS;
    uint32_t tower = 0x12345678u;
    int rows = 0;
    if (argc > 1) {
        const long n = strtol(argv[1], NULL, 10);
        if (n < 1 || n > BENCH_MAX_FLOORS) { printf("floors must be 1 to %d\n", BENCH_MAX_FLOORS); return 2; }
        floors = (int)n;
    }
    if (argc > 2) tower = (uint32_t)strtoul(argv[2], NULL, 0);
    if (argc > 3 && strcmp(argv[3], "rows") == 0) rows = 1;

    PrintBanner(floors, tower);

    // The first floors pay for cold caches and first touches of memory; they are built and thrown away.
    for (int i = 0; i < BENCH_WARMUP; ++i) GenerateFloorMap(&g_floor, 1 + i, tower ^ 0xA5A5A5A5u);

    uint32_t *stage[GEN_STAGE_COUNT], *total = (uint32_t *)malloc((size_t)floors * sizeof(uint32_t)), *attemptsV = (uint32_t *)malloc((size_t)floors * sizeof(uint32_t));
    uint32_t *openV = (uint32_t *)malloc((size_t)floors * sizeof(uint32_t)), *other = (uint32_t *)malloc((size_t)floors * sizeof(uint32_t));
    int allocated = total && attemptsV && openV && other;
    for (int s = 0; s < GEN_STAGE_COUNT; ++s) { stage[s] = (uint32_t *)malloc((size_t)floors * sizeof(uint32_t)); allocated = allocated && stage[s]; }
    if (!allocated) { printf("out of memory\n"); return 2; }

    uint32_t checksum = 2166136261u;
    long fallbacks = 0, firstTry = 0;
    uint32_t smallestSeen = UINT32_MAX;
    const clock_t runStart = clock();

    if (rows) printf("floor,seed,attempts,fallback,open,hash,total_us,");
    if (rows) { for (int s = 0; s < GEN_STAGE_COUNT; ++s) printf("%s_us%s", GenStageName(s), s + 1 < GEN_STAGE_COUNT ? "," : "\n"); }

    for (int i = 0; i < floors; ++i) {
        const int floorNo = 1 + i % 25;
        GenerateFloorMap(&g_floor, floorNo, tower + (uint32_t)i * 0x9E3779B1u);
        const FloorMap *f = &g_floor;
        const uint32_t hash = FgFloorHash(f);
        checksum = FgHashU32(checksum, hash);
        uint32_t stageSum = 0;
        for (int s = 0; s < GEN_STAGE_COUNT; ++s) {
            stage[s][i] = f->stageMicros[s];
            stageSum += f->stageMicros[s];
            if (f->stageMicros[s] > 0 && f->stageMicros[s] < smallestSeen) smallestSeen = f->stageMicros[s];
        }
        total[i] = f->genMicros;
        other[i] = f->genMicros > stageSum ? f->genMicros - stageSum : 0;
        attemptsV[i] = (uint32_t)f->attempts;
        openV[i] = (uint32_t)f->openTiles;
        if (f->usedFallback) ++fallbacks;
        if (f->attempts == 1 && !f->usedFallback) ++firstTry;
        if (f->genMicros > 0 && f->genMicros < smallestSeen) smallestSeen = f->genMicros;
        if (rows) {
            printf("%d,%08x,%d,%d,%d,%08x,%u", floorNo, (unsigned)f->seed, f->attempts, f->usedFallback ? 1 : 0, f->openTiles, (unsigned)hash, (unsigned)f->genMicros);
            for (int s = 0; s < GEN_STAGE_COUNT; ++s) printf(",%u", (unsigned)f->stageMicros[s]);
            printf("\n");
        }
    }
    const double runSecs = (double)(clock() - runStart) / (double)CLOCKS_PER_SEC;

    // Shares are taken from the means before the arrays are sorted (sorting loses the pairing, not the means).
    double mean[GEN_STAGE_COUNT], meanSum = 0.0, meanOther = 0.0;
    for (int s = 0; s < GEN_STAGE_COUNT; ++s) {
        double sum = 0.0;
        for (int i = 0; i < floors; ++i) sum += (double)stage[s][i];
        mean[s] = sum / (double)floors;
        meanSum += mean[s];
    }
    { double sum = 0.0; for (int i = 0; i < floors; ++i) sum += (double)other[i]; meanOther = sum / (double)floors; }

    printf("\nmicroseconds per floor (a floor's stage times are summed over its attempts)\n");
    printf("%-12s %9s %9s %9s %9s %7s\n", "stage", "mean", "median", "p95", "worst", "share");
    for (int s = 0; s < GEN_STAGE_COUNT; ++s) {
        const double share = meanSum + meanOther > 0.0 ? 100.0 * mean[s] / (meanSum + meanOther) : 0.0;
        const Stats st = Summarise(stage[s], floors);
        printf("%-12s %9.1f %9u %9u %9u %6.1f%%\n", GenStageName(s), st.mean, (unsigned)st.p50, (unsigned)st.p95, (unsigned)st.max, share);
    }
    {
        const Stats st = Summarise(other, floors);
        const double share = meanSum + meanOther > 0.0 ? 100.0 * meanOther / (meanSum + meanOther) : 0.0;
        printf("%-12s %9.1f %9u %9u %9u %6.1f%%   (the part of a floor's time that no stage timer covers)\n", "other", st.mean, (unsigned)st.p50, (unsigned)st.p95, (unsigned)st.max, share);
    }
    const Stats tot = Summarise(total, floors);
    printf("%-12s %9.1f %9u %9u %9u\n", "TOTAL", tot.mean, (unsigned)tot.p50, (unsigned)tot.p95, (unsigned)tot.max);
    if (tot.mean > 0.0) printf("about %.0f floors a second at the mean\n", 1000000.0 / tot.mean);

    const Stats att = Summarise(attemptsV, floors), opn = Summarise(openV, floors);
    printf("\nattempts per floor: mean %.2f, median %u, worst %u; %.1f percent passed on the first attempt; %ld floor%s fell back to the plain pillar hall\n",
           att.mean, (unsigned)att.p50, (unsigned)att.max, 100.0 * (double)firstTry / (double)floors, fallbacks, fallbacks == 1 ? "" : "s");
    printf("open tiles per floor: mean %.1f, least %u, most %u\n", opn.mean, (unsigned)opn.min, (unsigned)opn.max);
    printf("whole run: %.3f seconds of processor time for %d floors; smallest time seen %u us (what the clock can resolve)\n", runSecs, floors, smallestSeen == UINT32_MAX ? 0u : (unsigned)smallestSeen);
    printf("checksum of all floor hashes: %08x   <- must be the same on every device and build for the same floors, seed and generator version\n", (unsigned)checksum);

    for (int s = 0; s < GEN_STAGE_COUNT; ++s) free(stage[s]);
    free(total); free(attemptsV); free(openV); free(other);
    return 0;
}

#endif
