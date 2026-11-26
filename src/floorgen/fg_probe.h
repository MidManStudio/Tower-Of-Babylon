// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "fg_probe.h"
// ============================================================================
#ifndef FG_PROBE_H
#define FG_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#include "fg_bitrow.h"
#include "fg_fixed.h"

// Probe rays for the generator, integer only. A probe sits on a lattice over the floor and measures the open space around it:
// four axis rays (one count of free tiles each, by bit scans) and, when asked, 16 evenly spaced rays by a grid walk in Q16.16.
// Distances are in tiles as Q16.16: the ray starts at the centre of its tile, so a probe with n free tiles to the east reaches
// n + 0.5. The old float version (cosf and sinf for the directions, float steps) is gone from layout code; Probe_CastRay in
// dungeon_probes.c is the float ray the monsters use for sight and is not used here.
#define FG_PROBE_STRIDE 4
#define FG_PROBE_ORIGIN 2
#define FG_PROBE_COLS_MAX (((FG_MAP_MAX - 1 - FG_PROBE_ORIGIN) / FG_PROBE_STRIDE) + 1)
#define FG_PROBE_MAX      (FG_PROBE_COLS_MAX * FG_PROBE_COLS_MAX)
#define FG_PROBE_RAYS     16
#define FG_PROBE_MAX_DIST FX_FROM_INT(64)

typedef struct {
    int  tileX, tileZ;
    bool active;                       // the probe's own tile is free
    Fx   reachE, reachW, reachN, reachS;
    Fx   spanX, spanZ;                 // reachE + reachW, reachN + reachS
    Fx   meanReach, maxReach;          // over the four axis rays, or over the 16 when they were cast
} FgProbe;

typedef struct {
    int     cols, rows, count;
    FgProbe probes[FG_PROBE_MAX];
} FgProbeField;

// The 16 directions, k * 22.5 degrees, as Q16.16 (cos, sin), +z is south. Made once offline and written down, so nothing at
// run time depends on a trigonometry function. Length is 1 within 0.0005 percent.
static const int32_t FG_RAY_DIR[FG_PROBE_RAYS][2] = {
    {  65536,      0 }, {  60547,  25080 }, {  46341,  46341 }, {  25080,  60547 },
    {      0,  65536 }, { -25080,  60547 }, { -46341,  46341 }, { -60547,  25080 },
    { -65536,      0 }, { -60547, -25080 }, { -46341, -46341 }, { -25080, -60547 },
    {      0, -65536 }, {  25080, -60547 }, {  46341, -46341 }, {  60547, -25080 }
};

// A grid walk (Amanatides and Woo) from the centre of tile (tx, tz) along the Q16.16 direction (dx, dz), up to maxDist.
// Returns the distance to the first solid tile, or maxDist, as Q16.16 tiles. Which boundary comes first is decided by
// multiplying across, not by dividing, and a tie goes to the z boundary. One division at the end gives the distance.
static inline Fx FgProbe_CastRay(const FgBitGrid *g, int tx, int tz, int32_t dx, int32_t dz, Fx maxDist)
{
    if (FgBitGrid_Test(g, tx, tz)) return 0;
    const int stepX = dx > 0 ? 1 : -1, stepZ = dz > 0 ? 1 : -1;
    const uint64_t adx = dx < 0 ? (uint64_t)(-(int64_t)dx) : (uint64_t)dx;
    const uint64_t adz = dz < 0 ? (uint64_t)(-(int64_t)dz) : (uint64_t)dz;
    const uint64_t limit = (uint64_t)maxDist;
    uint64_t nextX = FX_HALF, nextZ = FX_HALF;          // distance along each axis to the next tile boundary, Q16.16 tiles
    int cx = tx, cz = tz;
    for (;;) {
        bool takeX;
        if (adx == 0) takeX = false;
        else if (adz == 0) takeX = true;
        else takeX = nextX * adz < nextZ * adx;
        uint64_t dist, den;
        if (takeX) { cx += stepX; dist = nextX; den = adx; nextX += FX_ONE; }
        else       { cz += stepZ; dist = nextZ; den = adz; nextZ += FX_ONE; }
        if (dist * FX_ONE >= limit * den) return maxDist;
        if (FgBitGrid_Test(g, cx, cz)) return (Fx)((dist * FX_ONE) / den);
    }
}

