// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/swarm.md, section "swarm.h and swarm.c"
// ============================================================================
#include "swarm.h"

#include <math.h>
#include <string.h>

#include "../dungeon/floor_access.h"
#include "../dungeon/stair_collision.h"
#include "../dungeon/stair_layout.h"

#define SW_HURT_SECS   0.15f
#define SW_MAX_STEP    0.2f      // metres a dot may travel in one step: less than two radii, so a dot cannot jump a wall or another dot
#define SW_PI          3.14159265f

static int TileOf(float world) { return (int)floorf((world + TILE_SIZE * 0.5f) / TILE_SIZE); }

static float RndSigned(Swarm *s)                        // -1 to 1; the same sequence for the same spawn, on every machine
{
    uint32_t x = s->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s->rng = x;
    return (float)(x & 0xffffu) / 32767.5f - 1.0f;
}

void Swarm_Clear(Swarm *s) { memset(s, 0, sizeof *s); s->flowTx = s->flowTz = -1; }

bool Swarm_Alive(const Swarm *s) { return s->state == SWARM_ASLEEP || s->state == SWARM_HUNTING; }
bool Swarm_Active(const Swarm *s) { return s->state != SWARM_NONE; }
unsigned Swarm_HitsLanded(const Swarm *s) { return s->hits; }
unsigned Swarm_KillsLanded(const Swarm *s) { return s->kills; }

// ---- the flow field: how many open tiles from the player ------------------------------------------------------------------
static void FlowUpdate(Swarm *s, const FloorMap *map, Vector3 pos)
{
    const int tx = TileOf(pos.x), tz = TileOf(pos.z);
    if (Floor_Solid(map, 0, tx, tz)) return;                                          // keep the last field
    if (tx == s->flowTx && tz == s->flowTz && s->flowSeed == map->seed && s->flowFloor == map->floorNumber) return;
    for (int z = 0; z < map->h; ++z) for (int x = 0; x < map->w; ++x) s->flow[z][x] = -1;
    static const int dx4[4] = { 1, -1, 0, 0 }, dz4[4] = { 0, 0, 1, -1 };
    // Static, not on the stack: two int arrays of a whole 128 by 128 floor are 128 KB, and the web build's stack is 64 KB.
    static int16_t qx[MAP_MAX * MAP_MAX], qz[MAP_MAX * MAP_MAX];
    int head = 0, tail = 0;
    s->flow[tz][tx] = 0; qx[tail] = (int16_t)tx; qz[tail] = (int16_t)tz; ++tail;
    while (head < tail) {
        const int x = qx[head], z = qz[head]; ++head;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx4[k], nz = z + dz4[k];
            if (Floor_Solid(map, 0, nx, nz) || s->flow[nz][nx] >= 0) continue;
            s->flow[nz][nx] = (int16_t)(s->flow[z][x] + 1);
            qx[tail] = (int16_t)nx; qz[tail] = (int16_t)nz; ++tail;
        }
    }
    s->flowTx = tx; s->flowTz = tz; s->flowSeed = map->seed; s->flowFloor = map->floorNumber;
    s->flowW = map->w; s->flowH = map->h;
}

static int FlowAt(const Swarm *s, float x, float z)             // steps from the tile holding (x, z) to the player, or -1
{
    const int tx = TileOf(x), tz = TileOf(z);
    if (tx < 0 || tz < 0 || tx >= s->flowW || tz >= s->flowH || s->flowTx < 0) return -1;
    return s->flow[tz][tx];
}

