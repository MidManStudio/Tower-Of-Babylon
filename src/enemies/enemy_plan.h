// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "enemy_plan.h"
// ============================================================================
#ifndef ENEMY_PLAN_H
#define ENEMY_PLAN_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"
#include "../dungeon/dungeon_probes.h"
#include "../dungeon/dungeon_types.h"
#include "../dungeon/floor_access.h"

// The rules a cylinder enemy (a ghoul, G.V.E.X.-X0-01) shares between the single-player game (enemy_manager.c) and the server's world of creatures
// (src/gvex/gvex_world.c): where they spawn on a floor, what they look like, where they can stand and what they can see. Header-only on purpose
// (every test and program lists its sources, and these need nothing but the floor): one copy of the rules, so a session and the solo game cannot drift.

#define ENEMY_LATTICE     3      // candidate spawn tiles sit on every third tile
#define ENEMY_SAFE_TILES  16.0f  // no spawns this close to either arrival point
#define ENEMY_MAX_CANDIDATES 2048        // 256 was enough for 45 by 45; the lattice of a 128 by 128 floor has about 1,700 points

typedef struct { float x, z; uint32_t hash; } EnemySpawn;      // where, and the hash that gives its looks and variant

static inline uint32_t EnemyHash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline int EnemyTileOf(float world) { return (int)floorf((world + TILE_SIZE * 0.5f) / TILE_SIZE); }

// A cylinder of the given radius is blocked if any corner of its bounding square lies in a solid tile.
static inline bool EnemyWalk_Blocked(const FloorMap *map, float x, float z, float r)
{
    return Floor_Solid(map, 0, EnemyTileOf(x - r), EnemyTileOf(z - r)) || Floor_Solid(map, 0, EnemyTileOf(x + r), EnemyTileOf(z - r)) ||
           Floor_Solid(map, 0, EnemyTileOf(x - r), EnemyTileOf(z + r)) || Floor_Solid(map, 0, EnemyTileOf(x + r), EnemyTileOf(z + r));
}

// One ray along the line between the two points, cast with the same DDA the dungeon generator uses. Ray space is tile units with tile centres at +0.5.
static inline bool EnemyWalk_LineOfSight(const FloorMap *map, Vector3 from, Vector3 to)
{
    const float ox = from.x / TILE_SIZE + 0.5f, oz = from.z / TILE_SIZE + 0.5f;
    const float dx = (to.x - from.x) / TILE_SIZE, dz = (to.z - from.z) / TILE_SIZE;
    const float len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-4f) return true;
    return Probe_CastRay(map, ox, oz, dx / len, dz / len, len) >= len;
}

static inline bool EnemyPlan_Candidate(const FloorMap *map, int x, int z)
{
    if (Floor_Reserved(map, x, z)) return false;
    const TileType type = Floor_Type(map, 0, x, z);
    if (type != TILE_FLOOR && type != TILE_CORRIDOR) return false;
    const FloorShell sh = Floor_Shell(map);                                   // the arrival points of a floor of this size
    if (hypotf((float)(x - sh.arriveBelowX), (float)(z - sh.arriveBelowZ)) < ENEMY_SAFE_TILES) return false;
    if (hypotf((float)(x - sh.arriveAboveX), (float)(z - sh.arriveAboveZ)) < ENEMY_SAFE_TILES) return false;
    return true;
}

// The spawns of a floor: `6 + 2 * tier` of them (at most `max`), on a lattice of tiles shuffled from the floor's seed so they spread over the whole
// floor. Returns how many were written. tier is the floor's tier, at least 1.
static inline int EnemyPlan_Spawns(const FloorMap *map, int tier, EnemySpawn *out, int max)
{
    if (!map || tier < 1 || max < 1) return 0;
    int cand[ENEMY_MAX_CANDIDATES];
    int n = 0;
    const int W = map->w;                                                     // a floor's own size, never MAP_WIDTH (dungeon_types.h)
    for (int z = ENEMY_LATTICE; z < map->h - 2 && n < ENEMY_MAX_CANDIDATES; z += ENEMY_LATTICE)
        for (int x = ENEMY_LATTICE; x < W - 2 && n < ENEMY_MAX_CANDIDATES; x += ENEMY_LATTICE)
            if (EnemyPlan_Candidate(map, x, z)) cand[n++] = z * W + x;

    uint32_t rng = EnemyHash32(map->seed ^ 0xE4E31E5Du);
    if (rng == 0) rng = 1;
    for (int i = n - 1; i > 0; --i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const int j = (int)(rng % (uint32_t)(i + 1));
        const int t = cand[i]; cand[i] = cand[j]; cand[j] = t;
    }

    int target = 6 + 2 * tier;
    if (target > max) target = max;
    if (target > n) target = n;
    for (int i = 0; i < target; ++i) {
        out[i].x = (float)(cand[i] % W) * TILE_SIZE;
        out[i].z = (float)(cand[i] / W) * TILE_SIZE;
        out[i].hash = EnemyHash32(map->seed + (uint32_t)i * 0x9e3779b1U);
    }
    return target;
}

// Size and colour from a spawn's hash.
static inline void EnemyLook(uint32_t h, float *height, float *radius, Color *tint)
{
    *height = 2.0f + (float)(h % 100) * 0.018f;
    *radius = 0.40f + (float)((h >> 8) % 100) * 0.003f;
    switch ((h >> 16) % 3) {
        case 0:  *tint = (Color){ 210, 60, 40, 255 };   break;  // hazard orange
        case 1:  *tint = (Color){ 95, 105, 115, 255 };  break;  // inmate steel
        default: *tint = (Color){ 140, 40, 160, 255 };  break;  // alien mutation
    }
}

#endif
