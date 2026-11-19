// ============================================================================
// NOTICE: Full documentation, design decisions, and measurement notes for this file
// live in docs/design/09-floor-generation-research.md, section "5. Prototype measurements"
// ============================================================================
// Candidate floor-generation primitives: bit mask rows against plain loops. A prototype for the plan in
// docs/design/08-floor-generation.md, to be replaced by the real benchmark harness in round R0. Every pair is checked for
// identical results before it is timed. Synthetic floors (random partitions with 3 tile doorways), not real ones.
//
// Build and run (Termux on the phone, or any computer with gcc or clang):
//   clang -O2 -std=gnu99 -DGEN_PRIMS_BENCH tools/bench/gen_prims_bench.c -o /tmp/gen_prims_bench && /tmp/gen_prims_bench
//   (also -O3, and on x86 -O2 -mpopcnt -mavx2; on ARM no extra flags are needed)
// Numbers from a build without optimisation are not a benchmark and the program says so.
// The whole file is inside #ifdef GEN_PRIMS_BENCH so that mray's build, which compiles every .c file in the project, does not pick up a
// second main() (the same rule as the headless tests, see docs/testing.md).
#ifdef GEN_PRIMS_BENCH
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXN 128
static int N;                                   // active floor size (tiles per side)
static uint8_t solid[MAXN][MAXN];               // 1 = wall

// ---- 128-bit row, two words (mid-math BitMask128 shape) ------------------------------------------------------------------
typedef struct { uint64_t lo, hi; } Row;
static inline Row R0(void) { Row r = {0, 0}; return r; }
static inline Row row_or(Row a, Row b)   { Row r = { a.lo | b.lo, a.hi | b.hi }; return r; }
static inline Row row_and(Row a, Row b)  { Row r = { a.lo & b.lo, a.hi & b.hi }; return r; }
static inline Row row_andn(Row a, Row b) { Row r = { a.lo & ~b.lo, a.hi & ~b.hi }; return r; }
static inline int row_any(Row a)         { return (a.lo | a.hi) != 0; }
static inline Row row_shl1(Row a)        { Row r = { a.lo << 1, (a.hi << 1) | (a.lo >> 63) }; return r; }
static inline Row row_shr1(Row a)        { Row r = { (a.lo >> 1) | (a.hi << 63), a.hi >> 1 }; return r; }
static inline Row row_shr(Row a, int n)
{
    if (n == 0) return a;
    if (n >= 128) return R0();
    if (n >= 64) { Row r = { a.hi >> (n - 64), 0 }; return r; }
    Row r = { (a.lo >> n) | (a.hi << (64 - n)), a.hi >> n }; return r;
}
static inline int row_ctz(Row a) { return a.lo ? __builtin_ctzll(a.lo) : 64 + __builtin_ctzll(a.hi); }       // a must be nonzero
static inline int row_clz(Row a) { return a.hi ? __builtin_clzll(a.hi) : 64 + __builtin_clzll(a.lo); }       // a must be nonzero
static inline int row_pop(Row a) { return __builtin_popcountll(a.lo) + __builtin_popcountll(a.hi); }
static inline Row mask_below(int x)                                                                          // bits [0, x)
{
    Row r;
    if (x <= 0) return R0();
    if (x >= 128) { r.lo = ~0ull; r.hi = ~0ull; return r; }
    if (x >= 64) { r.lo = ~0ull; r.hi = (x == 64) ? 0 : ((1ull << (x - 64)) - 1); return r; }
    r.lo = (1ull << x) - 1; r.hi = 0; return r;
}
static inline Row mask_range(int a, int b) { return row_andn(mask_below(b + 1), mask_below(a)); }              // bits [a, b]

static Row rowsS[MAXN], colsS[MAXN];            // solid bits by row (bit = x) and by column (bit = z)
static Row rowsF[MAXN];                         // free bits by row

