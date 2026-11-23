// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_probes.c"
// ============================================================================
#include "dungeon_probes.h"

#include <math.h>

#include "floor_access.h"

#define FAR_T 1e30f

float Probe_CastRay(const FloorMap *f, float ox, float oz, float dx, float dz, float maxDist)
{
    int cx = (int)floorf(ox);
    int cz = (int)floorf(oz);
    if (Floor_Solid(f, 0, cx, cz)) return 0.0f;

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
        if (Floor_Solid(f, 0, cx, cz)) return t;
    }
}
