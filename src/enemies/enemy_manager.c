// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "enemy_manager.c"
// ============================================================================
#include "enemy_manager.h"

#include <math.h>
#include <string.h>

#include "../dungeon/dungeon_probes.h"
#include "../dungeon/floor_access.h"
#include "../dungeon/stair_collision.h"
#include "../dungeon/staircase.h"
#include "enemy_plan.h"
#include "spider.h"

#define ENEMY_ALERT_SECS  3.0f

static Enemy g_enemies[MAX_FLOOR_ENEMIES];

static uint32_t Hash32(uint32_t x) { return EnemyHash32(x); }

static float g_rage = 1.0f;     // the stair clock raises it when the top door opens (Enemies_SetRage)

// Fills one enemy: size, colour, health and speed from a hash of its place and the floor's tier.
static void InitEnemy(Enemy *e, uint32_t id, float x, float z, uint32_t h, int tier)
{
    e->id = id;
    e->position = (Vector3){ x, 0.0f, z };
    e->prevPosition = e->position;
    EnemyLook(h, &e->height, &e->radius, &e->tint);

    InitHealth(&e->health, 40.0f + 20.0f * (float)tier);
    e->state = ENEMY_STATE_IDLE;
    e->aggroRange = 14.0f;
    e->attackRange = 1.8f;
    e->moveSpeed = ENEMY_CHASE_SPEED_BASE + ENEMY_CHASE_SPEED_STEP * (float)(tier % 4);
    e->alertTimer = 0.0f;
    e->attackTimer = ENEMY_WINDUP_SECS;   // a fresh enemy winds up before its first blow
    e->hurtTimer = e->rear = e->lunge = 0.0f;
    e->faceX = e->faceZ = 0.0f;
    e->active = true;
}

void InitEnemiesSystem(void)
{
    memset(g_enemies, 0, sizeof g_enemies);
    Spiders_Init();
}

void SpawnEnemiesForFloor(const FloorMap *floorMap, int tier)
{
    g_rage = 1.0f;
    memset(g_enemies, 0, sizeof g_enemies);
    Spiders_SpawnForFloor(floorMap, tier);
    if (!floorMap) return;
    if (tier < 1) tier = 1;

    EnemySpawn plan[MAX_FLOOR_ENEMIES];
    const int count = EnemyPlan_Spawns(floorMap, tier, plan, MAX_FLOOR_ENEMIES);      // enemy_plan.h: shared with the server's world of creatures
    for (int i = 0; i < count; ++i) InitEnemy(&g_enemies[i], (uint32_t)i, plan[i].x, plan[i].z, plan[i].hash, tier);
}

// One more enemy at a place chosen by the game (the unlock console calls them). Returns its slot, or -1 when all are in use.
// alerted: it starts out hunting, as if it had heard the player.
int Enemies_SpawnAt(Vector3 pos, int tier, bool alerted)
{
    static uint32_t made = 0;
    if (tier < 1) tier = 1;
    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) {
        if (g_enemies[i].active) continue;
        InitEnemy(&g_enemies[i], (uint32_t)i, pos.x, pos.z, Hash32(0xC0FFEEu + (made++) * 0x9e3779b1U), tier);
        g_enemies[i].position.y = pos.y;
        g_enemies[i].prevPosition = g_enemies[i].position;
        if (alerted) g_enemies[i].alertTimer = ENEMY_ALERT_SECS * 4.0f;
        return i;
    }
    return -1;
}

// ---- walking on the stairs ---------------------------------------------------------------------------------
#define ENEMY_FALL_SPEED 9.0f      // m/s when the ground drops away under it

static bool StairBlocked(const Enemy *e, float x, float z)
{
    const float r = e->radius < ENEMY_STAIR_CLEARANCE ? e->radius : ENEMY_STAIR_CLEARANCE;
    return Staircase_Blocked(x, z, r, e->position.y, e->height);
}