// ---- PCG32 as in mid-math (new = state 0 + seed, one discard step; Lemire range with the 32-bit threshold) --------------------
typedef struct { uint64_t state, inc; } Pcg32;
static inline uint32_t pcg_next(Pcg32 *r)
{
    const uint64_t old = r->state;
    r->state = old * 6364136223846793005ull + r->inc;
    const uint32_t xsh = (uint32_t)(((old >> 18) ^ old) >> 27), rot = (uint32_t)(old >> 59);
    return (xsh >> rot) | (xsh << ((32 - rot) & 31));
}
static void pcg_seed(Pcg32 *r, uint64_t seed, uint64_t seq) { r->inc = (seq << 1) | 1; r->state = seed; pcg_next(r); }
static uint32_t pcg_range(Pcg32 *r, uint32_t lo, uint32_t hi)
{
    const uint32_t range = hi - lo;
    uint64_t m = (uint64_t)pcg_next(r) * range;
    if ((uint32_t)m < range) {
        const uint32_t t = (uint32_t)(0u - range) % range;
        while ((uint32_t)m < t) m = (uint64_t)pcg_next(r) * range;
    }
    return lo + (uint32_t)(m >> 32);
}

static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static volatile uint64_t g_sink;

// ---- a floor-like grid: border, random partition segments with a 3-tile doorway ------------------------------------------------
static void make_floor(int n, uint32_t seed)
{
    N = n; memset(solid, 0, sizeof solid);
    for (int i = 0; i < n; ++i) solid[0][i] = solid[n - 1][i] = solid[i][0] = solid[i][n - 1] = 1;
    Pcg32 r; pcg_seed(&r, seed, 7);
    const int segs = n * n / 55;
    for (int s = 0; s < segs; ++s) {
        const int horiz = pcg_next(&r) & 1, len = 5 + (int)pcg_range(&r, 0, 18);
        const int x = 2 + (int)pcg_range(&r, 0, n - 4), z = 2 + (int)pcg_range(&r, 0, n - 4), door = (int)pcg_range(&r, 0, len);
        for (int k = 0; k < len; ++k) {
            if (k >= door && k < door + 3) continue;
            const int xx = horiz ? x + k : x, zz = horiz ? z : z + k;
            if (xx > 0 && xx < n - 1 && zz > 0 && zz < n - 1) solid[zz][xx] = 1;
        }
    }
    memset(rowsS, 0, sizeof rowsS); memset(colsS, 0, sizeof colsS); memset(rowsF, 0, sizeof rowsF);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const uint64_t bx = 1ull << (x & 63), bz = 1ull << (z & 63);
            if (solid[z][x]) { if (x < 64) rowsS[z].lo |= bx; else rowsS[z].hi |= bx; if (z < 64) colsS[x].lo |= bz; else colsS[x].hi |= bz; }
            else { if (x < 64) rowsF[z].lo |= bx; else rowsF[z].hi |= bx; }
        }
}

// ---- A. span scan: free tiles from (x,z) until a wall, four directions ---------------------------------------------------------
static int span_loop(int x, int z, int dir)
{
    int k = 0;
    switch (dir) {
    case 0: while (!solid[z][x + 1 + k]) ++k; break;
    case 1: while (!solid[z][x - 1 - k]) ++k; break;
    case 2: while (!solid[z + 1 + k][x]) ++k; break;
    default: while (!solid[z - 1 - k][x]) ++k; break;
    }
    return k;
}
static int span_bits(int x, int z, int dir)
{
    if (dir == 0) return row_ctz(row_shr(rowsS[z], x + 1));
    if (dir == 1) { Row t = row_and(rowsS[z], mask_below(x)); return x - (127 - row_clz(t)) - 1; }
    if (dir == 2) return row_ctz(row_shr(colsS[x], z + 1));
    Row t = row_and(colsS[x], mask_below(z)); return z - (127 - row_clz(t)) - 1;
}

