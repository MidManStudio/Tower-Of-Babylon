// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_probes.h"
// ============================================================================
#ifndef DUNGEON_PROBES_H
#define DUNGEON_PROBES_H

#include "dungeon_types.h"

// Grid DDA from (ox, oz) along the unit vector (dx, dz). Positions are in tile
// units with tile (x, z) covering [x, x+1) by [z, z+1). Returns the distance to
// the first solid tile, or maxDist.
//
// This is the float ray the monsters use for line of sight. It is NOT used by the
// generator: the probes that decide a layout are integer only and live in
// src/floorgen/fg_probe.h (see docs/floorgen.md).
float Probe_CastRay(const FloorMap *f, float ox, float oz, float dx, float dz, float maxDist);

#endif