// The way a dot at (x, z) should crawl, a unit vector on the floor plane: downhill on the flow field, which goes round walls; straight
// at the target when in the target's tile or when the dot is somewhere the field does not reach.
static void Steer(const Swarm *s, float x, float z, Vector3 target, float *ox, float *oz)
{
    float dx = target.x - x, dz = target.z - z;
    const int tx = TileOf(x), tz = TileOf(z);
    const int here = FlowAt(s, x, z);
    if (here > 0) {
        float gx = 0.0f, gz = 0.0f;
        for (int nz = -1; nz <= 1; ++nz)
            for (int nx = -1; nx <= 1; ++nx) {
                if (!nx && !nz) continue;
                const int ax = tx + nx, az = tz + nz;
                if (ax < 0 || az < 0 || ax >= s->flowW || az >= s->flowH || s->flow[az][ax] < 0) continue;
                if (nx && nz && (s->flow[tz][tx + nx] < 0 || s->flow[tz + nz][tx] < 0)) continue;     // no cutting a corner of a wall
                const float gain = (float)(here - s->flow[az][ax]);
                if (gain <= 0.0f) continue;
                const float inv = (nx && nz) ? 0.70710678f : 1.0f;
                gx += (float)nx * inv * gain; gz += (float)nz * inv * gain;
            }
        if (gx * gx + gz * gz > 1e-6f) { dx = gx; dz = gz; }
    }
    const float l = sqrtf(dx * dx + dz * dz);
    if (l > 1e-5f) { *ox = dx / l; *oz = dz / l; } else { *ox = 0.0f; *oz = 0.0f; }
}

// ---- spawning ----------------------------------------------------------------------------------------------------------------
bool Swarm_SpawnAt(Swarm *s, const SwarmConfig *cfg, float x, float z, uint32_t seed)
{
    if (!cfg || cfg->worms < 1 || cfg->worms > SWARM_MAX_WORMS || cfg->dotsPerWorm < 2 || cfg->worms * cfg->dotsPerWorm > SWARM_MAX_DOTS) return false;
    Swarm_Clear(s);
    s->cfg = *cfg;
    s->worms = cfg->worms; s->dotsPer = cfg->dotsPerWorm; s->count = s->worms * s->dotsPer;
    s->rng = seed ? seed : 0x9e3779b9u;
    for (int i = 0; i < 4; ++i) RndSigned(s);
    const float r = cfg->dotRadius;
    for (int w = 0; w < s->worms; ++w) {
        const float ang = 2.0f * SW_PI * ((float)w + 0.5f * (RndSigned(s) + 1.0f)) / (float)s->worms;
        const float ca = cosf(ang), sa = sinf(ang);
        s->wormSpeed[w] = 1.0f + cfg->speedSpread * RndSigned(s);
        s->wormPhase[w] = SW_PI * (RndSigned(s) + 1.0f);
        const float lift = r + 0.03f * (float)(w % 4);
        const float start = 2.2f * r * sqrtf((float)w + 0.5f);        // each worm starts a little farther out than the last (a spiral), so the heap begins nearly clear
        for (int k = 0; k < s->dotsPer; ++k) {
            const int i = w * s->dotsPer + k;
            const float d = start + cfg->linkLength * 0.9f * (float)k;
            s->p[i] = (Vector3){ x + ca * d, lift + 0.02f * (float)k, z + sa * d };
            s->q[i] = s->p[i]; s->r[i] = s->p[i];
        }
    }
    s->state = SWARM_ASLEEP;
    s->health = cfg->coreHealth;
    s->coreHalf = cfg->coreHalf;
    s->biteT = cfg->biteWindup;
    s->core = (Vector3){ x, cfg->coreHalf > r ? cfg->coreHalf : r, z };
    return true;
}

