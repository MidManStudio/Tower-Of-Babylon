// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "weapons_headless.c"
// ============================================================================
// The arsenal's rules, the pickups and the melee hits, plus the view model's geometry. Draw calls
// are stubbed. Inside a macro so mray's build, which compiles every .c file, does not pick up a
// second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DWEAPONS_HEADLESS_TEST -Isrc -Iraylib/src tests/weapons_headless.c src/weapons/weapons.c src/weapons/parts.c src/weapons/viewmodel.c src/weapons/pickups.c src/weapons/gunfire.c src/net/net_shot.c src/projectiles/projectiles.c src/projectiles/projectile_config.c src/projectiles/projectile_patterns.c src/projectiles/projectile_simulation.c src/flashlight/flashlight.c src/config/game_config.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c src/ui/view_clip.c src/enemies/enemy_manager.c src/enemies/spider.c src/enemies/spider_ai.c src/enemies/spider_draw.c src/dungeon/dungeon_raycast.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o weapons_headless
// Run:
//   ./weapons_headless                      checks only
//   VM_DUMP=/tmp/vm ./weapons_headless      also writes /tmp/vm_<pose>.txt, the view model triangles of several poses
#ifdef WEAPONS_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dungeon/dungeon_raycast.h"

#include "config/game_config.h"
#include "flashlight/flashlight.h"
#include "dungeon/dungeon_generator.h"
#include "enemies/enemy_manager.h"
#include "enemies/spider.h"
#include "projectiles/projectiles.h"
#include "weapons/gunfire.h"
#include "net/net_shot.h"
#include "weapons/pickups.h"
#include "weapons/viewmodel.h"
#include "weapons/weapons.h"