// ---- B. band check: rows z-3..z+3, columns a..b all free -------------------------------------------------------------------------
static int band_loop(int z, int a, int b)
{
    for (int zz = z - 3; zz <= z + 3; ++zz) for (int x = a; x <= b; ++x) if (solid[zz][x]) return 0;
    return 1;
}
static int band_bits(int z, int a, int b)
{
    Row acc = R0();
    for (int zz = z - 3; zz <= z + 3; ++zz) acc = row_or(acc, rowsS[zz]);
    return !row_any(row_and(acc, mask_range(a, b)));
}

// ---- C. flood fill from a start tile: reachable count and farthest distance ----------------------------------------------------------
static int flood_queue(int sx, int sz, int *maxd)
{
    static uint16_t dist[MAXN * MAXN], q[MAXN * MAXN];
    memset(dist, 0xFF, sizeof(uint16_t) * (size_t)N * N);
    int head = 0, tail = 0, count = 1, md = 0;
    dist[sz * N + sx] = 0; q[tail++] = (uint16_t)(sz * N + sx);
    while (head < tail) {
        const int cur = q[head++], cx = cur % N, cz = cur / N, d = dist[cur] + 1;
        const int nb[4] = { cur + 1, cur - 1, cur + N, cur - N };
        const int nx[4] = { cx + 1, cx - 1, cx, cx }, nz[4] = { cz, cz, cz + 1, cz - 1 };
        for (int k = 0; k < 4; ++k) {
            if (solid[nz[k]][nx[k]] || dist[nb[k]] != 0xFFFF) continue;
            dist[nb[k]] = (uint16_t)d; q[tail++] = (uint16_t)nb[k]; ++count; if (d > md) md = d;
        }
    }
    *maxd = md; return count;
}
static int flood_bits(int sx, int sz, int *maxd)
{
    static Row vis[MAXN], cur[MAXN], nxt[MAXN];
    memset(vis, 0, sizeof(Row) * N); memset(cur, 0, sizeof(Row) * N);
    if (sx < 64) cur[sz].lo = 1ull << sx; else cur[sz].hi = 1ull << (sx - 64);
    vis[sz] = cur[sz];
    int zlo = sz, zhi = sz, d = 0, count = 1;
    for (;;) {
        const int lo = zlo > 0 ? zlo - 1 : 0, hi = zhi < N - 1 ? zhi + 1 : N - 1;
        int nlo = N, nhi = -1;
        for (int z = lo; z <= hi; ++z) {
            const Row c = cur[z], up = z > 0 ? cur[z - 1] : R0(), dn = z < N - 1 ? cur[z + 1] : R0();
            Row m = row_or(row_or(c, row_shl1(c)), row_or(row_shr1(c), row_or(up, dn)));
            m = row_andn(row_and(m, rowsF[z]), vis[z]);
            nxt[z] = m;
            if (row_any(m)) { if (z < nlo) nlo = z; if (z > nhi) nhi = z; }
        }
        if (nhi < 0) break;
        ++d;
        for (int z = lo; z <= hi; ++z) { vis[z] = row_or(vis[z], nxt[z]); count += row_pop(nxt[z]); cur[z] = nxt[z]; }
        zlo = nlo; zhi = nhi;
    }
    *maxd = d; return count;
}

// ---- D. candidate mask: every tile whose 3x3 block is free (pillar / cover spots), then the count ------------------------------------
static int open3_loop(uint64_t *hash)
{
    int n = 0; uint64_t h = 1469598103934665603ull;
    for (int z = 1; z < N - 1; ++z)
        for (int x = 1; x < N - 1; ++x) {
            int ok = 1;
            for (int dz = -1; dz <= 1 && ok; ++dz) for (int dx = -1; dx <= 1; ++dx) if (solid[z + dz][x + dx]) { ok = 0; break; }
            if (ok) { ++n; h = (h ^ (uint64_t)(z * 256 + x)) * 1099511628211ull; }
        }
    *hash = h; return n;
}
static int open3_bits(uint64_t *hash)
{
    int n = 0; uint64_t h = 1469598103934665603ull;
    for (int z = 1; z < N - 1; ++z) {
        const Row f3 = row_and(row_and(rowsF[z - 1], rowsF[z]), rowsF[z + 1]);
        Row fit = row_and(f3, row_and(row_shl1(f3), row_shr1(f3)));
        n += row_pop(fit);
        while (row_any(fit)) {
            const int x = row_ctz(fit);
            h = (h ^ (uint64_t)(z * 256 + x)) * 1099511628211ull;
            if (fit.lo) fit.lo &= fit.lo - 1; else fit.hi &= fit.hi - 1;     // clear the lowest set bit
        }
    }
    *hash = h; return n;
}

