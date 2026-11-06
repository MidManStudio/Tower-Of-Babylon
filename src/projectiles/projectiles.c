// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectiles.h and projectiles.c"
// ============================================================================
#include "projectiles.h"

#include <math.h>
#include <stddef.h>

#include "rlgl.h"
#include "../dungeon/dungeon_raycast.h"

static Projectile g_pool[MAX_PROJECTILES];

// ---- wall sparks ----------------------------------------------------------------------------------------------
#define MAX_IMPACTS   24
#define IMPACT_SECS   0.22f
#define IMPACT_SPARKS 4
typedef struct {
    bool     active;
    float    age;
    Vector3  pos, normal;
    Vector3  spark[IMPACT_SPARKS];
    unsigned color;
} Impact;
static Impact g_impacts[MAX_IMPACTS];
static uint32_t g_fxRng = 0x2545f491u;

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }

static void Impact_Add(Vector3 pos, Vector3 normal, unsigned color)
{
    int slot = 0;
    for (int i = 0; i < MAX_IMPACTS; ++i) {
        if (!g_impacts[i].active) { slot = i; break; }
        if (g_impacts[i].age > g_impacts[slot].age) slot = i;       // none free: replace the oldest
    }
    Impact *im = &g_impacts[slot];
    im->active = true;
    im->age = 0.0f;
    im->pos = V(pos.x + normal.x * 0.02f, pos.y + normal.y * 0.02f, pos.z + normal.z * 0.02f);   // just off the surface, so it is not inside it
    im->normal = normal;
    im->color = color;
    for (int k = 0; k < IMPACT_SPARKS; ++k) {
        Vector3 v = V(Rng01(&g_fxRng) * 2.0f - 1.0f, Rng01(&g_fxRng) * 2.0f - 1.0f, Rng01(&g_fxRng) * 2.0f - 1.0f);
        v = V(v.x + normal.x * 0.9f, v.y + normal.y * 0.9f, v.z + normal.z * 0.9f);
        const float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        im->spark[k] = l > 1e-5f ? V(v.x / l, v.y / l, v.z / l) : normal;
    }
}

void InitProjectilesSystem(void) { Projectiles_Clear(); }

void Projectiles_Clear(void)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) g_pool[i].alive = 0;
    for (int i = 0; i < MAX_IMPACTS; i++) g_impacts[i].active = false;
}

bool Projectiles_Spawn(int config, Vector3 origin, Vector3 dir, float speed, float damage)
{
    const ProjectileConfig *cfg = ProjectileConfig_Get(config);
    if (!cfg) return false;

    int slot = -1;
    for (int i = 0; i < MAX_PROJECTILES; i++) if (!g_pool[i].alive) { slot = i; break; }
    if (slot < 0) {                                                  // full: replace the one that has flown longest
        slot = 0;
        for (int i = 1; i < MAX_PROJECTILES; i++) if (g_pool[i].lifetime > g_pool[slot].lifetime) slot = i;
    }

    static uint32_t nextId = 1;
    Projectile p = { 0 };
    p.x = p.px = origin.x;  p.y = p.py = origin.y;  p.z = p.pz = origin.z;
    p.vx = dir.x * speed;   p.vy = dir.y * speed;   p.vz = dir.z * speed;
    p.ay = -9.8f * cfg->gravity;
    p.scaleX = p.scaleY = p.scaleZ = 1.0f;
    p.scaleTarget = 1.0f;
    p.maxLifetime = cfg->lifetime;
    p.maxRange = cfg->maxRange;
    p.damage = damage;
    p.configId = (uint16_t)config;
    p.projId = nextId++;
    p.movementType = (uint8_t)cfg->motion;
    p.piercingType = (uint8_t)cfg->piercing;
    p.alive = 1;
    g_pool[slot] = p;
    return true;
}

void UpdateProjectilesSystem(float dt)
{
    TickProjectiles(g_pool, MAX_PROJECTILES, dt);
    for (int i = 0; i < MAX_IMPACTS; i++) {
        if (!g_impacts[i].active) continue;
        g_impacts[i].age += dt;
        if (g_impacts[i].age >= IMPACT_SECS) g_impacts[i].active = false;
    }
}