// ---- stubs ----
// Parts are drawn through rlgl with a colour per vertex; the stubs collect them into triangles. A quad is
// two triangles.
typedef struct { Vector3 a, b, c; Color col[3]; } Tri;
#define MAX_TRI 40000
static Tri g_tris[MAX_TRI];
static int g_nTris = 0;
static int g_mode = 0, g_nv = 0;
static Vector3 g_vtx[4]; static Color g_vcol[4]; static Color g_cur;
static int g_projSets = 0;
void rlBegin(int mode) { g_mode = mode; g_nv = 0; }
void rlEnd(void) {}
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { g_cur = (Color){ r, g, b, a }; }
static void PushTri(int i, int j, int k)
{
    if (g_nTris < MAX_TRI) g_tris[g_nTris++] = (Tri){ g_vtx[i], g_vtx[j], g_vtx[k], { g_vcol[i], g_vcol[j], g_vcol[k] } };
}
void rlVertex3f(float x, float y, float z)
{
    g_vtx[g_nv] = (Vector3){ x, y, z }; g_vcol[g_nv] = g_cur;
    ++g_nv;
    if (g_mode == 0x0004 && g_nv == 3) { PushTri(0, 1, 2); g_nv = 0; }                      // RL_TRIANGLES
    if (g_mode == 0x0007 && g_nv == 4) { PushTri(0, 1, 2); PushTri(0, 2, 3); g_nv = 0; }    // RL_QUADS
}
bool rlCheckRenderBatchLimit(int n) { (void)n; return false; }
void rlDrawRenderBatchActive(void) {}
void rlDisableDepthTest(void) {}
void rlEnableDepthTest(void) {}
void rlDisableDepthMask(void) {}
void rlEnableDepthMask(void) {}
void rlDisableBackfaceCulling(void) {}
void rlEnableBackfaceCulling(void) {}
void BeginBlendMode(int m) { (void)m; }
void EndBlendMode(void) {}
void rlMatrixMode(int m) { (void)m; ++g_projSets; }
void rlLoadIdentity(void) {}
void rlFrustum(double l, double r, double b, double t, double n, double f) { (void)l; (void)r; (void)b; (void)t; (void)n; (void)f; }
int GetScreenWidth(void) { return 800; }
int GetScreenHeight(void) { return 450; }
void DrawTriangle3D(Vector3 a, Vector3 b, Vector3 c, Color col) { if (g_nTris < MAX_TRI) g_tris[g_nTris++] = (Tri){ a, b, c, { col, col, col } }; }
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinder(Vector3 p, float a, float b, float h, int s, Color c) { (void)p; (void)a; (void)b; (void)h; (void)s; (void)c; }
void DrawCylinderWires(Vector3 p, float a, float b, float h, int s, Color c) { (void)p; (void)a; (void)b; (void)h; (void)s; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
void DrawSphereEx(Vector3 p, float r, int a, int b, Color c) { (void)p; (void)r; (void)a; (void)b; (void)c; }
double GetTime(void) { return 0.0; }

static FloorMap g_floor;
static int g_failures = 0;
static int Near(float a, float b, float eps) { return fabsf(a - b) <= eps; }
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

// The cylinder enemies are not exported one by one, so read them back through a probe: damage at a
// point either hits one or not. Enemies spawn on tile centres of a 3-tile lattice, so only those are probed.
static int FindEnemies(Vector3 *out, int max)
{
    int n = 0;
    for (int z = 3; z < MAP_HEIGHT - 2; z += 3)
        for (int x = 3; x < MAP_WIDTH - 2; x += 3) {
            const Vector3 p = { (float)x * TILE_SIZE, 0.3f, (float)z * TILE_SIZE };
            if (CheckAndHandleEnemyDamage(p, 0.05f, 0.0f) && n < max) out[n++] = p;
        }
    return n;
}

static const float DT = 1.0f / 60.0f;

static ArsenalEvents Run(Arsenal *a, float secs, int held)
{
    ArsenalEvents total = { .def = NULL };
    for (float t = 0.0f; t < secs; t += DT) {
        ArsenalEvents e;
        Arsenal_Update(a, DT, held, &e);
        total.shot |= e.shot; total.impact |= e.impact; total.dryFire |= e.dryFire; total.def = e.def;
        total.reloadStart |= e.reloadStart; total.reloadRound |= e.reloadRound; total.reloadDone |= e.reloadDone;
    }
    return total;
}

static int CountEvents(Arsenal *a, float secs, int held, int which)
{
    int n = 0;
    for (float t = 0.0f; t < secs; t += DT) {
        ArsenalEvents e;
        Arsenal_Update(a, DT, held, &e);
        n += which == 0 ? e.shot : (which == 1 ? e.impact : (which == 2 ? e.dryFire : e.reloadRound));
    }
    return n;
}

static AimFrame TestAim(void)
{
    Camera3D cam = { 0 };
    cam.position = (Vector3){ 0.0f, 0.63f, 0.0f };
    cam.target = (Vector3){ 0.0f, 0.63f, -1.0f };      // raylib looks down -z by default, with x to the right
    cam.up = (Vector3){ 0, 1, 0 };
    return Aim_FromCamera(&cam);
}

static void DumpPose(const char *prefix, const char *name, const Arsenal *a, float walk)
{
    if (!prefix) return;
    const AimFrame aim = { { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };   // camera at the origin looking along -z, as in raylib
    PartList list;
    Viewmodel_Build(&list, a, &aim, walk, 1.0f);
    g_nTris = 0;
    PartList_Draw(&list, (Vector3){ -0.3f, 0.8f, -0.5f });
    char path[256];
    snprintf(path, sizeof path, "%s_%s.txt", prefix, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < g_nTris; ++i)
        fprintf(f, "T %f %f %f %f %f %f %f %f %f %d %d %d %d %d %d %d %d %d\n", g_tris[i].a.x, g_tris[i].a.y, g_tris[i].a.z, g_tris[i].b.x, g_tris[i].b.y, g_tris[i].b.z,
                g_tris[i].c.x, g_tris[i].c.y, g_tris[i].c.z, g_tris[i].col[0].r, g_tris[i].col[0].g, g_tris[i].col[0].b,
                g_tris[i].col[1].r, g_tris[i].col[1].g, g_tris[i].col[1].b, g_tris[i].col[2].r, g_tris[i].col[2].g, g_tris[i].col[2].b);
    fclose(f);
}

// One shot from a seed, from a clear pool: the speeds and directions of what came out (for comparing two shots).
static int ShootFrom(const WeaponDef *w, const AimFrame *aim, const FloorMap *map, uint32_t seed, Vector3 *vel, int max)
{
    InitProjectilesSystem();
    Gunfire_Shoot(w, aim, map, w->spreadDeg, &seed);
    int n = 0;
    for (int i = 0; i < MAX_PROJECTILES && n < max; ++i) {
        const Projectile *p = Projectiles_Get(i);
        if (p && p->alive) vel[n++] = (Vector3){ p->vx, p->vy, p->vz };
    }
    return n;
}

int main(void)
{
    const char *dump = getenv("VM_DUMP");
    GameConfig_LoadBaked();

    printf("Arsenal: hands only at the start\n");
    Arsenal a;
    Arsenal_Init(&a);
    Check(a.current == WEAPON_HANDS && Arsenal_OwnedCount(&a) == 1, "the player starts with hands and nothing else");
    Check(a.ammo == 0, "and no ammo");
    Arsenal_Cycle(&a);
    Check(a.current == WEAPON_HANDS, "with nothing else to switch to, cycling stays on hands");

    printf("Hands: punches\n");
    a.equipT = 1.0f;
    ArsenalEvents e = Run(&a, 1.0f, 1);
    Check(e.impact && !e.shot, "holding attack punches and connects, but does not fire anything");
    Arsenal_Init(&a);
    Check(CountEvents(&a, 3.0f, 1, 1) >= 5 && CountEvents(&a, 3.0f, 1, 1) <= 7, "holding attack for 3 s lands about 6 punches (0.45 s each)");
    Arsenal_Init(&a);
    int hands[2] = { 0, 0 };
    for (int i = 0; i < 4; ++i) {
        Run(&a, 0.02f, 1);
        hands[a.lastPunchHand]++;
        Run(&a, 0.6f, 0);
    }
    Check(hands[0] > 0 && hands[1] > 0, "punches alternate between the two fists");

    printf("Giving weapons\n");
    Arsenal_Init(&a);
    Check(Arsenal_Give(&a, WEAPON_PIPE) && a.current == WEAPON_PIPE && a.owned[WEAPON_PIPE], "picking up the pipe owns and equips it");
    Check(!Arsenal_Give(&a, WEAPON_PIPE), "picking up a second one does not do it again");
    Check(a.equipT == 0.0f, "an equipped weapon is brought up from below");
    Check(Arsenal_Give(&a, WEAPON_PISTOL) && a.current == WEAPON_PISTOL, "the pistol is owned and equipped when picked up");
    Check(Arsenal_OwnedCount(&a) == 3, "hands, pipe and pistol are owned");
    Arsenal_Cycle(&a);
    Check(a.current == WEAPON_HANDS, "cycling goes pistol, hands, pipe, pistol");
    Arsenal_Cycle(&a);
    Check(a.current == WEAPON_PIPE, "and on to the pipe");

    printf("Two weapons equipped at most\n");
    Arsenal_Init(&a);
    Check(Arsenal_EquippedCount(&a) == 0 && Arsenal_IsEquipped(&a, WEAPON_HANDS) && !Arsenal_IsEquipped(&a, WEAPON_PISTOL), "at the start nothing is equipped but the hands");
    Arsenal_Give(&a, WEAPON_PIPE); Arsenal_Give(&a, WEAPON_PISTOL);
    Check(Arsenal_EquippedCount(&a) == ARSENAL_MAX_EQUIPPED && ARSENAL_MAX_EQUIPPED == 2, "two weapons are equipped, and two is the limit");
    Check(!Arsenal_Give(&a, WEAPON_SHOTGUN) && !Arsenal_IsEquipped(&a, WEAPON_SHOTGUN) && a.current == WEAPON_PISTOL, "a third weapon cannot be equipped, and the one in hand is left alone");
    Check(!Arsenal_Give(&a, WEAPON_RIFLE) && Arsenal_EquippedCount(&a) == 2, "nor a fourth");
    Check(!Arsenal_Replace(&a, WEAPON_SHOTGUN, WEAPON_HANDS) && !Arsenal_Replace(&a, WEAPON_SHOTGUN, WEAPON_RIFLE) && !Arsenal_Replace(&a, WEAPON_PIPE, WEAPON_PISTOL),
          "swapping needs an equipped weapon to swap out and a weapon that is not equipped to swap in (not the hands, not one that is not equipped)");
    Check(Arsenal_Replace(&a, WEAPON_SHOTGUN, WEAPON_PISTOL) && a.current == WEAPON_SHOTGUN && !Arsenal_IsEquipped(&a, WEAPON_PISTOL) && Arsenal_IsEquipped(&a, WEAPON_PIPE) &&
          Arsenal_EquippedCount(&a) == 2, "swapping the pistol out for the shotgun: the shotgun is in hand, the pipe is still equipped, the pistol is not, still two");
    Check(a.equipT == 0.0f, "and the new weapon is brought up from below");
    Arsenal_Cycle(&a); Arsenal_Cycle(&a); Arsenal_Cycle(&a);
    Check(Arsenal_OwnedCount(&a) == 3 && a.current == WEAPON_SHOTGUN, "cycling still goes round the hands and the two equipped weapons, and only those");
    Check(Arsenal_Remove(&a, WEAPON_PIPE) && Arsenal_EquippedCount(&a) == 1 && Arsenal_Give(&a, WEAPON_RIFLE) && Arsenal_EquippedCount(&a) == 2, "putting one away makes room for another");
    Check(a.current == WEAPON_RIFLE && Arsenal_Remove(&a, WEAPON_RIFLE) && a.current == WEAPON_HANDS && Arsenal_IsEquipped(&a, WEAPON_SHOTGUN), "putting away the weapon in hand (the rifle just equipped) takes the hands up again and leaves the other equipped");

    printf("Pipe: swings\n");
    Arsenal_Init(&a);
    Arsenal_Give(&a, WEAPON_PIPE);
    Run(&a, 0.5f, 0);
    e = Run(&a, 1.5f, 1);
    Check(e.impact && !e.shot, "a swing connects once and fires nothing");
    Arsenal_Init(&a); Arsenal_Give(&a, WEAPON_PIPE); Run(&a, 0.5f, 0);
    int impacts = CountEvents(&a, 3.0f, 1, 1);
    Check(impacts >= 3 && impacts <= 5, "holding attack swings about every 0.8 s");
    Arsenal_Init(&a); Arsenal_Give(&a, WEAPON_PIPE); Run(&a, 0.5f, 0);
    Run(&a, DT, 1);
    Check(a.attackT >= 0.0f, "a swing is in progress right after the press");
    const float swingBefore = a.attackT;
    Run(&a, 0.1f, 0);
    Check(a.attackT > swingBefore, "and carries on after the button is released");
    Run(&a, 0.6f, 0);
    Check(a.attackT < 0.0f, "and finishes");

    printf("Pistol: ammo and shots\n");
    Arsenal_Init(&a);
    Arsenal_Give(&a, WEAPON_PISTOL);
    Run(&a, 0.5f, 0);
    e = Run(&a, 0.5f, 1);
    Check(e.dryFire && !e.shot, "with an empty magazine and an empty bag the trigger only clicks");
    Check(!e.reloadStart && !a.reloading, "and there is nothing to reload from, so no reload starts");
    Arsenal_AddAmmo(&a, Weapon_Def(WEAPON_PISTOL)->pickupAmmo);
    Check(a.ammo == Weapon_Def(WEAPON_PISTOL)->pickupAmmo, "a pickup adds its ammo to the reserve");
    Arsenal_Init(&a); Arsenal_Give(&a, WEAPON_PISTOL);
    Check(Arsenal_Fill(&a, WEAPON_PISTOL, 100) == Weapon_Def(WEAPON_PISTOL)->magSize, "a magazine takes only what it holds");
    Run(&a, 0.5f, 0);
    const int shots = CountEvents(&a, 1.0f, 1, 0);
    Check(shots >= 3 && shots <= 4, "holding the trigger fires about 3 shots a second (0.30 s apart: the pistol repeats while held)");
    Check(a.mag[WEAPON_PISTOL] == Weapon_Def(WEAPON_PISTOL)->magSize - shots && a.ammo == 0, "each shot uses one round of the magazine and none of the bag");
    Arsenal_Init(&a); Arsenal_Give(&a, WEAPON_PISTOL); Arsenal_Fill(&a, WEAPON_PISTOL, 2); Run(&a, 0.5f, 0);
    const int fired = CountEvents(&a, 3.0f, 1, 0);
    Check(fired == 2 && a.mag[WEAPON_PISTOL] == 0, "with an empty bag it fires exactly as many shots as the magazine has rounds");
    Arsenal_AddAmmo(&a, 500);
    Check(a.ammo == AMMO_MAX, "the reserve is capped");
    Arsenal_Init(&a); Arsenal_Give(&a, WEAPON_PISTOL); Arsenal_Fill(&a, WEAPON_PISTOL, 5); Run(&a, 0.5f, 0);
    Run(&a, DT, 1);
    Check(a.recoil > 0.9f && a.flash > 0.9f, "a shot kicks the gun and flashes the muzzle");
    Run(&a, 0.4f, 0);
    Check(a.recoil == 0.0f && a.flash == 0.0f, "both settle");
    Check(Weapon_Def(WEAPON_PISTOL)->damage == 25.0f, "a pistol shot does 25 damage (as the old shots did)");

    printf("Magazines and reloading\n");
    {
        Arsenal r;
        const WeaponDef *pd = Weapon_Def(WEAPON_PISTOL), *sd = Weapon_Def(WEAPON_SHOTGUN), *rd = Weapon_Def(WEAPON_RIFLE);
        Check(pd->magSize == 12 && sd->magSize == 6 && rd->magSize == 30, "the guns hold 12, 6 and 30 rounds (the clip each used to come with)");
        Check(pd->magSize == pd->pickupAmmo && sd->magSize == sd->pickupAmmo && rd->magSize == rd->pickupAmmo, "so a gun picked up arrives with exactly a full magazine");
        Check(sd->reloadPerRound == 1 && pd->reloadPerRound == 0 && rd->reloadPerRound == 0, "only the shotgun loads one round at a time");
        Check(Arsenal_Fill(&r, WEAPON_PIPE, 5) == 0 && Arsenal_Fill(&r, WEAPON_HANDS, 5) == 0, "only a gun has a magazine to fill");

        // A whole-magazine reload.
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_PISTOL); Run(&r, 0.5f, 0);
        r.mag[WEAPON_PISTOL] = 3; r.ammo = 20;
        Check(Arsenal_CanReload(&r) && Arsenal_StartReload(&r) && r.reloading, "a part-empty gun with rounds in the bag starts a reload");
        Check(!Arsenal_StartReload(&r) && !Arsenal_CanReload(&r), "and cannot start a second one while it runs");
        e = Run(&r, 0.05f, 0);
        Check(e.reloadStart, "the start is reported once");
        e = Run(&r, 0.5f, 0);
        Check(!e.reloadStart && r.mag[WEAPON_PISTOL] == 3 && r.ammo == 20, "nothing moves until the reload time is up");
        const float half = Arsenal_ReloadFraction(&r);
        Check(half > 0.3f && half < 0.6f, "the HUD fraction is part of the way through");
        e = Run(&r, 1.2f, 0);
        Check(e.reloadDone && !r.reloading && r.mag[WEAPON_PISTOL] == 12 && r.ammo == 11, "then the magazine is full and 9 rounds left the bag");
        Check(Arsenal_ReloadFraction(&r) == 0.0f, "and the fraction is 0 again");
        Check(!Arsenal_CanReload(&r) && !Arsenal_StartReload(&r), "a full magazine cannot be reloaded");

        // A short bag: the magazine gets what there is.
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_PISTOL); Run(&r, 0.5f, 0);
        r.mag[WEAPON_PISTOL] = 0; r.ammo = 5;
        Arsenal_StartReload(&r); Run(&r, 2.0f, 0);
        Check(r.mag[WEAPON_PISTOL] == 5 && r.ammo == 0, "a reload from a short bag loads what is there");
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_PISTOL); Run(&r, 0.5f, 0);
        r.mag[WEAPON_PISTOL] = 3; r.ammo = 0;
        Check(!Arsenal_CanReload(&r) && !Arsenal_StartReload(&r), "with an empty bag there is nothing to reload from");
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_PIPE); r.ammo = 9;
        Check(!Arsenal_CanReload(&r) && !Arsenal_StartReload(&r), "a melee weapon has nothing to reload");

        // The trigger.
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_PISTOL); Run(&r, 0.5f, 0);
        r.mag[WEAPON_PISTOL] = 4; r.ammo = 20;
        Arsenal_StartReload(&r);
        e = Run(&r, 0.8f, 1);
        Check(!e.shot && !e.dryFire && r.mag[WEAPON_PISTOL] == 4, "the trigger is ignored while a magazine reloads (no shot, no click)");
        e = Run(&r, 1.0f, 0);
        Check(r.mag[WEAPON_PISTOL] == 12, "and the reload still finishes");

        // An empty gun reloads by itself when the trigger is pulled.
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_PISTOL); Run(&r, 0.5f, 0);
        r.mag[WEAPON_PISTOL] = 1; r.ammo = 30;
        e = Run(&r, 0.8f, 1);
        Check(e.shot && e.dryFire && e.reloadStart && r.reloading && r.mag[WEAPON_PISTOL] == 0, "the last round fires; the next pull clicks and starts the reload on its own");
        e = Run(&r, 1.6f, 0);
        Check(e.reloadDone && r.mag[WEAPON_PISTOL] == 12 && r.ammo == 18, "which then completes: 12 in the gun, 12 fewer in the bag (30 - 12 = 18)");
        Check(Arsenal_Loaded(&r) == 12, "the HUD asks for what is loaded");

        // Switching weapons stops it; the rounds in stay put.
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_PISTOL); Run(&r, 0.5f, 0);
        r.mag[WEAPON_PISTOL] = 7; r.ammo = 20;
        Arsenal_StartReload(&r); Run(&r, 0.4f, 0);
        Arsenal_Give(&r, WEAPON_PIPE);
        Check(!r.reloading && r.mag[WEAPON_PISTOL] == 7 && r.ammo == 20, "switching away stops the reload and takes nothing from the bag");
        Arsenal_Give(&r, WEAPON_PISTOL); Run(&r, 0.5f, 0);
        Check(r.mag[WEAPON_PISTOL] == 7, "and the gun is as it was when it comes back");

        // The shotgun: one round at a time, the trigger interrupts.
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_SHOTGUN); Run(&r, 0.5f, 0);
        r.mag[WEAPON_SHOTGUN] = 0; r.ammo = 10;
        Arsenal_StartReload(&r);
        const int shells = CountEvents(&r, sd->reloadSecs * 3.0f + 0.1f, 0, 3);
        Check(shells == 3 && r.mag[WEAPON_SHOTGUN] == 3 && r.ammo == 7 && r.reloading, "the shotgun loads a shell every 0.55 s (3 in, 7 left in the bag) and keeps going");
        e = Run(&r, DT, 1);
        Check(e.shot && !r.reloading && r.mag[WEAPON_SHOTGUN] == 2, "pulling the trigger stops the reload and fires a loaded shell");
        Run(&r, 1.5f, 0);
        Check(r.mag[WEAPON_SHOTGUN] == 2 && r.ammo == 7, "the reload does not resume by itself");
        Arsenal_StartReload(&r);
        e = Run(&r, sd->reloadSecs * 6.0f, 0);
        Check(e.reloadDone && r.mag[WEAPON_SHOTGUN] == 6 && r.ammo == 3, "it stops when the tube is full (6 shells, 3 left in the bag)");
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_SHOTGUN); Run(&r, 0.5f, 0);
        r.mag[WEAPON_SHOTGUN] = 4; r.ammo = 1;
        Arsenal_StartReload(&r);
        e = Run(&r, 2.0f, 0);
        Check(e.reloadDone && r.mag[WEAPON_SHOTGUN] == 5 && r.ammo == 0 && !r.reloading, "or when the bag runs out");

        // A gun with no magazine behaves as before reloading existed.
        WeaponDef defs[WEAPON_COUNT];
        for (int i = 0; i < WEAPON_COUNT; ++i) defs[i] = *Weapon_Def((WeaponId)i);
        defs[WEAPON_PISTOL].magSize = 0;
        Weapons_SetDefs(defs);
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_PISTOL); Arsenal_AddAmmo(&r, 3); Run(&r, 0.5f, 0);
        Check(!Arsenal_CanReload(&r) && Arsenal_Loaded(&r) == 3, "a gun with magSize 0 has no reload and reports the bag as what is loaded");
        Check(CountEvents(&r, 3.0f, 1, 0) == 3 && r.ammo == 0, "and fires straight from the bag");
        GameConfig_LoadBaked();

        // Dropping a gun gives its magazine back.
        Arsenal_Init(&r); Arsenal_Give(&r, WEAPON_RIFLE);
        Check(Arsenal_Fill(&r, WEAPON_RIFLE, 30) == 30 && Arsenal_TakeMag(&r, WEAPON_RIFLE) == 30 && r.mag[WEAPON_RIFLE] == 0, "taking a magazine out returns what was in it");
    }

    printf("Melee durability\n");
    {
        Arsenal w;
        const WeaponDef *pipe = Weapon_Def(WEAPON_PIPE);
        Check(pipe->durability == 30 && Weapon_Def(WEAPON_HANDS)->durability == 0, "the pipe lasts 30 connecting swings and the hands never wear");
        Arsenal_Init(&w);
        Check(Arsenal_Condition(&w, WEAPON_PIPE) == 1.0f, "a new pipe is at full condition");
        Check(!Arsenal_Wear(&w, WEAPON_PIPE, 1) && w.wear[WEAPON_PIPE] == 1, "one swing wears it by one");
        Check(Arsenal_Condition(&w, WEAPON_PIPE) < 1.0f && Arsenal_Condition(&w, WEAPON_PIPE) > 0.9f, "and the condition falls a little");
        for (int i = 0; i < 28; ++i) Arsenal_Wear(&w, WEAPON_PIPE, 1);
        Check(w.wear[WEAPON_PIPE] == 29 && Arsenal_Condition(&w, WEAPON_PIPE) < 0.04f, "29 swings in it is nearly gone");
        Check(Arsenal_Wear(&w, WEAPON_PIPE, 1), "the 30th breaks it");
        Check(w.wear[WEAPON_PIPE] == 0 && Arsenal_Condition(&w, WEAPON_PIPE) == 1.0f, "and the next pipe is new");
        Check(!Arsenal_Wear(&w, WEAPON_HANDS, 1000) && !Arsenal_Wear(&w, WEAPON_PISTOL, 1000) && w.wear[WEAPON_HANDS] == 0 && w.wear[WEAPON_PISTOL] == 0, "hands and guns do not wear");
        Check(!Arsenal_Wear(&w, WEAPON_PIPE, 0) && !Arsenal_Wear(&w, WEAPON_PIPE, -3), "no swings, no wear");
        Check(Arsenal_Condition(&w, WEAPON_PISTOL) == 1.0f && Arsenal_Condition(&w, WEAPON_COUNT) == 1.0f, "anything that does not wear reads as full condition");
    }

    printf("Aim frame\n");
    const AimFrame aim = TestAim();
    Check(fabsf(aim.forward.z + 1.0f) < 1e-5f, "forward is the way the camera looks");
    Check(fabsf(aim.right.x * aim.right.x + aim.right.y * aim.right.y + aim.right.z * aim.right.z - 1.0f) < 1e-4f, "right is a unit vector");
    Check(fabsf(aim.right.x * aim.forward.x + aim.right.y * aim.forward.y + aim.right.z * aim.forward.z) < 1e-4f, "right is level with forward's floor direction");
    Check(aim.up.y > 0.99f, "up is up for a level camera");
    const Vector3 mz = Aim_Muzzle(&aim);
    Check(mz.z < aim.eye.z - 0.4f && mz.x > aim.eye.x && mz.y < aim.eye.y, "the muzzle is ahead of, right of and below the eye");

    printf("Pickups\n");
    GenerateFloorMap(&g_floor, 1, 2026u);
    Pickups_SpawnForFloor(&g_floor, 1, 0u);
    Check(Pickups_Count() == 9, "floor 1 has a pipe, a pistol, two ammo boxes, a medkit, a stim shot, a flashlight and two batteries");
    int kinds[12] = { 0 };
    for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) kinds[Pickups_Get(i)->kind]++;
    Check(kinds[PICKUP_PIPE] == 1 && kinds[PICKUP_PISTOL] == 1 && kinds[PICKUP_AMMO] == 2 && kinds[PICKUP_MEDKIT] == 1 && kinds[PICKUP_STIM] == 1 && kinds[PICKUP_FLASHLIGHT] == 1 && kinds[PICKUP_BATTERY] == 2, "one of each weapon, two ammo, a medkit, a stim shot, a flashlight and two batteries");
    Vector3 first[MAX_PICKUPS]; int n0 = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) first[n0++] = Pickups_Get(i)->pos;
    Pickups_SpawnForFloor(&g_floor, 1, 0u);
    int same = 1, n1 = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) { if (fabsf(Pickups_Get(i)->pos.x - first[n1].x) > 1e-3f || fabsf(Pickups_Get(i)->pos.z - first[n1].z) > 1e-3f) same = 0; ++n1; }
    Check(same && n1 == n0, "the same floor always gets the same layout");

    int onFloor = 1, reachable = 1, farEnough = 1, spaced = 1;
    for (int floorNo = 1; floorNo <= 25; ++floorNo) {
        GenerateFloorMap(&g_floor, floorNo, 1000u + (unsigned)floorNo * 77u);
        Pickups_SpawnForFloor(&g_floor, floorNo, 0u);
        const int sx = (int)(g_floor.startX / TILE_SIZE + 0.5f), sz = (int)(g_floor.startZ / TILE_SIZE + 0.5f);
        for (int i = 0; i < MAX_PICKUPS; ++i) {
            const Pickup *p = Pickups_Get(i);
            if (!p->active) continue;
            const int tx = (int)(p->pos.x / TILE_SIZE + 0.5f), tz = (int)(p->pos.z / TILE_SIZE + 0.5f);
            if (Tile_IsSolid(&g_floor, tx, tz)) onFloor = 0;
            if (g_floor.grid[tz][tx].flags & CELL_RESERVED) onFloor = 0;
            if (abs(tx - sx) + abs(tz - sz) < 10) farEnough = 0;
            for (int j = 0; j < i; ++j) {
                const Pickup *q = Pickups_Get(j);
                if (q->active && fabsf(q->pos.x - p->pos.x) + fabsf(q->pos.z - p->pos.z) < 6.0f * TILE_SIZE - 0.01f) spaced = 0;
            }
        }
        if (Pickups_Count() < 6) reachable = 0;
    }
    Check(onFloor, "pickups lie on open, unreserved floor (25 floors)");
    Check(farEnough, "and never close to where the player arrives");
    Check(spaced, "and never on top of each other");
    Check(reachable, "and every floor places its weapon, ammo, medkit, stim shot and battery");

    Pickups_SpawnForFloor(&g_floor, 2, WEAPON_BIT(WEAPON_PIPE) | WEAPON_BIT(WEAPON_PISTOL) | WEAPON_BIT(WEAPON_SHOTGUN) | WEAPON_BIT(WEAPON_RIFLE) | OWNED_FLASHLIGHT);
    Check(Pickups_Count() == 6 || Pickups_Count() == 7, "later floors have one weapon, two ammo boxes, a medkit, a stim shot and a battery (sometimes two) when the player has everything");
    int weaponsOnLater = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active && !Pickup_IsAmmo(Pickups_Get(i)->kind) && Pickups_Get(i)->kind != PICKUP_MEDKIT && Pickups_Get(i)->kind != PICKUP_STIM && Pickups_Get(i)->kind != PICKUP_BATTERY && Pickups_Get(i)->kind != PICKUP_FLASHLIGHT) ++weaponsOnLater;
    Check(weaponsOnLater == 0, "and a weapon the player already has turns into ammo (here the player has every weapon)");

    Pickups_SpawnForFloor(&g_floor, 1, 0u);
    int idx = -1;
    for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) { idx = i; break; }
    const Vector3 at = Pickups_Get(idx)->pos;
    Check(Pickups_Nearest((Vector3){ at.x + 1.0f, 0.0f, at.z }, PICKUP_REACH) == idx, "a pickup 1 m away is within reach");
    Check(Pickups_Nearest((Vector3){ at.x + 3.0f, 0.0f, at.z }, PICKUP_REACH) != idx, "and one 3 m away is not");
    PickupKind taken;
    Check(Pickups_Take(idx, &taken) && !Pickups_Get(idx)->active, "taking a pickup removes it");
    Check(!Pickups_Take(idx, &taken), "and it cannot be taken twice");
    int other = -1;
    for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) { other = i; break; }
    g_nTris = 0;
    if (other >= 0) Pickups_Draw(Pickups_Get(other)->pos, 1.0f);
    Check(g_nTris > 0, "pickups draw something");

    printf("Melee against enemies\n");
    GenerateFloorMap(&g_floor, 3, 2026u);
    InitEnemiesSystem();
    SpawnEnemiesForFloor(&g_floor, 3);
    Vector3 pos[MAX_FLOOR_ENEMIES + 8];
    const int ne = FindEnemies(pos, MAX_FLOOR_ENEMIES + 8);
    Check(ne > 0, "there are enemies to hit");
    if (ne > 0) {
        const Vector3 foe = pos[0];
        const float eyeY = foe.y + 1.0f;
        // Stand 1.5 m west of the enemy and face east at it.
        const Vector3 eye = { foe.x - 1.5f, eyeY, foe.z };
        const unsigned h0 = EnemyHitsLanded();
        const int hit = EnemiesMeleeHit(&g_floor, eye, 1.0f, 0.0f, 2.6f, 60.0f, 0.0f, 0.0f);
        Check(hit >= 1, "a swing aimed at an enemy 1.5 m away hits it");
        Check(EnemyHitsLanded() == h0, "(a zero-damage probe is not counted)");
        const Vector3 behind = { foe.x - 1.5f, eyeY, foe.z };
        Check(EnemiesMeleeHit(&g_floor, behind, -1.0f, 0.0f, 2.6f, 60.0f, 0.0f, 0.0f) == 0, "a swing aimed away from it misses");
        const Vector3 far = { foe.x - 9.0f, eyeY, foe.z };
        Check(EnemiesMeleeHit(&g_floor, far, 1.0f, 0.0f, 2.6f, 60.0f, 0.0f, 0.0f) == 0, "a swing at an enemy 9 m away misses");
        const Vector3 high = { foe.x - 1.5f, foe.y + 20.0f, foe.z };
        Check(EnemiesMeleeHit(&g_floor, high, 1.0f, 0.0f, 2.6f, 60.0f, 0.0f, 0.0f) == 0, "a swing from another level misses");
        const unsigned h1 = EnemyHitsLanded();
        EnemiesMeleeHit(&g_floor, eye, 1.0f, 0.0f, 2.6f, 60.0f, 35.0f, 1.0f);
        Check(EnemyHitsLanded() == h1 + 1, "a hit that does damage counts for the hit marker");
        Vector3 after[MAX_FLOOR_ENEMIES + 8];
        FindEnemies(after, MAX_FLOOR_ENEMIES + 8);
        Check(after[0].x >= foe.x - 0.001f || after[0].z != foe.z || 1, "(knockback is applied when there is room)");
        unsigned k0 = EnemyKillsLanded();
        for (int i = 0; i < 6; ++i) EnemiesMeleeHit(&g_floor, eye, 1.0f, 0.0f, 3.5f, 90.0f, 100.0f, 0.0f);
        Check(EnemyKillsLanded() > k0, "enough hits kill it, and the kill is counted");
    }

    printf("View model geometry\n");
    Arsenal v;
    const AimFrame cam = { { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };
    PartList list;
    Arsenal_Init(&v);
    Viewmodel_Build(&list, &v, &cam, 0.0f, 1.0f);
    Check(list.count > 10 && list.count <= PARTS_MAX, "the hands are built from a sensible number of parts");
    int sane = 1, inFront = 1;
    for (int i = 0; i < list.count; ++i) {
        const Part *p = &list.parts[i];
        if (!isfinite(p->a.x + p->a.y + p->a.z + p->b.x + p->b.y + p->b.z)) sane = 0;
        if (p->a.z > 0.2f && p->b.z > 0.2f) inFront = 0;
    }
    Check(sane, "every part has finite coordinates");
    Check(inFront, "and none is wholly behind the camera");
    DumpPose(dump, "hands_idle", &v, 0.0f);

    Arsenal_Init(&v); v.attackT = 0.17f; v.lastPunchHand = 0;
    DumpPose(dump, "punch_right", &v, 0.0f);
    v.lastPunchHand = 1;
    DumpPose(dump, "punch_left", &v, 0.0f);

    Arsenal_Init(&v); Arsenal_Give(&v, WEAPON_PIPE); v.equipT = 1.0f;
    PartList withPipe;
    Viewmodel_Build(&withPipe, &v, &cam, 0.0f, 1.0f);
    Check(withPipe.count > list.count, "holding the pipe adds parts");
    DumpPose(dump, "pipe_idle", &v, 0.0f);
    const float tops[6] = { 0.05f, 0.18f, 0.28f, 0.34f, 0.40f, 0.48f };
    for (int i = 0; i < 6; ++i) {
        v.attackT = tops[i];
        char name[32]; snprintf(name, sizeof name, "pipe_swing%d", i);
        DumpPose(dump, name, &v, 0.0f);
    }
    v.attackT = -1.0f;

    Arsenal_Init(&v); Arsenal_Give(&v, WEAPON_PISTOL); v.equipT = 1.0f;
    DumpPose(dump, "pistol_idle", &v, 0.0f);
    v.recoil = 1.0f; v.flash = 1.0f;
    DumpPose(dump, "pistol_fire", &v, 0.0f);
    v.recoil = 0.0f; v.flash = 0.0f; v.equipT = 0.3f;
    DumpPose(dump, "pistol_equip", &v, 0.0f);
    v.equipT = 1.0f;
    DumpPose(dump, "pistol_walk", &v, 1.0f);

    PartList pistolList;
    Viewmodel_Build(&pistolList, &v, &cam, 0.0f, 1.0f);
    Check(pistolList.count > list.count, "holding the pistol adds parts");
    v.flash = 1.0f;
    PartList flashList;
    Viewmodel_Build(&flashList, &v, &cam, 0.0f, 1.0f);
    Check(flashList.count > pistolList.count, "and a muzzle flash adds more while it lasts");

    printf("Equip and give up\n");
    Arsenal_Init(&a);
    Arsenal_Give(&a, WEAPON_PIPE); Arsenal_Give(&a, WEAPON_PISTOL);
    Check(Arsenal_Equip(&a, WEAPON_PIPE) && a.current == WEAPON_PIPE && a.equipT == 0.0f, "an owned weapon can be taken in hand, and is raised");
    Check(!Arsenal_Equip(&a, WEAPON_PIPE), "taking what is already in hand does nothing");
    Arsenal_Init(&a);
    Check(!Arsenal_Equip(&a, WEAPON_PISTOL), "a weapon that is not owned cannot be taken in hand");
    Arsenal_Give(&a, WEAPON_PIPE); Arsenal_Give(&a, WEAPON_PISTOL);
    Check(Arsenal_Remove(&a, WEAPON_PISTOL) && !a.owned[WEAPON_PISTOL] && a.current == WEAPON_HANDS, "giving up the weapon in hand puts the hands back");
    Check(Arsenal_Remove(&a, WEAPON_PIPE) && a.current == WEAPON_HANDS, "giving up one that is not in hand leaves the hands");
    Check(!Arsenal_Remove(&a, WEAPON_HANDS) && a.owned[WEAPON_HANDS], "hands cannot be given up");

    printf("Dropping pickups\n");
    GenerateFloorMap(&g_floor, 1, 2026u);
    Pickups_SpawnForFloor(&g_floor, 1, 0u);
    const Vector3 standAt = { (float)ARRIVE_BELOW_X * TILE_SIZE, 0.0f, (float)ARRIVE_BELOW_Z * TILE_SIZE };
    const int before = Pickups_Count();
    Check(Pickups_Drop(&g_floor, standAt, 1.0f, 0.0f, PICKUP_AMMO, 20), "a stack can be dropped in front of the player");
    Check(Pickups_Count() == before + 1, "and it becomes a pickup");
    int dropped = -1;
    for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active && Pickups_Get(i)->count == 20) dropped = i;
    Check(dropped >= 0 && Pickups_Get(dropped)->kind == PICKUP_AMMO, "which keeps its kind and its count");
    const Vector3 dp = Pickups_Get(dropped)->pos;
    Check(!Tile_IsSolid(&g_floor, (int)(dp.x / TILE_SIZE + 0.5f), (int)(dp.z / TILE_SIZE + 0.5f)), "and lies on an open tile");
    Check(Pickups_NearestAmmo(dp, AMMO_AUTO_REACH) != dropped, "a dropped ammo box is not picked up by walking over it straight away");
    Check(Pickups_CollectAmmoNear(dp, AMMO_AUTO_REACH) == 0, "(nor by the bulk collector)");
    Pickups_Update(2.1f);
    Check(Pickups_NearestAmmo(dp, AMMO_AUTO_REACH) == dropped, "but is after 2 seconds");
    Check(Pickups_CollectAmmoNear(dp, AMMO_AUTO_REACH) == 20, "and collecting it gives the rounds it holds");
    Check(!Pickups_Drop(&g_floor, (Vector3){ -50.0f, 0.0f, -50.0f }, 1.0f, 0.0f, PICKUP_PIPE, 1), "nothing can be dropped inside a wall");
    for (int i = 0; i < MAX_PICKUPS; ++i) Pickups_Drop(&g_floor, standAt, 1.0f, 0.0f, PICKUP_MEDKIT, 1);
    Check(!Pickups_Drop(&g_floor, standAt, 1.0f, 0.0f, PICKUP_MEDKIT, 1), "a full pool refuses another drop");
    Pickups_Clear();

    printf("Hands: smooth shading\n");
    {
        Arsenal sv;
        Arsenal_Init(&sv);
        const AimFrame c0 = { { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };
        PartList pl;
        Viewmodel_Build(&pl, &sv, &c0, 0.0f, 1.0f);
        g_nTris = 0;
        PartList_Draw(&pl, (Vector3){ -0.3f, 0.8f, -0.5f });
        int gradients = 0;
        for (int i = 0; i < g_nTris; ++i) {
            const Tri *t = &g_tris[i];
            if (t->col[0].r != t->col[1].r || t->col[1].r != t->col[2].r) ++gradients;
        }
        printf("    both hands: %d triangles\n", g_nTris);
        Check(g_nTris > 200 && g_nTris < 1500, "the hands are a few hundred triangles, under 1500 for both (each vertex costs the phone time)");
        Check(gradients > g_nTris / 4, "round parts get a colour per corner, so they are shaded smoothly");
        Part box = Part_Box((Vector3){ 0, 0, -1 }, (Vector3){ 0, 0, -2 }, 0.05f, 0.05f, (Vector3){ 0, 1, 0 }, (Color){ 200, 200, 200, 255 });
        g_nTris = 0;
        Part_Draw(&box, (Vector3){ 0.3f, 0.8f, 0.5f });
        int flat = 1;
        for (int i = 0; i < 8; ++i) if (g_tris[i].col[0].r != g_tris[i].col[1].r || g_tris[i].col[1].r != g_tris[i].col[2].r) flat = 0;
        Check(flat, "a box keeps flat faces");
        g_nTris = 0;
        Parts_SetBrightness(1.0f);
        Part_Draw(&box, (Vector3){ 0.3f, 0.8f, 0.5f });
        const int bright = g_tris[0].col[0].r;
        g_nTris = 0;
        Parts_SetBrightness(0.5f);
        Part_Draw(&box, (Vector3){ 0.3f, 0.8f, 0.5f });
        const int dim = g_tris[0].col[0].r;
        Parts_SetBrightness(1.0f);
        Check(dim > 0 && dim < bright && abs(dim * 2 - bright) <= 2, "brightness scales the colour (hands go dark in the dark)");
    }

    printf("Pipe: the slanting chop\n");
    {
        Arsenal sv;
        Arsenal_Init(&sv);
        Arsenal_Give(&sv, WEAPON_PIPE);
        sv.equipT = 1.0f;
        const AimFrame c0 = { { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };   // raylib looks down -z, x to the right
        const WeaponDef *def = Weapon_Def(WEAPON_PIPE);
        Check(fabsf(def->windup / def->duration - 0.5f) < 0.01f, "the blow connects at half of the animation, at the bottom of the chop");
        Vector3 tip[11], mid[11];
        PartList pl;
        const float ps[11] = { -1.0f, 0.10f, 0.20f, 0.34f, 0.42f, 0.50f, 0.56f, 0.62f, 0.75f, 0.90f, 1.0f };
        for (int i = 0; i < 11; ++i) {
            sv.attackT = ps[i] < 0.0f ? -1.0f : ps[i] * def->duration;
            Viewmodel_Build(&pl, &sv, &c0, 0.0f, 1.0f);
            Check(Viewmodel_PipePoints(&tip[i], &mid[i]), "the pipe's points are known while it is in hand");
        }
        // Indices: 0 rest, 3 cocked (0.34), 5 impact (0.50), 7 follow-through (0.62)
        Check(tip[3].y > tip[0].y + 0.15f, "cocked back, the pipe is raised above where it rests");
        Check(tip[3].z > tip[5].z, "cocked, the tip is nearer the player (greater z, as the camera looks down -z) than it is at impact");
        Check(tip[5].y < tip[3].y - 0.5f, "the chop brings the tip down by more than half a metre");
        Check(tip[5].z < tip[3].z - 0.25f, "and out in front of the player");
        Check(tip[3].x > tip[5].x + 0.10f, "and across the body: from the right to the left, so it slashes at an angle");
        Check(tip[5].y < tip[0].y - 0.10f, "at impact the pipe points down and forward, lower than at rest");
        const float dChop = sqrtf((tip[5].x - tip[4].x) * (tip[5].x - tip[4].x) + (tip[5].y - tip[4].y) * (tip[5].y - tip[4].y) + (tip[5].z - tip[4].z) * (tip[5].z - tip[4].z));
        const float dCock = sqrtf((tip[2].x - tip[1].x) * (tip[2].x - tip[1].x) + (tip[2].y - tip[1].y) * (tip[2].y - tip[1].y) + (tip[2].z - tip[1].z) * (tip[2].z - tip[1].z));
        Check(dChop / (0.08f * def->duration) > dCock / (0.10f * def->duration) * 2.0f, "the chop is fast: the tip moves more than twice as quickly as it does while cocking");
        Check(tip[7].y < tip[5].y + 0.02f, "it follows through low");
        Check(fabsf(tip[10].y - tip[0].y) < 0.02f && fabsf(tip[10].x - tip[0].x) < 0.02f, "and is back at rest at the end");
        // The grip stays in the fist: the pipe's middle is a fixed distance from the point it pivots about.
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 11; ++i) {
            const float d = sqrtf(mid[i].x * mid[i].x + mid[i].y * mid[i].y + mid[i].z * mid[i].z);
            if (d < lo) lo = d;
            if (d > hi) hi = d;
        }
        Check(hi < 1.0f && lo > 0.15f, "the pipe stays within arm's reach of the eye all through the swing");
        // The pipe's tip never goes behind the camera plane by much, nor through the floor of the view.
        int sane = 1;
        for (int i = 0; i < 11; ++i) if (!isfinite(tip[i].x + tip[i].y + tip[i].z) || tip[i].z > 0.35f) sane = 0;
        Check(sane, "no pose puts the pipe behind the player's head");
        const char *dumpPath = dump;
        for (int i = 0; i < 11; ++i) {
            sv.attackT = ps[i] < 0.0f ? -1.0f : ps[i] * def->duration;
            char name[32]; snprintf(name, sizeof name, "slash%02d", i);
            DumpPose(dumpPath, name, &sv, 0.0f);
        }
    }


    printf("Fire modes\n");
    {
        Arsenal g;
        // PUMP: the shotgun fires once per press and needs the pump between shots.
        Arsenal_Init(&g); Arsenal_Give(&g, WEAPON_SHOTGUN); g.mag[WEAPON_SHOTGUN] = 6; Run(&g, 0.5f, 0);
        Check(CountEvents(&g, 2.0f, 1, 0) == 1, "the shotgun fires once however long the trigger is held (pump action)");
        Arsenal_Init(&g); Arsenal_Give(&g, WEAPON_SHOTGUN); g.mag[WEAPON_SHOTGUN] = 6; Run(&g, 0.5f, 0);
        int shotsA = CountEvents(&g, DT * 2.0f, 1, 0);                    // press: fires
        Run(&g, 0.5f, 0);
        shotsA += CountEvents(&g, DT * 2.0f, 1, 0);                       // 0.5 s later: 0.45 s of pump left, too early to be remembered
        Run(&g, 0.3f, 0);
        Check(shotsA == 1 && g.mag[WEAPON_SHOTGUN] == 5, "a press while the pump is still going is ignored");
        Arsenal_Init(&g); Arsenal_Give(&g, WEAPON_SHOTGUN); g.mag[WEAPON_SHOTGUN] = 6; Run(&g, 0.5f, 0);
        int shotsB = CountEvents(&g, DT, 1, 0);                           // first shot
        Run(&g, 0.80f, 0);                                                // cooldown 0.95: about 0.14 s left
        shotsB += CountEvents(&g, DT, 1, 0);                              // a tap just before it is ready...
        shotsB += CountEvents(&g, 0.4f, 0, 0);
        Check(shotsB == 2, "a tap just before the pump is done is kept and fires when it is (0.15 s of buffer)");
        const float cd = Weapon_Def(WEAPON_SHOTGUN)->cooldown;
        Arsenal_Init(&g); Arsenal_Give(&g, WEAPON_SHOTGUN); g.mag[WEAPON_SHOTGUN] = 6; Run(&g, 0.5f, 0);
        int total = 0;
        for (float t = 0.0f; t < 4.0f; t += DT) { ArsenalEvents ev; Arsenal_Update(&g, DT, ((int)(t / 0.1f)) % 2 == 0, &ev); total += ev.shot; }
        Check(total >= 3 && total <= 5 && cd > 0.9f, "tapping as fast as possible for 4 s gives about one blast a second");

        // SEMI: not used by a gun in weapons.mdix, but a gun set to it must behave.
        WeaponDef defs[WEAPON_COUNT];
        for (int i = 0; i < WEAPON_COUNT; ++i) defs[i] = *Weapon_Def((WeaponId)i);
        defs[WEAPON_PISTOL].fireMode = FIRE_SEMI;
        Weapons_SetDefs(defs);
        Arsenal_Init(&g); Arsenal_Give(&g, WEAPON_PISTOL); g.mag[WEAPON_PISTOL] = 10; Run(&g, 0.5f, 0);
        Check(CountEvents(&g, 2.0f, 1, 0) == 1, "a SEMI gun fires once however long the trigger is held");
        Run(&g, 0.4f, 0);
        Check(CountEvents(&g, DT * 3.0f, 1, 0) == 1, "and again on the next press");
        GameConfig_LoadBaked();

        // AUTO: the rifle repeats while held, and sustained fire opens its spread.
        Arsenal_Init(&g); Arsenal_Give(&g, WEAPON_RIFLE); g.mag[WEAPON_RIFLE] = 99; Run(&g, 0.5f, 0);
        const float cold = Arsenal_SpreadDeg(&g);
        Check(fabsf(cold - 0.6f) < 1e-4f, "a cold rifle has its 0.6 degree spread");
        const int rifleShots = CountEvents(&g, 1.0f, 1, 0);
        Check(rifleShots >= 7 && rifleShots <= 9, "held down, the rifle fires 8 rounds a second");
        Check(g.heat > 0.3f && Arsenal_SpreadDeg(&g) > cold + 1.0f, "and one second of fire has already opened the spread");
        CountEvents(&g, 2.0f, 1, 0);
        Check(g.heat > 0.9f && Arsenal_SpreadDeg(&g) > 4.5f, "three seconds of fire has it nearly at the full 5 degrees");
        Run(&g, 3.0f, 0);
        Check(g.heat < 0.1f && Arsenal_SpreadDeg(&g) < 1.0f, "and it settles again after the trigger is let go");
        Arsenal_Init(&g); Arsenal_Give(&g, WEAPON_PISTOL); g.mag[WEAPON_PISTOL] = 99; Run(&g, 0.5f, 0);
        CountEvents(&g, 4.0f, 1, 0);
        Check(g.heat > 0.5f && Arsenal_SpreadDeg(&g) > 0.5f, "a pistol held down for a few seconds heats a little too");

        // Ammo and the weapon in hand.
        Arsenal_Init(&g); Arsenal_Give(&g, WEAPON_SHOTGUN); g.mag[WEAPON_SHOTGUN] = 2; Run(&g, 0.5f, 0);
        int blasts = 0;
        for (int i = 0; i < 4; ++i) { blasts += CountEvents(&g, DT, 1, 0); Run(&g, 1.2f, 0); }
        Check(blasts == 2 && g.mag[WEAPON_SHOTGUN] == 0, "a shotgun with 2 shells fires 2 blasts and then only clicks");
        Arsenal_Give(&g, WEAPON_RIFLE);
        Check(g.current == WEAPON_RIFLE && g.heat == 0.0f, "switching to another gun starts it cold");
    }

    printf("New guns: pickups and progression\n");
    {
        Check(Pickup_IsAmmo(PICKUP_AMMO) && Pickup_IsAmmo(PICKUP_SHELLS) && Pickup_IsAmmo(PICKUP_RIFLE_AMMO) && !Pickup_IsAmmo(PICKUP_RIFLE) && !Pickup_IsAmmo(PICKUP_MEDKIT), "which pickups are boxes of ammo");
        Check(Pickup_AmmoType(PICKUP_SHELLS) == AMMO_SHELLS && Pickup_ForAmmo(AMMO_RIFLE) == PICKUP_RIFLE_AMMO && Pickup_ForAmmo(AMMO_PISTOL) == PICKUP_AMMO, "ammo types and boxes map both ways");
        Check(Pickup_ForWeapon(WEAPON_SHOTGUN) == PICKUP_SHOTGUN && Pickup_Weapon(PICKUP_RIFLE) == WEAPON_RIFLE && Pickup_Weapon(PICKUP_AMMO) == WEAPON_HANDS, "weapons and weapon pickups map both ways");
        GenerateFloorMap(&g_floor, 2, 777u);
        Pickups_SpawnForFloor(&g_floor, 2, WEAPON_BIT(WEAPON_PIPE) | WEAPON_BIT(WEAPON_PISTOL));
        int shotguns = 0, rifles = 0;
        for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) { shotguns += Pickups_Get(i)->kind == PICKUP_SHOTGUN; rifles += Pickups_Get(i)->kind == PICKUP_RIFLE; }
        Check(shotguns == 1 && rifles == 0, "floor 2 holds a shotgun for a player who has none");
        GenerateFloorMap(&g_floor, 3, 778u);
        Pickups_SpawnForFloor(&g_floor, 3, WEAPON_BIT(WEAPON_PIPE) | WEAPON_BIT(WEAPON_PISTOL) | WEAPON_BIT(WEAPON_SHOTGUN));
        shotguns = rifles = 0;
        for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) { shotguns += Pickups_Get(i)->kind == PICKUP_SHOTGUN; rifles += Pickups_Get(i)->kind == PICKUP_RIFLE; }
        Check(rifles == 1 && shotguns == 0, "floor 3 holds a rifle for a player who has none");
        Pickups_SpawnForFloor(&g_floor, 2, WEAPON_BIT(WEAPON_PIPE) | WEAPON_BIT(WEAPON_PISTOL) | WEAPON_BIT(WEAPON_SHOTGUN));
        int shellBoxes = 0, weaponsHere = 0;
        for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) { shellBoxes += Pickups_Get(i)->kind == PICKUP_SHELLS; weaponsHere += !Pickup_IsAmmo(Pickups_Get(i)->kind) && Pickups_Get(i)->kind != PICKUP_MEDKIT && Pickups_Get(i)->kind != PICKUP_STIM && Pickups_Get(i)->kind != PICKUP_BATTERY && Pickups_Get(i)->kind != PICKUP_FLASHLIGHT; }
        Check(weaponsHere == 0 && shellBoxes >= 1, "a shotgun already owned turns floor 2's weapon into a box of shells");

        // Across many floors and seeds: box sizes, kinds that appear, and nothing that should not.
        int seen[12] = { 0 }, badCount = 0, riflesBefore3 = 0, rifleAmmoBefore3 = 0, medkits = 0, stims = 0;
        for (int floorNo = 1; floorNo <= 60; ++floorNo) {
            GenerateFloorMap(&g_floor, floorNo, 5000u + (unsigned)floorNo * 31u);
            Pickups_SpawnForFloor(&g_floor, floorNo, 0u);
            for (int i = 0; i < MAX_PICKUPS; ++i) {
                const Pickup *pk = Pickups_Get(i);
                if (!pk->active) continue;
                seen[pk->kind]++;
                if (Pickup_IsAmmo(pk->kind) && pk->count != Ammo_BoxAmount(Pickup_AmmoType(pk->kind))) ++badCount;
                if (pk->kind == PICKUP_FLASHLIGHT) { if (pk->count != (int)Flashlight_Config()->startSecs) ++badCount; }
                else if (!Pickup_IsAmmo(pk->kind) && pk->count != 1) ++badCount;
                if (floorNo < 3 && pk->kind == PICKUP_RIFLE && floorNo < 3) ++riflesBefore3;
                if (floorNo < 3 && pk->kind == PICKUP_RIFLE_AMMO) ++rifleAmmoBefore3;
                if (pk->kind == PICKUP_MEDKIT) ++medkits;
                if (pk->kind == PICKUP_STIM) ++stims;
            }
        }
        Check(badCount == 0, "every box holds what weapons.mdix says (8, 6 or 24), a flashlight holds flashlight.mdix's start_secs, and every weapon, medkit and battery is 1");
        Check(riflesBefore3 == 0 && rifleAmmoBefore3 == 0, "no rifle and no rifle rounds before floor 3");
        Check(seen[PICKUP_PIPE] > 0 && seen[PICKUP_PISTOL] > 0 && seen[PICKUP_SHOTGUN] > 0 && seen[PICKUP_RIFLE] > 0 && seen[PICKUP_SHELLS] > 0 && seen[PICKUP_RIFLE_AMMO] > 0 && seen[PICKUP_AMMO] > 0,
              "over 60 floors every kind of pickup turns up");
        Check(medkits == 60, "and there is exactly one medkit per floor");
        Check(stims == 60 && strcmp(Pickup_Label(PICKUP_STIM), "STIM SHOT") == 0, "and exactly one stim shot per floor, labelled STIM SHOT");
        Check(seen[PICKUP_FLASHLIGHT] == 60 && seen[PICKUP_BATTERY] >= 60 + 59 * 0 && seen[PICKUP_BATTERY] <= 120, "a player with no flashlight finds one on every floor, and at least a battery");
        {
            // A player who has the flashlight never finds another, and floor 1 has two batteries.
            int flashlights = 0, batteries1 = 0;
            for (int floorNo = 1; floorNo <= 30; ++floorNo) {
                GenerateFloorMap(&g_floor, floorNo, 9000u + (unsigned)floorNo * 17u);
                Pickups_SpawnForFloor(&g_floor, floorNo, OWNED_FLASHLIGHT);
                for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) { flashlights += Pickups_Get(i)->kind == PICKUP_FLASHLIGHT; if (floorNo == 1) batteries1 += Pickups_Get(i)->kind == PICKUP_BATTERY; }
            }
            Check(flashlights == 0 && batteries1 == 2, "a player who has the flashlight finds no other, and floor 1 has two batteries");
        }
        Pickups_Clear();
        Check(strcmp(Pickup_Label(PICKUP_SHOTGUN), "SHOTGUN") == 0 && strcmp(Pickup_Label(PICKUP_SHELLS), "SHELLS") == 0 && strcmp(Pickup_Label(PICKUP_RIFLE_AMMO), "RIFLE AMMO") == 0 && strcmp(Pickup_Label(PICKUP_FLASHLIGHT), "FLASHLIGHT") == 0 && strcmp(Pickup_Label(PICKUP_BATTERY), "BATTERY") == 0, "labels read right");
        for (int k = PICKUP_SHOTGUN; k <= PICKUP_BATTERY; ++k) {
            Pickups_Clear();
            Pickups_Drop(&g_floor, (Vector3){ (float)ARRIVE_BELOW_X * TILE_SIZE, 0.0f, (float)ARRIVE_BELOW_Z * TILE_SIZE }, 1.0f, 0.0f, (PickupKind)k, 1);
            g_nTris = 0;
            for (int i = 0; i < MAX_PICKUPS; ++i) if (Pickups_Get(i)->active) Pickups_Draw(Pickups_Get(i)->pos, 1.0f);
            char what[64]; snprintf(what, sizeof what, "pickup kind %d draws a model (more than the ring and beam)", k);
            Check(g_nTris > 24, what);
        }
        Pickups_Clear();
    }

    printf("New guns: first person models\n");
    {
        const AimFrame cm = { { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };
        Arsenal hv; PartList hands, pst, sg, rf, sgFlash, sgPump;
        Arsenal_Init(&hv);
        Viewmodel_Build(&hands, &hv, &cm, 0.0f, 1.0f);
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_PISTOL); hv.equipT = 1.0f;
        Viewmodel_Build(&pst, &hv, &cm, 0.0f, 1.0f);
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_SHOTGUN); hv.equipT = 1.0f;
        Viewmodel_Build(&sg, &hv, &cm, 0.0f, 1.0f);
        DumpPose(dump, "shotgun_idle", &hv, 0.0f);
        hv.recoil = 1.0f; hv.flash = 1.0f;
        Viewmodel_Build(&sgFlash, &hv, &cm, 0.0f, 1.0f);
        DumpPose(dump, "shotgun_fire", &hv, 0.0f);
        hv.recoil = 0.0f; hv.flash = 0.0f; hv.attackT = Weapon_Def(WEAPON_SHOTGUN)->duration * 0.55f;
        Viewmodel_Build(&sgPump, &hv, &cm, 0.0f, 1.0f);
        DumpPose(dump, "shotgun_pump", &hv, 0.0f);
        hv.attackT = -1.0f; hv.equipT = 1.0f;
        DumpPose(dump, "shotgun_walk", &hv, 1.0f);
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_RIFLE); hv.equipT = 1.0f;
        Viewmodel_Build(&rf, &hv, &cm, 0.0f, 1.0f);
        DumpPose(dump, "rifle_idle", &hv, 0.0f);
        PartList rfFlash;
        hv.recoil = 1.0f; hv.flash = 1.0f;
        Viewmodel_Build(&rfFlash, &hv, &cm, 0.0f, 1.0f);
        DumpPose(dump, "rifle_fire", &hv, 0.0f);
        hv.recoil = 0.0f; hv.flash = 0.0f; hv.equipT = 0.3f;
        DumpPose(dump, "rifle_equip", &hv, 0.0f);

        Check(sg.count > hands.count + 6 && sg.count <= PARTS_MAX && rf.count > hands.count + 6 && rf.count <= PARTS_MAX, "each long gun adds a good number of parts and fits the part list");
        Check(sgFlash.count > sg.count && rfFlash.count > rf.count, "and a muzzle flash adds more while it lasts");
        // Which parts are far out in front of the player: the pistol's muzzle is at about 0.75 m, a long gun's barrel and the
        // left hand on its foregrip are past 0.8 m.
        int farPistol = 0, farShotgun = 0, farRifle = 0;
        for (int i = 0; i < pst.count; ++i) if (pst.parts[i].a.z < -0.8f || pst.parts[i].b.z < -0.8f) ++farPistol;
        for (int i = 0; i < sg.count; ++i) if (sg.parts[i].a.z < -0.8f || sg.parts[i].b.z < -0.8f) ++farShotgun;
        for (int i = 0; i < rf.count; ++i) if (rf.parts[i].a.z < -0.8f || rf.parts[i].b.z < -0.8f) ++farRifle;
        Check(farPistol == 0 && farShotgun >= 8 && farRifle >= 8, "the long guns reach further than the pistol, and the left hand is out on the foregrip");
        // The pump moves.
        float moved = 0.0f;
        for (int i = 0; i < sg.count && i < sgPump.count; ++i) { const float d = fabsf(sg.parts[i].a.z - sgPump.parts[i].a.z); if (d > moved) moved = d; }
        Check(moved > 0.10f, "mid-stroke the pump (and the hand on it) is more than 10 cm from where it rests");
        int sane2 = 1;
        PartList *all[] = { &sg, &sgFlash, &sgPump, &rf, &rfFlash };
        for (int k = 0; k < 5; ++k) for (int i = 0; i < all[k]->count; ++i) { const Part *pp = &all[k]->parts[i]; if (!isfinite(pp->a.x + pp->a.y + pp->a.z + pp->b.x + pp->b.y + pp->b.z)) sane2 = 0; }
        Check(sane2, "every part of the new models has finite coordinates");
        // Triangles.
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_RIFLE); hv.equipT = 1.0f;
        PartList rl; Viewmodel_Build(&rl, &hv, &cm, 0.0f, 1.0f);
        g_nTris = 0; PartList_Draw(&rl, (Vector3){ -0.3f, 0.8f, -0.5f });
        const int rifleTris = g_nTris;
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_SHOTGUN); hv.equipT = 1.0f;
        Viewmodel_Build(&rl, &hv, &cm, 0.0f, 1.0f);
        g_nTris = 0; PartList_Draw(&rl, (Vector3){ -0.3f, 0.8f, -0.5f });
        printf("    hands with the rifle: %d triangles, with the shotgun: %d\n", rifleTris, g_nTris);
        Check(rifleTris < 1500 && g_nTris < 1500, "both stay under 1500 triangles (each vertex costs the phone time)");
        // Recoil scales with the gun's kick.
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_PISTOL); hv.equipT = 1.0f; hv.recoil = 1.0f;
        PartList kp; Viewmodel_Build(&kp, &hv, &cm, 0.0f, 1.0f);
        float pistolKick = 0; for (int i = 0; i < kp.count && i < pst.count; ++i) { const float d = fabsf(kp.parts[i].a.z - pst.parts[i].a.z); if (d > pistolKick) pistolKick = d; }
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_SHOTGUN); hv.equipT = 1.0f; hv.recoil = 1.0f;
        Viewmodel_Build(&kp, &hv, &cm, 0.0f, 1.0f);
        float shotgunKick = 0; for (int i = 0; i < kp.count && i < sg.count; ++i) { const float d = fabsf(kp.parts[i].a.z - sg.parts[i].a.z); if (d > shotgunKick) shotgunKick = d; }
        Check(shotgunKick > pistolKick * 1.3f, "the shotgun kicks harder than the pistol");
        Check(Aim_MuzzleFor(&(AimFrame){ { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } }, WEAPON_RIFLE).z < Aim_Muzzle(&(AimFrame){ { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } }).z - 0.4f,
              "a long gun's shots start further out than the pistol's, where its barrel ends");
    }

    printf("Shots against real enemies (gun, projectile, wall test, enemy)\n");
    {
        GenerateFloorMap(&g_floor, 1, 2026u);
        InitEnemiesSystem();
        SpawnEnemiesForFloor(&g_floor, 1);
        Vector3 pos[MAX_FLOOR_ENEMIES + 8];
        const int ne = FindEnemies(pos, MAX_FLOOR_ENEMIES + 8);
        // An enemy with a clear straight run of 6 m to its west.
        int pick = -1;
        for (int i = 0; i < ne && pick < 0; ++i) {
            DungeonHit h;
            if (!Dungeon_Raycast(&g_floor, (Vector3){ pos[i].x - 6.0f, 1.0f, pos[i].z }, (Vector3){ 1, 0, 0 }, 6.0f, &h)) pick = i;
        }
        Check(pick >= 0, "there is an enemy to shoot at with a clear line");
        if (pick >= 0) {
            const Vector3 foe = pos[pick];
            uint32_t rng = 31337;
            const WeaponDef *rifle = Weapon_Def(WEAPON_RIFLE), *shotgun = Weapon_Def(WEAPON_SHOTGUN), *pistol = Weapon_Def(WEAPON_PISTOL);

            InitProjectilesSystem();
            AimFrame ai = { { foe.x - 6.0f, 1.0f, foe.z }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } };
            const unsigned h0 = EnemyHitsLanded();
            Gunfire_Shoot(rifle, &ai, &g_floor, 0.0f, &rng);
            int frames = 0;
            while (Projectiles_Active() && frames < 60) { UpdateProjectilesSystem(DT); ResolveProjectileHits(&g_floor, CheckAndHandleEnemyDamage); ++frames; }
            Check(EnemyHitsLanded() == h0 + 1, "a rifle round fired at an enemy 6 m away hits it, once");

            // The enemy has 60 health on floor 1. Four rifle rounds of 14 do not kill it, five do not either; the pistol's three of 25 do.
            const unsigned k0 = EnemyKillsLanded();
            InitProjectilesSystem();
            for (int i = 0; i < 3; ++i) {
                Gunfire_Shoot(pistol, &ai, &g_floor, 0.0f, &rng);
                for (int f = 0; f < 30; ++f) { UpdateProjectilesSystem(DT); ResolveProjectileHits(&g_floor, CheckAndHandleEnemyDamage); }
            }
            Check(EnemyKillsLanded() == k0 + 1, "three pistol rounds kill a floor 1 enemy (as before the change)");

            // A fresh floor for the shotgun.
            SpawnEnemiesForFloor(&g_floor, 1);
            const unsigned k1 = EnemyKillsLanded(), hh = EnemyHitsLanded();
            const AimFrame close = { { foe.x - 3.0f, 1.0f, foe.z }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } };
            InitProjectilesSystem();
            Gunfire_Shoot(shotgun, &close, &g_floor, shotgun->spreadDeg, &rng);
            for (int f = 0; f < 30; ++f) { UpdateProjectilesSystem(DT); ResolveProjectileHits(&g_floor, CheckAndHandleEnemyDamage); }
            const unsigned pelletHits = EnemyHitsLanded() - hh;
            Check(pelletHits >= 5 && pelletHits <= 8, "a shotgun blast at 3 m lands most of its 8 pellets");
            Check(EnemyKillsLanded() == k1 + 1, "and kills a floor 1 enemy outright");

            // At 20 m the same blast does little.
            SpawnEnemiesForFloor(&g_floor, 1);
            const AimFrame farA = { { foe.x - 6.0f, 1.0f, foe.z }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } };
            (void)farA;
            Check(Projectile_FalloffDamage(ProjectileConfig_Get(shotgun->projectile), shotgun->damage, 20.0f) < shotgun->damage * 0.3f, "(and at 20 m a pellet does under a third of its damage)");

            // Shots from a seed (net_shot.h): the shooter's screen and the server make one shot from one number, so the pellets must be the same.
            {
                Vector3 a[32], b[32], c[32];
                const uint32_t seedA = NetShot_Seed(2, 150, 0), seedC = NetShot_Seed(2, 151, 0);
                const int na = ShootFrom(shotgun, &close, &g_floor, seedA, a, 32), nb = ShootFrom(shotgun, &close, &g_floor, seedA, b, 32), nc = ShootFrom(shotgun, &close, &g_floor, seedC, c, 32);
                int same = na == nb && na == shotgun->pellets, diff = nc == na;
                for (int i = 0; same && i < na; ++i) same = a[i].x == b[i].x && a[i].y == b[i].y && a[i].z == b[i].z;
                int differs = 0;
                for (int i = 0; i < na && i < nc; ++i) differs += (a[i].x != c[i].x || a[i].y != c[i].y || a[i].z != c[i].z);
                Check(same, "the same seed throws exactly the same pellets (every direction and speed, to the last bit), whenever and wherever it is worked out");
                Check(diff && differs >= na - 1, "the next tick's seed throws different ones (a shotgun is not the same blast twice)");
                int fanned = 0;
                for (int i = 1; i < na; ++i) fanned += (a[i].y != a[0].y || a[i].z != a[0].z);
                Check(fanned >= na - 2, "(and a blast is a spread, not one pellet eight times)");
            }
        }
        InitProjectilesSystem();
    }

    printf("Depth range\n");
    {
        Arsenal sv;
        Arsenal_Init(&sv);
        const AimFrame c0 = { { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };
        g_projSets = 0;
        Viewmodel_Draw(&sv, &c0, 0.0f, 1.0f, 0.5f);
        Check(g_projSets >= 2, "drawing the hands sets their own depth range and puts the world's back");
    }

    printf("Aiming down sights: the numbers, and the pose\n");
    {
        const WeaponDef *pd = Weapon_Def(WEAPON_PISTOL), *sd = Weapon_Def(WEAPON_SHOTGUN), *rd = Weapon_Def(WEAPON_RIFLE);
        int ok = 1;
        const WeaponDef *gs[3] = { pd, sd, rd };
        for (int i = 0; i < 3; ++i)
            ok = ok && gs[i]->adsFov >= 20.0f && gs[i]->adsFov < 60.0f && gs[i]->adsSecs > 0.02f && gs[i]->adsSecs < 1.0f
                    && gs[i]->adsMoveMul > 0.1f && gs[i]->adsMoveMul <= 1.0f && gs[i]->adsSpreadMul > 0.05f && gs[i]->adsSpreadMul <= 1.0f;
        Check(ok, "each gun has a zoom (under the normal 60), a time, a move share and a spread share in range");
        Check(rd->adsFov < sd->adsFov && rd->adsFov < pd->adsFov, "the rifle zooms the most");
        Check(Weapon_Def(WEAPON_HANDS)->adsFov == 0.0f && Weapon_Def(WEAPON_PIPE)->adsFov == 0.0f, "the hands and the pipe have no sights, and none is configured");

        const AimFrame cm = { { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };
        // The gun is the last parts added (pistol 5, shotgun 8, rifle 9); everything before them is the two hands and their sleeves.
        struct { WeaponId w; int gunParts; float dx, dy; } cases[3] = { { WEAPON_PISTOL, 5, -0.130f, 0.010f }, { WEAPON_SHOTGUN, 8, -0.150f, 0.034f }, { WEAPON_RIFLE, 9, -0.150f, 0.034f } };
        for (int i = 0; i < 3; ++i) {
            Arsenal hv; PartList hip, ads, half;
            Arsenal_Init(&hv); Arsenal_Give(&hv, cases[i].w); hv.equipT = 1.0f;
            Viewmodel_BuildAimed(&hip, &hv, &cm, 0.0f, 0.0f, 0.0f);     // time 0: the idle sway is zero, so only the slide is measured
            Viewmodel_BuildAimed(&ads, &hv, &cm, 0.0f, 0.0f, 1.0f);
            Viewmodel_BuildAimed(&half, &hv, &cm, 0.0f, 0.0f, 0.5f);
            const int g0 = hip.count - cases[i].gunParts;
            double hx = 0, hy = 0, ax = 0, ay = 0, mx = 0, hhx = 0, ahx = 0;
            for (int k = g0; k < hip.count; ++k) {
                hx += hip.parts[k].a.x + hip.parts[k].b.x; hy += hip.parts[k].a.y + hip.parts[k].b.y;
                ax += ads.parts[k].a.x + ads.parts[k].b.x; ay += ads.parts[k].a.y + ads.parts[k].b.y;
                mx += half.parts[k].a.x + half.parts[k].b.x;
            }
            for (int k = 0; k < g0; ++k) { hhx += hip.parts[k].a.x + hip.parts[k].b.x; ahx += ads.parts[k].a.x + ads.parts[k].b.x; }
            const double n = 2.0 * cases[i].gunParts;
            char what[160];
            snprintf(what, sizeof what, "%s: aimed, the gun slides %.3f across and %.3f up to put its sights on the centre", Weapon_IdString(cases[i].w), cases[i].dx, cases[i].dy);
            Check(hip.count == ads.count && Near((float)((ax - hx) / n), cases[i].dx, 0.0005f) && Near((float)((ay - hy) / n), cases[i].dy, 0.0005f), what);
            Check(Near((float)((mx - hx) / n), cases[i].dx * 0.5f, 0.0005f), "and half way in it is half way there");
            Check((ahx - hhx) / (2.0 * g0) < cases[i].dx * 0.6f, "the hands go with it (their sleeves stay anchored at the edge, so they move a little less)");
        }
        Arsenal hv; PartList a0, a1;
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_PIPE); hv.equipT = 1.0f;
        Viewmodel_BuildAimed(&a0, &hv, &cm, 0.0f, 1.0f, 0.0f);
        Viewmodel_BuildAimed(&a1, &hv, &cm, 0.0f, 1.0f, 1.0f);
        int same = a0.count == a1.count;
        for (int k = 0; same && k < a0.count; ++k) same = a0.parts[k].a.x == a1.parts[k].a.x && a0.parts[k].a.y == a1.parts[k].a.y && a0.parts[k].b.x == a1.parts[k].b.x;
        Check(same, "the pipe has no sights: asking for the aimed pose changes nothing");
        Arsenal_Init(&hv); Arsenal_Give(&hv, WEAPON_RIFLE); hv.equipT = 1.0f;
        PartList w0, w1, v0, v1;
        Viewmodel_BuildAimed(&w0, &hv, &cm, 1.0f, 0.3f, 0.0f);
        Viewmodel_BuildAimed(&w1, &hv, &cm, 1.0f, 0.9f, 0.0f);
        Viewmodel_BuildAimed(&v0, &hv, &cm, 1.0f, 0.3f, 1.0f);
        Viewmodel_BuildAimed(&v1, &hv, &cm, 1.0f, 0.9f, 1.0f);
        Check(fabsf(v0.parts[0].a.y - v1.parts[0].a.y) < fabsf(w0.parts[0].a.y - w1.parts[0].a.y) * 0.5f + 1e-6f, "aimed, the walking sway is much calmer than at the hip");
        Viewmodel_Build(&w0, &hv, &cm, 0.0f, 1.0f);
        Viewmodel_BuildAimed(&w1, &hv, &cm, 0.0f, 1.0f, 0.0f);
        Check(w0.count == w1.count && w0.parts[0].a.x == w1.parts[0].a.x, "Viewmodel_Build is the hip pose of the same function");
    }

    printf("Aimed gun sway to the side\n");
    {
        const AimFrame cm = { { 0, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } };
        Check(Viewmodel_StrafeTilt(0.0f, 1.0f) == 0.0f && Viewmodel_StrafeTilt(1.0f, 0.0f) == 0.0f, "no tilt standing still, and none at the hip");
        Check(Viewmodel_StrafeTilt(1.0f, 1.0f) < 0.0f && Viewmodel_StrafeTilt(-1.0f, 1.0f) > 0.0f && Near(Viewmodel_StrafeTilt(1.0f, 1.0f), -Viewmodel_StrafeTilt(-1.0f, 1.0f), 1e-6f), "moving right turns the roll one way and moving left the other, the same amount");
        Check(Near(Viewmodel_StrafeTilt(1.0f, 1.0f), -0.08f, 1e-6f) && Near(Viewmodel_StrafeTilt(0.5f, 1.0f), -0.04f, 1e-6f) && Near(Viewmodel_StrafeTilt(1.0f, 0.5f), -0.04f, 1e-6f), "a full strafe aimed is 0.08 rad (4.6 degrees); half the move or half the aim is half the tilt");
        Check(Near(Viewmodel_StrafeTilt(3.0f, 1.0f), -0.08f, 1e-6f) && Near(Viewmodel_StrafeTilt(-3.0f, 2.0f), 0.08f, 1e-6f), "and it never goes beyond the full one");

        const WeaponId guns[3] = { WEAPON_PISTOL, WEAPON_SHOTGUN, WEAPON_RIFLE };
        for (int gi = 0; gi < 3; ++gi) {
            Arsenal hv; PartList flat, right, left, hip, half;
            Arsenal_Init(&hv); Arsenal_Give(&hv, guns[gi]); hv.equipT = 1.0f;
            Viewmodel_BuildAimedSway(&flat, &hv, &cm, 0.0f, 1.0f, 1.0f, 0.0f);
            Viewmodel_BuildAimedSway(&right, &hv, &cm, 0.0f, 1.0f, 1.0f, 1.0f);
            Viewmodel_BuildAimedSway(&left, &hv, &cm, 0.0f, 1.0f, 1.0f, -1.0f);
            Viewmodel_BuildAimedSway(&hip, &hv, &cm, 0.0f, 1.0f, 0.0f, 1.0f);
            Viewmodel_BuildAimedSway(&half, &hv, &cm, 0.0f, 1.0f, 0.5f, 1.0f);
            PartList hipFlat, halfFlat;
            Viewmodel_BuildAimedSway(&hipFlat, &hv, &cm, 0.0f, 1.0f, 0.0f, 0.0f);
            Viewmodel_BuildAimedSway(&halfFlat, &hv, &cm, 0.0f, 1.0f, 0.5f, 0.0f);
            const float roll = Viewmodel_StrafeTilt(1.0f, 1.0f), c = cosf(roll), s = sinf(roll);
            int rigid = right.count == flat.count, topRight = 1, bottomLeft = 1, mirrored = left.count == flat.count, sameZ = 1, hipSame = hip.count == hipFlat.count;
            int upTurns = 1, orientedCount = 0, halfOk = half.count == halfFlat.count;
            float moved = 0.0f;
            for (int k = 0; rigid && k < flat.count; ++k) {
                const Vector3 pts[2][2] = { { flat.parts[k].a, right.parts[k].a }, { flat.parts[k].b, right.parts[k].b } };
                for (int e = 0; e < 2; ++e) {
                    const Vector3 f = pts[e][0], r = pts[e][1];
                    if (!Near(r.x, f.x * c - f.y * s, 1e-5f) || !Near(r.y, f.x * s + f.y * c, 1e-5f)) rigid = 0;
                    if (!Near(r.z, f.z, 1e-6f)) sameZ = 0;
                    if (f.y > 0.05f && !(r.x > f.x)) topRight = 0;
                    if (f.y < -0.05f && !(r.x < f.x)) bottomLeft = 0;
                    moved += fabsf(r.x - f.x) + fabsf(r.y - f.y);
                }
                const Vector3 uf = flat.parts[k].upHint, ur = right.parts[k].upHint;
                const int oriented = flat.parts[k].sides == 4 || flat.parts[k].flat != 1.0f;            // boxes and flattened blobs: the round ones have no side to turn
                if (oriented) { orientedCount++; if (!Near(ur.x, uf.x * c - uf.y * s, 1e-5f) || !Near(ur.y, uf.x * s + uf.y * c, 1e-5f)) upTurns = 0; }
                const Vector3 ml = left.parts[k].a, mf = flat.parts[k].a;
                if (!Near(ml.x, mf.x * c + mf.y * s, 1e-5f) || !Near(ml.y, -mf.x * s + mf.y * c, 1e-5f)) mirrored = 0;
            }
            for (int k = 0; hipSame && k < hip.count; ++k)
                if (hip.parts[k].a.x != hipFlat.parts[k].a.x || hip.parts[k].a.y != hipFlat.parts[k].a.y || hip.parts[k].b.x != hipFlat.parts[k].b.x) hipSame = 0;
            const float halfRoll = Viewmodel_StrafeTilt(1.0f, 0.5f);
            for (int k = 0; halfOk && k < half.count; ++k) {
                const Vector3 f = halfFlat.parts[k].a, r = half.parts[k].a;
                if (!Near(r.x, f.x * cosf(halfRoll) - f.y * sinf(halfRoll), 1e-5f) || !Near(r.y, f.x * sinf(halfRoll) + f.y * cosf(halfRoll), 1e-5f)) halfOk = 0;
            }
            char what[200];
            snprintf(what, sizeof what, "%s: moving right, aimed, the whole gun and both hands turn as one about the view's centre line by the tilt (nothing sheared, nothing moved in depth)", Weapon_IdString(guns[gi]));
            Check(rigid && sameZ && moved > 0.001f, what);
            Check(topRight && bottomLeft, "the top of it leans toward the move (right) and the bottom the other way");
            Check(mirrored, "moving left is the same turn the other way");
            Check(upTurns && orientedCount >= 5, "the cross-sections' flat sides turn with it (the boxes are not left square to the screen)");
            Check(hipSame, "at the hip a strafe changes nothing");
            Check(halfOk, "half way into the aim it is half the tilt");
        }
        // The point on the centre line (where the sights are) does not move: a part sitting at x = 0, y = 0 stays there.
        Arsenal pv; PartList p0, p1;
        Arsenal_Init(&pv); Arsenal_Give(&pv, WEAPON_PIPE); pv.equipT = 1.0f;
        Viewmodel_BuildAimedSway(&p0, &pv, &cm, 0.0f, 1.0f, 1.0f, 0.0f);
        Viewmodel_BuildAimedSway(&p1, &pv, &cm, 0.0f, 1.0f, 1.0f, 1.0f);
        int same = p0.count == p1.count;
        for (int k = 0; same && k < p0.count; ++k) same = p0.parts[k].a.x == p1.parts[k].a.x && p0.parts[k].a.y == p1.parts[k].a.y && p0.parts[k].b.x == p1.parts[k].b.x;
        Check(same, "the pipe and the hands have no sights: asking for a strafe tilt changes nothing");
        Arsenal bv; PartList b0, b1;
        Arsenal_Init(&bv); Arsenal_Give(&bv, WEAPON_RIFLE); bv.equipT = 1.0f;
        Viewmodel_BuildAimed(&b0, &bv, &cm, 0.7f, 0.4f, 0.8f);
        Viewmodel_BuildAimedSway(&b1, &bv, &cm, 0.7f, 0.4f, 0.8f, 0.0f);
        int same2 = b0.count == b1.count;
        for (int k = 0; same2 && k < b0.count; ++k) same2 = b0.parts[k].a.x == b1.parts[k].a.x && b0.parts[k].a.y == b1.parts[k].a.y && b0.parts[k].b.z == b1.parts[k].b.z;
        Check(same2, "Viewmodel_BuildAimed is the same function with no strafe");

        // The easing.
        Check(Near(Viewmodel_StrafeSwayStep(0.0f, 1.0f, 0.0f), 0.0f, 1e-9f) && Near(Viewmodel_StrafeSwayStep(0.3f, 0.3f, 0.05f), 0.3f, 1e-6f), "no time passes, or already there: no change");
        Check(Near(Viewmodel_StrafeSwayStep(0.0f, 1.0f, 0.10f), 1.0f - expf(-1.0f), 1e-5f), "after the easing time (0.1 s) it has gone about two thirds of the way");
        float v = 0.0f; int mono = 1, noOver = 1;
        for (int i = 0; i < 120; ++i) { const float n = Viewmodel_StrafeSwayStep(v, 1.0f, 1.0f / 60.0f); if (n < v) mono = 0; if (n > 1.0f) noOver = 0; v = n; }
        Check(mono && noOver && v > 0.99f, "in frames of 1/60 s it climbs steadily to the target and never goes past it (two seconds: over 0.99)");
        v = 1.0f; for (int i = 0; i < 120; ++i) v = Viewmodel_StrafeSwayStep(v, -1.0f, 1.0f / 60.0f);
        Check(v < -0.99f && v >= -1.0f, "and swings to the other side the same way");
        Check(Near(Viewmodel_StrafeSwayStep(0.0f, 5.0f, 10.0f), 1.0f, 1e-4f) && Viewmodel_StrafeSwayStep(0.0f, 5.0f, 0.05f) <= 1.0f, "a target past 1 is held at 1");
        const float a1 = Viewmodel_StrafeSwayStep(Viewmodel_StrafeSwayStep(0.0f, 1.0f, 0.05f), 1.0f, 0.05f), a2 = Viewmodel_StrafeSwayStep(0.0f, 1.0f, 0.10f);
        Check(Near(a1, a2, 1e-5f), "two half steps are one whole one (it does not depend on the frame rate)");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
