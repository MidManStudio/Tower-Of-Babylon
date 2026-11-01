// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "spider_ai.c"
// ============================================================================
#include <math.h>
#include <string.h>

#include "spider.h"
#include "spider_math.h"
#include "../dungeon/dungeon_raycast.h"
#include "enemy_types.h"

#define NEAR_RADIUS      9.0f    // closer than this (horizontally) a spider commits to coming down to the floor
#define FAR_RADIUS       15.0f   // it climbs again only after losing the player by this much
#define HUNT_BASE_TILES  12      // path distance at which a spider notices a silent player; every metre of noise adds a tile
#define HUNT_MAX_TILES   120
#define CHASE_SPEED      SPIDER_CHASE_SPEED
#define ATTACK_DIST      1.35f   // a spider closes to this distance from the player, then bites
#define SPIDER_SAFE_TILES 18.0f  // no spawns this close to either arrival point
#define NAV_REFRESH_SECS 0.5f
#define MAX_CANDIDATES   256

static float  g_noise = 22.0f;   // until the game says otherwise, as loud as the old fixed range of 34 tiles was
void Spiders_SetNoise(float metres) { g_noise = metres < 0.0f ? 0.0f : metres; }

static Spider g_spiders[MAX_SPIDERS];
static bool   g_descend[MAX_SPIDERS];   // latched: once the player is close the spider keeps coming down

// Path distance to the player's tile for every open tile.
static int  g_nav[MAP_HEIGHT][MAP_WIDTH];
static int  g_navQueue[MAP_WIDTH * MAP_HEIGHT];
static int  g_navTX = -1, g_navTZ = -1;
static float g_navAge = 0.0f;

static int TileOf(float w) { return (int)floorf((w + TILE_SIZE * 0.5f) / TILE_SIZE); }

static uint32_t Hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static void NavCompute(const FloorMap *f, int px, int pz)
{
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) g_nav[z][x] = -1;
    if (Tile_IsSolid(f, px, pz)) return;

    static const int dxs[4] = { 1, -1, 0, 0 }, dzs[4] = { 0, 0, 1, -1 };
    int head = 0, tail = 0;
    g_nav[pz][px] = 0;
    g_navQueue[tail++] = pz * MAP_WIDTH + px;
    while (head < tail) {
        const int cur = g_navQueue[head++];
        const int cx = cur % MAP_WIDTH, cz = cur / MAP_WIDTH;
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + dxs[k], nz = cz + dzs[k];
            if (Tile_IsSolid(f, nx, nz) || g_nav[nz][nx] >= 0) continue;
            g_nav[nz][nx] = g_nav[cz][cx] + 1;
            g_navQueue[tail++] = nz * MAP_WIDTH + nx;
        }
    }
}

// Centre of the neighbouring tile that is closest to the player by path.
static bool NextWaypoint(const FloorMap *f, int sx, int sz, Vector3 player, Vector3 *out)
{
    const int d0 = g_nav[sz][sx];
    if (d0 < 0) return false;
    if (d0 == 0) { *out = player; return true; }
    int best = d0, bx = sx, bz = sz;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) continue;
            const int nx = sx + dx, nz = sz + dz;
            if (!Tile_InBounds(nx, nz) || g_nav[nz][nx] < 0) continue;
            if (dx && dz && (Tile_IsSolid(f, sx + dx, sz) || Tile_IsSolid(f, sx, sz + dz))) continue;  // no corner cutting
            if (g_nav[nz][nx] < best) { best = g_nav[nz][nx]; bx = nx; bz = nz; }
        }
    if (best == d0) return false;
    *out = V3((float)bx * TILE_SIZE, 0.0f, (float)bz * TILE_SIZE);
    return true;
}