float Projectile_FalloffDamage(const ProjectileConfig *cfg, float baseDamage, float travel)
{
    if (travel <= cfg->falloffStart || cfg->falloffEnd <= cfg->falloffStart) return baseDamage;
    if (travel >= cfg->falloffEnd) return baseDamage * cfg->falloffMin;
    const float u = (travel - cfg->falloffStart) / (cfg->falloffEnd - cfg->falloffStart);
    return baseDamage * (1.0f + (cfg->falloffMin - 1.0f) * u);
}

#define HIT_STEP     0.25f    // metres between hit tests along a projectile's path
#define PIERCE_SKIP  1.6f     // metres a piercing projectile flies before it can hit again: more than the longest path through a target
                              // (2 x (0.7 m radius + the projectile's), so one target is not hit twice

int ResolveProjectileHits(const FloorMap *map, ProjectileHitFn hit)
{
    int hits = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *p = &g_pool[i];
        if (!p->alive) continue;
        const ProjectileConfig *cfg = ProjectileConfig_Get(p->configId);
        if (!cfg) { p->alive = 0; continue; }

        const Vector3 a = V(p->px, p->py, p->pz);
        const float dx = p->x - p->px, dy = p->y - p->py, dz = p->z - p->pz;
        const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist < 1e-5f) continue;
        const Vector3 dir = V(dx / dist, dy / dist, dz / dist);

        // Where a wall (or the floor or ceiling) stops it, if one does this frame.
        float limit = dist;
        bool wall = false;
        DungeonHit wh = { 0 };
        if (map && Dungeon_Raycast(map, a, dir, dist, &wh) && wh.hit) { limit = wh.t; wall = true; }

        // Targets, from the start of the path to the wall.
        const float base = p->travelDist - dist;
        const int n = (int)ceilf(limit / HIT_STEP) > 0 ? (int)ceilf(limit / HIT_STEP) : 1;
        const int first = (base < 1e-3f) ? 0 : 1;        // a new projectile also tests the place it left the gun
        for (int k = first; k <= n; ++k) {
            const float t = limit * (float)k / (float)n;
            if (base + t < p->noHitUntil) continue;
            const Vector3 pos = V(a.x + dir.x * t, a.y + dir.y * t, a.z + dir.z * t);
            const float dmg = Projectile_FalloffDamage(cfg, p->damage, base + t);
            if (hit(pos, cfg->radius, dmg)) {
                ++hits;
                ++p->collisionCount;
                if (cfg->piercing == PIERCE_NONE || p->collisionCount >= cfg->maxHits) { p->alive = 0; break; }
                p->noHitUntil = base + t + PIERCE_SKIP;
            }
        }
        if (p->alive && wall) {
            p->alive = 0;
            Impact_Add(wh.point, wh.normal, cfg->colorGlow);
        }
    }
    return hits;
}

// ---- drawing ------------------------------------------------------------------------------------------------------
static Color Col(unsigned rgb, int a) { return (Color){ (unsigned char)((rgb >> 16) & 255u), (unsigned char)((rgb >> 8) & 255u), (unsigned char)(rgb & 255u), (unsigned char)(a < 0 ? 0 : (a > 255 ? 255 : a)) }; }

// A flat strip from a to b that always faces the viewer, fading from colour ca at a to cb at b.
static void Ribbon(Vector3 a, Vector3 b, Vector3 viewer, float width, Color ca, Color cb)
{
    const Vector3 axis = V(b.x - a.x, b.y - a.y, b.z - a.z);
    const Vector3 toEye = V(viewer.x - (a.x + b.x) * 0.5f, viewer.y - (a.y + b.y) * 0.5f, viewer.z - (a.z + b.z) * 0.5f);
    Vector3 side = V(axis.y * toEye.z - axis.z * toEye.y, axis.z * toEye.x - axis.x * toEye.z, axis.x * toEye.y - axis.y * toEye.x);
    const float sl = sqrtf(side.x * side.x + side.y * side.y + side.z * side.z);
    if (sl < 1e-7f) return;
    const float k = width * 0.5f / sl;
    side = V(side.x * k, side.y * k, side.z * k);
    if (rlCheckRenderBatchLimit(4)) rlDrawRenderBatchActive();
    rlBegin(RL_QUADS);
    rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex3f(a.x - side.x, a.y - side.y, a.z - side.z);
    rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex3f(a.x + side.x, a.y + side.y, a.z + side.z);
    rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlVertex3f(b.x + side.x, b.y + side.y, b.z + side.z);
    rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlVertex3f(b.x - side.x, b.y - side.y, b.z - side.z);
    rlEnd();
}

