// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/testing.md, section "tests/gvex_test.c"
// ============================================================================
// Headless test of the server's world of creatures (src/gvex/gvex_world.c, Flecs): the spawns (the same places and looks the single-player game gives
// its cylinder enemies), what a Ghoul senses (sight with a clear line, noise through walls, a few seconds of staying after what it sensed), how it
// walks (at its grade's speed, never into a wall), that two worlds in one process do not touch each other, that the same inputs give the same world,
// and that a creature can be hurt and killed. It needs the Flecs object, so it is built by tests/gvex_check.sh (which exits 77, SKIP, where the
// build is not possible); its name ends in _test, not _headless, so that run_tests.sh does not build it with a plain line:
//   cc -O1 -std=c99 -DGVEX_HEADLESS_TEST -DTOWER_GVEX=1 -Isrc -Iraylib/src -Ithird_party/flecs tests/gvex_test.c src/gvex/gvex_world.c third_party/flecs/tob_flecs.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -lpthread -o gvex_test
#ifdef GVEX_HEADLESS_TEST
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dungeon/dungeon_generator.h"
#include "enemies/enemy_manager.h"
#include "enemies/enemy_plan.h"
#include "enemies/enemy_types.h"
#include "gvex/gvex_world.h"

// The staircase's drawing calls are never made here: stand-ins, as in the other headless tests.
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }

static int g_failures = 0, g_checks = 0;
static void Check(int ok, const char *what) { ++g_checks; printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

#define DT (1.0f / 30.0f)
static FloorMap g_map, g_mapB;

// Records compared field by field: a struct's padding bytes are not part of its value (and are not the same on every machine).
static int SameRecords(const GvexRecord *a, const GvexRecord *b, int n)
{
    for (int i = 0; i < n; ++i)
        if (a[i].id != b[i].id || a[i].kind != b[i].kind || a[i].variant != b[i].variant || a[i].state != b[i].state ||
            a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z || a[i].yaw != b[i].yaw || a[i].health != b[i].health || a[i].maxHealth != b[i].maxHealth) return 0;
    return 1;
}

static const GvexRecord *ById(const GvexRecord *r, int n, int id) { for (int i = 0; i < n; ++i) if (r[i].id == id) return &r[i]; return NULL; }
static float Dist(const GvexRecord *r, float x, float z) { return hypotf(r->x - x, r->z - z); }
static GvexTarget Target(float x, float z, float noise) { return (GvexTarget){ true, x, 0.0f, z, noise }; }

// A floor tile centre at 3 to 11 metres from (x, z), with (clear == true) or without a clear line from it, that nothing blocks a 0.8 m body to stand on.
static bool FindSpotAt(float x, float z, bool clear, float dMin, float dMax, float *ox, float *oz)
{
    for (int tz = 1; tz < g_map.h - 1; ++tz)
        for (int tx = 1; tx < g_map.w - 1; ++tx) {
            const float px = (float)tx * TILE_SIZE, pz = (float)tz * TILE_SIZE;
            const float d = hypotf(px - x, pz - z);
            if (d < dMin || d > dMax) continue;
            if (EnemyWalk_Blocked(&g_map, px, pz, 0.5f)) continue;
            if (EnemyWalk_LineOfSight(&g_map, (Vector3){ x, 0, z }, (Vector3){ px, 0, pz }) != clear) continue;
            *ox = px; *oz = pz;
            return true;
        }
    return false;
}

static bool FindSpot(float x, float z, bool clear, float *ox, float *oz) { return FindSpotAt(x, z, clear, 5.0f, 11.0f, ox, oz); }

int main(void)
{
    GenerateFloorMap(&g_map, 3, 424242u);
    GenerateFloorMap(&g_mapB, 3, 777u);
    const int tier = 3;
    GvexRecord rec[GVEX_MAX_CREATURES];

    printf("the spawns\n");
    {
        GvexWorld *w = GvexWorld_Create(&g_map, tier);
        Check(w != NULL, "a world is made from a floor");
        EnemySpawn plan[GVEX_MAX_CREATURES];
        const int want = EnemyPlan_Spawns(&g_map, tier, plan, GVEX_MAX_CREATURES);
        const int n = GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(n == want && n == GvexWorld_Count(w) && n == 6 + 2 * tier, "as many creatures as the single-player game would spawn on that floor (6 + 2 * tier)");
        int same = 1, idle = 1, ids = 1;
        for (int i = 0; i < n; ++i) {
            same &= fabsf(rec[i].x - plan[i].x) < 1e-4f && fabsf(rec[i].z - plan[i].z) < 1e-4f;
            idle &= rec[i].state == GVEX_STATE_IDLE;
            ids &= rec[i].id == i && rec[i].kind == GVEX_KIND_GHOUL && rec[i].variant < GVEX_GRADES && rec[i].health > 0.0f && rec[i].health == rec[i].maxHealth;
        }
        Check(same, "at the same places, in the same order");
        Check(idle && ids, "idle at first, numbered from 0, all Ghouls of a known grade with full health");
        Check(rec[0].maxHealth == 40.0f + 20.0f * (float)tier, "their health is the cylinder enemies' for the tier");
        Check(strcmp(Gvex_Designation(GVEX_KIND_GHOUL), "G.V.E.X.-X0-01") == 0 && strcmp(Gvex_Name(GVEX_KIND_GHOUL), "Ghouls") == 0 && strcmp(Gvex_Designation(200), "?") == 0,
              "the designation and name of the dossier, and a question mark for a byte that is no kind");
        Check(GvexWorld_Create(NULL, 1) == NULL, "no floor, no world");
        {   // a high floor: the cap of the solo game (MAX_FLOOR_ENEMIES), not the 32 a snapshot could carry
            static FloorMap high;
            GenerateFloorMap(&high, 20, 99u);
            GvexWorld *wh = GvexWorld_Create(&high, 20);
            Check(wh && GvexWorld_Count(wh) == MAX_FLOOR_ENEMIES && MAX_FLOOR_ENEMIES <= GVEX_MAX_CREATURES, "on floor 20 there are as many as the solo game's cap (24), and no more");
            GvexWorld_Destroy(wh);
        }
        GvexWorld_Destroy(w);
        GvexWorld_Destroy(NULL);
    }

    printf("grades\n");
    {
        int seen[GVEX_GRADES] = { 0, 0, 0 };
        for (uint32_t seed = 1; seed <= 12; ++seed) {
            GenerateFloorMap(&g_mapB, 5, seed * 7919u);
            GvexWorld *w = GvexWorld_Create(&g_mapB, 5);
            const int n = GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
            for (int i = 0; i < n; ++i) ++seen[rec[i].variant];
            GvexWorld_Destroy(w);
        }
        Check(seen[0] > 0 && seen[1] > 0 && seen[2] > 0, "all three mobility grades turn up across floors");
        Check(Gvex_GradeSpeedFactor(0) < Gvex_GradeSpeedFactor(1) && Gvex_GradeSpeedFactor(1) < Gvex_GradeSpeedFactor(2) && Gvex_GradeSpeedFactor(9) == 1.0f,
              "slow < medium < runner, and an unknown grade is medium");
        GenerateFloorMap(&g_mapB, 3, 777u);
    }

    printf("nothing to sense\n");
    {
        GvexWorld *w = GvexWorld_Create(&g_map, tier);
        GvexRecord before[GVEX_MAX_CREATURES];
        const int n = GvexWorld_Records(w, before, GVEX_MAX_CREATURES);
        for (int i = 0; i < 90; ++i) GvexWorld_Step(w, NULL, 0, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(SameRecords(before, rec, n), "with no player in the game nobody moves or wakes");
        GvexTarget absent = { false, 0, 0, 0, 99.0f };
        for (int i = 0; i < 30; ++i) GvexWorld_Step(w, &absent, 1, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(SameRecords(before, rec, n), "and a player who is not there (not started, or gone) is nobody");
        GvexWorld_Destroy(w);
    }

    printf("sight, hearing and staying\n");
    {
        GvexWorld *w = GvexWorld_Create(&g_map, tier);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        const GvexRecord g0 = rec[0];
        float sx, sz;
        Check(FindSpot(g0.x, g0.z, true, &sx, &sz), "(a spot in clear view of the first creature, 5 to 11 m away, was found)");
        GvexTarget t = Target(sx, sz, 0.0f);
        GvexWorld_Step(w, &t, 1, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        const GvexRecord *a = ById(rec, GvexWorld_Count(w), 0);
        const float sightRange = 14.0f;
        Check(a && a->state == GVEX_STATE_CHASE && hypotf(sx - g0.x, sz - g0.z) <= sightRange, "a player in clear view within sight range wakes it: it is chasing at once");
        float d0 = Dist(a, sx, sz);
        GvexWorld_Step(w, &t, 1, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(Dist(ById(rec, GvexWorld_Count(w), 0), sx, sz) < d0, "and it comes closer");
        GvexWorld_Destroy(w);

        // out of sight and silent: nothing wakes. In earshot but out of sight: it wakes.
        w = GvexWorld_Create(&g_map, tier);
        float hx, hz;
        Check(FindSpot(g0.x, g0.z, false, &hx, &hz), "(a spot with a wall in between, 5 to 11 m away, was found)");
        t = Target(hx, hz, 0.0f);
        for (int i = 0; i < 20; ++i) GvexWorld_Step(w, &t, 1, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(ById(rec, GvexWorld_Count(w), 0)->state == GVEX_STATE_IDLE, "a player behind a wall, making no noise, is not noticed");
        t = Target(hx, hz, 30.0f);
        GvexWorld_Step(w, &t, 1, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(ById(rec, GvexWorld_Count(w), 0)->state == GVEX_STATE_CHASE, "the same player making noise that carries 30 m is heard through the wall");
        GvexWorld_Destroy(w);

        // sight has a range: in clear view but beyond it, and silent, nothing wakes
        {
            int farFound = 0, stillIdle = 1;
            for (int id = 0; id < 12 && !farFound; ++id) {
                GvexWorld *wf = GvexWorld_Create(&g_map, tier);
                GvexWorld_Records(wf, rec, GVEX_MAX_CREATURES);
                const GvexRecord gf = rec[id];
                float fx, fz;
                if (FindSpotAt(gf.x, gf.z, true, 15.0f, 40.0f, &fx, &fz)) {
                    farFound = 1;
                    GvexTarget tf = Target(fx, fz, 0.0f);
                    for (int i = 0; i < 20; ++i) GvexWorld_Step(wf, &tf, 1, DT);
                    GvexWorld_Records(wf, rec, GVEX_MAX_CREATURES);
                    stillIdle = ById(rec, GvexWorld_Count(wf), id)->state == GVEX_STATE_IDLE;
                }
                GvexWorld_Destroy(wf);
            }
            Check(farFound && stillIdle, "a silent player in clear view but more than 14 m away is not noticed");
        }

        // two players: the nearer one hides behind a wall and is silent, the farther one is in clear view: it goes for the one it can sense
        {
            float ax, az, bx, bz;
            const bool haveA = FindSpotAt(g0.x, g0.z, false, 5.0f, 8.0f, &ax, &az);
            const bool haveB = FindSpotAt(g0.x, g0.z, true, 9.0f, 12.0f, &bx, &bz);
            Check(haveA && haveB, "(a hidden spot at 5 to 8 m and a clear one at 9 to 12 m were found)");
            GvexWorld *w2 = GvexWorld_Create(&g_map, tier);
            GvexTarget two[2] = { Target(ax, az, 0.0f), Target(bx, bz, 0.0f) };
            GvexWorld_Step(w2, two, 2, DT);
            GvexWorld_Records(w2, rec, GVEX_MAX_CREATURES);
            const GvexRecord *r = ById(rec, GvexWorld_Count(w2), 0);
            Check(r->state == GVEX_STATE_CHASE && Dist(r, bx, bz) < hypotf(bx - g0.x, bz - g0.z) - 0.01f,
                  "with a hidden player nearer and a visible one farther it goes for the one it can sense");
            GvexWorld_Destroy(w2);
        }

        // staying: it keeps after what it sensed for about three seconds, then gives up
        w = GvexWorld_Create(&g_map, tier);
        t = Target(sx, sz, 0.0f);
        GvexWorld_Step(w, &t, 1, DT);                                         // sensed
        t = Target(g0.x + 200.0f, g0.z + 200.0f, 0.0f);                        // far, silent: nothing is sensed from now on
        for (int i = 0; i < 80; ++i) GvexWorld_Step(w, &t, 1, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(ById(rec, GvexWorld_Count(w), 0)->state == GVEX_STATE_CHASE, "after losing the player it is still chasing 2.7 s later");
        for (int i = 0; i < 30; ++i) GvexWorld_Step(w, &t, 1, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(ById(rec, GvexWorld_Count(w), 0)->state == GVEX_STATE_IDLE, "and has given up by 3.7 s");
        GvexWorld_Destroy(w);
    }

    printf("walking\n");
    {
        // the speed of its grade, exactly, along a clear straight line (found by trying the spots round each creature)
        int measured = 0;
        for (int id = 0; id < 12 && !measured; ++id) {
            GvexWorld *w = GvexWorld_Create(&g_map, tier);
            GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
            const GvexRecord g = rec[id];
            float sx, sz;
            if (!FindSpot(g.x, g.z, true, &sx, &sz)) { GvexWorld_Destroy(w); continue; }
            GvexTarget t = Target(sx, sz, 0.0f);
            const float expect = (ENEMY_CHASE_SPEED_BASE + ENEMY_CHASE_SPEED_STEP * (float)(tier % 4)) * Gvex_GradeSpeedFactor(g.variant) * DT;
            float px = g.x, pz = g.z; int exact = 0;
            for (int i = 0; i < 6; ++i) {
                GvexWorld_Step(w, &t, 1, DT);
                GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
                const GvexRecord *r = ById(rec, GvexWorld_Count(w), id);
                if (fabsf(hypotf(r->x - px, r->z - pz) - expect) < 2e-3f) ++exact;
                px = r->x; pz = r->z;
            }
            if (exact == 6) measured = 1;
            GvexWorld_Destroy(w);
        }
        Check(measured, "chasing along a clear line it covers exactly its grade's speed times the step, tick after tick");

        // reaching and attacking: it stops at its reach and does not walk through the player
        GvexWorld *w = GvexWorld_Create(&g_map, tier);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        const GvexRecord g0 = rec[0];
        GvexTarget t = Target(g0.x + 1.0f, g0.z, 0.0f);
        GvexWorld_Step(w, &t, 1, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        const GvexRecord *a = ById(rec, GvexWorld_Count(w), 0);
        Check(a->state == GVEX_STATE_ATTACK && fabsf(a->x - g0.x) < 1e-5f && fabsf(a->z - g0.z) < 1e-5f, "within reach it attacks and does not move");
        Check(fabsf(a->yaw - atan2f(1.0f, 0.0f)) < 1e-4f, "and it faces the player (yaw is atan2(dx, dz), like the players')");
        GvexWorld_Destroy(w);

        // the edge of its reach: a player wobbling across the 1.8 m line must not make it flicker between attacking and chasing (the glitch seen on the phones)
        {
            int hid = -1; float hdx = 0.0f, hdz = 0.0f;
            for (int cand = 0; cand < 24 && hid < 0; ++cand) {
                GvexWorld *hw = GvexWorld_Create(&g_map, tier);
                const int hn = GvexWorld_Records(hw, rec, GVEX_MAX_CREATURES);
                const GvexRecord *c = ById(rec, hn, cand);
                if (c) {
                    static const float dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (int k = 0; k < 4 && hid < 0; ++k) {
                        int clear = 1;
                        for (float dd = 0.8f; dd <= 3.0f; dd += 0.2f) {
                            const float px = c->x + dirs[k][0] * dd, pz = c->z + dirs[k][1] * dd;
                            if (EnemyWalk_Blocked(&g_map, px, pz, 0.5f) || !EnemyWalk_LineOfSight(&g_map, (Vector3){ c->x, 0, c->z }, (Vector3){ px, 0, pz })) { clear = 0; break; }
                        }
                        if (clear) { hid = cand; hdx = dirs[k][0]; hdz = dirs[k][1]; }
                    }
                }
                GvexWorld_Destroy(hw);
            }
            Check(hid >= 0, "(set up: a creature with 3 m of clear floor to one side was found)");
            if (hid >= 0) {
                #define HOLD_AT(world, dist) Target(hx + hdx * (dist), hz + hdz * (dist), 0.0f)
                GvexWorld *hw = GvexWorld_Create(&g_map, tier);
                GvexWorld_Records(hw, rec, GVEX_MAX_CREATURES);
                const float hx = ById(rec, GvexWorld_Count(hw), hid)->x, hz = ById(rec, GvexWorld_Count(hw), hid)->z;

                // a player at 2.2 m who never came closer is chased, not attacked
                GvexTarget far2 = HOLD_AT(hw, 2.2f);
                GvexWorld_Step(hw, &far2, 1, DT);
                GvexWorld_Records(hw, rec, GVEX_MAX_CREATURES);
                Check(ById(rec, GvexWorld_Count(hw), hid)->state == GVEX_STATE_CHASE, "a player 2.2 m away who never came closer is chased, not attacked");
                GvexWorld_Destroy(hw);

                // one at 1.7 m, then wobbling between 1.7 and 1.95 m for two seconds: it attacks the whole time
                hw = GvexWorld_Create(&g_map, tier);
                int changes = 0, chased = 0; uint8_t last = 255;
                for (int i = 0; i < 60; ++i) {
                    GvexTarget wob = HOLD_AT(hw, (i & 1) ? 1.95f : 1.7f);
                    GvexWorld_Step(hw, &wob, 1, DT);
                    GvexWorld_Records(hw, rec, GVEX_MAX_CREATURES);
                    const uint8_t st = ById(rec, GvexWorld_Count(hw), hid)->state;
                    if (i > 0 && st != last) ++changes;
                    if (st == GVEX_STATE_CHASE) ++chased;
                    last = st;
                }
                Check(changes == 0 && chased == 0 && last == GVEX_STATE_ATTACK, "a player wobbling across 1.8 m for 60 ticks: the state never changes, it attacks throughout");
                GvexWorld_Destroy(hw);

                // attacking, then the player steps back to 2.2 m: it stays in the attack and closes up to its reach, no nearer than 98% of it
                hw = GvexWorld_Create(&g_map, tier);
                GvexTarget near = HOLD_AT(hw, 1.5f);
                GvexWorld_Step(hw, &near, 1, DT);
                GvexTarget back = HOLD_AT(hw, 2.2f);
                float least = 99.0f; int attackTicks = 0;
                for (int i = 0; i < 12; ++i) {
                    GvexWorld_Step(hw, &back, 1, DT);
                    GvexWorld_Records(hw, rec, GVEX_MAX_CREATURES);
                    const GvexRecord *r = ById(rec, GvexWorld_Count(hw), hid);
                    if (r->state == GVEX_STATE_ATTACK) ++attackTicks;
                    const float dd = Dist(r, back.x, back.z); if (dd < least) least = dd;
                }
                GvexWorld_Records(hw, rec, GVEX_MAX_CREATURES);
                const float endDist = Dist(ById(rec, GvexWorld_Count(hw), hid), back.x, back.z);
                Check(attackTicks == 12, "a player who steps back to 2.2 m is still being attacked (it holds the attack across the band)");
                Check(endDist <= 1.8f + 1e-3f && least >= 1.8f * 0.98f - 1e-3f, "and it closes up to its reach, no nearer than 98% of it: it does not stand swinging at the air, and does not walk through the player");
                GvexWorld_Destroy(hw);

                // far enough (2.6 m, past 1.35 reaches) it gives up the attack and chases
                hw = GvexWorld_Create(&g_map, tier);
                GvexTarget near2 = HOLD_AT(hw, 1.5f);
                GvexWorld_Step(hw, &near2, 1, DT);
                GvexTarget gone = HOLD_AT(hw, 2.6f);
                GvexWorld_Step(hw, &gone, 1, DT);
                GvexWorld_Records(hw, rec, GVEX_MAX_CREATURES);
                Check(ById(rec, GvexWorld_Count(hw), hid)->state == GVEX_STATE_CHASE, "a player who gets away to 2.6 m ends the attack: it chases");
                GvexWorld_Destroy(hw);
                #undef HOLD_AT
            }
        }

        // 800 ticks after a player who walks in circles: nobody is ever inside a wall
        w = GvexWorld_Create(&g_map, tier);
        int inside = 0;
        for (int i = 0; i < 800; ++i) {
            const float a2 = (float)i * 0.01f;
            GvexTarget c = Target(g0.x + 6.0f * cosf(a2), g0.z + 6.0f * sinf(a2), 60.0f);
            GvexWorld_Step(w, &c, 1, DT);
            const int n = GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
            for (int k = 0; k < n; ++k) if (Floor_Solid(&g_map, 0, EnemyTileOf(rec[k].x), EnemyTileOf(rec[k].z))) ++inside;
        }
        Check(inside == 0, "800 ticks of everyone chasing a noisy player round the floor: nobody ever stands in a wall");
        GvexWorld_Destroy(w);

        // two players: it goes for the nearer one
        w = GvexWorld_Create(&g_map, tier);
        GvexRecord g;
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        g = rec[0];
        GvexTarget two[2] = { Target(g.x + 9.0f, g.z, 60.0f), Target(g.x - 5.0f, g.z, 60.0f) };
        GvexWorld_Step(w, two, 2, DT);
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        const GvexRecord *r = ById(rec, GvexWorld_Count(w), 0);
        Check(r->state == GVEX_STATE_CHASE && (r->x - g.x) <= 0.0f, "with two players in earshot it heads for the nearer (the one at -5, not the one at +9)");
        GvexWorld_Destroy(w);
    }

    printf("worlds\n");
    {
        GvexWorld *a = GvexWorld_Create(&g_map, tier), *b = GvexWorld_Create(&g_mapB, tier);
        GvexRecord ra[GVEX_MAX_CREATURES], rb0[GVEX_MAX_CREATURES], rb1[GVEX_MAX_CREATURES];
        const int nb = GvexWorld_Records(b, rb0, GVEX_MAX_CREATURES);
        GvexWorld_Records(a, ra, GVEX_MAX_CREATURES);
        GvexTarget t = Target(ra[0].x + 4.0f, ra[0].z, 60.0f);
        for (int i = 0; i < 60; ++i) GvexWorld_Step(a, &t, 1, DT);
        GvexWorld_Records(b, rb1, GVEX_MAX_CREATURES);
        Check(SameRecords(rb0, rb1, nb), "two worlds in one process: stepping one leaves the other exactly as it was");
        GvexWorld_Destroy(a);
        Check(GvexWorld_Records(b, rb1, GVEX_MAX_CREATURES) == nb, "and destroying one leaves the other whole");
        GvexWorld_Destroy(b);

        GvexWorld *x = GvexWorld_Create(&g_map, tier), *y = GvexWorld_Create(&g_map, tier);
        GvexRecord rx[GVEX_MAX_CREATURES], ry[GVEX_MAX_CREATURES];
        GvexWorld_Records(x, rx, GVEX_MAX_CREATURES);
        int same = 1;
        for (int i = 0; i < 300; ++i) {
            const float a2 = (float)i * 0.02f;
            GvexTarget c[2] = { Target(rx[0].x + 5.0f * cosf(a2), rx[0].z + 5.0f * sinf(a2), (i / 50) % 2 ? 40.0f : 0.0f), Target(rx[3].x, rx[3].z + 3.0f, 10.0f) };
            GvexWorld_Step(x, c, 2, DT); GvexWorld_Step(y, c, 2, DT);
            const int nx = GvexWorld_Records(x, rx, GVEX_MAX_CREATURES), ny = GvexWorld_Records(y, ry, GVEX_MAX_CREATURES);
            same &= nx == ny && SameRecords(rx, ry, nx);
        }
        Check(same, "the same seed and the same inputs give the same creatures, bit for bit, for 300 ticks");
        GvexWorld_Destroy(x); GvexWorld_Destroy(y);
    }

    printf("hurting\n");
    {
        GvexWorld *w = GvexWorld_Create(&g_map, tier);
        const int n0 = GvexWorld_Count(w);
        Check(!GvexWorld_Damage(w, 3, 10.0f), "a hit that does not kill says so");
        GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(ById(rec, GvexWorld_Count(w), 3)->health == ById(rec, GvexWorld_Count(w), 3)->maxHealth - 10.0f, "and the creature has lost that much health");
        Check(GvexWorld_Damage(w, 3, 1000.0f) && GvexWorld_Count(w) == n0 - 1, "a hit that kills says so, and the creature is gone");
        const int n = GvexWorld_Records(w, rec, GVEX_MAX_CREATURES);
        Check(n == n0 - 1 && ById(rec, n, 3) == NULL && ById(rec, n, 4) != NULL, "it is no longer in the records and the others keep their ids");
        int ascending = 1;
        for (int i = 1; i < n; ++i) ascending &= rec[i].id > rec[i - 1].id;
        Check(ascending, "and the records are still in id order (the table moves the last creature into the gap; the snapshot does not show it)");
        Check(!GvexWorld_Damage(w, 3, 5.0f) && !GvexWorld_Damage(w, -1, 5.0f) && !GvexWorld_Damage(w, 99, 5.0f), "the dead, and ids that never were, cannot be hurt");
        GvexTarget t = Target(rec[0].x + 4.0f, rec[0].z, 60.0f);
        for (int i = 0; i < 30; ++i) GvexWorld_Step(w, &t, 1, DT);
        Check(GvexWorld_Count(w) == n0 - 1, "the world goes on without it");
        GvexWorld_Destroy(w);
    }

    printf("\n%s (%d failures of %d checks)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_checks);
    return g_failures ? 1 : 0;
}
#endif