// An enemy stands on whatever is under it: the floor, or a step of the flight (a step up of a quarter metre is nothing to it).
static void FollowGround(Enemy *e, float dt)
{
    const float gy = Staircase_GroundHeight(e->position.x, e->position.z, e->position.y);
    if (gy >= e->position.y) { e->position.y = gy; return; }
    e->position.y -= ENEMY_FALL_SPEED * dt;
    if (e->position.y < gy) e->position.y = gy;
}

void Enemies_SetRage(float speedMultiplier) { g_rage = speedMultiplier < 1.0f ? 1.0f : speedMultiplier; }

static float g_renderAlpha = 1.0f;
void Enemies_BeginTick(void)
{
    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) if (g_enemies[i].active) g_enemies[i].prevPosition = g_enemies[i].position;
    Spiders_BeginTick();
}
void Enemies_SetRenderAlpha(float alpha)
{
    g_renderAlpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    Spiders_SetRenderAlpha(g_renderAlpha);
}

const Enemy *Enemies_At(int slot) { return (slot >= 0 && slot < MAX_FLOOR_ENEMIES) ? &g_enemies[slot] : NULL; }

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

        const bool sees = dist <= e->aggroRange && floorMap && EnemyWalk_LineOfSight(floorMap, e->position, playerPos);
        const bool hears = dist <= g_playerNoise;          // noise carries through walls
        if (sees || hears) e->alertTimer = ENEMY_ALERT_SECS;
        else if (e->alertTimer > 0.0f) e->alertTimer -= dt;

        if (dist <= e->attackRange)      e->state = ENEMY_STATE_ATTACK;
        else if (e->alertTimer > 0.0f)   e->state = ENEMY_STATE_CHASE;
        else                             e->state = ENEMY_STATE_IDLE;

        FollowGround(e, dt);
        if (e->state != ENEMY_STATE_CHASE || dist < 0.01f || !floorMap) continue;
        if (e->hurtTimer > 0.0f) continue;                 // staggered by a hit

        // Where to walk: straight at the player, unless the stairs are in the way (either of them on the flight), then
        // along the switchback to the next corner, up or down.
        float gx = playerPos.x, gz = playerPos.z, wx, wz;
        if (Stair_ChaseWaypoint(e->position, playerPos, &wx, &wz)) { gx = wx; gz = wz; }
        float mx = gx - e->position.x, mz = gz - e->position.z;
        const float ml = sqrtf(mx * mx + mz * mz);
        if (ml < 0.01f) continue;
        mx /= ml; mz /= ml;

        // Axis-separated movement lets an enemy slide along a wall instead of sticking to it. A block or railing of the flight
        // stops it like a wall, unless it is a step it can climb.
        const float step = e->moveSpeed * g_rage * dt;
        const float nx = e->position.x + mx * step;
        if (!EnemyWalk_Blocked(floorMap, nx, e->position.z, e->radius) && !StairBlocked(e, nx, e->position.z)) e->position.x = nx;
        const float nz = e->position.z + mz * step;
        if (!EnemyWalk_Blocked(floorMap, e->position.x, nz, e->radius) && !StairBlocked(e, e->position.x, nz)) e->position.z = nz;
        FollowGround(e, dt);
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
        Vector3 pos = { e->prevPosition.x + (e->position.x - e->prevPosition.x) * g_renderAlpha,
                        e->prevPosition.y + (e->position.y - e->prevPosition.y) * g_renderAlpha,
                        e->prevPosition.z + (e->position.z - e->prevPosition.z) * g_renderAlpha };
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
    int hits = 0;

    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) {
        Enemy *e = &g_enemies[i];
        if (!e->active) continue;
        if (!EnemyMelee_Reaches(eye, fx, fz, reach, arcDeg, e->position, e->radius, e->height)) continue;     // the rule is shared with the server (enemy_plan.h)
        const float dx = e->position.x - eye.x, dz = e->position.z - eye.z;
        const float dist = sqrtf(dx * dx + dz * dz);

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
            if (!EnemyWalk_Blocked(floorMap, nx, e->position.z, e->radius) && !StairBlocked(e, nx, e->position.z)) e->position.x = nx;
            if (!EnemyWalk_Blocked(floorMap, e->position.x, nz, e->radius) && !StairBlocked(e, e->position.x, nz)) e->position.z = nz;
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
