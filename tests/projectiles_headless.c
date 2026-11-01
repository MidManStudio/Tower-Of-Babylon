// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "tests/projectiles_headless.c"
// ============================================================================
// Projectiles without a window: pellet patterns, damage falloff, flight (gravity, lifetime, range), the swept hit test
// against targets and walls, piercing, the pool, the streaks that are drawn, and Gunfire_Shoot (aim convergence, the
// muzzle against a wall, pellets). Drawing goes through stubs that collect the quads.
// Inside a macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DPROJECTILES_HEADLESS_TEST -Isrc -Iraylib/src tests/projectiles_headless.c src/projectiles/projectiles.c src/projectiles/projectile_config.c src/projectiles/projectile_patterns.c src/projectiles/projectile_simulation.c src/weapons/gunfire.c src/weapons/weapons.c src/dungeon/dungeon_raycast.c src/flashlight/flashlight.c src/config/game_config.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c -lm -o projectiles_headless
// Run:
//   ./projectiles_headless
#ifdef PROJECTILES_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/game_config.h"
#include "projectiles/projectiles.h"
#include "weapons/gunfire.h"

// ---- stubs: the quads that would be drawn are collected as triangles -------------------------------------------------------
typedef struct { Vector3 a, b, c; Color col; } Tri;
#define MAX_TRI 4000
static Tri g_tris[MAX_TRI];
static int g_nTris = 0, g_mode = 0, g_nv = 0, g_additive = 0, g_depthMaskOff = 0, g_cullOff = 0;
static Vector3 g_vtx[4]; static Color g_cols[4]; static Color g_cur;
void rlBegin(int mode) { g_mode = mode; g_nv = 0; }
void rlEnd(void) {}
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { g_cur = (Color){ r, g, b, a }; }
void rlVertex3f(float x, float y, float z)
{
    g_vtx[g_nv] = (Vector3){ x, y, z }; g_cols[g_nv] = g_cur; ++g_nv;
    if (g_mode == 0x0007 && g_nv == 4) {
        if (g_nTris + 2 <= MAX_TRI) {
            g_tris[g_nTris++] = (Tri){ g_vtx[0], g_vtx[1], g_vtx[2], g_cols[0] };
            g_tris[g_nTris++] = (Tri){ g_vtx[0], g_vtx[2], g_vtx[3], g_cols[2] };
        }
        g_nv = 0;
    }
}
bool rlCheckRenderBatchLimit(int n) { (void)n; return false; }
void rlDrawRenderBatchActive(void) {}
void rlDisableDepthMask(void) { g_depthMaskOff = 1; }
void rlEnableDepthMask(void) { g_depthMaskOff = 0; }
void rlDisableBackfaceCulling(void) { g_cullOff = 1; }
void rlEnableBackfaceCulling(void) { g_cullOff = 0; }
void BeginBlendMode(int mode) { g_additive = (mode == BLEND_ADDITIVE); }
void EndBlendMode(void) { g_additive = 0; }