bool Swarm_Spawn(Swarm *s, const SwarmConfig *cfg, const FloorMap *map, uint32_t seed)
{
    static int cx[MAP_MAX * MAP_MAX], cz[MAP_MAX * MAP_MAX];
    static float cd[MAP_MAX * MAP_MAX];
    int n = 0;
    float farthest = 0.0f;
    const FloorShell sh = Floor_Shell(map);
    for (int z = 1; z < map->h - 1; ++z)
        for (int x = 1; x < map->w - 1; ++x) {
            if (Floor_Reserved(map, x, z)) continue;
            const TileType type = Floor_Type(map, 0, x, z);
            if (type != TILE_FLOOR && type != TILE_CORRIDOR) continue;
            if (x >= sh.wallX && (z <= sh.upAlcoveZ1 || z >= sh.downAlcoveZ0)) continue;     // the alcoves (the stairs stand in them)
            int open = 1;
            for (int dz = -1; dz <= 1 && open; ++dz) for (int dx = -1; dx <= 1; ++dx) if (Floor_Solid(map, 0, x + dx, z + dz)) { open = 0; break; }
            if (!open) continue;
            const float wx = Tile_ToWorld(x), wz = Tile_ToWorld(z);
            const float d1 = hypotf(wx - map->startX, wz - map->startZ), d2 = hypotf(wx - map->arriveAboveX, wz - map->arriveAboveZ);
            const float d = d1 < d2 ? d1 : d2;
            cx[n] = x; cz[n] = z; cd[n] = d; ++n;
            if (d > farthest) farthest = d;
        }
    if (n == 0) return false;
    float need = cfg->spawnMinDist;
    if (farthest < need) need = farthest * 0.5f;                                         // a small floor: the farthest half
    int m = 0;
    for (int i = 0; i < n; ++i) if (cd[i] >= need) { cx[m] = cx[i]; cz[m] = cz[i]; ++m; }
    uint32_t h = seed * 2654435761u + 0x7f4a7c15u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const int pick = (int)(h % (uint32_t)m);
    return Swarm_SpawnAt(s, cfg, Tile_ToWorld(cx[pick]), Tile_ToWorld(cz[pick]), seed);
}

void Swarm_BeginTick(Swarm *s) { if (s->state != SWARM_NONE) memcpy(s->r, s->p, sizeof(Vector3) * (size_t)s->count); }

// ---- the grid that finds neighbours ----------------------------------------------------------------------------------------
static uint32_t CellHash(int ix, int iy, int iz)
{
    return (((uint32_t)ix * 73856093u) ^ ((uint32_t)iy * 19349663u) ^ ((uint32_t)iz * 83492791u)) & (SWARM_HASH - 1);
}

static void BuildGrid(Swarm *s)
{
    const float inv = 1.0f / (2.0f * s->cfg.dotRadius);
    for (int b = 0; b < SWARM_HASH; ++b) s->head[b] = -1;
    for (int i = 0; i < s->count; ++i) {
        const uint32_t b = CellHash((int)floorf(s->p[i].x * inv), (int)floorf(s->p[i].y * inv), (int)floorf(s->p[i].z * inv));
        s->next[i] = s->head[b];
        s->head[b] = i;
    }
}

// Pushes apart every pair of dots that overlap, neighbours in a worm excepted (the chain sets their distance).
static void Separate(Swarm *s)
{
    const float rr = 2.0f * s->cfg.dotRadius, inv = 1.0f / rr;
    for (int i = 0; i < s->count; ++i) {
        const int cx = (int)floorf(s->p[i].x * inv), cy = (int)floorf(s->p[i].y * inv), cz = (int)floorf(s->p[i].z * inv);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    for (int j = s->head[CellHash(cx + dx, cy + dy, cz + dz)]; j >= 0; j = s->next[j]) {
                        if (j <= i) continue;
                        if (j == i + 1 && (i / s->dotsPer) == (j / s->dotsPer)) continue;
                        float vx = s->p[i].x - s->p[j].x, vy = s->p[i].y - s->p[j].y, vz = s->p[i].z - s->p[j].z;
                        float d2 = vx * vx + vy * vy + vz * vz;
                        if (d2 >= rr * rr) continue;
                        if (d2 < 1e-10f) { vx = RndSigned(s) * 1e-3f + 1e-3f; vy = 1e-3f; vz = RndSigned(s) * 1e-3f; d2 = vx * vx + vy * vy + vz * vz; }
                        const float d = sqrtf(d2), k = (rr - d) * 0.5f / d;
                        s->p[i].x += vx * k; s->p[i].y += vy * k; s->p[i].z += vz * k;
                        s->p[j].x -= vx * k; s->p[j].y -= vy * k; s->p[j].z -= vz * k;
                    }
    }
}

