// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "spider_headless.c"
// ============================================================================
// Everything is inside the macro so mray's build, which compiles every .c file,
// does not pick up a second main(). It needs raylib's headers for the Vector3
// and Color types and stubs the few draw calls it links against.
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DSPIDER_HEADLESS_TEST -Isrc -Iraylib/src tests/spider_headless.c src/enemies/spider.c src/enemies/spider_ai.c src/enemies/spider_draw.c src/dungeon/dungeon_raycast.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o spider_headless
// Run:
//   ./spider_headless
#ifdef SPIDER_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dungeon/dungeon_generator.h"
#include "dungeon/dungeon_raycast.h"
#include "enemies/spider.h"
#include "dungeon/dungeon_types.h"

// Draw calls the spider code references, never executed here.
double GetTime(void) { return 0.0; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
void DrawSphereEx(Vector3 p, float r, int rings, int slices, Color c) { (void)p; (void)r; (void)rings; (void)slices; (void)c; }

static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static FloorMap g_floor;

// Open rectangle of tiles inside solid rock, ceiling everywhere (x < 36).
static void MakeRoom(FloorMap *f, int x0, int z0, int x1, int z1)
{
    memset(f, 0, sizeof *f);
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) { f->grid[z][x].type = TILE_WALL; f->grid[z][x].variant = WALL_SHELL; }
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) f->grid[z][x].type = TILE_FLOOR;
}

typedef struct {
    long frames, nonFinite;
    long bodyPenetration;     // frames where a body sphere sank more than 3 cm into geometry
    long feetChecked, feetOff; // planted feet farther than 8 cm from any surface
    long reachChecked, reachOver;
    float worstPenetration, worstFootOff;
} Stats;

static void Track(const Spider *s, const FloorMap *f, Stats *st)
{
    ++st->frames;
    if (!isfinite(s->pos.x + s->pos.y + s->pos.z)) { ++st->nonFinite; return; }

    static const float offF[3] = { 0.36f, 0.0f, -0.42f }, offU[3] = { 0.02f, 0.0f, 0.04f }, rad[3] = { 0.17f, 0.22f, 0.28f };
    for (int k = 0; k < 3; ++k) {
        const Vector3 c = { s->pos.x + s->fwd.x * offF[k] + s->up.x * offU[k], s->pos.y + s->fwd.y * offF[k] + s->up.y * offU[k], s->pos.z + s->fwd.z * offF[k] + s->up.z * offU[k] };
        const float pen = rad[k] - Dungeon_SurfaceDistance(f, c);
        if (pen > 0.03f) { ++st->bodyPenetration; if (pen > st->worstPenetration) st->worstPenetration = pen; break; }
    }
    for (int i = 0; i < SPIDER_LEGS; ++i) {
        const SpiderLeg *l = &s->legs[i];
        if (l->grounded && !l->swinging) {
            ++st->feetChecked;
            const float d = Dungeon_SurfaceDistance(f, l->foot);
            if (d > 0.08f) { ++st->feetOff; if (d > st->worstFootOff) st->worstFootOff = d; }
        }
        ++st->reachChecked;
        const Vector3 dd = { l->foot.x - l->root.x, l->foot.y - l->root.y, l->foot.z - l->root.z };
        if (sqrtf(dd.x * dd.x + dd.y * dd.y + dd.z * dd.z) > 1.9f * 1.02f + 0.3f) ++st->reachOver;
    }
}

// Runs until done() or the time limit. Returns seconds used, or -1 on timeout.
typedef int (*DoneFn)(const Spider *);
static float RunUntil(Spider *s, const FloorMap *f, Vector3 want, float speed, DoneFn done, float limit, Stats *st)
{
    const float dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < limit; t += dt) {
        Spider_Step(s, f, dt, want, speed);
        Track(s, f, st);
        if (done(s)) return t;
    }
    return -1.0f;
}

static int OnEastWall(const Spider *s)  { return !s->transActive && s->normal.x < -0.9f; }
static int OnCeiling(const Spider *s)   { return !s->transActive && s->normal.y < -0.9f; }
static int OnWestWall(const Spider *s)  { return !s->transActive && s->normal.x > 0.9f; }
static int OnFloor(const Spider *s)     { return !s->transActive && s->normal.y > 0.9f; }

static void Report(const char *name, const Stats *st, const Spider *s)
{
    printf("  %s: %ld frames, switches %d, failsafes %d, lost-surface %d\n", name, st->frames, s->switches, s->failsafes, s->lostSurface);
    printf("     body penetration frames %ld (worst %.3f m), planted feet off surface %ld of %ld (worst %.3f m), reach violations %ld of %ld\n",
           st->bodyPenetration, st->worstPenetration, st->feetOff, st->feetChecked, st->worstFootOff, st->reachOver, st->reachChecked);
}

