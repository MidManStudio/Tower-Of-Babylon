// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_probes.c"
// ============================================================================
#include "dungeon_probes.h"

#include <math.h>

#define FAR_T 1e30f

float Probe_CastRay(const FloorMap *f, float ox, float oz, float dx, float dz, float maxDist)
{
    int cx = (int)floorf(ox);
    int cz = (int)floorf(oz);
    if (Tile_IsSolid(f, cx, cz)) return 0.0f;

    const int stepX = dx > 0.0f ? 1 : -1;
    const int stepZ = dz > 0.0f ? 1 : -1;
    const float tDeltaX = dx != 0.0f ? fabsf(1.0f / dx) : FAR_T;
    const float tDeltaZ = dz != 0.0f ? fabsf(1.0f / dz) : FAR_T;
    float tMaxX = dx > 0.0f ? ((float)(cx + 1) - ox) * tDeltaX : (dx < 0.0f ? (ox - (float)cx) * tDeltaX : FAR_T);
    float tMaxZ = dz > 0.0f ? ((float)(cz + 1) - oz) * tDeltaZ : (dz < 0.0f ? (oz - (float)cz) * tDeltaZ : FAR_T);

    for (;;) {
        float t;
        if (tMaxX < tMaxZ) {
            t = tMaxX;
            cx += stepX;
            tMaxX += tDeltaX;
        } else {
            t = tMaxZ;
            cz += stepZ;
            tMaxZ += tDeltaZ;
        }
        if (t >= maxDist) return maxDist;
        if (Tile_IsSolid(f, cx, cz)) return t;
    }
}

void ProbeField_Analyze(const FloorMap *f, ProbeField *pf, bool fullRays)
{
    for (int rz = 0; rz < PROBE_ROWS; ++rz) {
        for (int rx = 0; rx < PROBE_COLS; ++rx) {
            Probe *p = &pf->probes[rz * PROBE_COLS + rx];
            p->tileX = PROBE_ORIGIN + rx * PROBE_STRIDE;
            p->tileZ = PROBE_ORIGIN + rz * PROBE_STRIDE;
            p->active = !Tile_IsSolid(f, p->tileX, p->tileZ);
            p->reachE = p->reachW = p->reachN = p->reachS = 0.0f;
            p->spanX = p->spanZ = p->meanReach = p->maxReach = 0.0f;
            if (!p->active) continue;

            // Rays start at the tile centre.
            const float ox = (float)p->tileX + 0.5f;
            const float oz = (float)p->tileZ + 0.5f;
            p->reachE = Probe_CastRay(f, ox, oz,  1.0f,  0.0f, PROBE_MAX_DIST);
            p->reachW = Probe_CastRay(f, ox, oz, -1.0f,  0.0f, PROBE_MAX_DIST);
            p->reachN = Probe_CastRay(f, ox, oz,  0.0f, -1.0f, PROBE_MAX_DIST);
            p->reachS = Probe_CastRay(f, ox, oz,  0.0f,  1.0f, PROBE_MAX_DIST);
            p->spanX = p->reachE + p->reachW;
            p->spanZ = p->reachN + p->reachS;

            if (!fullRays) {
                p->meanReach = 0.25f * (p->reachE + p->reachW + p->reachN + p->reachS);
                float m = p->reachE;
                if (p->reachW > m) m = p->reachW;
                if (p->reachN > m) m = p->reachN;
                if (p->reachS > m) m = p->reachS;
                p->maxReach = m;
                continue;
            }

            float sum = 0.0f, mx = 0.0f;
            for (int k = 0; k < PROBE_RAYS; ++k) {
                const float a = (float)k * (6.2831853f / (float)PROBE_RAYS);
                const float r = Probe_CastRay(f, ox, oz, cosf(a), sinf(a), PROBE_MAX_DIST);
                sum += r;
                if (r > mx) mx = r;
            }
            p->meanReach = sum / (float)PROBE_RAYS;
            p->maxReach  = mx;
        }
    }
}
