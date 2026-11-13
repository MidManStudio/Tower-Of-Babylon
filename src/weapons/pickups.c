// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "pickups.h and pickups.c"
// ============================================================================
#include "pickups.h"

#include "../flashlight/flashlight.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "parts.h"
#include "weapons.h"

static Pickup g_pickups[MAX_PICKUPS];

bool Pickup_IsAmmo(PickupKind k) { return k == PICKUP_AMMO || k == PICKUP_SHELLS || k == PICKUP_RIFLE_AMMO; }

AmmoType Pickup_AmmoType(PickupKind k)
{
    switch (k) {
        case PICKUP_AMMO:       return AMMO_PISTOL;
        case PICKUP_SHELLS:     return AMMO_SHELLS;
        case PICKUP_RIFLE_AMMO: return AMMO_RIFLE;
        default:                return AMMO_NONE;
    }
}

PickupKind Pickup_ForAmmo(AmmoType t)
{
    switch (t) {
        case AMMO_SHELLS: return PICKUP_SHELLS;
        case AMMO_RIFLE:  return PICKUP_RIFLE_AMMO;
        default:          return PICKUP_AMMO;
    }
}

PickupKind Pickup_ForWeapon(WeaponId id)
{
    switch (id) {
        case WEAPON_PISTOL:  return PICKUP_PISTOL;
        case WEAPON_SHOTGUN: return PICKUP_SHOTGUN;
        case WEAPON_RIFLE:   return PICKUP_RIFLE;
        default:             return PICKUP_PIPE;
    }
}

WeaponId Pickup_Weapon(PickupKind k)
{
    switch (k) {
        case PICKUP_PIPE:    return WEAPON_PIPE;
        case PICKUP_PISTOL:  return WEAPON_PISTOL;
        case PICKUP_SHOTGUN: return WEAPON_SHOTGUN;
        case PICKUP_RIFLE:   return WEAPON_RIFLE;
        default:             return WEAPON_HANDS;
    }
}

static uint32_t g_rng = 1;
static uint32_t Rand(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return g_rng;
}

void Pickups_Clear(void)
{
    for (int i = 0; i < MAX_PICKUPS; ++i) g_pickups[i].active = false;
}

int Pickups_Count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i) n += g_pickups[i].active ? 1 : 0;
    return n;
}

const Pickup *Pickups_Get(int i) { return (i >= 0 && i < MAX_PICKUPS) ? &g_pickups[i] : NULL; }

static void Place(PickupKind kind, int tx, int tz)
{
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        if (g_pickups[i].active) continue;
        g_pickups[i].active = true;
        g_pickups[i].kind = kind;
        g_pickups[i].pos = (Vector3){ (float)tx * TILE_SIZE, 0.0f, (float)tz * TILE_SIZE };
        g_pickups[i].phase = (float)(Rand() % 628) / 100.0f;
        g_pickups[i].count = Pickup_IsAmmo(kind) ? Ammo_BoxAmount(Pickup_AmmoType(kind)) : (kind == PICKUP_FLASHLIGHT ? (int)Flashlight_Config()->startSecs : 1);
        g_pickups[i].ignoreT = 0.0f;
        return;
    }
}