// Horizontal direction toward the closest solid tile within six tiles.
static bool NearestWallDir(const FloorMap *f, Vector3 p, Vector3 *dir)
{
    const int cx = TileOf(p.x), cz = TileOf(p.z);
    float best = 1e30f;
    for (int z = cz - 6; z <= cz + 6; ++z)
        for (int x = cx - 6; x <= cx + 6; ++x) {
            if (!Tile_InBounds(x, z) || !Tile_IsSolid(f, x, z)) continue;
            const float dx = (float)x * TILE_SIZE - p.x, dz = (float)z * TILE_SIZE - p.z;
            const float d = dx * dx + dz * dz;
            if (d < best) { best = d; *dir = V3NormOr(V3(dx, 0.0f, dz), V3(1.0f, 0.0f, 0.0f)); }
        }
    return best < 1e29f;
}

// What the spider wants to do this frame. The locomotion code turns this into
// motion across floors, walls and ceilings.
static void ComputeWant(const Spider *s, int index, const FloorMap *f, Vector3 player, Vector3 *want, float *speed)
{
    *want = s->heading;
    *speed = 0.0f;

    const Vector3 probe = V3Mad(s->anchor, s->normal, 0.7f);
    const int tx = TileOf(probe.x), tz = TileOf(probe.z);
    if (!Tile_InBounds(tx, tz) || g_nav[tz][tx] < 0 || g_nav[tz][tx] > (int)fminf((float)HUNT_MAX_TILES, (float)HUNT_BASE_TILES + g_noise)) return;
    if (V3Len(V3Sub(player, s->pos)) < ATTACK_DIST) return;

    Vector3 wp;
    if (!NextWaypoint(f, tx, tz, player, &wp)) return;
    const Vector3 toWp = V3NormOr(V3(wp.x - s->anchor.x, 0.0f, wp.z - s->anchor.z), s->heading);
    // The distance test has hysteresis, otherwise a spider hovering around the
    // threshold would climb and descend over and over.
    const float hdist = hypotf(player.x - s->anchor.x, player.z - s->anchor.z);
    if (hdist < NEAR_RADIUS) g_descend[index] = true;
    else if (hdist > FAR_RADIUS && s->normal.y > 0.7f) g_descend[index] = false;
    bool near = g_descend[index];

    if (s->normal.y < -0.7f) {                       // ceiling: stalk from above, come down when close
        if (!Dungeon_HasCeiling(TileOf(wp.x), TileOf(wp.z))) near = true;   // the ceiling ends ahead
        Vector3 wd;
        *want = (near && NearestWallDir(f, probe, &wd)) ? wd : toWp;
    } else if (s->normal.y > 0.7f) {                 // floor: follow the path
        *want = toWp;
    } else {                                         // wall: climb while far, descend when near, drift along the path
        const Vector3 t = V3NormOr(V3Cross(V3(0.0f, 1.0f, 0.0f), s->normal), s->heading);
        const Vector3 vert = near ? V3(0.0f, -1.0f, 0.0f) : V3(0.0f, 1.0f, 0.0f);
        *want = V3NormOr(V3Add(vert, V3Scale(t, V3Dot(toWp, t) * 0.6f)), vert);
    }
    *speed = CHASE_SPEED;
}

void Spiders_Init(void)
{
    memset(g_spiders, 0, sizeof g_spiders);
    memset(g_descend, 0, sizeof g_descend);
    g_navTX = g_navTZ = -1;
}

