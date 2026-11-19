// ============================================================================
// NOTICE: Full documentation, design decisions, and measurement notes for this file
// live in docs/design/09-floor-generation-research.md, section "5. Prototype measurements"
// ============================================================================
// A replica of the two whole-grid loops in Lighting_Update (src/lighting/lighting.c: the tone curve with expf, then the corner
// means) to see how they scale with the floor size, and what a window around the viewer costs instead. A prototype for the plan
// in docs/design/08-floor-generation.md, section 13.4.
//
// Build and run: clang -O2 -std=gnu99 -DLIGHT_SCALE_BENCH tools/bench/light_scale_bench.c -o /tmp/light_scale_bench -lm && /tmp/light_scale_bench
// Numbers from a build without optimisation are not a benchmark and the program says so.
// The whole file is inside #ifdef LIGHT_SCALE_BENCH so that mray's build, which compiles every .c file in the project, does not pick up a
// second main() (the same rule as the headless tests, see docs/testing.md).
#ifdef LIGHT_SCALE_BENCH
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define MAXN 128
static float tileI[MAXN][MAXN], tile[MAXN][MAXN], corner[MAXN + 1][MAXN + 1];
static uint8_t open_[MAXN][MAXN];
static int N;
static volatile float sink;
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int Open(int x, int z) { return x >= 0 && x < N && z >= 0 && z < N && open_[z][x]; }
static void update(int x0, int z0, int x1, int z1)            // the same two loops, over a window [x0,x1) x [z0,z1)
{
    for (int z = z0; z < z1; ++z) for (int x = x0; x < x1; ++x) { const float I = tileI[z][x]; tile[z][x] = I > 0.0f ? 1.0f - expf(-1.4f * I) : 0.0f; }
    for (int cj = z0; cj <= z1; ++cj) for (int ci = x0; ci <= x1; ++ci) {
        float sum = 0; int n = 0;
        for (int dz = -1; dz <= 0; ++dz) for (int dx = -1; dx <= 0; ++dx) { if (!Open(ci + dx, cj + dz)) continue; sum += tile[cj + dz][ci + dx]; ++n; }
        corner[cj][ci] = n ? sum / (float)n : 0.0f;
    }
}
int main(void)
{
#if defined(__OPTIMIZE__)
    printf("light_scale_bench: optimised build\n");
#else
    printf("light_scale_bench: NOT OPTIMISED: these numbers are not a benchmark\n");
#endif
    const int sizes[3] = { 45, 64, 128 };
    printf("size | full update us | memset(tileI) us | window 37x37 us\n");
    for (int s = 0; s < 3; ++s) {
        N = sizes[s]; srand(5);
        for (int z = 0; z < N; ++z) for (int x = 0; x < N; ++x) { open_[z][x] = (x > 0 && z > 0 && x < N - 1 && z < N - 1 && rand() % 100 > 22); tileI[z][x] = (rand() % 100 < 70) ? (rand() % 1000) / 600.0f : 0.0f; }
        double bf = 1e30, bm = 1e30, bw = 1e30;
        for (int r = 0; r < 40; ++r) { double t0 = now_us(); update(0, 0, N, N); double t1 = now_us(); if (t1 - t0 < bf) bf = t1 - t0; sink = corner[N / 2][N / 2]; }
        for (int r = 0; r < 40; ++r) { double t0 = now_us(); memset(tileI, 0, sizeof tileI); double t1 = now_us(); if (t1 - t0 < bm) bm = t1 - t0; sink = tileI[1][1]; }
        const int w = N < 37 ? N : 37, c = N / 2;
        for (int r = 0; r < 40; ++r) { double t0 = now_us(); update(c - w / 2, c - w / 2, c - w / 2 + w, c - w / 2 + w); double t1 = now_us(); if (t1 - t0 < bw) bw = t1 - t0; sink = corner[c][c]; }
        printf("%4d | %8.1f | %8.1f | %8.1f\n", N, bf, bm, bw);
    }
    return 0;
}
#endif // LIGHT_SCALE_BENCH