void Pickups_SpawnForFloor(const FloorMap *f, int floorNumber, unsigned owned)
{
    Pickups_Clear();
    g_rng = (f->seed ^ ((uint32_t)floorNumber * 2654435761u)) | 1u;

    // Walking distance from the arrival point over open tiles.
    static int dist[MAP_HEIGHT][MAP_WIDTH];
    static int queue[MAP_WIDTH * MAP_HEIGHT];
    for (int z = 0; z < MAP_HEIGHT; ++z) for (int x = 0; x < MAP_WIDTH; ++x) dist[z][x] = -1;
    const int sx = (int)(f->startX / TILE_SIZE + 0.5f), sz = (int)(f->startZ / TILE_SIZE + 0.5f);
    int head = 0, tail = 0;
    if (!Tile_IsSolid(f, sx, sz)) { dist[sz][sx] = 0; queue[tail++] = sz * MAP_WIDTH + sx; }
    while (head < tail) {
        const int cz = queue[head] / MAP_WIDTH, cx = queue[head] % MAP_WIDTH;
        ++head;
        const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + dx[k], nz = cz + dz[k];
            if (Tile_IsSolid(f, nx, nz) || dist[nz][nx] >= 0) continue;
            dist[nz][nx] = dist[cz][cx] + 1;
            queue[tail++] = nz * MAP_WIDTH + nx;
        }
    }

    // What this floor holds.
    PickupKind list[MAX_PICKUPS];
    int n = 0;
    if (floorNumber <= 1) {
        list[n++] = PICKUP_PIPE;
        list[n++] = PICKUP_PISTOL;
    } else {
        WeaponId w;
        if (floorNumber == 2 && !(owned & WEAPON_BIT(WEAPON_SHOTGUN))) w = WEAPON_SHOTGUN;       // a new gun each of the first floors
        else if (floorNumber == 3 && !(owned & WEAPON_BIT(WEAPON_RIFLE))) w = WEAPON_RIFLE;
        else {
            static const WeaponId pool[4] = { WEAPON_PIPE, WEAPON_PISTOL, WEAPON_SHOTGUN, WEAPON_RIFLE };
            w = pool[Rand() & 3u];
        }
        if (owned & WEAPON_BIT(w)) list[n++] = (w == WEAPON_PIPE) ? PICKUP_AMMO : Pickup_ForAmmo(Weapon_Def(w)->ammo);
        else list[n++] = Pickup_ForWeapon(w);
    }
    list[n++] = PICKUP_AMMO;
    if (floorNumber <= 1) {
        list[n++] = PICKUP_AMMO;
    } else {
        const int kinds = floorNumber >= 3 ? 3 : 2;        // pistol, shells and, from floor 3, rifle rounds
        static const AmmoType types[3] = { AMMO_PISTOL, AMMO_SHELLS, AMMO_RIFLE };
        list[n++] = Pickup_ForAmmo(types[Rand() % (uint32_t)kinds]);
    }
    list[n++] = PICKUP_MEDKIT;
    list[n++] = PICKUP_STIM;
    if (!(owned & OWNED_FLASHLIGHT)) list[n++] = PICKUP_FLASHLIGHT;
    list[n++] = PICKUP_BATTERY;
    if (floorNumber <= 1 || Rand() % 10u < 4u) list[n++] = PICKUP_BATTERY;

    int placedX[MAX_PICKUPS], placedZ[MAX_PICKUPS], placed = 0;
    for (int i = 0; i < n; ++i) {
        for (int tries = 0; tries < 400; ++tries) {
            const int tx = 1 + (int)(Rand() % (uint32_t)(MAP_WIDTH - 2)), tz = 1 + (int)(Rand() % (uint32_t)(MAP_HEIGHT - 2));
            if (dist[tz][tx] < 10) continue;
            if (f->grid[tz][tx].flags & CELL_RESERVED) continue;
            bool apart = true;
            for (int k = 0; k < placed; ++k)
                if (abs(placedX[k] - tx) + abs(placedZ[k] - tz) < 6) { apart = false; break; }
            if (!apart) continue;
            Place(list[i], tx, tz);
            placedX[placed] = tx; placedZ[placed] = tz; ++placed;
            break;
        }
    }
}

void Pickups_Update(float dt)
{
    for (int i = 0; i < MAX_PICKUPS; ++i)
        if (g_pickups[i].active && g_pickups[i].ignoreT > 0.0f) g_pickups[i].ignoreT -= dt;
}

bool Pickups_Drop(const FloorMap *f, Vector3 from, float fx, float fz, PickupKind kind, int count)
{
    static const float steps[4] = { 1.4f, 1.0f, 0.6f, 0.0f };
    for (int s = 0; s < 4; ++s) {
        const float x = from.x + fx * steps[s], z = from.z + fz * steps[s];
        const int tx = (int)floorf(x / TILE_SIZE + 0.5f), tz = (int)floorf(z / TILE_SIZE + 0.5f);
        if (Tile_IsSolid(f, tx, tz)) continue;
        for (int i = 0; i < MAX_PICKUPS; ++i) {
            if (g_pickups[i].active) continue;
            g_pickups[i].active = true;
            g_pickups[i].kind = kind;
            g_pickups[i].pos = (Vector3){ x, 0.0f, z };   // pickups lie on the ground floor; the caller refuses a drop on the stairs
            g_pickups[i].phase = (float)(Rand() % 628) / 100.0f;
            g_pickups[i].count = count;
            g_pickups[i].ignoreT = Pickup_IsAmmo(kind) ? 2.0f : 0.0f;
            return true;
        }
        return false;
    }
    return false;
}

