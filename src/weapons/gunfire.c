// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "gunfire.h and gunfire.c"
// ============================================================================
#include "gunfire.h"

#include <math.h>

#include "../dungeon/dungeon_raycast.h"
#include "../projectiles/projectiles.h"

#define AIM_MIN_DIST 2.5f
#define AIM_MAX_DIST 40.0f

static Vector3 Norm(Vector3 v, Vector3 fallback)
{
    const float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 1e-6f ? (Vector3){ v.x / l, v.y / l, v.z / l } : fallback;
}

static Vector3 Cross(Vector3 a, Vector3 b) { return (Vector3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

int Gunfire_Shoot(const WeaponDef *def, const AimFrame *aim, const FloorMap *map, float spreadDeg, uint32_t *rng)
{
    const ProjectileConfig *cfg = ProjectileConfig_Get(def->projectile);
    if (!cfg || def->pellets < 1) return 0;

    // What the crosshair is on.
    float dist = AIM_MAX_DIST;
    DungeonHit hit = { 0 };
    if (map && Dungeon_Raycast(map, aim->eye, aim->forward, AIM_MAX_DIST, &hit) && hit.hit) dist = hit.t;
    if (dist < AIM_MIN_DIST) dist = AIM_MIN_DIST;
    const Vector3 target = { aim->eye.x + aim->forward.x * dist, aim->eye.y + aim->forward.y * dist, aim->eye.z + aim->forward.z * dist };

    // Where it leaves the gun, kept on the near side of any wall between the eye and the muzzle.
    Vector3 origin = Aim_MuzzleFor(aim, def->id);
    {
        const Vector3 toMuzzle = { origin.x - aim->eye.x, origin.y - aim->eye.y, origin.z - aim->eye.z };
        const float len = sqrtf(toMuzzle.x * toMuzzle.x + toMuzzle.y * toMuzzle.y + toMuzzle.z * toMuzzle.z);
        if (map && len > 1e-5f) {
            const Vector3 d = { toMuzzle.x / len, toMuzzle.y / len, toMuzzle.z / len };
            DungeonHit wh = { 0 };
            if (Dungeon_Raycast(map, aim->eye, d, len, &wh) && wh.hit) {
                const float t = wh.t > 0.08f ? wh.t - 0.05f : 0.0f;
                origin = (Vector3){ aim->eye.x + d.x * t, aim->eye.y + d.y * t, aim->eye.z + d.z * t };
            }
        }
    }

    // The straight line from the muzzle to that point is the centre of the spread.
    const Vector3 base = Norm((Vector3){ target.x - origin.x, target.y - origin.y, target.z - origin.z }, aim->forward);
    const Vector3 right = Norm(Cross(base, (Vector3){ 0.0f, 1.0f, 0.0f }), aim->right);
    const Vector3 up = Cross(right, base);

    Vector3 dirs[32];
    int n = def->pellets > 32 ? 32 : def->pellets;
    Pattern_Directions((ShotPattern)def->pattern, n, spreadDeg, base, right, up, rng, dirs);

    int fired = 0;
    for (int i = 0; i < n; ++i) {
        const float speed = cfg->speedMin + (cfg->speedMax - cfg->speedMin) * Rng01(rng);
        if (Projectiles_Spawn(def->projectile, origin, dirs[i], speed, def->damage)) ++fired;
    }
    return fired;
}
