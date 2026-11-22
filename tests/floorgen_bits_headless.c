// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "floorgen_bits_headless.c"
// ============================================================================
// The bit rows and the integer probes of the floor generator (src/floorgen/fg_bitrow.h and fg_probe.h), against plain loops
// that know nothing about bits: shifts across the 64 bit word boundary, masks, runs in four directions, 3 by 3 fits, side bands,
// the probe field, and the grid walk against a float version of the same walk. Each fast function is also copied with one rule
// broken and the check has to catch it. Needs nothing but the headers, and is inside a macro so mray's build, which compiles
// every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DFLOORGEN_BITS_HEADLESS_TEST -Isrc tests/floorgen_bits_headless.c -lm -o floorgen_bits_headless
// Run:
//   ./floorgen_bits_headless
#ifdef FLOORGEN_BITS_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "floorgen/fg_bitrow.h"
#include "floorgen/fg_probe.h"
#include "floorgen/fg_rng.h"

static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static FgPcg32 g_rng;
static uint32_t Rnd(void) { return FgPcg32_Next(&g_rng); }

// ---- plain references: one bit at a time -----------------------------------------------------------------------------------------

static int NGet(FgRow r, int i) { return i < 0 || i > 127 ? 0 : ((i < 64 ? r.lo >> i : r.hi >> (i - 64)) & 1u) != 0; }
static FgRow NSetBit(FgRow r, int i) { if (i >= 0 && i <= 127) FgRow_Set(&r, i); return r; }
static FgRow NShr(FgRow a, int n) { FgRow r = FgRow_Zero(); for (int i = 0; i < 128; ++i) if (NGet(a, i + n)) r = NSetBit(r, i); return r; }
static FgRow NShl(FgRow a, int n) { FgRow r = FgRow_Zero(); for (int i = 0; i < 128; ++i) if (NGet(a, i - n)) r = NSetBit(r, i); return r; }
static int NCtz(FgRow a) { int i = 0; while (!NGet(a, i)) ++i; return i; }
static int NClz(FgRow a) { int n = 0; while (!NGet(a, 127 - n)) ++n; return n; }
static int NPop(FgRow a) { int n = 0; for (int i = 0; i < 128; ++i) n += NGet(a, i); return n; }
static int Same(FgRow a, FgRow b) { return a.lo == b.lo && a.hi == b.hi; }

static FgRow RandomRow(int style)
{
    FgRow r = { 0, 0 };
    switch (style % 5) {
    case 0: r.lo = ((uint64_t)Rnd() << 32) | Rnd(); r.hi = ((uint64_t)Rnd() << 32) | Rnd(); break;       // dense
    case 1: for (int k = 0; k < 4; ++k) r = NSetBit(r, (int)(Rnd() % 128)); break;                          // sparse
    case 2: r = NSetBit(r, (int)(Rnd() % 128)); break;                                                      // one bit
    case 3: r.lo = ~0ull; r.hi = ~0ull; r = NSetBit(FgRow_AndNot(r, NSetBit(FgRow_Zero(), (int)(Rnd() % 128))), 0); break; // almost full
    default: r.lo = 0x8000000000000000ull; r.hi = 1ull; break;                                              // the two bits at the word boundary
    }
    return r;
}

// ---- shifts, written so a broken copy can be run through the same check ----------------------------------------------------------

typedef FgRow (*ShiftFn)(FgRow, int);
static int ShiftMismatches(ShiftFn shr, ShiftFn shl)
{
    static const int ns[] = { 0, 1, 2, 31, 32, 33, 62, 63, 64, 65, 66, 100, 126, 127, 128, 200 };
    int bad = 0;
    for (int t = 0; t < 400; ++t) {
        const FgRow a = RandomRow(t);
        for (unsigned k = 0; k < sizeof ns / sizeof ns[0]; ++k) {
            if (!Same(shr(a, ns[k]), NShr(a, ns[k]))) ++bad;
            if (!Same(shl(a, ns[k]), NShl(a, ns[k]))) ++bad;
        }
    }
    return bad;
}
// A copy of the right shift that does not carry bits from the high word into the low one.
static FgRow ShrNoCarry(FgRow a, int n)
{
    FgRow r;
    if (n <= 0) return a;
    if (n >= 128) return FgRow_Zero();
    if (n >= 64) { r.lo = a.hi >> (n - 64); r.hi = 0; return r; }
    r.lo = a.lo >> n;
    r.hi = a.hi >> n;
    return r;
}