static inline Fx FgProbe_AxisReach(int freeTiles)
{
    const Fx r = FX_HALF + (Fx)freeTiles * FX_ONE;
    return r > FG_PROBE_MAX_DIST ? FG_PROBE_MAX_DIST : r;
}

// One probe's fields from the four axis runs (what FgProbeField_Analyze gives with fullRays false), for a probe whose tileX and
// tileZ are set. A probe's axis fields read only its own row and its own column of the grid and nothing else.
static inline void FgProbe_FillAxes(const FgBitGrid *g, FgProbe *p)
{
    p->active = !FgBitGrid_Test(g, p->tileX, p->tileZ);
    if (!p->active) {
        p->reachE = p->reachW = p->reachN = p->reachS = 0;
        p->spanX = p->spanZ = p->meanReach = p->maxReach = 0;
        return;
    }
    p->reachE = FgProbe_AxisReach(FgBitGrid_RunE(g, p->tileX, p->tileZ));
    p->reachW = FgProbe_AxisReach(FgBitGrid_RunW(g, p->tileX, p->tileZ));
    p->reachS = FgProbe_AxisReach(FgBitGrid_RunS(g, p->tileX, p->tileZ));
    p->reachN = FgProbe_AxisReach(FgBitGrid_RunN(g, p->tileX, p->tileZ));
    p->spanX = p->reachE + p->reachW;
    p->spanZ = p->reachN + p->reachS;
    p->meanReach = (p->reachE + p->reachW + p->reachN + p->reachS) >> 2;
    Fx m = p->reachE;
    if (p->reachW > m) m = p->reachW;
    if (p->reachN > m) m = p->reachN;
    if (p->reachS > m) m = p->reachS;
    p->maxReach = m;
}

static inline void FgProbeField_Analyze(const FgBitGrid *g, FgProbeField *pf, bool fullRays)
{
    pf->cols = (g->w - 1 - FG_PROBE_ORIGIN) / FG_PROBE_STRIDE + 1;
    pf->rows = (g->h - 1 - FG_PROBE_ORIGIN) / FG_PROBE_STRIDE + 1;
    pf->count = pf->cols * pf->rows;
    for (int j = 0; j < pf->rows; ++j)
        for (int i = 0; i < pf->cols; ++i) {
            FgProbe *p = &pf->probes[j * pf->cols + i];
            p->tileX = FG_PROBE_ORIGIN + FG_PROBE_STRIDE * i;
            p->tileZ = FG_PROBE_ORIGIN + FG_PROBE_STRIDE * j;
            FgProbe_FillAxes(g, p);
            if (!p->active || !fullRays) continue;
            {
                int32_t sum = 0;
                Fx m = 0;
                for (int k = 0; k < FG_PROBE_RAYS; ++k) {
                    const Fx r = FgProbe_CastRay(g, p->tileX, p->tileZ, FG_RAY_DIR[k][0], FG_RAY_DIR[k][1], FG_PROBE_MAX_DIST);
                    sum += r;
                    if (r > m) m = r;
                }
                p->meanReach = sum >> 4;
                p->maxReach = m;
            }
        }
}

// After the tile (x, z) of the grid changed, refreshes the axis fields (fullRays false) of exactly the probes that can have changed:
// the ones on the same row or the same column of the probe lattice. The result equals FgProbeField_Analyze(g, pf, false) on the new
// grid, at a few dozen probes of work instead of all of them.
static inline void FgProbeField_TileChanged(const FgBitGrid *g, FgProbeField *pf, int x, int z)
{
    const int dz = z - FG_PROBE_ORIGIN, dx = x - FG_PROBE_ORIGIN;
    if (dz >= 0 && dz % FG_PROBE_STRIDE == 0 && dz / FG_PROBE_STRIDE < pf->rows) {
        FgProbe *row = &pf->probes[(dz / FG_PROBE_STRIDE) * pf->cols];
        for (int i = 0; i < pf->cols; ++i) FgProbe_FillAxes(g, &row[i]);
    }
    if (dx >= 0 && dx % FG_PROBE_STRIDE == 0 && dx / FG_PROBE_STRIDE < pf->cols) {
        const int i = dx / FG_PROBE_STRIDE;
        for (int j = 0; j < pf->rows; ++j) FgProbe_FillAxes(g, &pf->probes[j * pf->cols + i]);
    }
}

#endif
