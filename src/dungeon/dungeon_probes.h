// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_probes.h"
// ============================================================================
#ifndef DUNGEON_PROBES_H
#define DUNGEON_PROBES_H

#include "dungeon_types.h"

// Probes sit on a sparse lattice. Each one shoots rays through the grid to see
// how far the open space around it runs.
#define PROBE_STRIDE   4
#define PROBE_ORIGIN   2
#define PROBE_COLS     (((MAP_WIDTH  - 1 - PROBE_ORIGIN) / PROBE_STRIDE) + 1)
#define PROBE_ROWS     (((MAP_HEIGHT - 1 - PROBE_ORIGIN) / PROBE_STRIDE) + 1)
#define PROBE_COUNT    (PROBE_COLS * PROBE_ROWS)
#define PROBE_RAYS     16
#define PROBE_MAX_DIST 64.0f

typedef struct {
    int   tileX, tileZ;
    bool  active;                         // false when the probe tile is solid
    float reachE, reachW, reachN, reachS; // free distance to the first wall, in tiles
    float spanX, spanZ;                   // reachE + reachW, reachN + reachS
    float meanReach, maxReach;            // over all rays when fullRays is set
} Probe;

typedef struct {
    Probe probes[PROBE_COUNT];
} ProbeField;

// Grid DDA from (ox, oz) along the unit vector (dx, dz). Positions are in tile
// units with tile (x, z) covering [x, x+1) by [z, z+1). Returns the distance to
// the first solid tile, or maxDist.
float Probe_CastRay(const FloorMap *f, float ox, float oz, float dx, float dz, float maxDist);

// Re-measures every probe. The four axis rays are always cast. With fullRays
// set, PROBE_RAYS evenly spaced rays are cast as well.
void ProbeField_Analyze(const FloorMap *f, ProbeField *pf, bool fullRays);

#endif
