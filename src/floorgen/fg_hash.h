// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "fg_hash.h"
// ============================================================================
#ifndef FG_HASH_H
#define FG_HASH_H

#include <stdint.h>

#include "../dungeon/dungeon_types.h"
#include "fg_rng.h"

// A 32 bit FNV-1a over what makes a floor what it is: the generator version, the size, the floor number and seed, every tile's
// type, variant and flags (not the discovered bit), and the counts of the generation report that are results (not the timings,
// not the float). Two builds that make the same floor give the same hash, on any platform; the tests keep golden values.
static inline uint32_t FgHashByte(uint32_t h, uint32_t b) { return (h ^ (b & 0xFFu)) * 16777619u; }
static inline uint32_t FgHashU32(uint32_t h, uint32_t v)
{
    h = FgHashByte(h, v);
    h = FgHashByte(h, v >> 8);
    h = FgHashByte(h, v >> 16);
    return FgHashByte(h, v >> 24);
}

static inline uint32_t FgFloorHash(const FloorMap *f)
{
    uint32_t h = 2166136261u;
    h = FgHashByte(h, 'F');
    h = FgHashByte(h, 'G');
    h = FgHashByte(h, FG_GEN_VERSION);
    h = FgHashByte(h, (uint32_t)f->w);
    h = FgHashByte(h, (uint32_t)f->h);
    h = FgHashU32(h, (uint32_t)f->floorNumber);
    h = FgHashU32(h, f->seed);
    for (int z = 0; z < f->h; ++z)
        for (int x = 0; x < f->w; ++x) {
            const MapCell *c = &f->grid[z][x];
            h = FgHashByte(h, (uint32_t)c->type);
            h = FgHashByte(h, c->variant);
            h = FgHashByte(h, c->flags);
        }
    h = FgHashU32(h, (uint32_t)f->partitions);
    h = FgHashU32(h, (uint32_t)f->pillars);
    h = FgHashU32(h, (uint32_t)f->pathLength);
    h = FgHashU32(h, (uint32_t)f->attempts);
    h = FgHashByte(h, f->usedFallback ? 1u : 0u);
    return h;
}

#endif