// ---- F. per-tile hash fill (dressing, light mood): scalar loop against a 4-wide vector-extension loop --------------------------------
typedef uint32_t u32x4 __attribute__((vector_size(16)));
static inline uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static uint32_t hbuf_a[MAXN * MAXN] __attribute__((aligned(16))), hbuf_b[MAXN * MAXN] __attribute__((aligned(16)));
static void hash_scalar(uint32_t seed, int count) { for (int i = 0; i < count; ++i) hbuf_a[i] = hash32(seed + (uint32_t)i * 0x9E3779B1u); }
static void hash_wide(uint32_t seed, int count)
{
    const u32x4 lane = { 0u, 0x9E3779B1u, 0x3C6EF362u, 0xDAA66D13u }, step = { 4u * 0x9E3779B1u, 4u * 0x9E3779B1u, 4u * 0x9E3779B1u, 4u * 0x9E3779B1u };
    u32x4 base = { seed, seed, seed, seed }; base += lane;
    for (int i = 0; i < count; i += 4) {
        u32x4 x = base;
        x ^= x >> 16; x *= (u32x4){ 0x7feb352dU, 0x7feb352dU, 0x7feb352dU, 0x7feb352dU };
        x ^= x >> 15; x *= (u32x4){ 0x846ca68bU, 0x846ca68bU, 0x846ca68bU, 0x846ca68bU };
        x ^= x >> 16;
        *(u32x4 *)&hbuf_b[i] = x; base += step;
    }
}

// ---- timing helpers: best of several repeats, so a noisy neighbour does not decide the result -----------------------------------------
#define BEST(reps, code) ({ double best = 1e30; for (int rep_ = 0; rep_ < (reps); ++rep_) { const double t0_ = now_us(); code; const double t1_ = now_us(); if (t1_ - t0_ < best) best = t1_ - t0_; } best; })