// ---- the world ---------------------------------------------------------------------------------------------------------------
static void CollideDot(const Swarm *s, const FloorMap *map, const SwarmTarget *pl, Vector3 *p)
{
    const float r = s->cfg.dotRadius;
    if (p->y < r) p->y = r;
    if (p->y > Floor_RoofY(map) - r) p->y = Floor_RoofY(map) - r;          // the roof is one plane; map may be NULL
    if (map) {
        const int tx = TileOf(p->x), tz = TileOf(p->z);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int ax = tx + dx, az = tz + dz;
                if (!Floor_Solid(map, 0, ax, az)) continue;
                const float x0 = Tile_ToWorld(ax) - TILE_SIZE * 0.5f, x1 = x0 + TILE_SIZE, z0 = Tile_ToWorld(az) - TILE_SIZE * 0.5f, z1 = z0 + TILE_SIZE;
                const float nx = p->x < x0 ? x0 : (p->x > x1 ? x1 : p->x), nz = p->z < z0 ? z0 : (p->z > z1 ? z1 : p->z);
                const float vx = p->x - nx, vz = p->z - nz, d2 = vx * vx + vz * vz;
                if (d2 >= r * r) continue;
                if (d2 > 1e-10f) {                                                         // outside the tile but closer than a radius
                    const float d = sqrtf(d2), k = (r - d) / d;
                    p->x += vx * k; p->z += vz * k;
                } else {                                                                   // the centre is inside the tile: out through the nearest face
                    const float eW = p->x - x0, eE = x1 - p->x, eN = p->z - z0, eS = z1 - p->z;
                    float m = eW; int f = 0;
                    if (eE < m) { m = eE; f = 1; }
                    if (eN < m) { m = eN; f = 2; }
                    if (eS < m) { m = eS; f = 3; }
                    if (f == 0) p->x = x0 - r; else if (f == 1) p->x = x1 + r; else if (f == 2) p->z = z0 - r; else p->z = z1 + r;
                }
            }
        // The flight of stairs stands in the up alcove (x from ALC_X0, z up to UP_Z1): a dot there is pushed out of its steps.
        if (p->x > ALC_X0 - 0.5f && p->z < UP_Z1 + 0.5f) Stair_PushOutSphere(p, r);
    }
    if (pl && pl->radius > 0.0f) {                                                         // the player's body is a cylinder; a dot does not pass through it
        if (p->y > pl->pos.y - r && p->y < pl->pos.y + pl->height + r) {
            const float vx = p->x - pl->pos.x, vz = p->z - pl->pos.z, rad = pl->radius + r, d2 = vx * vx + vz * vz;
            if (d2 < rad * rad) {
                if (d2 > 1e-10f) { const float d = sqrtf(d2), k = (rad - d) / d; p->x += vx * k; p->z += vz * k; }
                else p->x += rad;
            }
        }
    }
}

static void VClamp(Vector3 *v, float maxLen)
{
    const float l2 = v->x * v->x + v->y * v->y + v->z * v->z;
    if (l2 > maxLen * maxLen) { const float k = maxLen / sqrtf(l2); v->x *= k; v->y *= k; v->z *= k; }
}