static void CheckStats(const Stats *st)
{
    Check(st->nonFinite == 0, "no non-finite values");
    Check(st->bodyPenetration * 100 <= st->frames, "body spheres stay out of geometry (99% of frames)");
    Check(st->feetChecked == 0 || st->feetOff * 100 <= st->feetChecked * 3, "planted feet rest on a surface (97% of foot-frames)");
    Check(st->reachOver * 100 <= st->reachChecked, "feet stay within leg reach (99% of leg-frames)");
}

static int OnNorthFace(const Spider *s) { return !s->transActive && s->normal.z > 0.9f; }

int main(void)
{
    Stats st;

    printf("Scenario A: floor -> east wall -> ceiling -> west wall -> floor (concave corners)\n");
    MakeRoom(&g_floor, 5, 5, 25, 25);
    Spider s;
    Spider_Init(&s, &g_floor, (Vector3){ 20.0f, 0.0f, 30.0f }, (Vector3){ 0, 1, 0 }, (Vector3){ 1, 0, 0 });
    memset(&st, 0, sizeof st);
    float t1 = RunUntil(&s, &g_floor, (Vector3){ 1, 0, 0 }, 2.5f, OnEastWall, 30.0f, &st);
    float t2 = RunUntil(&s, &g_floor, (Vector3){ 0, 1, 0 }, 2.5f, OnCeiling, 20.0f, &st);
    float t3 = RunUntil(&s, &g_floor, (Vector3){ -1, 0, 0 }, 2.5f, OnWestWall, 40.0f, &st);
    float t4 = RunUntil(&s, &g_floor, (Vector3){ 0, -1, 0 }, 2.5f, OnFloor, 20.0f, &st);
    printf("  times: floor->wall %.1fs, wall->ceiling %.1fs, ceiling->wall %.1fs, wall->floor %.1fs\n", t1, t2, t3, t4);
    Report("A", &st, &s);
    Check(t1 >= 0, "floor to wall");
    Check(t2 >= 0, "wall to ceiling");
    Check(t3 >= 0, "ceiling to wall");
    Check(t4 >= 0, "wall to floor");
    Check(s.switches == 4, "exactly four surface switches");
    Check(s.failsafes == 0, "every switch waited for both front feet (no failsafe)");
    CheckStats(&st);

    printf("Scenario B: around a free-standing pillar (convex corners)\n");
    MakeRoom(&g_floor, 5, 5, 25, 25);
    g_floor.grid[15][15].type = TILE_WALL;   // pillar at tile (15, 15): faces at 29, 31 in x and z
    Spider_Init(&s, &g_floor, (Vector3){ 29.0f, 2.0f, 29.4f }, (Vector3){ -1, 0, 0 }, (Vector3){ 0, 0, 1 });
    memset(&st, 0, sizeof st);
    float tb = RunUntil(&s, &g_floor, (Vector3){ 0, 0, 1 }, 2.0f, OnNorthFace, 20.0f, &st);
    printf("  west face -> north face after %.1fs\n", tb);
    Check(tb >= 0, "wrapped around the first convex edge onto the next face");
    // keep walking along whatever the heading is; count how many faces it manages
    for (int i = 0; i < 40 * 60 && s.switches < 6; ++i) {
        Spider_Step(&s, &g_floor, 1.0f / 60.0f, s.heading, 2.0f);
        Track(&s, &g_floor, &st);
    }
    Report("B", &st, &s);
    Check(s.switches >= 4, "keeps wrapping around the pillar (4 or more convex switches)");
    Check(s.failsafes == 0, "no failsafe switches on convex corners");
    CheckStats(&st);


    printf("Scenario C: hunting through generated floors (path field, ceilings, walls)\n");
    {
        const uint32_t seeds[] = { 1u, 42u, 2026u, 7u, 0xC0FFEEu, 99u };
        const int floors[] = { 1, 2, 5, 9 };
        int bitRuns = 0;
        int runs = 0, reached = 0, usedCeiling = 0, usedWall = 0, bad = 0;
        long penetr = 0, frames = 0;
        float sumReach = 0.0f;
        for (unsigned si = 0; si < sizeof seeds / sizeof seeds[0]; ++si)
            for (unsigned fi = 0; fi < sizeof floors / sizeof floors[0]; ++fi) {
                GenerateFloorMap(&g_floor, floors[fi], seeds[si]);
                Spiders_Init();
                Spiders_SpawnForFloor(&g_floor, floors[fi]);
                const Spider *sp = Spiders_Get(0);
                if (!sp || !sp->active) continue;

                // Put the player 18 to 28 path steps from the spider.
                static int dist[MAP_HEIGHT][MAP_WIDTH];
                static int queue[MAP_WIDTH * MAP_HEIGHT];
                for (int z = 0; z < MAP_HEIGHT; ++z) for (int x = 0; x < MAP_WIDTH; ++x) dist[z][x] = -1;
                const int sx = (int)floorf(sp->anchor.x / TILE_SIZE + 0.5f), sz = (int)floorf(sp->anchor.z / TILE_SIZE + 0.5f);
                int head = 0, tail = 0, px = -1, pz = -1;
                dist[sz][sx] = 0; queue[tail++] = sz * MAP_WIDTH + sx;
                while (head < tail) {
                    const int cur = queue[head++], cx = cur % MAP_WIDTH, cz = cur / MAP_WIDTH;
                    if (dist[cz][cx] >= 18 && dist[cz][cx] <= 28 && !(g_floor.grid[cz][cx].flags & CELL_RESERVED)) { px = cx; pz = cz; }
                    static const int dx4[4] = { 1, -1, 0, 0 }, dz4[4] = { 0, 0, 1, -1 };
                    for (int k = 0; k < 4; ++k) {
                        const int nx = cx + dx4[k], nz = cz + dz4[k];
                        if (Tile_IsSolid(&g_floor, nx, nz) || dist[nz][nx] >= 0) continue;
                        dist[nz][nx] = dist[cz][cx] + 1; queue[tail++] = nz * MAP_WIDTH + nx;
                    }
                }
                if (px < 0) continue;
                const Vector3 player = { (float)px * TILE_SIZE, 1.0f, (float)pz * TILE_SIZE };

                ++runs;
                float reachedAt = -1.0f; int ceil = 0, wall = 0, nonFinite = 0;
                for (int i = 0; i < 150 * 60; ++i) {
                    Spiders_Update(1.0f / 60.0f, player, &g_floor);
                    sp = Spiders_Get(0);
                    if (!sp->active) { nonFinite = 1; break; }
                    ++frames;
                    if (sp->normal.y < -0.7f) ceil = 1;
                    if (fabsf(sp->normal.y) < 0.3f) wall = 1;
                    Vector3 c[3]; float r[3]; Spider_BodySpheres(sp, c, r);
                    for (int k = 0; k < 3; ++k) if (r[k] - Dungeon_SurfaceDistance(&g_floor, c[k]) > 0.03f) { ++penetr; break; }
                    const float dx = player.x - sp->pos.x, dy = player.y - sp->pos.y, dz = player.z - sp->pos.z;
                    if (reachedAt < 0.0f && sqrtf(dx * dx + dy * dy + dz * dz) < 3.0f) { reachedAt = (float)i / 60.0f; break; }
                }
                if (nonFinite) ++bad;
                if (reachedAt >= 0.0f) {
                    ++reached; sumReach += reachedAt;
                    // Stay put for 8 more seconds: a spider that has come all the way must get close
                    // enough to bite, not stop short of the player.
                    const Vector3 feet = { player.x, 0.0f, player.z };
                    float dmg; Vector3 from; int bit = 0;
                    for (int i = 0; i < 8 * 60 && !bit; ++i) {
                        Spiders_Update(1.0f / 60.0f, player, &g_floor);
                        bit = Spiders_AttackPlayer(1.0f / 60.0f, feet, 1.8f, &dmg, &from);
                    }
                    bitRuns += bit;
                }
                usedCeiling += ceil; usedWall += wall;
            }
        printf("  %d runs: reached the player in %d (avg %.1f s), used a ceiling in %d, used a wall in %d, lost spiders %d\n",
               runs, reached, reached ? sumReach / (float)reached : 0.0f, usedCeiling, usedWall, bad);
        printf("  body penetration frames %ld of %ld\n", penetr, frames);
        Check(runs >= 12, "enough runs were set up");
        Check(bad == 0, "no spider went non-finite");
        Check(reached * 10 >= runs * 8, "spider reaches the player in at least 80% of runs");
        Check(penetr * 100 <= frames, "body stays out of geometry (99% of frames)");
        printf("  bit the player in %d of %d runs that arrived\n", bitRuns, reached);
        Check(bitRuns * 10 >= reached * 8, "a spider that arrives gets close enough to bite in at least 80% of runs");
    }

    printf("\n%s\n", g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
#endif
