// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "enemy_manager.c"
// ============================================================================
#include "enemy_manager.h"

#include <math.h>
#include <string.h>

#include "../dungeon/dungeon_probes.h"
#include "spider.h"

#define ENEMY_LATTICE     3      // candidate spawn tiles sit on every third tile
#define ENEMY_SAFE_TILES  16.0f  // no spawns this close to either arrival point
#define ENEMY_ALERT_SECS  3.0f
#define MAX_CANDIDATES    256

static Enemy g_enemies[MAX_FLOOR_ENEMIES];

static uint32_t Hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static int TileOf(float world) { return (int)floorf((world + TILE_SIZE * 0.5f) / TILE_SIZE); }

// A cylinder of the given radius is blocked if any corner of its bounding
// square lies in a solid tile.
static bool Blocked(const FloorMap *map, float x, float z, float r)
{
    return Tile_IsSolid(map, TileOf(x - r), TileOf(z - r)) || Tile_IsSolid(map, TileOf(x + r), TileOf(z - r)) ||
           Tile_IsSolid(map, TileOf(x - r), TileOf(z + r)) || Tile_IsSolid(map, TileOf(x + r), TileOf(z + r));
}

// One ray along the line between the two points, cast with the same DDA the
// dungeon generator uses. Ray space is tile units with tile centres at +0.5.
static bool HasLineOfSight(const FloorMap *map, Vector3 from, Vector3 to)
{
    const float ox = from.x / TILE_SIZE + 0.5f, oz = from.z / TILE_SIZE + 0.5f;
    const float dx = (to.x - from.x) / TILE_SIZE, dz = (to.z - from.z) / TILE_SIZE;
    const float len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-4f) return true;
    return Probe_CastRay(map, ox, oz, dx / len, dz / len, len) >= len;
}

static float DistTiles(int x, int z, int ax, int az)
{
    return hypotf((float)(x - ax), (float)(z - az));
}

static bool SpawnCandidate(const FloorMap *map, int x, int z)
{
    const MapCell *c = &map->grid[z][x];
    if (c->flags & CELL_RESERVED) return false;
    if (c->type != TILE_FLOOR && c->type != TILE_CORRIDOR) return false;
    if (DistTiles(x, z, ARRIVE_BELOW_X, ARRIVE_BELOW_Z) < ENEMY_SAFE_TILES) return false;
    if (DistTiles(x, z, ARRIVE_ABOVE_X, ARRIVE_ABOVE_Z) < ENEMY_SAFE_TILES) return false;
    return true;
}

void InitEnemiesSystem(void)
{
    memset(g_enemies, 0, sizeof g_enemies);
    Spiders_Init();
}