static int g_failed = 0;
static void Check(int ok, const char *what) { printf("  %s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++g_failed; }
static int Near(float a, float b, float eps) { return fabsf(a - b) < eps; }
static float Len(Vector3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static Vector3 Sub(Vector3 a, Vector3 b) { return (Vector3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static const float DT = 1.0f / 60.0f;

// ---- a room to shoot in: tiles 4..14 in x and z are floor (world 7 to 29), everything else is wall ------------------------------
static FloorMap g_map;
static void BuildRoom(void)
{
    memset(&g_map, 0, sizeof g_map);
    for (int z = 0; z < MAP_HEIGHT; ++z) for (int x = 0; x < MAP_WIDTH; ++x) g_map.grid[z][x].type = TILE_WALL;
    for (int z = 4; z <= 14; ++z) for (int x = 4; x <= 14; ++x) g_map.grid[z][x].type = TILE_FLOOR;
}

// Targets for the hit function: spheres.
typedef struct { Vector3 c; float r; int hits; float dmgSum; } Target;
static Target g_targets[4];
static int g_nTargets = 0, g_calls = 0;
static bool FakeHit(Vector3 pos, float radius, float damage)
{
    ++g_calls;
    for (int i = 0; i < g_nTargets; ++i) {
        if (Len(Sub(pos, g_targets[i].c)) <= radius + g_targets[i].r) { ++g_targets[i].hits; g_targets[i].dmgSum += damage; return true; }
    }
    return false;
}
static void ClearTargets(void) { g_nTargets = 0; g_calls = 0; memset(g_targets, 0, sizeof g_targets); }
static void AddTarget(float x, float y, float z, float r) { g_targets[g_nTargets++] = (Target){ { x, y, z }, r, 0, 0.0f }; }

// One frame of the game's projectile step.
static int Step(const FloorMap *map) { UpdateProjectilesSystem(DT); return ResolveProjectileHits(map, FakeHit); }

static AimFrame AimAt(Vector3 eye, Vector3 look)
{
    Camera3D cam = { 0 };
    cam.position = eye;
    cam.target = (Vector3){ eye.x + look.x, eye.y + look.y, eye.z + look.z };
    cam.up = (Vector3){ 0, 1, 0 };
    return Aim_FromCamera(&cam);
}

int main(void)
{
    GameConfig_LoadBaked();
    BuildRoom();
    const int PISTOL = 0, BUCK = 1, RIFLE = 2;      // projectile config indices in projectiles.mdix

    printf("Patterns\n");
    {
        uint32_t seed = 12345, again = 12345;
        float lo = 1.0f, hi = 0.0f;
        for (int i = 0; i < 1000; ++i) { const float r = Rng01(&seed); if (r < lo) lo = r; if (r > hi) hi = r; }
        Check(lo >= 0.0f && hi < 1.0f && hi > 0.99f && lo < 0.01f, "Rng01 stays in [0, 1) and covers it");
        uint32_t s1 = 7, s2 = 7;
        Check(Rng01(&s1) == Rng01(&s2) && Rng01(&s1) == Rng01(&s2), "the same seed gives the same numbers");
        (void)again;
        const Vector3 f = { 0, 0, -1 }, r = { 1, 0, 0 }, u = { 0, 1, 0 };
        Vector3 d[32];
        uint32_t rng = 99;
        Pattern_Directions(SHOT_CONE, 1, 0.0f, f, r, u, &rng, d);
        Check(Near(d[0].x, 0, 1e-6f) && Near(d[0].y, 0, 1e-6f) && Near(d[0].z, -1, 1e-6f), "a single pellet in a 0 degree cone goes exactly forward");
        Pattern_Directions(SHOT_CONE, 32, 5.0f, f, r, u, &rng, d);
        float worst = 0, meanOff = 0; int unit = 1;
        for (int i = 0; i < 32; ++i) {
            const float ang = acosf(-d[i].z) * 180.0f / 3.14159265f;
            if (ang > worst) worst = ang;
            meanOff += ang / 32.0f;
            if (!Near(Len(d[i]), 1.0f, 1e-4f)) unit = 0;
        }
        Check(unit && worst <= 5.0001f, "32 pellets in a 5 degree cone: all unit length, none outside the cone");
        Check(worst > 3.5f && meanOff > 1.5f && meanOff < 4.0f, "and they actually spread over it (not all at the middle)");
        Pattern_Directions(SHOT_FAN, 5, 10.0f, f, r, u, &rng, d);
        Check(Near(d[2].x, 0, 1e-5f) && d[0].x < 0 && d[4].x > 0 && Near(d[0].x, -d[4].x, 1e-5f) && Near(asinf(d[4].x) * 57.29578f, 10.0f, 0.01f),
              "a fan of 5 is symmetric, evenly spaced, and reaches 10 degrees at each edge");
        Pattern_Directions(SHOT_FAN, 1, 10.0f, f, r, u, &rng, d);
        Check(Near(d[0].x, 0, 1e-6f) && Near(d[0].z, -1, 1e-6f), "a fan of one goes straight");
    }

    printf("Damage falloff (pistol_round: full to 25 m, 60%% at 60 m)\n");
    {
        const ProjectileConfig *c = ProjectileConfig_Get(PISTOL);
        Check(Near(Projectile_FalloffDamage(c, 25.0f, 0.0f), 25.0f, 1e-4f) && Near(Projectile_FalloffDamage(c, 25.0f, 25.0f), 25.0f, 1e-4f), "full damage up to the start");
        Check(Near(Projectile_FalloffDamage(c, 25.0f, 42.5f), 20.0f, 1e-3f), "halfway down the slope it is 80%");
        Check(Near(Projectile_FalloffDamage(c, 25.0f, 60.0f), 15.0f, 1e-4f) && Near(Projectile_FalloffDamage(c, 25.0f, 500.0f), 15.0f, 1e-4f), "60% from the end on");
        const ProjectileConfig *b = ProjectileConfig_Get(BUCK);
        Check(Near(Projectile_FalloffDamage(b, 11.0f, 5.0f), 11.0f, 1e-4f) && Projectile_FalloffDamage(b, 11.0f, 14.0f) < 11.0f * 0.6f && Near(Projectile_FalloffDamage(b, 11.0f, 30.0f), 1.65f, 1e-3f),
              "buckshot falls off fast: gone to 15% by 22 m");
    }

    printf("Flight\n");
    {
        InitProjectilesSystem();
        const ProjectileConfig *bc = ProjectileConfig_Get(BUCK);
        Check(Projectiles_Spawn(BUCK, (Vector3){ 0, 2, 0 }, (Vector3){ 0, 0, -1 }, 40.0f, 11.0f) && Projectiles_Active() == 1, "a spawn makes one live projectile");
        Check(!Projectiles_Spawn(99, (Vector3){ 0, 2, 0 }, (Vector3){ 0, 0, -1 }, 40.0f, 11.0f) && Projectiles_Active() == 1, "a bad config index is refused");
        for (int i = 0; i < 30; ++i) UpdateProjectilesSystem(DT);       // 0.5 s
        const Projectile *p = NULL;
        for (int i = 0; i < MAX_PROJECTILES; ++i) if (Projectiles_Get(i)->alive) p = Projectiles_Get(i);
        const float drop = 2.0f - p->y, expect = 0.5f * 9.8f * bc->gravity * 0.25f;
        Check(Near(p->z, -20.0f, 0.4f), "40 m/s for half a second goes 20 m");
        Check(Near(drop, expect, 0.03f) && drop > 0.1f, "buckshot drops by 1/2 g t^2 scaled by its gravity (about 15 cm in half a second)");
        for (int i = 0; i < 10; ++i) UpdateProjectilesSystem(DT);       // past its 0.6 s lifetime
        Check(Projectiles_Active() == 0, "it ends at its lifetime");

        Projectiles_Clear();
        Projectiles_Spawn(RIFLE, (Vector3){ 0, 2, 0 }, (Vector3){ 0, 0, -1 }, 85.0f, 14.0f);
        int frames = 0;
        while (Projectiles_Active() && frames < 200) { UpdateProjectilesSystem(DT); ++frames; }
        Check(frames >= 49 && frames <= 51, "a rifle round ends at its 70 m range (0.82 s) before its 1.2 s lifetime");
        Projectiles_Clear();
        Projectiles_Spawn(RIFLE, (Vector3){ 0, 0.2f, 0 }, (Vector3){ 0, -1, 0 }, 85.0f, 14.0f);
        UpdateProjectilesSystem(DT); UpdateProjectilesSystem(DT);
        Check(Projectiles_Active() == 0, "one that is under the floor ends");
    }

    printf("The swept hit test\n");
    {
        // Frame ends of a rifle round fired from x = 0 are at 1.4167 m steps: 5.667 and 7.083 straddle a target at 6.375, whose
        // reach (0.5 + the round's 0.1) is 5.775 to 6.975. Neither frame end is inside it, so only a test along the path sees it.
        const float cx = 6.375f;
        Check(Len(Sub((Vector3){ 85.0f / 60.0f * 4.0f, 1.5f, 0 }, (Vector3){ cx, 1.5f, 0 })) > 0.6f
              && Len(Sub((Vector3){ 85.0f / 60.0f * 5.0f, 1.5f, 0 }, (Vector3){ cx, 1.5f, 0 })) > 0.6f, "set-up: no frame end lands inside the target");
        InitProjectilesSystem(); ClearTargets();
        AddTarget(cx, 1.5f, 0.0f, 0.5f);
        Projectiles_Spawn(RIFLE, (Vector3){ 0, 1.5f, 0 }, (Vector3){ 1, 0, 0 }, 85.0f, 14.0f);
        int total = 0;
        for (int i = 0; i < 12; ++i) total += Step(NULL);
        Check(total == 1 && g_targets[0].hits == 1 && Projectiles_Active() == 0, "a fast round that jumps over the target between two frames still hits it, once, and is spent");
        Check(Near(g_targets[0].dmgSum, 14.0f, 1e-3f), "at 6 m it does full damage: 14");

        // Falloff reaches the target: the pistol far down the slope.
        InitProjectilesSystem(); ClearTargets();
        AddTarget(50.0f, 1.5f, 0.0f, 0.5f);
        Projectiles_Spawn(PISTOL, (Vector3){ 0, 1.5f, 0 }, (Vector3){ 1, 0, 0 }, 55.0f, 25.0f);
        for (int i = 0; i < 80; ++i) Step(NULL);
        Check(g_targets[0].hits == 1 && g_targets[0].dmgSum < 25.0f * 0.80f && g_targets[0].dmgSum > 25.0f * 0.6f, "a pistol round at 50 m does less than full damage but more than the floor (about 69%)");

        // A wall in the way.
        InitProjectilesSystem(); ClearTargets();
        g_map.grid[9][10].type = TILE_WALL;                              // a wall tile at world x 19..21, z 17..19
        AddTarget(24.0f, 1.5f, 18.0f, 0.6f);                             // behind it
        Projectiles_Spawn(RIFLE, (Vector3){ 10.0f, 1.5f, 18.0f }, (Vector3){ 1, 0, 0 }, 85.0f, 14.0f);
        int hit = 0, frames = 0;
        while (Projectiles_Active() && frames < 30) { hit += Step(&g_map); ++frames; }
        Check(hit == 0 && g_targets[0].hits == 0 && Projectiles_Active() == 0, "a round that meets a wall stops there: the target behind it is not hit");
        Check(Impacts_Active() == 1, "and leaves a spark");
        g_map.grid[9][10].type = TILE_FLOOR;
        for (int i = 0; i < 20; ++i) UpdateProjectilesSystem(DT);
        Check(Impacts_Active() == 0, "the spark fades");

        // A target that is in front of the wall is hit first.
        InitProjectilesSystem(); ClearTargets();
        g_map.grid[9][10].type = TILE_WALL;
        AddTarget(15.0f, 1.5f, 18.0f, 0.5f);
        Projectiles_Spawn(RIFLE, (Vector3){ 10.0f, 1.5f, 18.0f }, (Vector3){ 1, 0, 0 }, 85.0f, 14.0f);
        for (int i = 0; i < 30; ++i) Step(&g_map);
        Check(g_targets[0].hits == 1 && Impacts_Active() == 0, "a target in front of a wall is hit, and the round does not go on to spark the wall");
        g_map.grid[9][10].type = TILE_FLOOR;

        // The floor and the ceiling.
        InitProjectilesSystem(); ClearTargets();
        Projectiles_Spawn(PISTOL, (Vector3){ 10.0f, 1.5f, 18.0f }, (Vector3){ 0.6f, -0.8f, 0 }, 55.0f, 25.0f);
        for (frames = 0; Projectiles_Active() && frames < 20; ++frames) Step(&g_map);
        Check(Projectiles_Active() == 0 && Impacts_Active() == 1, "a shot into the floor ends with a spark");
        InitProjectilesSystem();
        Projectiles_Spawn(PISTOL, (Vector3){ 10.0f, 1.5f, 18.0f }, (Vector3){ 0.3f, 0.95f, 0 }, 55.0f, 25.0f);
        for (frames = 0; Projectiles_Active() && frames < 20; ++frames) Step(&g_map);
        Check(Projectiles_Active() == 0 && Impacts_Active() == 1, "and into the ceiling");

        // Hits on the first frame count too: a target right at the muzzle.
        InitProjectilesSystem(); ClearTargets();
        AddTarget(10.3f, 1.5f, 18.0f, 0.5f);
        Projectiles_Spawn(PISTOL, (Vector3){ 10.0f, 1.5f, 18.0f }, (Vector3){ 1, 0, 0 }, 55.0f, 25.0f);
        Step(&g_map);
        Check(g_targets[0].hits == 1, "a target at point-blank range is hit on the first frame");
    }

    printf("Piercing\n");
    {
        ProjectileConfig pc = *ProjectileConfig_Get(RIFLE);
        strcpy(pc.id, "piercer");
        pc.piercing = PIERCE_PIERCER; pc.maxHits = 2;
        ProjectileConfig all[4]; int n = 0;
        for (int i = 0; i < 3; ++i) all[n++] = *ProjectileConfig_Get(i);
        all[n++] = pc;
        ProjectileConfig_SetAll(all, n);
        InitProjectilesSystem(); ClearTargets();
        AddTarget(5.0f, 1.5f, 0.0f, 0.5f); AddTarget(8.0f, 1.5f, 0.0f, 0.5f); AddTarget(11.0f, 1.5f, 0.0f, 0.5f);
        Projectiles_Spawn(3, (Vector3){ 0, 1.5f, 0 }, (Vector3){ 1, 0, 0 }, 85.0f, 14.0f);
        int total = 0;
        for (int i = 0; i < 30; ++i) total += Step(NULL);
        Check(total == 2 && g_targets[0].hits == 1 && g_targets[1].hits == 1 && g_targets[2].hits == 0, "a piercer with max_hits 2 goes through the first target, hits the second and stops; each exactly once");
        Check(Projectiles_Active() == 0, "and is spent");
        n = 0;
        for (int i = 0; i < 3; ++i) all[n++] = *ProjectileConfig_Get(i);
        ProjectileConfig_SetAll(all, n);
        GameConfig_LoadBaked();                                          // the real table again
    }

    printf("The pool\n");
    {
        InitProjectilesSystem();
        for (int i = 0; i < MAX_PROJECTILES; ++i) {
            Projectiles_Spawn(RIFLE, (Vector3){ (float)i, 2, 0 }, (Vector3){ 0, 0, -1 }, 85.0f, 14.0f);
            UpdateProjectilesSystem(0.001f);                             // ages the older ones a little
        }
        Check(Projectiles_Active() == MAX_PROJECTILES, "the pool fills");
        Projectiles_Spawn(RIFLE, (Vector3){ 500, 2, 0 }, (Vector3){ 0, 0, -1 }, 85.0f, 14.0f);
        int replaced = 0, found = 0;
        for (int i = 0; i < MAX_PROJECTILES; ++i) { const Projectile *p = Projectiles_Get(i); if (p->x == 500.0f) found = 1; if (p->x == 0.0f && p->alive) replaced = 0; }
        for (int i = 0; i < MAX_PROJECTILES; ++i) if (Projectiles_Get(i)->x == 0.0f) replaced = 1;
        Check(found && !replaced && Projectiles_Active() == MAX_PROJECTILES, "a spawn into a full pool replaces the oldest, never fails");
    }

    printf("Drawing\n");
    {
        InitProjectilesSystem();
        g_nTris = 0;
        DrawProjectilesSystem((Vector3){ 0, 1.6f, 0 });
        Check(g_nTris == 0, "nothing is drawn when nothing is flying");
        Projectiles_Spawn(RIFLE, (Vector3){ 3, 1.5f, -8 }, (Vector3){ 0, 0, -1 }, 85.0f, 14.0f);
        UpdateProjectilesSystem(DT);                                     // flown 1.4 m: less than the 1.8 m streak
        const Vector3 viewer = { 0, 1.6f, 0 };
        g_nTris = 0;
        DrawProjectilesSystem(viewer);
        Check(g_nTris == 6, "one projectile is three strips (glow, line, bullet): six triangles");
        Check(g_depthMaskOff == 0 && g_additive == 0 && g_cullOff == 0, "the render state is put back after drawing");
        // A streak turns about its own axis to show its face to the viewer: its normal is square to the flight direction and
        // points along the part of the line of sight that is square to it. (Seen from behind, a streak flying straight away
        // is seen end-on and shrinks to the dot of its width: that is how a tracer looks.)
        int facing = 1;
        const Vector3 axis = { 0, 0, -1 };
        for (int i = 0; i < g_nTris; ++i) {
            const Vector3 e1 = Sub(g_tris[i].b, g_tris[i].a), e2 = Sub(g_tris[i].c, g_tris[i].a);
            const Vector3 nrm = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
            const Vector3 ctr = { (g_tris[i].a.x + g_tris[i].b.x + g_tris[i].c.x) / 3, (g_tris[i].a.y + g_tris[i].b.y + g_tris[i].c.y) / 3, (g_tris[i].a.z + g_tris[i].b.z + g_tris[i].c.z) / 3 };
            const Vector3 toEye = Sub(viewer, ctr);
            const float along = Dot(toEye, axis);
            const Vector3 perp = { toEye.x - axis.x * along, toEye.y - axis.y * along, toEye.z - axis.z * along };
            if (fabsf(Dot(nrm, axis)) / Len(nrm) > 1e-3f || fabsf(Dot(nrm, perp)) / (Len(nrm) * Len(perp)) < 0.999f) facing = 0;
        }
        Check(facing, "every strip is square to the flight direction and turned to show its face to the viewer");
        // The line strip is the second ribbon (triangles 2 and 3): its far end should be the head minus the streak, but
        // the streak is cut to the distance flown (1.4167 m) so it does not reach behind the gun.
        float zMax = -1e9f, zMin = 1e9f;
        for (int i = 2; i < 4; ++i) { const Vector3 *v[3] = { &g_tris[i].a, &g_tris[i].b, &g_tris[i].c }; for (int k = 0; k < 3; ++k) { if (v[k]->z > zMax) zMax = v[k]->z; if (v[k]->z < zMin) zMin = v[k]->z; } }
        Check(Near(zMin, -8.0f - 85.0f / 60.0f, 0.01f) && Near(zMax, -8.0f, 0.01f), "a round that has flown 1.4 m draws a 1.4 m streak, not 1.8 m, and none behind the muzzle");
        for (int i = 0; i < 10; ++i) UpdateProjectilesSystem(DT);
        g_nTris = 0;
        DrawProjectilesSystem(viewer);
        zMax = -1e9f; zMin = 1e9f;
        for (int i = 2; i < 4; ++i) { const Vector3 *v[3] = { &g_tris[i].a, &g_tris[i].b, &g_tris[i].c }; for (int k = 0; k < 3; ++k) { if (v[k]->z > zMax) zMax = v[k]->z; if (v[k]->z < zMin) zMin = v[k]->z; } }
        Check(Near(zMax - zMin, 1.8f, 0.01f), "later it is the config's full 1.8 m");
        Check(g_tris[2].col.a == 255 && g_tris[3].col.a == 0, "the line is solid at the head and fades to nothing at the tail");
    }

    printf("Gunfire\n");
    {
        InitProjectilesSystem();
        const WeaponDef *pistol = Weapon_Def(WEAPON_PISTOL), *shotgun = Weapon_Def(WEAPON_SHOTGUN), *rifle = Weapon_Def(WEAPON_RIFLE);
        uint32_t rng = 4242;
        // The player is in the room at x = 18, z = 26, looking along -z at the wall at z = 7 (tile 4's edge is 7): 19 m away.
        AimFrame aim = AimAt((Vector3){ 18.0f, 1.6f, 26.0f }, (Vector3){ 0, 0, -1 });
        Check(Gunfire_Shoot(pistol, &aim, &g_map, 0.0f, &rng) == 1 && Projectiles_Active() == 1, "the pistol fires one projectile");
        int frames = 0;
        float xAtWall = 99;
        while (Projectiles_Active() && frames < 100) {
            const Projectile *p = NULL;
            for (int i = 0; i < MAX_PROJECTILES; ++i) if (Projectiles_Get(i)->alive) p = Projectiles_Get(i);
            if (p) xAtWall = p->x;
            Step(&g_map); ++frames;
        }
        Check(Near(xAtWall, 18.0f, 0.05f), "the shot converges on the crosshair: it arrives at the wall on the view line, not 17 cm to the side of it");
        Check(Impacts_Active() == 1, "and sparks the wall");

        InitProjectilesSystem();
        Check(Gunfire_Shoot(shotgun, &aim, &g_map, 4.5f, &rng) == 8 && Projectiles_Active() == 8, "the shotgun fires 8 pellets");
        // The centre of the spread is the line from the muzzle to the crosshair point 19 m away (the wall), not straight ahead.
        // Each weapon has its own muzzle, so its own centre line to the crosshair point.
        Vector3 toCross = Sub((Vector3){ 18.0f, 1.6f, 7.0f }, Aim_MuzzleFor(&aim, WEAPON_SHOTGUN));
        Vector3 base = { toCross.x / Len(toCross), toCross.y / Len(toCross), toCross.z / Len(toCross) };
        float maxAngle = 0, minSpeed = 1e9f, maxSpeed = 0; int distinct = 0;
        Vector3 first = { 0, 0, 0 };
        for (int i = 0, k = 0; i < MAX_PROJECTILES; ++i) {
            const Projectile *p = Projectiles_Get(i);
            if (!p->alive) continue;
            const Vector3 v = { p->vx, p->vy, p->vz };
            const float sp = Len(v);
            if (sp < minSpeed) minSpeed = sp;
            if (sp > maxSpeed) maxSpeed = sp;
            const float ang = acosf(Dot(v, base) / sp) * 57.29578f;
            if (ang > maxAngle) maxAngle = ang;
            if (k == 0) first = v; else if (Len(Sub(v, first)) > 0.01f) ++distinct;
            ++k;
        }
        Check(maxAngle <= 4.5f + 0.01f && maxAngle > 1.0f, "the pellets stay inside the 4.5 degree cone around the aim and use it");
        Check(distinct == 7 && minSpeed >= 42.0f - 0.01f && maxSpeed <= 52.0f + 0.01f && maxSpeed - minSpeed > 1.0f, "each pellet has its own direction and its own speed within buckshot's 42 to 52 m/s");
        float dmg = 0; for (int i = 0; i < MAX_PROJECTILES; ++i) if (Projectiles_Get(i)->alive) dmg = Projectiles_Get(i)->damage;
        Check(Near(dmg, 11.0f, 1e-4f), "each pellet carries 11 damage");

        // Heat opens the rifle's spread.
        InitProjectilesSystem();
        float coneCold = 0, coneHot = 0;
        toCross = Sub((Vector3){ 18.0f, 1.6f, 7.0f }, Aim_MuzzleFor(&aim, WEAPON_RIFLE));
        base = (Vector3){ toCross.x / Len(toCross), toCross.y / Len(toCross), toCross.z / Len(toCross) };
        for (int pass = 0; pass < 2; ++pass) {
            const float spread = pass == 0 ? rifle->spreadDeg : rifle->spreadMaxDeg;
            float worst = 0;
            for (int s = 0; s < 200; ++s) {
                Projectiles_Clear();
                Gunfire_Shoot(rifle, &aim, &g_map, spread, &rng);
                for (int i = 0; i < MAX_PROJECTILES; ++i) {
                    const Projectile *p = Projectiles_Get(i);
                    if (!p->alive) continue;
                    const Vector3 v = { p->vx, p->vy, p->vz };
                    const float ang = acosf(Dot(v, base) / Len(v)) * 57.29578f;
                    if (ang > worst) worst = ang;
                }
            }
            if (pass == 0) coneCold = worst; else coneHot = worst;
        }
        Check(coneCold <= 0.6f + 0.01f && coneHot > 3.0f && coneHot <= 5.0f + 0.01f, "a cold rifle shoots inside 0.6 degrees of the aim, a hot one inside 5");

        // Against a wall: the muzzle (0.55 m ahead) would be behind a wall 0.3 m in front of the eye.
        InitProjectilesSystem(); ClearTargets();
        aim = AimAt((Vector3){ 18.0f, 1.6f, 7.0f + 0.3f }, (Vector3){ 0, 0, -1 });      // 0.3 m from the wall face at z = 7
        Check(Gunfire_Shoot(pistol, &aim, &g_map, 0.0f, &rng) == 1, "a shot is still fired with the player against a wall");
        const Projectile *wp = NULL;
        for (int i = 0; i < MAX_PROJECTILES; ++i) if (Projectiles_Get(i)->alive) wp = Projectiles_Get(i);
        Check(wp && wp->z > 7.0f, "it starts on the player's side of the wall");
        Step(&g_map);
        Check(Projectiles_Active() == 0 && Impacts_Active() == 1, "and it hits the wall rather than going through");

        // A map of NULL (no walls) still works.
        InitProjectilesSystem();
        aim = AimAt((Vector3){ 0, 1.6f, 0 }, (Vector3){ 0, 0, -1 });
        Check(Gunfire_Shoot(pistol, &aim, NULL, 0.0f, &rng) == 1, "with no map the shot is fired and aimed at 40 m");
        Weapon_Def(WEAPON_HANDS);
        WeaponDef melee = *Weapon_Def(WEAPON_HANDS);
        Check(Gunfire_Shoot(&melee, &aim, &g_map, 0.0f, &rng) == 0, "a weapon with no projectile fires nothing");
    }

    printf(g_failed ? "\n%d FAILED\n" : "\nall passed\n", g_failed);
    return g_failed ? 1 : 0;
}

#endif