typedef FgRow (*MaskBelowFn)(int);
static int MaskBelowMismatches(MaskBelowFn fn)
{
    int bad = 0;
    for (int n = -3; n <= 131; ++n) {
        FgRow want = FgRow_Zero();
        for (int i = 0; i < n && i < 128; ++i) want = NSetBit(want, i);
        if (!Same(fn(n), want)) ++bad;
    }
    return bad;
}
// A copy that is wrong from 65 up: the high word gets one bit too many.
static FgRow MaskBelowOffByOne(int n)
{
    FgRow r;
    if (n <= 0) return FgRow_Zero();
    if (n >= 128) { r.lo = ~0ull; r.hi = ~0ull; return r; }
    if (n >= 64) { r.lo = ~0ull; r.hi = (n == 64) ? 0ull : ((1ull << (n - 63)) - 1u); return r; }
    r.lo = (1ull << n) - 1u; r.hi = 0; return r;
}

// ---- grids -----------------------------------------------------------------------------------------------------------------------

typedef struct { int w, h; uint8_t cell[FG_MAP_MAX][FG_MAP_MAX]; } Plain;   // 1 = solid

static void RandomGrid(Plain *p, FgBitGrid *g, int w, int h, int percent)
{
    p->w = w; p->h = h;
    FgBitGrid_Init(g, w, h);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) {
            p->cell[z][x] = (uint8_t)(Rnd() % 100 < (uint32_t)percent);
            if (p->cell[z][x]) FgBitGrid_Set(g, x, z, true);
        }
}
static int PSolid(const Plain *p, int x, int z) { return x < 0 || z < 0 || x >= p->w || z >= p->h || p->cell[z][x]; }

static int PRun(const Plain *p, int x, int z, int dx, int dz)
{
    int n = 0;
    while (!PSolid(p, x + dx * (n + 1), z + dz * (n + 1))) ++n;
    return n;
}
static int POpen3(const Plain *p, int x, int z)
{
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) if (PSolid(p, x + dx, z + dz)) return 0;
    return 1;
}
static int PSides(const Plain *p, int vertical, int line, int i, int half)
{
    for (int d = -half; d <= half; ++d) {
        if (d == 0) continue;
        if (PSolid(p, vertical ? line + d : i, vertical ? i : line + d)) return 0;
    }
    return 1;
}

typedef int (*RunFn)(const FgBitGrid *, int, int);
static int RunMismatches(const Plain *p, const FgBitGrid *g, RunFn fn, int dx, int dz)
{
    int bad = 0;
    for (int z = 0; z < p->h; ++z)
        for (int x = 0; x < p->w; ++x)
            if (fn(g, x, z) != PRun(p, x, z, dx, dz)) ++bad;
    return bad;
}
// A copy of the west run that adjusts the highest set bit by 126 instead of 127.
static int RunWOffByOne(const FgBitGrid *g, int x, int z)
{
    const FgRow t = FgRow_And(g->rows[z], FgRow_MaskBelow(x));
    return FgRow_Any(t) ? x - (126 - FgRow_Clz(t)) - 1 : x;
}

typedef int (*Open3Fn)(const FgBitGrid *, int, int);
static int Open3Mismatches(const Plain *p, const FgBitGrid *g, Open3Fn fn)
{
    int bad = 0;
    for (int z = -1; z <= p->h; ++z)
        for (int x = -1; x <= p->w; ++x)
            if ((fn(g, x, z) != 0) != (POpen3(p, x, z) != 0)) ++bad;
    return bad;
}
static int Open3Fast(const FgBitGrid *g, int x, int z) { return FgBitGrid_Open3x3_Fast(g, x, z); }
static int Open3Plain(const FgBitGrid *g, int x, int z) { return FgBitGrid_Open3x3_Plain(g, x, z); }
// A copy that forgets the edge test: it reads rows that are not there.
static int Open3NoEdge(const FgBitGrid *g, int x, int z)
{
    if (z < 1 || z > FG_MAP_MAX - 2 || x < 1) return 0;
    const FgRow occ = FgRow_Or(FgRow_Or(g->rows[z - 1], g->rows[z]), g->rows[z + 1]);
    return !FgRow_Any(FgRow_And(occ, FgRow_MaskRange(x - 1, x + 1)));
}