void SpawnEnemiesForFloor(const FloorMap *floorMap, int tier)
{
    memset(g_enemies, 0, sizeof g_enemies);
    Spiders_SpawnForFloor(floorMap, tier);
    if (!floorMap) return;
    if (tier < 1) tier = 1;

    int cand[MAX_CANDIDATES];
    int n = 0;
    for (int z = ENEMY_LATTICE; z < MAP_HEIGHT - 2 && n < MAX_CANDIDATES; z += ENEMY_LATTICE)
        for (int x = ENEMY_LATTICE; x < MAP_WIDTH - 2 && n < MAX_CANDIDATES; x += ENEMY_LATTICE)
            if (SpawnCandidate(floorMap, x, z)) cand[n++] = z * MAP_WIDTH + x;

    // Shuffle so the enemies spread over the whole floor instead of filling one corner.
    uint32_t rng = Hash32(floorMap->seed ^ 0xE4E31E5Du);
    if (rng == 0) rng = 1;
    for (int i = n - 1; i > 0; --i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const int j = (int)(rng % (uint32_t)(i + 1));
        const int t = cand[i]; cand[i] = cand[j]; cand[j] = t;
    }

    int target = 6 + 2 * tier;
    if (target > MAX_FLOOR_ENEMIES) target = MAX_FLOOR_ENEMIES;
    if (target > n) target = n;

    for (int i = 0; i < target; ++i) {
        const int x = cand[i] % MAP_WIDTH, z = cand[i] / MAP_WIDTH;
        const uint32_t h = Hash32(floorMap->seed + (uint32_t)i * 0x9e3779b1U);
        Enemy *e = &g_enemies[i];

        e->id = (uint32_t)i;
        e->position = (Vector3){ (float)x * TILE_SIZE, 0.0f, (float)z * TILE_SIZE };
        e->height = 2.0f + (float)(h % 100) * 0.018f;
        e->radius = 0.40f + (float)((h >> 8) % 100) * 0.003f;

        switch ((h >> 16) % 3) {
            case 0:  e->tint = (Color){ 210, 60, 40, 255 };   break;  // hazard orange
            case 1:  e->tint = (Color){ 95, 105, 115, 255 };  break;  // inmate steel
            default: e->tint = (Color){ 140, 40, 160, 255 };  break;  // alien mutation
        }

        InitHealth(&e->health, 40.0f + 20.0f * (float)tier);
        e->state = ENEMY_STATE_IDLE;
        e->aggroRange = 14.0f;
        e->attackRange = 1.8f;
        e->moveSpeed = ENEMY_CHASE_SPEED_BASE + ENEMY_CHASE_SPEED_STEP * (float)(tier % 4);
        e->alertTimer = 0.0f;
        e->attackTimer = ENEMY_WINDUP_SECS;   // a fresh enemy winds up before its first blow
        e->hurtTimer = e->rear = e->lunge = 0.0f;
        e->active = true;
    }
}

static float g_playerNoise = 0.0f;
void Enemies_SetPlayerNoise(float metres) { g_playerNoise = metres < 0.0f ? 0.0f : metres; }
float Enemies_PlayerNoise(void) { return g_playerNoise; }

void UpdateEnemiesSystem(float dt, Vector3 playerPos, const FloorMap *floorMap)
{
    Spiders_SetNoise(g_playerNoise);
    Spiders_Update(dt, playerPos, floorMap);
    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) {
        Enemy *e = &g_enemies[i];
        if (!e->active) continue;

        UpdateHealthTimer(&e->health, dt);
        if (e->hurtTimer > 0.0f) e->hurtTimer -= dt;
        if (!e->health.isAlive) {
            e->state = ENEMY_STATE_DEAD;
            e->active = false;
            continue;
        }

        const float dx = playerPos.x - e->position.x;
        const float dz = playerPos.z - e->position.z;
        const float dist = sqrtf(dx * dx + dz * dz);

        const bool sees = dist <= e->aggroRange && floorMap && HasLineOfSight(floorMap, e->position, playerPos);
        const bool hears = dist <= g_playerNoise;          // noise carries through walls
        if (sees || hears) e->alertTimer = ENEMY_ALERT_SECS;
        else if (e->alertTimer > 0.0f) e->alertTimer -= dt;

        if (dist <= e->attackRange)      e->state = ENEMY_STATE_ATTACK;
        else if (e->alertTimer > 0.0f)   e->state = ENEMY_STATE_CHASE;
        else                             e->state = ENEMY_STATE_IDLE;

        if (e->state != ENEMY_STATE_CHASE || dist < 0.01f || !floorMap) continue;
        if (e->hurtTimer > 0.0f) continue;                 // staggered by a hit

        // Axis-separated movement lets an enemy slide along a wall instead of sticking to it.
        const float step = e->moveSpeed * dt;
        const float nx = e->position.x + (dx / dist) * step;
        if (!Blocked(floorMap, nx, e->position.z, e->radius)) e->position.x = nx;
        const float nz = e->position.z + (dz / dist) * step;
        if (!Blocked(floorMap, e->position.x, nz, e->radius)) e->position.z = nz;
    }
}