int main(void)
{
    printf("gen_prims_bench: compiler %s, ", __VERSION__);
#if defined(__aarch64__)
    printf("arch aarch64, ");
#elif defined(__x86_64__)
    printf("arch x86_64, ");
#elif defined(__wasm__)
    printf("arch wasm, ");
#else
    printf("arch other, ");
#endif
#if defined(__OPTIMIZE__)
    printf("optimised build\n");
#else
    printf("NOT OPTIMISED: these numbers are not a benchmark\n");
#endif
    const int sizes[4] = { 45, 64, 81, 128 };
    printf("size | span ns/q loop->bits | band ns/q loop->bits | flood us queue->bits (reach, far) | 3x3 us loop->bits (n) | hash us scalar->wide4 (all tiles)\n");
    for (int si = 0; si < 4; ++si) {
        make_floor(sizes[si], 12345u + (uint32_t)si);
        // queries
        enum { Q = 20000 };
        static int qx[Q], qz[Q], qd[Q], qa[Q], qb[Q];
        Pcg32 r; pcg_seed(&r, 99, 3);
        for (int i = 0; i < Q; ++i) {
            do { qx[i] = 2 + (int)pcg_range(&r, 0, N - 4); qz[i] = 2 + (int)pcg_range(&r, 0, N - 4); } while (solid[qz[i]][qx[i]]);
            qd[i] = (int)(pcg_next(&r) & 3);
            qa[i] = 1 + (int)pcg_range(&r, 0, N - 26); qb[i] = qa[i] + 4 + (int)pcg_range(&r, 0, 20); if (qb[i] > N - 2) qb[i] = N - 2;
            if (qz[i] < 4) qz[i] = 4;
            if (qz[i] > N - 5) qz[i] = N - 5;
        }
        uint64_t sa = 0, sb = 0;
        for (int i = 0; i < Q; ++i) { sa += (uint64_t)span_loop(qx[i], qz[i], qd[i]); sb += (uint64_t)span_bits(qx[i], qz[i], qd[i]); }
        if (sa != sb) { printf("MISMATCH span %llu %llu\n", (unsigned long long)sa, (unsigned long long)sb); return 1; }
        uint64_t ba = 0, bb = 0;
        for (int i = 0; i < Q; ++i) { ba += (uint64_t)band_loop(qz[i], qa[i], qb[i]); bb += (uint64_t)band_bits(qz[i], qa[i], qb[i]); }
        if (ba != bb) { printf("MISMATCH band %llu %llu\n", (unsigned long long)ba, (unsigned long long)bb); return 1; }
        const double tsl = BEST(7, { uint64_t s = 0; for (int i = 0; i < Q; ++i) s += (uint64_t)span_loop(qx[i], qz[i], qd[i]); g_sink = s; }) * 1000.0 / Q;
        const double tsb = BEST(7, { uint64_t s = 0; for (int i = 0; i < Q; ++i) s += (uint64_t)span_bits(qx[i], qz[i], qd[i]); g_sink = s; }) * 1000.0 / Q;
        const double tbl = BEST(7, { uint64_t s = 0; for (int i = 0; i < Q; ++i) s += (uint64_t)band_loop(qz[i], qa[i], qb[i]); g_sink = s; }) * 1000.0 / Q;
        const double tbb = BEST(7, { uint64_t s = 0; for (int i = 0; i < Q; ++i) s += (uint64_t)band_bits(qz[i], qa[i], qb[i]); g_sink = s; }) * 1000.0 / Q;
        // flood from the first free tile near the centre
        int sx = N / 2, sz = N / 2; while (solid[sz][sx]) { ++sx; if (sx >= N - 1) { sx = 1; ++sz; } }
        int d1, d2; const int c1 = flood_queue(sx, sz, &d1), c2 = flood_bits(sx, sz, &d2);
        if (c1 != c2 || d1 != d2) { printf("MISMATCH flood %d/%d %d/%d\n", c1, c2, d1, d2); return 1; }
        const double tfq = BEST(30, { int d; g_sink = (uint64_t)flood_queue(sx, sz, &d); });
        const double tfb = BEST(30, { int d; g_sink = (uint64_t)flood_bits(sx, sz, &d); });
        uint64_t h1, h2; const int n1 = open3_loop(&h1), n2 = open3_bits(&h2);
        if (n1 != n2 || h1 != h2) { printf("MISMATCH open3 %d/%d\n", n1, n2); return 1; }
        const double tol = BEST(30, { uint64_t h; g_sink = (uint64_t)open3_loop(&h); });
        const double tob = BEST(30, { uint64_t h; g_sink = (uint64_t)open3_bits(&h); });
        const int cnt = N * N; hash_scalar(777u, cnt); hash_wide(777u, cnt & ~3);
        if (memcmp(hbuf_a, hbuf_b, sizeof(uint32_t) * (size_t)(cnt & ~3)) != 0) { printf("MISMATCH hash\n"); return 1; }
        const double ths = BEST(60, { hash_scalar(777u, cnt); g_sink = hbuf_a[cnt / 2]; });
        const double thw = BEST(60, { hash_wide(777u, cnt & ~3); g_sink = hbuf_b[cnt / 2]; });
        printf("%4d | %5.1f -> %5.1f | %6.1f -> %5.1f | %7.1f -> %7.1f (%d, %d) | %6.1f -> %5.1f (%d) | %6.1f -> %6.1f\n",
               N, tsl, tsb, tbl, tbb, tfq, tfb, c1, d1, tol, tob, n1, ths, thw);
    }
    return 0;
}
#endif // GEN_PRIMS_BENCH