void Swarm_Step(Swarm *s, float dt, const FloorMap *map, const SwarmTarget *pl)
{
    if (s->state == SWARM_NONE || dt <= 0.0f) return;
    const SwarmConfig *c = &s->cfg;
    s->time += dt;
    if (s->hurtT > 0.0f) s->hurtT -= dt;
    if (s->state == SWARM_DEAD) {
        s->deathT += dt;
        if (s->deathT >= c->deathSecs) { s->state = SWARM_NONE; return; }
    }
    const bool playerOn = pl && pl->alive;
    if (map && playerOn && s->state != SWARM_DEAD) FlowUpdate(s, map, pl->pos);
    if (s->state == SWARM_ASLEEP && playerOn) {                                            // wakes when the player is near by the way a walker would go
        const int d = FlowAt(s, s->core.x, s->core.z);
        if (d >= 0 && (float)d * TILE_SIZE <= c->senseRange) s->state = SWARM_HUNTING;
    }
    const bool goal = s->state == SWARM_HUNTING && playerOn && map;

    // Move every dot: what motion it has (gravity, being pushed), then, when hunting, the one goal. The goal moves a dot without giving it
    // momentum, so the crawl speed is the speed.
    const float damp = 1.0f - (c->damping * dt < 0.9f ? c->damping * dt : 0.9f);
    for (int i = 0; i < s->count; ++i) {
        Vector3 v = { (s->p[i].x - s->q[i].x) * damp, (s->p[i].y - s->q[i].y) * damp, (s->p[i].z - s->q[i].z) * damp };
        VClamp(&v, SW_MAX_STEP);
        s->q[i] = s->p[i];
        s->p[i].x += v.x; s->p[i].y += v.y - c->gravity * dt * dt; s->p[i].z += v.z;
        const int w = i / s->dotsPer, k = i % s->dotsPer;
        if (goal) {
            float dx, dz;
            Steer(s, s->p[i].x, s->p[i].z, pl->pos, &dx, &dz);
            const float a = c->weave * sinf(s->time * 3.0f + s->wormPhase[w] + (float)k * 0.9f), ca = cosf(a), sa = sinf(a);
            const float mx = (dx * ca - dz * sa) * c->crawlSpeed * s->wormSpeed[w] * dt, mz = (dx * sa + dz * ca) * c->crawlSpeed * s->wormSpeed[w] * dt;
            s->p[i].x += mx; s->p[i].z += mz; s->q[i].x += mx; s->q[i].z += mz;
        } else if (s->state == SWARM_ASLEEP && c->squirm > 0.0f) {
            const float mx = RndSigned(s) * c->squirm * dt * 0.25f, mz = RndSigned(s) * c->squirm * dt * 0.25f;
            s->p[i].x += mx; s->p[i].z += mz;
        }
    }

    // Fix what that broke, a few rounds: each link back to its length, overlapping dots apart, then the walls, floor, stairs and the player.
    BuildGrid(s);
    const float link = c->linkLength;
    for (int it = 0; it < c->iterations; ++it) {
        for (int w = 0; w < s->worms; ++w)
            for (int k = 1; k < s->dotsPer; ++k) {
                const int a = w * s->dotsPer + k, b = a - 1;
                const float vx = s->p[a].x - s->p[b].x, vy = s->p[a].y - s->p[b].y, vz = s->p[a].z - s->p[b].z;
                const float d = sqrtf(vx * vx + vy * vy + vz * vz);
                if (d < 1e-6f) continue;
                const float f = (d - link) / d * 0.5f;
                s->p[a].x -= vx * f; s->p[a].y -= vy * f; s->p[a].z -= vz * f;
                s->p[b].x += vx * f; s->p[b].y += vy * f; s->p[b].z += vz * f;
            }
        Separate(s);
        for (int i = 0; i < s->count; ++i) CollideDot(s, map, pl, &s->p[i]);
    }

    // The health box sits in the middle of the swarm (a little behind it, so a shot is not jittered by every wriggle).
    Vector3 m = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < s->count; ++i) { m.x += s->p[i].x; m.y += s->p[i].y; m.z += s->p[i].z; }
    m.x /= (float)s->count; m.y /= (float)s->count; m.z /= (float)s->count;
    if (m.y < s->coreHalf) m.y = s->coreHalf;
    s->core.x += (m.x - s->core.x) * 0.5f; s->core.y += (m.y - s->core.y) * 0.5f; s->core.z += (m.z - s->core.z) * 0.5f;
}