void Spiders_SpawnForFloor(const FloorMap *f, int tier)
{
    memset(g_spiders, 0, sizeof g_spiders);
    memset(g_descend, 0, sizeof g_descend);
    g_navTX = g_navTZ = -1;
    if (!f) return;
    if (tier < 1) tier = 1;

    int cand[MAX_CANDIDATES], n = 0;
    for (int z = 4; z < MAP_HEIGHT - 3 && n < MAX_CANDIDATES; z += 4)
        for (int x = 4; x < MAP_WIDTH - 3 && n < MAX_CANDIDATES; x += 4) {
            const MapCell *c = &f->grid[z][x];
            if ((c->flags & CELL_RESERVED) || c->type == TILE_WALL) continue;
            if (hypotf((float)(x - ARRIVE_BELOW_X), (float)(z - ARRIVE_BELOW_Z)) < SPIDER_SAFE_TILES) continue;
            if (hypotf((float)(x - ARRIVE_ABOVE_X), (float)(z - ARRIVE_ABOVE_Z)) < SPIDER_SAFE_TILES) continue;
            cand[n++] = z * MAP_WIDTH + x;
        }

    uint32_t rng = Hash32(f->seed ^ 0x5B1DE7A9U);
    if (rng == 0) rng = 1;
    for (int i = n - 1; i > 0; --i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const int j = (int)(rng % (uint32_t)(i + 1));
        const int t = cand[i]; cand[i] = cand[j]; cand[j] = t;
    }

    int count = 1 + tier / 4;
    if (count > MAX_SPIDERS) count = MAX_SPIDERS;
    if (count > n) count = n;

    for (int k = 0; k < count; ++k) {
        const int x = cand[k] % MAP_WIDTH, z = cand[k] / MAP_WIDTH;
        const uint32_t h = Hash32(f->seed + (uint32_t)k * 0x9e3779b1U);
        const float a = (float)(h % 628) * 0.01f;
        const bool ceiling = (k % 2 == 0) && Dungeon_HasCeiling(x, z);
        const Vector3 anchor = V3((float)x * TILE_SIZE, ceiling ? WALL_HEIGHT : 0.0f, (float)z * TILE_SIZE);
        const Vector3 normal = ceiling ? V3(0.0f, -1.0f, 0.0f) : V3(0.0f, 1.0f, 0.0f);

        Spider *s = &g_spiders[k];
        Spider_Init(s, f, anchor, normal, V3(cosf(a), 0.0f, sinf(a)));
        s->id = (uint32_t)k;
        s->attackTimer = SPIDER_WINDUP_SECS;   // a fresh spider winds up before its first bite
        InitHealth(&s->health, 90.0f + 30.0f * (float)tier);
        s->tint = (Color){ (unsigned char)(24 + h % 20), (unsigned char)(20 + (h >> 8) % 14), (unsigned char)(26 + (h >> 16) % 20), 255 };
    }
}

void Spiders_Update(float dt, Vector3 playerPos, const FloorMap *f)
{
    if (!f || dt <= 0.0f) return;
    if (dt > 0.2f) dt = 0.2f;

    g_navAge += dt;
    const int ptx = TileOf(playerPos.x), ptz = TileOf(playerPos.z);
    if (Tile_InBounds(ptx, ptz) && (ptx != g_navTX || ptz != g_navTZ || g_navAge > NAV_REFRESH_SECS)) {
        NavCompute(f, ptx, ptz);
        g_navTX = ptx; g_navTZ = ptz; g_navAge = 0.0f;
    }

    for (int i = 0; i < MAX_SPIDERS; ++i) {
        Spider *s = &g_spiders[i];
        if (!s->active) continue;
        UpdateHealthTimer(&s->health, dt);
        if (!s->health.isAlive) { s->active = false; continue; }

        if (s->hurtTimer > 0.0f) s->hurtTimer -= dt;

        Vector3 want; float speed;
        ComputeWant(s, i, f, playerPos, &want, &speed);
        if (s->hurtTimer > 0.0f) speed *= 0.2f;       // staggers when hit
        for (float left = dt; left > 1e-6f && s->active; ) {
            const float h = left < 0.05f ? left : 0.05f;
            Spider_Step(s, f, h, want, speed);
            left -= h;
        }
    }
}

void Spiders_Unload(void) { memset(g_spiders, 0, sizeof g_spiders); }

bool Spiders_CheckDamage(Vector3 pos, float radius, float damage)
{
    for (int i = 0; i < MAX_SPIDERS; ++i) {
        Spider *s = &g_spiders[i];
        if (!s->active) continue;
        Vector3 c[3]; float r[3];
        Spider_BodySpheres(s, c, r);
        for (int k = 0; k < 3; ++k) {
            const Vector3 d = V3Sub(pos, c[k]);
            const float reach = r[k] + radius;
            if (V3Dot(d, d) > reach * reach) continue;
            ApplyDamage(&s->health, damage);
            if (damage > 0.0f) s->hurtTimer = SPIDER_HURT_SECS;
            if (!s->health.isAlive) s->active = false;
            return true;
        }
    }
    return false;
}