typedef int (*SidesFn)(const FgBitGrid *, int, int, int, int);
static int SidesMismatches(const Plain *p, const FgBitGrid *g, SidesFn fn)
{
    int bad = 0;
    for (int t = 0; t < 4000; ++t) {
        const int vertical = (int)(Rnd() & 1u);
        const int lim = vertical ? p->w : p->h, run = vertical ? p->h : p->w;
        const int line = (int)(Rnd() % (uint32_t)(lim + 6)) - 3;     // includes lines outside the floor
        const int i = (int)(Rnd() % (uint32_t)run);
        const int half = 1 + (int)(Rnd() % 4u);
        if ((fn(g, vertical, line, i, half) != 0) != (PSides(p, vertical, line, i, half) != 0)) ++bad;
    }
    return bad;
}
static int SidesFast(const FgBitGrid *g, int v, int line, int i, int half) { return FgBitGrid_SidesFree_Fast(g, v != 0, line, i, half); }
static int SidesPlain(const FgBitGrid *g, int v, int line, int i, int half) { return FgBitGrid_SidesFree_Plain(g, v != 0, line, i, half); }
// A copy that leaves the segment's own tile in the band, so a solid centre counts against it.
static int SidesWithCentre(const FgBitGrid *g, int v, int line, int i, int half)
{
    const int vertical = v != 0;
    const int lim = vertical ? g->w : g->h;
    if (line - half < 0 || line + half >= lim) return 0;
    const FgRow m = FgRow_MaskRange(line - half, line + half);
    return !FgRow_Any(FgRow_And(vertical ? g->rows[i] : g->cols[i], m));
}

// Rows and columns must describe the same tiles.
static int RowsAndColsAgree(const FgBitGrid *g)
{
    for (int z = 0; z < g->h; ++z)
        for (int x = 0; x < g->w; ++x)
            if (FgRow_Test(g->rows[z], x) != FgRow_Test(g->cols[x], z)) return 0;
    return 1;
}
// A copy of Set that only updates the rows.
static void SetRowsOnly(FgBitGrid *g, int x, int z, int solid)
{
    if (solid) FgRow_Set(&g->rows[z], x); else FgRow_Clear(&g->rows[z], x);
}

// ---- the same walk in doubles, for the probe rays ---------------------------------------------------------------------------------
// Doubles and the same tie rule (a tie goes to the z boundary), so with the integer directions it is the integer walk in exact
// arithmetic, and with cosf and sinf directions it is the walk the generator used to take.

static double RefRay(const Plain *p, int tx, int tz, double dx, double dz, double maxDist)
{
    if (PSolid(p, tx, tz)) return 0.0;
    int cx = tx, cz = tz;
    const int stepX = dx > 0.0 ? 1 : -1, stepZ = dz > 0.0 ? 1 : -1;
    const double adx = fabs(dx), adz = fabs(dz);
    double nextX = 0.5, nextZ = 0.5;
    for (;;) {
        int takeX;
        if (adx == 0.0) takeX = 0; else if (adz == 0.0) takeX = 1; else takeX = nextX * adz < nextZ * adx;
        double dist, den;
        if (takeX) { cx += stepX; dist = nextX; den = adx; nextX += 1.0; }
        else       { cz += stepZ; dist = nextZ; den = adz; nextZ += 1.0; }
        const double t = dist / den;
        if (t >= maxDist) return maxDist;
        if (PSolid(p, cx, cz)) return t;
    }
}