static Color MixColor(Color a, Color b, float t)
{
    return (Color){ (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
                    (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
                    (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t), 255 };
}

void DrawEnemiesSystem(Vector3 viewer)
{
    Spiders_Draw(viewer);
    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) {
        const Enemy *e = &g_enemies[i];
        if (!e->active) continue;
        if (hypotf(e->position.x - viewer.x, e->position.z - viewer.z) > 40.0f) continue;

        // Before a blow the enemy crouches and swells and turns warm; at the blow it lunges at the
        // player; when hit it flashes white. None of this touches its position or collision.
        const float hurt = e->hurtTimer > 0.0f ? e->hurtTimer / ENEMY_HURT_SECS : 0.0f;
        const float radius = e->radius * (1.0f + 0.22f * e->rear);
        const float height = e->height * (1.0f - 0.10f * e->rear + 0.06f * e->lunge);
        Vector3 pos = e->position;
        pos.x += e->faceX * e->lunge * 0.5f;
        pos.z += e->faceZ * e->lunge * 0.5f;
        Color col = MixColor(e->tint, (Color){ 255, 140, 90, 255 }, 0.6f * e->rear);
        col = MixColor(col, (Color){ 255, 240, 240, 255 }, hurt);

        DrawCylinder(pos, radius, radius, height, 8, col);
        DrawCylinderWires(pos, radius * 1.02f, radius * 1.02f, height, 8, BLACK);   // a little outside, so the lines are not in the surface's depth
    }
}

void UnloadEnemiesSystem(void)
{
    memset(g_enemies, 0, sizeof g_enemies);
    Spiders_Unload();
}

static unsigned g_hitsLanded = 0, g_killsLanded = 0;
unsigned EnemyHitsLanded(void) { return g_hitsLanded; }
unsigned EnemyKillsLanded(void) { return g_killsLanded; }

int EnemiesMeleeHit(const FloorMap *floorMap, Vector3 eye, float fx, float fz, float reach, float arcDeg, float damage, float knockback)
{
    const float cosArc = cosf(arcDeg * (PI / 180.0f));
    int hits = 0;

    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) {
        Enemy *e = &g_enemies[i];
        if (!e->active) continue;
        if (eye.y < e->position.y - 0.3f || eye.y > e->position.y + e->height + 0.3f) continue;

        const float dx = e->position.x - eye.x, dz = e->position.z - eye.z;
        const float dist = sqrtf(dx * dx + dz * dz);
        const float edge = dist - e->radius;
        if (edge > reach) continue;
        if (dist > 0.01f && edge > 0.5f && (dx * fx + dz * fz) / dist < cosArc) continue;   // outside the arc

        ++hits;
        ApplyDamage(&e->health, damage);
        if (damage > 0.0f) {
            e->hurtTimer = ENEMY_HURT_SECS * 1.6f;
            ++g_hitsLanded;
            if (!e->health.isAlive) ++g_killsLanded;
        }
        if (!e->health.isAlive) {
            e->state = ENEMY_STATE_DEAD;
            e->active = false;
            continue;
        }
        // Knock it back along the line from the player, one axis at a time so it slides along walls.
        if (floorMap && knockback > 0.0f && dist > 0.01f) {
            const float nx = e->position.x + dx / dist * knockback, nz = e->position.z + dz / dist * knockback;
            if (!Blocked(floorMap, nx, e->position.z, e->radius)) e->position.x = nx;
            if (!Blocked(floorMap, e->position.x, nz, e->radius)) e->position.z = nz;
        }
    }

    const int spidersBefore = Spiders_CountActive();
    const int spiderHits = Spiders_MeleeHit(eye, fx, fz, reach, arcDeg, damage);
    if (spiderHits > 0 && damage > 0.0f) {
        g_hitsLanded += (unsigned)spiderHits;
        const int died = spidersBefore - Spiders_CountActive();
        if (died > 0) g_killsLanded += (unsigned)died;
    }
    return hits + spiderHits;
}