bool Spiders_AttackPlayer(float dt, Vector3 playerPos, float playerHeight, float *outDamage, Vector3 *outFrom)
{
    const float playerR = 0.35f;
    const float y0 = playerPos.y + 0.2f, y1 = playerPos.y + playerHeight - 0.2f;
    bool landed = false;

    for (int i = 0; i < MAX_SPIDERS; ++i) {
        Spider *s = &g_spiders[i];
        if (!s->active) continue;

        Vector3 c[3]; float r[3];
        Spider_BodySpheres(s, c, r);
        bool touching = false;
        for (int k = 0; k < 3 && !touching; ++k) {
            float cy = c[k].y;                                   // nearest point on the player's axis
            if (cy < y0) cy = y0;
            if (cy > y1) cy = y1;
            const float dx = c[k].x - playerPos.x, dy = c[k].y - cy, dz = c[k].z - playerPos.z;
            const float reach = r[k] + playerR + SPIDER_REACH;
            if (dx * dx + dy * dy + dz * dz <= reach * reach) touching = true;
        }

        // Animation state: lunge fades on its own, rear follows the last part of the countdown.
        s->lunge -= dt / SPIDER_LUNGE_SECS;
        if (s->lunge < 0.0f) s->lunge = 0.0f;

        if (!touching) {
            s->attackTimer = SPIDER_WINDUP_SECS;
            s->rear -= dt * 4.0f;
            if (s->rear < 0.0f) s->rear = 0.0f;
            continue;
        }
        s->attackTimer -= dt;
        s->rear = s->attackTimer > SPIDER_WINDUP_SECS ? 0.0f : 1.0f - s->attackTimer / SPIDER_WINDUP_SECS;
        if (s->rear < 0.0f) s->rear = 0.0f;
        if (s->attackTimer > 0.0f) continue;
        s->attackTimer = SPIDER_COOLDOWN_SECS;
        s->lunge = 1.0f;
        s->rear = 0.0f;
        if (!landed) {
            landed = true;
            *outDamage = SPIDER_DAMAGE;
            *outFrom = s->pos;
        }
    }
    return landed;
}

int Spiders_MeleeHit(Vector3 eye, float fx, float fz, float reach, float arcDeg, float damage)
{
    const float cosArc = cosf(arcDeg * (PI / 180.0f));
    int hits = 0;
    for (int i = 0; i < MAX_SPIDERS; ++i) {
        Spider *s = &g_spiders[i];
        if (!s->active) continue;

        Vector3 c[3]; float r[3];
        Spider_BodySpheres(s, c, r);
        bool hit = false;
        for (int k = 0; k < 3 && !hit; ++k) {
            const float dx = c[k].x - eye.x, dy = c[k].y - eye.y, dz = c[k].z - eye.z;
            const float d3 = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d3 - r[k] > reach) continue;
            const float dh = sqrtf(dx * dx + dz * dz);
            if (dh > 0.3f && d3 - r[k] > 0.5f && (dx * fx + dz * fz) / dh < cosArc) continue;   // outside the arc
            hit = true;
        }
        if (!hit) continue;

        ApplyDamage(&s->health, damage);
        if (damage > 0.0f) s->hurtTimer = 0.35f;
        if (!s->health.isAlive) s->active = false;
        ++hits;
    }
    return hits;
}

int Spiders_CountActive(void)
{
    int n = 0;
    for (int i = 0; i < MAX_SPIDERS; ++i) if (g_spiders[i].active) ++n;
    return n;
}

const Spider *Spiders_Get(int index)
{
    return (index >= 0 && index < MAX_SPIDERS) ? &g_spiders[index] : NULL;
}