int main(void)
{
    FgPcg32_Seed(&g_rng, 20261121, 5);

    printf("Rows of 128 tiles\n");
    {
        int ok = 1;
        FgRow r = FgRow_Zero();
        for (int i = 0; i < 128 && ok; ++i) {
            FgRow_Set(&r, i);
            if (!FgRow_Test(r, i) || NPop(r) != 1 || FgRow_Pop(r) != 1) ok = 0;
            FgRow_Clear(&r, i);
            if (FgRow_Any(r)) ok = 0;
        }
        Check(ok, "set, test and clear work for every one of the 128 positions");
        ok = 1;
        for (int t = 0; t < 2000; ++t) {
            const FgRow a = RandomRow(t), b = RandomRow(t + 1);
            const FgRow o = FgRow_Or(a, b), n = FgRow_And(a, b), d = FgRow_AndNot(a, b);
            if (o.lo != (a.lo | b.lo) || o.hi != (a.hi | b.hi) || n.lo != (a.lo & b.lo) || n.hi != (a.hi & b.hi) || d.lo != (a.lo & ~b.lo) || d.hi != (a.hi & ~b.hi)) ok = 0;
        }
        Check(ok, "or, and and and-not agree with the words on 2000 pairs");
    }
    Check(ShiftMismatches(FgRow_Shr, FgRow_Shl) == 0, "shifts right and left by 0 to 200 agree with the bit-by-bit version, across the word boundary");
    {
        int ok = 1;
        for (int t = 0; t < 400; ++t) {
            const FgRow a = RandomRow(t);
            if (!Same(FgRow_Shl1(a), NShl(a, 1)) || !Same(FgRow_Shr1(a), NShr(a, 1))) ok = 0;
            if (FgRow_Pop(a) != NPop(a)) ok = 0;
            if (FgRow_Any(a) && (FgRow_Ctz(a) != NCtz(a) || FgRow_Clz(a) != NClz(a))) ok = 0;
        }
        for (int i = 0; i < 128; ++i) {
            const FgRow one = NSetBit(FgRow_Zero(), i);
            if (FgRow_Ctz(one) != i || FgRow_Clz(one) != 127 - i) ok = 0;
        }
        Check(ok, "shift by one, population count, trailing and leading zeros agree, and every single bit is found");
    }
    Check(MaskBelowMismatches(FgRow_MaskBelow) == 0, "the mask of the first n bits is right for every n from -3 to 131");
    {
        int ok = 1;
        for (int a = 0; a < 128 && ok; ++a)
            for (int b = a; b < 128; ++b) {
                FgRow want = FgRow_Zero();
                for (int i = a; i <= b; ++i) want = NSetBit(want, i);
                if (!Same(FgRow_MaskRange(a, b), want)) { ok = 0; break; }
            }
        Check(ok, "the mask of bits a to b is right for all 8256 pairs");
    }

    printf("\nGrids of tiles\n");
    {
        static const int sizes[][2] = { { 45, 45 }, { 64, 64 }, { 81, 81 }, { 128, 128 }, { 7, 9 }, { 66, 40 }, { 128, 5 }, { 5, 128 }, { 1, 1 } };
        static Plain p;
        static FgBitGrid g;
        int testOk = 1, countOk = 1, agreeOk = 1, runBad = 0, open3Bad = 0, sidesBad = 0;
        for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; ++s)
            for (int percent = 0; percent <= 60; percent += 20) {
                RandomGrid(&p, &g, sizes[s][0], sizes[s][1], percent);
                int solid = 0;
                for (int z = -2; z <= p.h + 1; ++z)
                    for (int x = -2; x <= p.w + 1; ++x) {
                        if ((FgBitGrid_Test(&g, x, z) != 0) != (PSolid(&p, x, z) != 0)) testOk = 0;
                        if (x >= 0 && z >= 0 && x < p.w && z < p.h) solid += p.cell[z][x];
                    }
                if (FgBitGrid_CountSolid(&g) != solid) countOk = 0;
                if (!RowsAndColsAgree(&g)) agreeOk = 0;
                runBad += RunMismatches(&p, &g, FgBitGrid_RunE_Fast, 1, 0) + RunMismatches(&p, &g, FgBitGrid_RunW_Fast, -1, 0)
                        + RunMismatches(&p, &g, FgBitGrid_RunS_Fast, 0, 1) + RunMismatches(&p, &g, FgBitGrid_RunN_Fast, 0, -1);
                runBad += RunMismatches(&p, &g, FgBitGrid_RunE_Plain, 1, 0) + RunMismatches(&p, &g, FgBitGrid_RunW_Plain, -1, 0)
                        + RunMismatches(&p, &g, FgBitGrid_RunS_Plain, 0, 1) + RunMismatches(&p, &g, FgBitGrid_RunN_Plain, 0, -1);
                open3Bad += Open3Mismatches(&p, &g, Open3Fast) + Open3Mismatches(&p, &g, Open3Plain);
                sidesBad += SidesMismatches(&p, &g, SidesFast) + SidesMismatches(&p, &g, SidesPlain);
            }
        Check(testOk, "the solid test agrees with the plain grid everywhere, and out of bounds is solid");
        Check(countOk, "the count of solid tiles agrees with the plain grid");
        Check(agreeOk, "the rows and the columns describe the same tiles");
        Check(runBad == 0, "free runs east, west, south and north (fast and plain) agree with the loop on 9 sizes and 4 densities");
        Check(open3Bad == 0, "the 3 by 3 fit (fast and plain) agrees with the loop, including at the edges");
        Check(sidesBad == 0, "the side band of a segment (fast and plain) agrees with the loop, including lines outside the floor");

        int toggleOk = 1;
        RandomGrid(&p, &g, 100, 70, 30);
        for (int t = 0; t < 20000; ++t) {
            const int x = (int)(Rnd() % 100u), z = (int)(Rnd() % 70u), solid = (int)(Rnd() & 1u);
            FgBitGrid_Set(&g, x, z, solid != 0);
            p.cell[z][x] = (uint8_t)solid;
        }
        for (int z = 0; z < 70; ++z) for (int x = 0; x < 100; ++x) if ((FgBitGrid_Test(&g, x, z) != 0) != (p.cell[z][x] != 0)) toggleOk = 0;
        Check(toggleOk && RowsAndColsAgree(&g), "20,000 random sets and clears keep both orientations right");
    }

    printf("\nProbe rays\n");
    {
        static Plain p;
        static FgBitGrid g;
        RandomGrid(&p, &g, 45, 45, 22);
        for (int i = 0; i < 45; ++i) { p.cell[0][i] = p.cell[44][i] = p.cell[i][0] = p.cell[i][44] = 1; FgBitGrid_Set(&g, i, 0, true); FgBitGrid_Set(&g, i, 44, true); FgBitGrid_Set(&g, 0, i, true); FgBitGrid_Set(&g, 44, i, true); }
        int axisOk = 1;
        for (int z = 1; z < 44; ++z)
            for (int x = 1; x < 44; ++x) {
                if (p.cell[z][x]) continue;
                if (FgProbe_CastRay(&g, x, z, 65536, 0, FG_PROBE_MAX_DIST) != FgProbe_AxisReach(PRun(&p, x, z, 1, 0))) axisOk = 0;
                if (FgProbe_CastRay(&g, x, z, -65536, 0, FG_PROBE_MAX_DIST) != FgProbe_AxisReach(PRun(&p, x, z, -1, 0))) axisOk = 0;
                if (FgProbe_CastRay(&g, x, z, 0, 65536, FG_PROBE_MAX_DIST) != FgProbe_AxisReach(PRun(&p, x, z, 0, 1))) axisOk = 0;
                if (FgProbe_CastRay(&g, x, z, 0, -65536, FG_PROBE_MAX_DIST) != FgProbe_AxisReach(PRun(&p, x, z, 0, -1))) axisOk = 0;
            }
        Check(axisOk, "a ray along an axis is exactly the free run plus half a tile");
        Check(FgProbe_AxisReach(0) == FX_HALF && FgProbe_AxisReach(2) == 5 * FX_HALF && FgProbe_AxisReach(500) == FG_PROBE_MAX_DIST,
              "the axis reach is n + 0.5 tiles and is capped at 64");
        Check(FgProbe_CastRay(&g, 0, 0, 65536, 0, FG_PROBE_MAX_DIST) == 0, "a ray that starts inside a wall has length 0");

        // The 16 rays against the same walk in doubles with the same integer directions: only the last division can differ.
        int total = 0, exactOff = 0;
        // And against the walk with the cosf and sinf directions the generator used to take. The four diagonal rays pass exactly
        // through tile corners, where which neighbour is looked at first is a tie. The integer walk settles it the same way at all
        // four (z first); the old float code settled it by rounding, which differed at 225 degrees. The other twelve directions
        // are compared on their own.
        int oldTotal = 0, oldClose = 0, oldFar = 0;
        for (int z = 1; z < 44; ++z)
            for (int x = 1; x < 44; ++x) {
                if (p.cell[z][x]) continue;
                for (int k = 0; k < FG_PROBE_RAYS; ++k) {
                    const double got = (double)FgProbe_CastRay(&g, x, z, FG_RAY_DIR[k][0], FG_RAY_DIR[k][1], FG_PROBE_MAX_DIST) / 65536.0;
                    const double exact = RefRay(&p, x, z, FG_RAY_DIR[k][0] / 65536.0, FG_RAY_DIR[k][1] / 65536.0, 64.0);
                    ++total;
                    if (fabs(got - exact) > 0.001) ++exactOff;
                    if (k % 4 == 2) continue;                       // the diagonals
                    const double a = (double)k * 6.283185307179586 / 16.0;
                    const double old = RefRay(&p, x, z, (double)cosf((float)a), (double)sinf((float)a), 64.0);
                    ++oldTotal;
                    if (fabs(got - old) < 0.01) ++oldClose; else if (fabs(got - old) > 1.0) ++oldFar;
                }
            }
        printf("      (%d rays; %d differ from exact arithmetic; against the old float directions on the 12 non-diagonal ones: %d of %d within 0.01 tile, %d more than a tile apart)\n",
               total, exactOff, oldClose, oldTotal, oldFar);
        Check(exactOff == 0, "every one of the 16 rays is the exact walk along its integer direction (within 0.001 tile)");
        Check(oldClose * 100 >= oldTotal * 99 && oldFar == 0, "the 12 non-diagonal rays agree with the old float directions to 0.01 tile on 99 percent, and none is a tile off");

        FgProbeField pf;
        FgProbeField_Analyze(&g, &pf, false);
        int fieldOk = pf.cols == 11 && pf.rows == 11 && pf.count == 121;
        for (int i = 0; i < pf.count && fieldOk; ++i) {
            const FgProbe *q = &pf.probes[i];
            if (q->active != !p.cell[q->tileZ][q->tileX]) { fieldOk = 0; break; }
            if (!q->active) continue;
            if (q->reachE != FgProbe_AxisReach(PRun(&p, q->tileX, q->tileZ, 1, 0)) || q->reachS != FgProbe_AxisReach(PRun(&p, q->tileX, q->tileZ, 0, 1))) fieldOk = 0;
            if (q->spanX != q->reachE + q->reachW || q->spanZ != q->reachN + q->reachS) fieldOk = 0;
            if (q->meanReach != (q->reachE + q->reachW + q->reachN + q->reachS) / 4) fieldOk = 0;
            if (q->maxReach < q->meanReach) fieldOk = 0;
        }
        Check(fieldOk, "the probe field on a 45 by 45 floor is 11 by 11 probes with the right reaches, spans and means");
        FgProbeField_Analyze(&g, &pf, true);
        int fullOk = 1;
        for (int i = 0; i < pf.count; ++i) if (pf.probes[i].active && (pf.probes[i].meanReach <= 0 || pf.probes[i].maxReach < pf.probes[i].meanReach)) fullOk = 0;
        Check(fullOk, "with all 16 rays cast, every active probe has a positive mean no larger than its maximum");
    }

    printf("\nBroken on purpose: each rule has to be caught\n");
    Check(ShiftMismatches(FgRow_Shr, FgRow_Shl) == 0, "(the real shifts pass the shift check)");
    Check(ShiftMismatches(ShrNoCarry, FgRow_Shl) > 0, "a right shift that does not carry between the words is caught");
    Check(MaskBelowMismatches(MaskBelowOffByOne) > 0, "a mask that is one bit too long in the high word is caught");
    {
        static Plain p;
        static FgBitGrid g;
        RandomGrid(&p, &g, 66, 40, 30);
        Check(RunMismatches(&p, &g, FgBitGrid_RunW_Fast, -1, 0) == 0, "(the real west run passes)");
        Check(RunMismatches(&p, &g, RunWOffByOne, -1, 0) > 0, "a west run with the wrong bit adjustment is caught");
        Check(Open3Mismatches(&p, &g, Open3Fast) == 0, "(the real 3 by 3 fit passes)");
        Check(Open3Mismatches(&p, &g, Open3NoEdge) > 0, "a 3 by 3 fit with no edge test is caught");
        Check(SidesMismatches(&p, &g, SidesFast) == 0, "(the real side band passes)");
        Check(SidesMismatches(&p, &g, SidesWithCentre) > 0, "a side band that keeps the segment's own tile in it is caught");
        FgBitGrid h;
        FgBitGrid_Init(&h, 66, 40);
        for (int t = 0; t < 500; ++t) SetRowsOnly(&h, (int)(Rnd() % 66u), (int)(Rnd() % 40u), 1);
        Check(!RowsAndColsAgree(&h), "a set that only updates the rows leaves the columns out of step and is caught");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