bool CheckAndHandleEnemyDamage(Vector3 pos, float radius, float damage)
{
    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) {
        Enemy *e = &g_enemies[i];
        if (!e->active) continue;

        const float dx = pos.x - e->position.x;
        const float dz = pos.z - e->position.z;
        const float reach = radius + e->radius;
        if (dx * dx + dz * dz > reach * reach) continue;
        if (pos.y < e->position.y - radius || pos.y > e->position.y + e->height + radius) continue;

        ApplyDamage(&e->health, damage);
        if (damage > 0.0f) {
            e->hurtTimer = ENEMY_HURT_SECS;
            ++g_hitsLanded;
            if (!e->health.isAlive) ++g_killsLanded;
        }
        if (!e->health.isAlive) {
            e->state = ENEMY_STATE_DEAD;
            e->active = false;
        }
        return true;
    }
    const int spidersBefore = Spiders_CountActive();
    const bool spiderHit = Spiders_CheckDamage(pos, radius, damage);
    if (spiderHit && damage > 0.0f) {
        ++g_hitsLanded;
        if (Spiders_CountActive() < spidersBefore) ++g_killsLanded;
    }
    return spiderHit;
}

bool Enemies_NearestAwake(Vector3 from, float maxDist, Vector3 *at, float *dist)
{
    float best = maxDist;
    int pick = -1;
    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) {
        const Enemy *e = &g_enemies[i];
        if (!e->active || (e->state != ENEMY_STATE_CHASE && e->state != ENEMY_STATE_ATTACK)) continue;
        const float dx = e->position.x - from.x, dz = e->position.z - from.z;
        const float d = sqrtf(dx * dx + dz * dz);
        if (d < best) { best = d; pick = i; }
    }
    if (pick < 0) return false;
    if (at) *at = (Vector3){ g_enemies[pick].position.x, g_enemies[pick].position.y + g_enemies[pick].height * 0.5f, g_enemies[pick].position.z };
    if (dist) *dist = best;
    return true;
}

bool EnemiesAttackPlayer(float dt, Vector3 playerPos, float playerHeight, float *outDamage, Vector3 *outFrom)
{
    bool landed = false;

    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) {
        Enemy *e = &g_enemies[i];
        if (!e->active) continue;

        e->lunge -= dt / ENEMY_LUNGE_SECS;
        if (e->lunge < 0.0f) e->lunge = 0.0f;

        const bool inReach = e->state == ENEMY_STATE_ATTACK
                          && playerPos.y < e->position.y + e->height
                          && playerPos.y + playerHeight > e->position.y;
        if (!inReach) {
            e->attackTimer = ENEMY_WINDUP_SECS;
            e->rear -= dt * 4.0f;
            if (e->rear < 0.0f) e->rear = 0.0f;
            continue;
        }

        // Face the player for the animation.
        const float fx = playerPos.x - e->position.x, fz = playerPos.z - e->position.z;
        const float fl = sqrtf(fx * fx + fz * fz);
        if (fl > 0.001f) { e->faceX = fx / fl; e->faceZ = fz / fl; }

        e->attackTimer -= dt;
        e->rear = e->attackTimer > ENEMY_WINDUP_SECS ? 0.0f : 1.0f - e->attackTimer / ENEMY_WINDUP_SECS;
        if (e->rear < 0.0f) e->rear = 0.0f;
        if (e->attackTimer > 0.0f) continue;
        e->attackTimer = ENEMY_COOLDOWN_SECS;
        e->lunge = 1.0f;
        e->rear = 0.0f;
        if (!landed) {
            landed = true;
            *outDamage = CYLINDER_DAMAGE;
            *outFrom = e->position;
        }
    }

    float sd;
    Vector3 sf;
    if (Spiders_AttackPlayer(dt, playerPos, playerHeight, &sd, &sf) && !landed) {
        landed = true;
        *outDamage = sd;
        *outFrom = sf;
    }
    return landed;
}

int CountActiveEnemies(void)
{
    int n = 0;
    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i)
        if (g_enemies[i].active) ++n;
    return n + Spiders_CountActive();
}