int Pickups_NearestAmmo(Vector3 p, float reach)
{
    int best = -1;
    float bestD = reach * reach;
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        if (!g_pickups[i].active || !Pickup_IsAmmo(g_pickups[i].kind) || g_pickups[i].ignoreT > 0.0f) continue;
        const float dx = p.x - g_pickups[i].pos.x, dz = p.z - g_pickups[i].pos.z;
        const float d = dx * dx + dz * dz;
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

int Pickups_Nearest(Vector3 p, float reach)
{
    int best = -1;
    float bestD = reach * reach;
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        if (!g_pickups[i].active) continue;
        const float dx = p.x - g_pickups[i].pos.x, dz = p.z - g_pickups[i].pos.z;
        const float d = dx * dx + dz * dz;
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

int Pickups_NearestWeapon(Vector3 p, float reach)
{
    int best = -1;
    float bestD = reach * reach;
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        if (!g_pickups[i].active || Pickup_IsAmmo(g_pickups[i].kind)) continue;
        const float dx = p.x - g_pickups[i].pos.x, dz = p.z - g_pickups[i].pos.z;
        const float d = dx * dx + dz * dz;
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

int Pickups_CollectAmmoNear(Vector3 p, float reach)
{
    int n = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        if (!g_pickups[i].active || !Pickup_IsAmmo(g_pickups[i].kind)) continue;
        const float dx = p.x - g_pickups[i].pos.x, dz = p.z - g_pickups[i].pos.z;
        if (g_pickups[i].ignoreT > 0.0f) continue;
        if (dx * dx + dz * dz <= reach * reach) { g_pickups[i].active = false; n += g_pickups[i].count; }
    }
    return n;
}

bool Pickups_Take(int i, PickupKind *kind)
{
    if (i < 0 || i >= MAX_PICKUPS || !g_pickups[i].active) return false;
    if (kind) *kind = g_pickups[i].kind;
    g_pickups[i].active = false;
    return true;
}

const char *Pickup_Label(PickupKind kind)
{
    switch (kind) {
        case PICKUP_PIPE:       return "PIPE";
        case PICKUP_PISTOL:     return "PISTOL";
        case PICKUP_SHOTGUN:    return "SHOTGUN";
        case PICKUP_RIFLE:      return "RIFLE";
        case PICKUP_MEDKIT:     return "MEDKIT";
        case PICKUP_SHELLS:     return "SHELLS";
        case PICKUP_RIFLE_AMMO: return "RIFLE AMMO";
        case PICKUP_FLASHLIGHT: return "FLASHLIGHT";
        case PICKUP_BATTERY:    return "BATTERY";
        case PICKUP_STIM:       return "STIM SHOT";
        default:                return "PISTOL AMMO";
    }
}

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }

void Pickups_Draw(Vector3 viewer, float t)
{
    const Vector3 light = { 0.35f, 0.80f, 0.45f };
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        const Pickup *p = &g_pickups[i];
        if (!p->active) continue;
        if (hypotf(p->pos.x - viewer.x, p->pos.z - viewer.z) > 40.0f) continue;

        const float bob = 0.62f + sinf(t * 2.0f + p->phase) * 0.07f;
        const float ang = t * 1.2f + p->phase;
        const Vector3 dir = V(cosf(ang), 0.0f, sinf(ang));          // the prop's long axis, turning
        const Vector3 side = V(-dir.z, 0.0f, dir.x);
        const Vector3 c = V(p->pos.x, bob, p->pos.z);
        const Vector3 up = V(0, 1, 0);

        // Where it lies: a flat glowing ring and a thin beam, so it can be seen across a dark room.
        Part_Draw(&(Part){ V(p->pos.x, 0.02f, p->pos.z), V(p->pos.x, 0.05f, p->pos.z), 0.55f, 0.50f, 8, 1.0f, 0.0f, up, (Color){ 120, 100, 40, 255 } }, light);
        Part_Draw(&(Part){ V(p->pos.x, 0.05f, p->pos.z), V(p->pos.x, 2.3f, p->pos.z), 0.025f, 0.015f, 4, 1.0f, 0.0f, up, (Color){ 235, 220, 150, 255 } }, light);

        if (p->kind == PICKUP_PIPE) {
            const Vector3 a = V(c.x - dir.x * 0.42f, c.y, c.z - dir.z * 0.42f), b = V(c.x + dir.x * 0.42f, c.y, c.z + dir.z * 0.42f);
            Part_Draw(&(Part){ a, b, 0.032f, 0.032f, 6, 1.0f, 0.0f, up, (Color){ 128, 132, 140, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.42f, c.y, c.z - dir.z * 0.42f), V(c.x - dir.x * 0.22f, c.y, c.z - dir.z * 0.22f), 0.042f, 0.042f, 6, 1.0f, 0.0f, up, (Color){ 30, 30, 34, 255 } }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.30f, c.y, c.z + dir.z * 0.30f), V(c.x + dir.x * 0.34f, c.y, c.z + dir.z * 0.34f), 0.041f, 0.041f, 6, 1.0f, 0.0f, up, (Color){ 170, 30, 30, 255 } }, light);
        } else if (p->kind == PICKUP_PISTOL) {
            const Color dark = (Color){ 50, 52, 58, 255 };
            Part_Draw(&(Part){ V(c.x - dir.x * 0.17f, c.y + 0.03f, c.z - dir.z * 0.17f), V(c.x + dir.x * 0.17f, c.y + 0.03f, c.z + dir.z * 0.17f), 0.03f, 0.03f, 4, 1.15f, 0.78539816f, up, dark }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.10f, c.y + 0.0f, c.z - dir.z * 0.10f), V(c.x - dir.x * 0.13f, c.y - 0.14f, c.z - dir.z * 0.13f), 0.026f, 0.026f, 4, 1.0f, 0.78539816f, side, (Color){ 76, 56, 42, 255 } }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.17f, c.y + 0.03f, c.z + dir.z * 0.17f), V(c.x + dir.x * 0.27f, c.y + 0.03f, c.z + dir.z * 0.27f), 0.014f, 0.014f, 6, 1.0f, 0.0f, up, (Color){ 30, 30, 34, 255 } }, light);
        } else if (p->kind == PICKUP_SHOTGUN) {
            // Long barrel with a tube under it, a wooden pump and stock, and a boxy receiver.
            const Color steel = (Color){ 74, 78, 90, 255 }, dark = (Color){ 44, 46, 52, 255 }, wood = (Color){ 110, 80, 56, 255 };
            Part_Draw(&(Part){ V(c.x - dir.x * 0.08f, c.y + 0.02f, c.z - dir.z * 0.08f), V(c.x + dir.x * 0.46f, c.y + 0.02f, c.z + dir.z * 0.46f), 0.017f, 0.017f, 8, 1.0f, 0.0f, up, steel }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.02f, c.y - 0.025f, c.z - dir.z * 0.02f), V(c.x + dir.x * 0.34f, c.y - 0.025f, c.z + dir.z * 0.34f), 0.014f, 0.014f, 8, 1.0f, 0.0f, up, dark }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.10f, c.y - 0.025f, c.z + dir.z * 0.10f), V(c.x + dir.x * 0.25f, c.y - 0.025f, c.z + dir.z * 0.25f), 0.026f, 0.026f, 4, 0.9f, 0.78539816f, up, wood }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.18f, c.y + 0.0f, c.z - dir.z * 0.18f), V(c.x - dir.x * 0.02f, c.y + 0.0f, c.z - dir.z * 0.02f), 0.032f, 0.032f, 4, 1.2f, 0.78539816f, up, steel }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.46f, c.y - 0.045f, c.z - dir.z * 0.46f), V(c.x - dir.x * 0.18f, c.y - 0.005f, c.z - dir.z * 0.18f), 0.033f, 0.027f, 4, 1.1f, 0.78539816f, up, wood }, light);
        } else if (p->kind == PICKUP_RIFLE) {
            const Color steel = (Color){ 74, 78, 90, 255 }, dark = (Color){ 44, 46, 52, 255 }, black = (Color){ 30, 30, 34, 255 };
            Part_Draw(&(Part){ V(c.x - dir.x * 0.20f, c.y, c.z - dir.z * 0.20f), V(c.x + dir.x * 0.08f, c.y, c.z + dir.z * 0.08f), 0.031f, 0.031f, 4, 1.15f, 0.78539816f, up, dark }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.08f, c.y + 0.012f, c.z + dir.z * 0.08f), V(c.x + dir.x * 0.50f, c.y + 0.012f, c.z + dir.z * 0.50f), 0.011f, 0.011f, 8, 1.0f, 0.0f, up, steel }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.08f, c.y + 0.008f, c.z + dir.z * 0.08f), V(c.x + dir.x * 0.30f, c.y + 0.008f, c.z + dir.z * 0.30f), 0.024f, 0.024f, 4, 0.95f, 0.78539816f, up, steel }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.48f, c.y + 0.012f, c.z + dir.z * 0.48f), V(c.x + dir.x * 0.53f, c.y + 0.012f, c.z + dir.z * 0.53f), 0.017f, 0.017f, 6, 1.0f, 0.0f, up, black }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.44f, c.y - 0.03f, c.z - dir.z * 0.44f), V(c.x - dir.x * 0.20f, c.y - 0.005f, c.z - dir.z * 0.20f), 0.027f, 0.024f, 4, 1.15f, 0.78539816f, up, black }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.01f, c.y - 0.02f, c.z - dir.z * 0.01f), V(c.x + dir.x * 0.03f, c.y - 0.16f, c.z + dir.z * 0.03f), 0.019f, 0.019f, 4, 0.8f, 0.78539816f, side, dark }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.12f, c.y + 0.034f, c.z - dir.z * 0.12f), V(c.x - dir.x * 0.07f, c.y + 0.034f, c.z - dir.z * 0.07f), 0.012f, 0.012f, 4, 1.0f, 0.78539816f, up, black }, light);
        } else if (p->kind == PICKUP_SHELLS) {
            // A red box with a brass band.
            Part_Draw(&(Part){ V(c.x - dir.x * 0.11f, c.y, c.z - dir.z * 0.11f), V(c.x + dir.x * 0.11f, c.y, c.z + dir.z * 0.11f), 0.08f, 0.08f, 4, 0.75f, 0.78539816f, up, (Color){ 150, 38, 34, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.03f, c.y + 0.002f, c.z - dir.z * 0.03f), V(c.x + dir.x * 0.03f, c.y + 0.002f, c.z + dir.z * 0.03f), 0.095f, 0.095f, 4, 0.82f, 0.78539816f, up, (Color){ 214, 178, 60, 255 } }, light);
        } else if (p->kind == PICKUP_RIFLE_AMMO) {
            // A drab box with a brass strip on top.
            Part_Draw(&(Part){ V(c.x - dir.x * 0.11f, c.y, c.z - dir.z * 0.11f), V(c.x + dir.x * 0.11f, c.y, c.z + dir.z * 0.11f), 0.08f, 0.08f, 4, 0.75f, 0.78539816f, up, (Color){ 70, 76, 64, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.09f, c.y + 0.062f, c.z - dir.z * 0.09f), V(c.x + dir.x * 0.09f, c.y + 0.062f, c.z + dir.z * 0.09f), 0.03f, 0.03f, 4, 0.30f, 0.78539816f, up, (Color){ 214, 178, 60, 255 } }, light);
        } else if (p->kind == PICKUP_FLASHLIGHT) {
            // A black body, a silver head and a lit lens.
            Part_Draw(&(Part){ V(c.x - dir.x * 0.13f, c.y, c.z - dir.z * 0.13f), V(c.x + dir.x * 0.07f, c.y, c.z + dir.z * 0.07f), 0.028f, 0.028f, 8, 1.0f, 0.0f, up, (Color){ 40, 42, 48, 255 } }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.07f, c.y, c.z + dir.z * 0.07f), V(c.x + dir.x * 0.15f, c.y, c.z + dir.z * 0.15f), 0.030f, 0.046f, 8, 1.0f, 0.0f, up, (Color){ 138, 142, 154, 255 } }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.15f, c.y, c.z + dir.z * 0.15f), V(c.x + dir.x * 0.158f, c.y, c.z + dir.z * 0.158f), 0.040f, 0.040f, 8, 1.0f, 0.0f, up, (Color){ 255, 233, 160, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.04f, c.y + 0.026f, c.z - dir.z * 0.04f), V(c.x + dir.x * 0.01f, c.y + 0.026f, c.z + dir.z * 0.01f), 0.008f, 0.008f, 4, 1.0f, 0.78539816f, up, (Color){ 200, 70, 50, 255 } }, light);
        } else if (p->kind == PICKUP_BATTERY) {
            // Two cells side by side: green with a gold cap.
            for (int k = -1; k <= 1; k += 2) {
                const Vector3 o = V(side.x * 0.035f * (float)k, 0.0f, side.z * 0.035f * (float)k);
                Part_Draw(&(Part){ V(c.x + o.x - dir.x * 0.07f, c.y, c.z + o.z - dir.z * 0.07f), V(c.x + o.x + dir.x * 0.05f, c.y, c.z + o.z + dir.z * 0.05f), 0.030f, 0.030f, 8, 1.0f, 0.0f, up, (Color){ 40, 120, 70, 255 } }, light);
                Part_Draw(&(Part){ V(c.x + o.x + dir.x * 0.05f, c.y, c.z + o.z + dir.z * 0.05f), V(c.x + o.x + dir.x * 0.075f, c.y, c.z + o.z + dir.z * 0.075f), 0.016f, 0.016f, 8, 1.0f, 0.0f, up, (Color){ 214, 178, 60, 255 } }, light);
            }
        } else if (p->kind == PICKUP_STIM) {
            // A syringe: a cyan barrel with a dark flange and thumb pad, a plunger rod, and a thin needle.
            Part_Draw(&(Part){ V(c.x - dir.x * 0.07f, c.y, c.z - dir.z * 0.07f), V(c.x + dir.x * 0.07f, c.y, c.z + dir.z * 0.07f), 0.024f, 0.024f, 8, 1.0f, 0.0f, up, (Color){ 90, 220, 230, 255 } }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.07f, c.y, c.z + dir.z * 0.07f), V(c.x + dir.x * 0.085f, c.y, c.z + dir.z * 0.085f), 0.040f, 0.040f, 8, 1.0f, 0.0f, up, (Color){ 50, 54, 62, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.14f, c.y, c.z - dir.z * 0.14f), V(c.x - dir.x * 0.07f, c.y, c.z - dir.z * 0.07f), 0.008f, 0.008f, 6, 1.0f, 0.0f, up, (Color){ 190, 194, 204, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.155f, c.y, c.z - dir.z * 0.155f), V(c.x - dir.x * 0.14f, c.y, c.z - dir.z * 0.14f), 0.026f, 0.026f, 8, 1.0f, 0.0f, up, (Color){ 50, 54, 62, 255 } }, light);
            Part_Draw(&(Part){ V(c.x + dir.x * 0.085f, c.y, c.z + dir.z * 0.085f), V(c.x + dir.x * 0.18f, c.y, c.z + dir.z * 0.18f), 0.004f, 0.003f, 6, 1.0f, 0.0f, up, (Color){ 214, 218, 226, 255 } }, light);
        } else if (p->kind == PICKUP_MEDKIT) {
            // A white case with a red cross on top.
            Part_Draw(&(Part){ V(c.x - dir.x * 0.12f, c.y, c.z - dir.z * 0.12f), V(c.x + dir.x * 0.12f, c.y, c.z + dir.z * 0.12f), 0.105f, 0.105f, 4, 0.62f, 0.78539816f, up, (Color){ 226, 226, 220, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.075f, c.y + 0.076f, c.z - dir.z * 0.075f), V(c.x + dir.x * 0.075f, c.y + 0.076f, c.z + dir.z * 0.075f), 0.024f, 0.024f, 4, 0.50f, 0.78539816f, up, (Color){ 190, 28, 28, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - side.x * 0.075f, c.y + 0.076f, c.z - side.z * 0.075f), V(c.x + side.x * 0.075f, c.y + 0.076f, c.z + side.z * 0.075f), 0.024f, 0.024f, 4, 0.50f, 0.78539816f, up, (Color){ 190, 28, 28, 255 } }, light);
        } else {
            Part_Draw(&(Part){ V(c.x - dir.x * 0.11f, c.y, c.z - dir.z * 0.11f), V(c.x + dir.x * 0.11f, c.y, c.z + dir.z * 0.11f), 0.08f, 0.08f, 4, 0.75f, 0.78539816f, up, (Color){ 84, 96, 60, 255 } }, light);
            Part_Draw(&(Part){ V(c.x - dir.x * 0.03f, c.y + 0.002f, c.z - dir.z * 0.03f), V(c.x + dir.x * 0.03f, c.y + 0.002f, c.z + dir.z * 0.03f), 0.095f, 0.095f, 4, 0.82f, 0.78539816f, up, (Color){ 214, 178, 60, 255 } }, light);
        }
    }
}