static float g_renderLag = 0.0f;
void Projectiles_SetRenderLag(float seconds) { g_renderLag = seconds < 0.0f ? 0.0f : seconds; }

void DrawProjectilesSystem(Vector3 viewer)
{
    if (Projectiles_Active() == 0 && Impacts_Active() == 0) return;

    // Light that adds to what is behind it, drawn last and not written to depth, so a streak in front of an enemy shows
    // and one behind a wall does not.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        const Projectile *p = &g_pool[i];
        if (!p->alive) continue;
        const ProjectileConfig *cfg = ProjectileConfig_Get(p->configId);
        if (!cfg) continue;
        const float sp = sqrtf(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz);
        if (sp < 1e-3f) continue;
        const Vector3 d = V(p->vx / sp, p->vy / sp, p->vz / sp);
        const float back = g_renderLag * sp < p->travelDist ? g_renderLag * sp : p->travelDist;        // drawn between two steps: a little of the flight behind
        const Vector3 head = V(p->x - d.x * back, p->y - d.y * back, p->z - d.z * back);
        const float shown = p->travelDist - back;
        const float len = cfg->trailLength < shown ? cfg->trailLength : shown;                         // no streak behind the gun when it has just left it
        const Vector3 tail = V(head.x - d.x * len, head.y - d.y * len, head.z - d.z * len);
        const float w = cfg->trailWidth;
        Ribbon(head, tail, viewer, w * 3.4f, Col(cfg->colorGlow, 80), Col(cfg->colorGlow, 0));       // the glow around it
        Ribbon(head, tail, viewer, w,        Col(cfg->colorCore, 255), Col(cfg->colorCore, 0));      // the bright line
        Ribbon(V(head.x - d.x * w * 2.5f, head.y - d.y * w * 2.5f, head.z - d.z * w * 2.5f),
               V(head.x + d.x * w * 1.0f, head.y + d.y * w * 1.0f, head.z + d.z * w * 1.0f), viewer, w * 2.8f,
               Col(cfg->colorCore, 255), Col(cfg->colorCore, 255));                                  // the bullet itself
    }

    for (int i = 0; i < MAX_IMPACTS; i++) {
        const Impact *im = &g_impacts[i];
        if (!im->active) continue;
        const float u = im->age / IMPACT_SECS, fade = 1.0f - u;
        const int alpha = (int)(255.0f * fade);
        for (int k = 0; k < IMPACT_SPARKS; ++k) {
            const float out = 0.5f * u * (2.0f - u);                      // flies out fast, then slows
            const Vector3 s0 = V(im->pos.x + im->spark[k].x * out, im->pos.y + im->spark[k].y * out, im->pos.z + im->spark[k].z * out);
            const float tl = 0.10f * fade;
            const Vector3 s1 = V(s0.x - im->spark[k].x * tl, s0.y - im->spark[k].y * tl, s0.z - im->spark[k].z * tl);
            Ribbon(s0, s1, viewer, 0.018f, Col(im->color, alpha), Col(im->color, 0));
        }
        // A short flash straight out of the wall.
        Ribbon(im->pos, V(im->pos.x + im->normal.x * 0.22f * fade, im->pos.y + im->normal.y * 0.22f * fade, im->pos.z + im->normal.z * 0.22f * fade),
               viewer, 0.10f * fade, Col(0xFFFFFF, alpha), Col(im->color, 0));
    }

    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

int Projectiles_Active(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++) n += g_pool[i].alive ? 1 : 0;
    return n;
}

const Projectile *Projectiles_Get(int index) { return (index >= 0 && index < MAX_PROJECTILES) ? &g_pool[index] : NULL; }

int Impacts_Active(void)
{
    int n = 0;
    for (int i = 0; i < MAX_IMPACTS; i++) n += g_impacts[i].active ? 1 : 0;
    return n;
}

void UnloadProjectilesSystem(void) {}