// ---- hurting and being hurt --------------------------------------------------------------------------------------------------
static void TakeDamage(Swarm *s, float damage)
{
    if (damage <= 0.0f) return;
    s->health -= damage;
    s->hurtT = SW_HURT_SECS;
    ++s->hits;
    if (s->state == SWARM_ASLEEP) s->state = SWARM_HUNTING;
    if (s->health <= 0.0f) { s->health = 0.0f; s->state = SWARM_DEAD; s->deathT = 0.0f; ++s->kills; }
}

bool Swarm_Hit(Swarm *s, Vector3 pos, float radius, float damage)
{
    if (!Swarm_Alive(s)) return false;
    const float h = s->coreHalf;
    const float nx = pos.x < s->core.x - h ? s->core.x - h : (pos.x > s->core.x + h ? s->core.x + h : pos.x);
    const float ny = pos.y < s->core.y - h ? s->core.y - h : (pos.y > s->core.y + h ? s->core.y + h : pos.y);
    const float nz = pos.z < s->core.z - h ? s->core.z - h : (pos.z > s->core.z + h ? s->core.z + h : pos.z);
    const float dx = pos.x - nx, dy = pos.y - ny, dz = pos.z - nz;
    if (dx * dx + dy * dy + dz * dz > radius * radius) return false;
    TakeDamage(s, damage);
    return true;
}

int Swarm_MeleeHit(Swarm *s, Vector3 eye, float fx, float fz, float reach, float arcDeg, float damage)
{
    if (!Swarm_Alive(s)) return 0;
    const float h = s->coreHalf;
    if (fabsf(s->core.y - eye.y) > h + 1.0f) return 0;                                    // level with the eye, more or less: the swarm is low and the eye is not
    const float nx = eye.x < s->core.x - h ? s->core.x - h : (eye.x > s->core.x + h ? s->core.x + h : eye.x);
    const float nz = eye.z < s->core.z - h ? s->core.z - h : (eye.z > s->core.z + h ? s->core.z + h : eye.z);
    const float dx = nx - eye.x, dz = nz - eye.z, d = sqrtf(dx * dx + dz * dz);
    if (d > reach) return 0;
    if (d > 0.05f && (fx * dx + fz * dz) / d < cosf(arcDeg * 0.01745329f)) return 0;
    TakeDamage(s, damage);
    return 1;
}

bool Swarm_Bite(Swarm *s, float dt, const SwarmTarget *pl, float *outDamage, Vector3 *outFrom)
{
    const SwarmConfig *c = &s->cfg;
    if (s->state != SWARM_HUNTING || !pl || !pl->alive) { s->biteT = c->biteWindup; return false; }
    const float reach = pl->radius + c->biteReach + c->dotRadius;
    int touching = 0;
    Vector3 at = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < s->count; ++i) {
        if (s->p[i].y < pl->pos.y - c->dotRadius || s->p[i].y > pl->pos.y + pl->height) continue;
        const float dx = s->p[i].x - pl->pos.x, dz = s->p[i].z - pl->pos.z;
        if (dx * dx + dz * dz > reach * reach) continue;
        ++touching; at.x += s->p[i].x; at.y += s->p[i].y; at.z += s->p[i].z;
    }
    if (touching == 0) { s->biteT = c->biteWindup; return false; }
    s->biteT -= dt;
    if (s->biteT > 0.0f) return false;
    s->biteT = c->biteInterval;
    float dmg = (float)touching * c->biteDamage;
    if (dmg > c->biteCap) dmg = c->biteCap;
    if (outDamage) *outDamage = dmg;
    if (outFrom) *outFrom = (Vector3){ at.x / (float)touching, at.y / (float)touching, at.z / (float)touching };
    return true;
}
