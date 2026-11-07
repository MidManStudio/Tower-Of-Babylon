// Headless test of the worm swarm: its numbers (assets/config/swarm.mdix and the loader's refusals), spawning, waking by the way a walker
// would go, crawling round walls by the flow field, the worms staying whole and out of walls, floor, stairs, one another and the player,
// the health box (shots, melee, death), the bite, determinism, and what a step costs. No window, no raylib calls. Run from the project root:
//   cc -O2 -std=c99 -DSWARM_HEADLESS_TEST -Isrc -Iraylib/src tests/swarm_headless.c src/swarm/swarm.c src/swarm/swarm_config.c src/dungeon/stair_collision.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c -lm -o swarm_headless
#ifdef SWARM_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dungeon/stair_collision.h"
#include "swarm/swarm.h"

extern const MdixBaked g_bakedSwarm;

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

#define DT (1.0f / 30.0f)
static FloorMap g_map;
static Swarm g_a, g_b;
static const float R_EPS = 0.01f;

static void Walls(void)
{
    memset(&g_map, 0, sizeof g_map);
    g_map.seed = 7; g_map.floorNumber = 1;
    for (int z = 0; z < MAP_HEIGHT; ++z) for (int x = 0; x < MAP_WIDTH; ++x) g_map.grid[z][x].type = TILE_WALL;
}
static void Room(int x0, int z0, int x1, int z1) { for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) g_map.grid[z][x].type = TILE_FLOOR; }

static SwarmConfig Cfg(void) { SwarmConfig c; SwarmConfig_Defaults(&c); return c; }
static SwarmTarget Player(float x, float z) { SwarmTarget t = { { x, 0.0f, z }, 0.35f, 1.7f, true }; return t; }

static void Run(Swarm *s, int steps, const SwarmTarget *pl) { for (int i = 0; i < steps; ++i) { Swarm_BeginTick(s); Swarm_Step(s, DT, &g_map, pl); } }
static float Dist2D(Vector3 a, Vector3 b) { return hypotf(a.x - b.x, a.z - b.z); }

// How far a dot's body reaches into a wall tile (0 when clear): the same box test the swarm uses, written again here.
static float WorstWallPenetration(const Swarm *s)
{
    float worst = 0.0f;
    for (int i = 0; i < s->count; ++i) {
        const int tx = (int)floorf((s->p[i].x + 1.0f) / 2.0f), tz = (int)floorf((s->p[i].z + 1.0f) / 2.0f);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int ax = tx + dx, az = tz + dz;
                if (!Tile_IsSolid(&g_map, ax, az)) continue;
                const float x0 = (float)ax * 2.0f - 1.0f, z0 = (float)az * 2.0f - 1.0f;
                const float nx = fminf(fmaxf(s->p[i].x, x0), x0 + 2.0f), nz = fminf(fmaxf(s->p[i].z, z0), z0 + 2.0f);
                const float pen = s->cfg.dotRadius - hypotf(s->p[i].x - nx, s->p[i].z - nz);
                if (pen > worst) worst = pen;
            }
    }
    return worst;
}
static float MaxLink(const Swarm *s)
{
    float m = 0.0f;
    for (int w = 0; w < s->worms; ++w)
        for (int k = 1; k < s->dotsPer; ++k) {
            const int a = w * s->dotsPer + k, b = a - 1;
            const float d = sqrtf((s->p[a].x - s->p[b].x) * (s->p[a].x - s->p[b].x) + (s->p[a].y - s->p[b].y) * (s->p[a].y - s->p[b].y) + (s->p[a].z - s->p[b].z) * (s->p[a].z - s->p[b].z));
            if (d > m) m = d;
        }
    return m;
}
static float MinNonNeighbourGap(const Swarm *s)             // the closest two dots that are not neighbours in a worm, centre to centre
{
    float m = 1e9f;
    for (int i = 0; i < s->count; ++i)
        for (int j = i + 1; j < s->count; ++j) {
            if (j == i + 1 && i / s->dotsPer == j / s->dotsPer) continue;
            const float d = sqrtf((s->p[i].x - s->p[j].x) * (s->p[i].x - s->p[j].x) + (s->p[i].y - s->p[j].y) * (s->p[i].y - s->p[j].y) + (s->p[i].z - s->p[j].z) * (s->p[i].z - s->p[j].z));
            if (d < m) m = d;
        }
    return m;
}
static float CpuMs(void) { return (float)clock() * 1000.0f / (float)CLOCKS_PER_SEC; }

// ---- a copy of the shipped table that a test can break ------------------------------------------------------------------
#define MAXE 64
static MdixBakedEntry g_e[MAXE];
static int g_n;
static void Reset(void) { g_n = g_bakedSwarm.count; memcpy(g_e, g_bakedSwarm.entries, sizeof(MdixBakedEntry) * (size_t)g_n); }
static MdixBakedEntry *Find(const char *path) { for (int i = 0; i < g_n; ++i) if (strcmp(g_e[i].path, path) == 0) return &g_e[i]; return NULL; }
static void Drop(const char *path) { for (int i = 0; i < g_n; ++i) if (strcmp(g_e[i].path, path) == 0) { memmove(&g_e[i], &g_e[i + 1], sizeof(MdixBakedEntry) * (size_t)(g_n - i - 1)); --g_n; return; } }
static int Try(char *err, int cap)
{
    MdixBaked t = { "test.mdix", 0u, g_e, g_n };
    MdixDoc *d = MdixDoc_FromBaked(&t);
    const int ok = SwarmConfig_LoadFrom(d, err, cap);
    MdixDoc_Free(d);
    return ok;
}

int main(void)
{
    char err[240];

    printf("assets/config/swarm.mdix\n");
    {
        MdixDoc *doc = MdixDoc_FromBaked(&g_bakedSwarm);
        const int ok = SwarmConfig_LoadFrom(doc, err, sizeof err);
        MdixDoc_Free(doc);
        Check(ok, "the shipped file loads");
        SwarmConfig def; SwarmConfig_Defaults(&def);
        Check(memcmp(&def, SwarmConfig_Get(), sizeof def) == 0, "and says what the built-in defaults say (so the two cannot drift apart)");
        const SwarmConfig *c = SwarmConfig_Get();
        Check(c->worms * c->dotsPerWorm <= SWARM_MAX_DOTS && c->crawlSpeed < 4.5f && c->coreHealth > 0.0f && c->linkLength < 2.0f * c->dotRadius,
              "worms fit the arrays, crawl slower than the player walks, health above zero, a link shorter than two radii (one unbroken body)");

        static SwarmConfig snap; snap = *SwarmConfig_Get();
        Reset(); Check(Try(err, sizeof err), "an unbroken copy loads (the baseline of the next checks)");
        Reset(); Drop("swarm.gravity");
        Check(!Try(err, sizeof err) && strstr(err, "swarm.gravity") && strstr(err, "missing"), "a missing value is refused and named");
        Reset(); Find("swarm.worms")->text = "0";
        Check(!Try(err, sizeof err) && strstr(err, "swarm.worms") && strstr(err, "outside"), "no worms is refused");
        Reset(); Find("swarm.worms")->text = "65";
        Check(!Try(err, sizeof err) && strstr(err, "swarm.worms"), "more worms than the arrays hold is refused");
        Reset(); Find("swarm.dots_per_worm")->text = "2";
        Check(!Try(err, sizeof err) && strstr(err, "swarm.dots_per_worm"), "a worm of two dots is refused");
        Reset(); Find("swarm.link_length")->text = "0.0";
        Check(!Try(err, sizeof err) && strstr(err, "swarm.link_length"), "a link of no length is refused");
        Reset(); Find("swarm.crawl_speed")->text = "50.0";
        Check(!Try(err, sizeof err) && strstr(err, "swarm.crawl_speed"), "a crawl that would carry a dot through a wall in a step is refused");
        Reset(); Find("swarm.core_health")->text = "0.0";
        Check(!Try(err, sizeof err) && strstr(err, "swarm.core_health"), "a swarm with no health is refused");
        Reset(); Find("swarm.show_core")->text = "2";
        Check(!Try(err, sizeof err) && strstr(err, "swarm.show_core"), "show_core is 0 or 1");
        Reset(); Find("swarm.worms")->type = 'f'; Find("swarm.worms")->text = "24.5";
        Check(!Try(err, sizeof err) && strstr(err, "swarm.worms"), "a fraction where a whole number belongs is refused");
        Check(!SwarmConfig_LoadFrom(NULL, err, sizeof err), "no document is refused");
        Check(memcmp(&snap, SwarmConfig_Get(), sizeof snap) == 0, "after all those refusals the numbers in use are, byte for byte, the ones from before");
        Reset(); Find("swarm.worms")->text = "3"; Find("swarm.dots_per_worm")->text = "5";
        Check(Try(err, sizeof err) && SwarmConfig_Get()->worms == 3 && SwarmConfig_Get()->dotsPerWorm == 5, "a smaller valid swarm is taken");
        Reset(); Try(err, sizeof err);
    }

    printf("\nspawning\n");
    {
        Walls(); Room(3, 3, 41, 41);
        const SwarmConfig c = Cfg();
        Check(Swarm_SpawnAt(&g_a, &c, 20.0f, 20.0f, 5u), "a swarm spawns");
        Check(g_a.count == c.worms * c.dotsPerWorm && g_a.state == SWARM_ASLEEP && g_a.health == c.coreHealth, "every dot of every worm, asleep, with full health");
        int inside = 1; float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < g_a.count; ++i) {
            if (Tile_IsSolid(&g_map, (int)floorf((g_a.p[i].x + 1.0f) / 2.0f), (int)floorf((g_a.p[i].z + 1.0f) / 2.0f))) inside = 0;
            if (g_a.p[i].y < lo) lo = g_a.p[i].y;
            if (g_a.p[i].y > hi) hi = g_a.p[i].y;
        }
        Check(inside && lo >= c.dotRadius - 1e-4f && hi < 1.0f, "all of them in the open room and lying on the floor");
        Check(MaxLink(&g_a) <= c.linkLength * 1.2f, "the worms start whole: no link stretched");
        SwarmConfig bad = c; bad.worms = 0;
        Check(!Swarm_SpawnAt(&g_b, &bad, 20, 20, 1u), "no worms: refused");
        bad = c; bad.dotsPerWorm = 17; bad.worms = 64;
        Check(!Swarm_SpawnAt(&g_b, &bad, 20, 20, 1u), "more dots than the arrays hold: refused");
        Check(!Swarm_SpawnAt(&g_b, NULL, 20, 20, 1u), "no numbers: refused");

        // Choosing a place on a floor.
        Walls(); Room(3, 3, 41, 41);
        g_map.startX = 38.0f * 2.0f; g_map.startZ = 40.0f * 2.0f; g_map.arriveAboveX = 38.0f * 2.0f; g_map.arriveAboveZ = 4.0f * 2.0f;
        int okPlace = 1, far = 1, clear = 1, deterministic = 1, differ = 0;
        float firstX = -1.0f, firstZ = -1.0f;
        for (uint32_t seed = 1; seed <= 30; ++seed) {
            if (!Swarm_Spawn(&g_a, &c, &g_map, seed)) { okPlace = 0; continue; }
            const float x = g_a.core.x, z = g_a.core.z;
            if (hypotf(x - g_map.startX, z - g_map.startZ) < c.spawnMinDist || hypotf(x - g_map.arriveAboveX, z - g_map.arriveAboveZ) < c.spawnMinDist) far = 0;
            const int tx = (int)floorf((x + 1.0f) / 2.0f), tz = (int)floorf((z + 1.0f) / 2.0f);
            for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) if (Tile_IsSolid(&g_map, tx + dx, tz + dz)) clear = 0;
            if (tx >= 36 && tx <= 44 && (tz <= 8 || tz >= 36)) clear = 0;
            Swarm_Spawn(&g_b, &c, &g_map, seed);
            if (memcmp(g_a.p, g_b.p, sizeof(Vector3) * (size_t)g_a.count) != 0) deterministic = 0;
            if (seed == 1) { firstX = x; firstZ = z; } else if (x != firstX || z != firstZ) differ = 1;
        }
        Check(okPlace, "a floor with room has a place for it, for every seed");
        Check(far, "always at least spawn_min_dist from where the player arrives, from below and from above");
        Check(clear, "always with a tile of clearance all round, and never in an alcove");
        Check(deterministic, "the same seed puts it in the same place with the same layout, every time");
        Check(differ, "different seeds put it in different places");

        Walls(); Room(38, 2, 42, 6);                                  // open only inside the up alcove's tiles
        Check(!Swarm_Spawn(&g_a, &c, &g_map, 3u), "a floor whose only open tiles are an alcove has no place for it");
        Walls();
        Check(!Swarm_Spawn(&g_a, &c, &g_map, 3u), "a floor with no open tile at all has none");
        Walls(); Room(3, 3, 12, 12);
        g_map.startX = 12.0f; g_map.startZ = 12.0f; g_map.arriveAboveX = 12.0f; g_map.arriveAboveZ = 12.0f;
        Check(Swarm_Spawn(&g_a, &c, &g_map, 3u), "a floor too small for the distance still gets one (the farthest half is allowed)");
    }

    printf("\nwaking\n");
    {
        Walls(); Room(3, 3, 41, 41);
        const SwarmConfig c = Cfg();
        SwarmTarget pl = Player(70.0f, 70.0f);
        Swarm_SpawnAt(&g_a, &c, 10.0f, 10.0f, 5u);
        Run(&g_a, 30 * 20, &pl);
        Check(g_a.state == SWARM_ASLEEP && Dist2D(g_a.core, (Vector3){ 10.0f, 0.0f, 10.0f }) < 1.0f, "a player far off: it stays asleep, in its heap, for twenty seconds");
        pl = Player(30.0f, 10.0f);
        Run(&g_a, 2, &pl);
        Check(g_a.state == SWARM_HUNTING, "a player within sense_range by the way a walker goes: it wakes");

        Walls(); Room(3, 3, 12, 20); Room(3, 3, 41, 5); Room(36, 3, 41, 20);       // a long way round: up, along, back down; a wall between the two ends
        Swarm_SpawnAt(&g_a, &c, 8.0f, 30.0f, 5u);                                    // a heap at one foot of the U
        pl = Player(76.0f, 30.0f);                                                    // 68 m away in a straight line; far more by the way round
        Run(&g_a, 10, &pl);
        Check(g_a.state == SWARM_ASLEEP, "a player across a wall, far by the way round: it sleeps");
        Walls(); Room(3, 3, 12, 20); Room(3, 3, 41, 5); Room(10, 14, 12, 20);
        Room(14, 18, 20, 20);                                                          // a wall between, the way round is short
        Swarm_SpawnAt(&g_a, &c, 8.0f, 30.0f, 5u);
        pl = Player(8.0f, 6.0f);                                                        // 24 m away straight, a short way round
        Run(&g_a, 10, &pl);
        Check(g_a.state == SWARM_HUNTING, "a player near by the way round: it wakes");

        Walls(); Room(3, 3, 41, 41);
        Swarm_SpawnAt(&g_a, &c, 10.0f, 10.0f, 5u);
        pl = Player(30.0f, 10.0f); pl.alive = false;
        Run(&g_a, 60, &pl);
        Check(g_a.state == SWARM_ASLEEP, "a dead player does not wake it");
        pl.alive = true; pl.pos = (Vector3){ 70.0f, 0.0f, 70.0f };
        Swarm_SpawnAt(&g_a, &c, 10.0f, 10.0f, 5u);
        Run(&g_a, 5, &pl);
        Swarm_Hit(&g_a, g_a.core, 0.05f, 1.0f);
        Check(g_a.state == SWARM_HUNTING, "being shot wakes it, however far the player is");
    }

    printf("\ncrawling\n");
    {
        Walls(); Room(3, 3, 41, 41);
        const SwarmConfig c = Cfg();
        SwarmTarget pl = Player(36.0f, 10.0f);
        Swarm_SpawnAt(&g_a, &c, 10.0f, 10.0f, 5u);
        Run(&g_a, 2, &pl);
        const Vector3 c0 = g_a.core;
        Run(&g_a, 150, &pl);
        const float speed = Dist2D(g_a.core, c0) / 5.0f;
        printf("    (the swarm's middle crawled at %.2f m/s; the numbers say %.1f)\n", (double)speed, (double)c.crawlSpeed);
        Check(speed > c.crawlSpeed * 0.8f && speed < c.crawlSpeed * 1.15f, "in the open it crawls at about crawl_speed");
        Check(g_a.core.x > c0.x + 10.0f && fabsf(g_a.core.z - 10.0f) < 3.0f, "straight at the player");
        Run(&g_a, 30 * 15, &pl);
        Check(Dist2D(g_a.core, pl.pos) < 2.0f, "and it arrives at the player");

        const Vector3 held = g_a.core;
        pl.alive = false;
        Run(&g_a, 150, &pl);
        Check(Dist2D(g_a.core, held) < 0.5f, "when the player is dead it stops hunting");

        // Round a wall: a block between the swarm and the player, the way round is a U.
        Walls(); Room(3, 3, 20, 10); Room(3, 3, 8, 30); Room(3, 24, 30, 30); Room(25, 3, 30, 30);
        SwarmConfig far = c; far.senseRange = 120.0f;
        pl = Player(52.0f, 10.0f);
        Swarm_SpawnAt(&g_a, &far, 10.0f, 8.0f, 9u);
        int reach = -1; float worstPen = 0.0f, worstLink = 0.0f; int sawDetour = 0;
        for (int i = 0; i < 30 * 90; ++i) {
            Run(&g_a, 1, &pl);
            const float pen = WorstWallPenetration(&g_a), ml = MaxLink(&g_a);
            if (pen > worstPen) worstPen = pen;
            if (ml > worstLink) worstLink = ml;
            if (g_a.core.z > 20.0f) sawDetour = 1;
            if (reach < 0 && Dist2D(g_a.core, pl.pos) < 2.5f) reach = i;
        }
        printf("    (round the wall in %.1f s; worst wall penetration %.4f m; longest link %.3f m)\n", reach / 30.0, (double)worstPen, (double)worstLink);
        Check(reach >= 0 && reach < 30 * 80, "it finds its way round a wall between it and the player, to the player");
        Check(sawDetour, "by going the long way (a swarm that only headed straight would be pressed against the block)");
        Check(worstPen < R_EPS, "no dot ever reached into a wall on the way (every dot, every step)");
        Check(worstLink < c.linkLength * 1.6f, "the worms stayed whole on the way: no link stretched past 1.6 times its length");
    }

    printf("\nthe worms' bodies\n");
    {
        Walls(); Room(3, 3, 41, 41);
        const SwarmConfig c = Cfg();
        Swarm_SpawnAt(&g_a, &c, 20.0f, 20.0f, 5u);
        SwarmTarget pl = Player(70.0f, 70.0f);
        Run(&g_a, 30 * 4, &pl);
        const float gap = MinNonNeighbourGap(&g_a);
        printf("    (in the heap the closest two dots that are not neighbours are %.3f m apart; a diameter is %.3f)\n", (double)gap, (double)(2.0f * c.dotRadius));
        Check(gap > 1.2f * c.dotRadius, "in the heap no two dots (not neighbours) are pressed more than about a third into each other");

        // Two dots on top of each other, then two across a cell boundary of the grid.
        SwarmConfig two = c; two.worms = 2; two.dotsPerWorm = 3; two.gravity = 1.0f;
        Swarm_SpawnAt(&g_a, &two, 20.0f, 20.0f, 5u);
        g_a.p[0] = (Vector3){ 20.0f, 1.0f, 20.0f }; g_a.p[3] = (Vector3){ 20.0f + 0.02f, 1.0f, 20.0f };
        for (int i = 0; i < g_a.count; ++i) g_a.q[i] = g_a.p[i];
        g_a.p[1] = (Vector3){ 25.0f, 1.0f, 25.0f }; g_a.p[2] = (Vector3){ 26.0f, 1.0f, 25.0f }; g_a.p[4] = (Vector3){ 30.0f, 1.0f, 30.0f }; g_a.p[5] = (Vector3){ 31.0f, 1.0f, 30.0f };
        for (int i = 0; i < g_a.count; ++i) g_a.q[i] = g_a.p[i];
        Run(&g_a, 1, NULL);
        Check(hypotf(g_a.p[0].x - g_a.p[3].x, g_a.p[0].z - g_a.p[3].z) > 1.5f * c.dotRadius, "two dots on top of each other are pushed apart");
        const float cell = 2.0f * c.dotRadius;
        const float edge = ceilf(20.0f / cell) * cell;                 // a cell boundary of the grid near x = 20
        Swarm_SpawnAt(&g_a, &two, 20.0f, 20.0f, 5u);
        g_a.p[0] = (Vector3){ edge - 0.01f, 1.0f, 20.0f }; g_a.p[3] = (Vector3){ edge + 0.01f, 1.0f, 20.0f };
        g_a.p[1] = (Vector3){ 25.0f, 1.0f, 25.0f }; g_a.p[2] = (Vector3){ 26.0f, 1.0f, 25.0f }; g_a.p[4] = (Vector3){ 30.0f, 1.0f, 30.0f }; g_a.p[5] = (Vector3){ 31.0f, 1.0f, 30.0f };
        for (int i = 0; i < g_a.count; ++i) g_a.q[i] = g_a.p[i];
        Run(&g_a, 1, NULL);
        Check(hypotf(g_a.p[0].x - g_a.p[3].x, g_a.p[0].z - g_a.p[3].z) > 1.5f * c.dotRadius, "and so are two on either side of a cell boundary of the grid");

        // Floor and ceiling: a whole worm below the floor, and one above the ceiling, is put back inside in a step.
        SwarmConfig lone = c; lone.worms = 1; lone.dotsPerWorm = 3;
        Swarm_SpawnAt(&g_a, &lone, 20.0f, 20.0f, 5u);
        for (int i = 0; i < 3; ++i) { g_a.p[i] = (Vector3){ 30.0f + 0.16f * (float)i, -2.0f, 30.0f }; g_a.q[i] = g_a.p[i]; }
        Run(&g_a, 1, NULL);
        int upFine = 1; for (int i = 0; i < 3; ++i) if (g_a.p[i].y < c.dotRadius - 1e-4f) upFine = 0;
        Swarm_SpawnAt(&g_a, &lone, 20.0f, 20.0f, 5u);
        for (int i = 0; i < 3; ++i) { g_a.p[i] = (Vector3){ 30.0f + 0.16f * (float)i, 9.0f, 30.0f }; g_a.q[i] = g_a.p[i]; }
        Run(&g_a, 1, NULL);
        int downFine = 1; for (int i = 0; i < 3; ++i) if (g_a.p[i].y > WALL_HEIGHT - c.dotRadius + 1e-4f) downFine = 0;
        Check(upFine, "a worm below the floor is put back on it");
        Check(downFine, "and one above the ceiling is put back under it");

        // Gravity: a worm let go in the air falls (it does not jump to the floor) and comes to rest on it.
        SwarmConfig one = c; one.worms = 1; one.dotsPerWorm = 3;
        Swarm_SpawnAt(&g_a, &one, 20.0f, 20.0f, 5u);
        for (int i = 0; i < 3; ++i) { g_a.p[i] = (Vector3){ 30.0f + 0.16f * (float)i, 3.0f, 30.0f }; g_a.q[i] = g_a.p[i]; }
        Run(&g_a, 10, NULL);
        const float midFall = g_a.p[0].y;
        Run(&g_a, 60, NULL);
        Check(midFall < 3.0f && midFall > 1.0f && fabsf(g_a.p[0].y - c.dotRadius) < 0.02f, "a worm let go in the air falls (not in one step) and comes to rest on the floor");

        // A dot inside a wall tile, or a corner of one.
        Walls(); Room(3, 3, 41, 41);
        g_map.grid[20][20].type = TILE_WALL;                           // a pillar: world x 39 to 41, z 39 to 41
        Swarm_SpawnAt(&g_a, &c, 20.0f, 20.0f, 5u);
        g_a.p[0] = (Vector3){ 40.0f, 0.5f, 40.0f }; g_a.q[0] = g_a.p[0];
        g_a.p[10] = (Vector3){ 41.05f, 0.5f, 41.05f }; g_a.q[10] = g_a.p[10];
        Swarm_Step(&g_a, DT, &g_map, NULL);
        Check(WorstWallPenetration(&g_a) < R_EPS, "a dot in the middle of a pillar, and one against its corner, are put out of it");

        // The flight of stairs.
        Walls(); Room(36, 1, 43, 7);
        Swarm_SpawnAt(&g_a, &c, 76.0f, 8.0f, 5u);
        const StairBox *box = Stair_Box(0);
        Check(box != NULL, "the flight has boxes to test against");
        if (box) {
            g_a.p[0] = (Vector3){ (box->x0 + box->x1) * 0.5f, (box->y0 + box->y1) * 0.5f, (box->z0 + box->z1) * 0.5f }; g_a.q[0] = g_a.p[0];
            Swarm_Step(&g_a, DT, &g_map, NULL);
            Check(Stair_SurfaceDistance(g_a.p[0]) > c.dotRadius - R_EPS || g_a.p[0].y > box->y1, "a dot inside a step is pushed out of the flight");
        }

        // The player's body.
        Walls(); Room(3, 3, 41, 41);
        pl = Player(40.0f, 40.0f);
        Swarm_SpawnAt(&g_a, &c, 30.0f, 40.0f, 5u);
        float worstIn = 0.0f;
        for (int i = 0; i < 30 * 12; ++i) {
            Run(&g_a, 1, &pl);
            for (int d = 0; d < g_a.count; ++d) {
                if (g_a.p[d].y < pl.pos.y || g_a.p[d].y > pl.pos.y + pl.height) continue;
                const float in = pl.radius + c.dotRadius - Dist2D(g_a.p[d], pl.pos);
                if (in > worstIn) worstIn = in;
            }
        }
        Check(Dist2D(g_a.core, pl.pos) < 2.5f, "the swarm gathers round a player who stands still");
        Check(worstIn < R_EPS, "and no dot ever passes into the player's body (every dot, every step)");
        const Vector3 core = g_a.core;
        Check(core.y >= g_a.coreHalf - 1e-4f, "the health box never sinks into the floor");
    }

    printf("\ndrawing between steps\n");
    {
        Walls(); Room(3, 3, 41, 41);
        const SwarmConfig c = Cfg();
        SwarmTarget pl = Player(36.0f, 10.0f);
        Swarm_SpawnAt(&g_a, &c, 10.0f, 10.0f, 5u);
        Run(&g_a, 10, &pl);
        Swarm_BeginTick(&g_a);
        int same = 1; for (int i = 0; i < g_a.count; ++i) if (g_a.r[i].x != g_a.p[i].x || g_a.r[i].z != g_a.p[i].z) same = 0;
        Check(same, "BeginTick remembers where every dot is");
        Swarm_Step(&g_a, DT, &g_map, &pl);
        int moved = 0; for (int i = 0; i < g_a.count; ++i) if (fabsf(g_a.r[i].x - g_a.p[i].x) > 1e-4f) ++moved;
        Check(moved > g_a.count / 2, "and the step then moves them, so drawing has somewhere to blend from");
    }

    printf("\nthe same every time\n");
    {
        Walls(); Room(3, 3, 41, 41);
        const SwarmConfig c = Cfg();
        SwarmTarget pl = Player(36.0f, 10.0f);
        Swarm_SpawnAt(&g_a, &c, 10.0f, 10.0f, 5u);
        Swarm_SpawnAt(&g_b, &c, 10.0f, 10.0f, 5u);
        Run(&g_a, 400, &pl); Run(&g_b, 400, &pl);
        Check(memcmp(g_a.p, g_b.p, sizeof(Vector3) * (size_t)g_a.count) == 0 && g_a.core.x == g_b.core.x, "two swarms with one seed, 400 steps of one game: every dot in the same place, to the bit");
        Swarm_SpawnAt(&g_b, &c, 10.0f, 10.0f, 6u);
        Run(&g_b, 400, &pl);
        Check(memcmp(g_a.p, g_b.p, sizeof(Vector3) * (size_t)g_a.count) != 0, "a different seed gives a different swarm");
    }

    printf("\nthe health box\n");
    {
        Walls(); Room(3, 3, 41, 41);
        const SwarmConfig c = Cfg();
        SwarmTarget pl = Player(36.0f, 10.0f);
        Swarm_SpawnAt(&g_a, &c, 10.0f, 10.0f, 5u);
        int inside = 1;
        for (int i = 0; i < 30 * 10; ++i) {
            Run(&g_a, 1, &pl);
            float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
            for (int d = 0; d < g_a.count; ++d) { if (g_a.p[d].x < x0) x0 = g_a.p[d].x; if (g_a.p[d].x > x1) x1 = g_a.p[d].x; if (g_a.p[d].z < z0) z0 = g_a.p[d].z; if (g_a.p[d].z > z1) z1 = g_a.p[d].z; }
            if (g_a.core.x < x0 || g_a.core.x > x1 || g_a.core.z < z0 || g_a.core.z > z1) inside = 0;
        }
        Check(inside, "the box is in the middle of the swarm: always inside the spread of its dots");

        const Vector3 core = g_a.core; const float h = g_a.coreHalf;
        const float before = g_a.health;
        Check(Swarm_Hit(&g_a, core, 0.05f, 10.0f) && g_a.health == before - 10.0f && Swarm_HitsLanded(&g_a) == 1, "a shot into the middle damages it and is counted");
        Check(Swarm_Hit(&g_a, (Vector3){ core.x + h + 0.05f - 0.07f, core.y, core.z }, 0.07f, 5.0f) && g_a.health == before - 15.0f, "a round whose edge just reaches a face of the box hits");
        Check(!Swarm_Hit(&g_a, (Vector3){ core.x + h + 0.05f + 0.07f, core.y, core.z }, 0.07f, 5.0f) && g_a.health == before - 15.0f, "one just short of it misses, and takes nothing");
        Check(!Swarm_Hit(&g_a, (Vector3){ core.x, core.y + h + 1.0f, core.z }, 0.07f, 5.0f), "a shot above the swarm misses");
        const unsigned hitsSoFar = Swarm_HitsLanded(&g_a);
        Check(Swarm_Hit(&g_a, core, 0.05f, 0.0f) && Swarm_HitsLanded(&g_a) == hitsSoFar && g_a.health == before - 15.0f, "a touch with no damage in it is a hit that costs and counts nothing");
        Check(g_a.state == SWARM_HUNTING && Swarm_Alive(&g_a) && Swarm_KillsLanded(&g_a) == 0, "it lives while it has health");
        Check(Swarm_Hit(&g_a, core, 0.05f, 1000.0f) && g_a.state == SWARM_DEAD && g_a.health == 0.0f && Swarm_KillsLanded(&g_a) == 1, "the last of the health kills it, once");
        Check(!Swarm_Hit(&g_a, core, 0.05f, 10.0f) && Swarm_KillsLanded(&g_a) == 1, "a shot at the dead passes through it");
        const Vector3 laid = g_a.core;
        Run(&g_a, 90, &pl);
        Check(Dist2D(g_a.core, laid) < 0.4f && Swarm_Active(&g_a) && !Swarm_Alive(&g_a), "the dead stop where they are and lie there");
        Run(&g_a, 30 * 5, &pl);
        Check(!Swarm_Active(&g_a), "after death_secs they are gone");

        // Melee.
        Walls(); Room(3, 3, 41, 41);
        Swarm_SpawnAt(&g_a, &c, 20.0f, 20.0f, 5u);
        const Vector3 m = g_a.core;
        const Vector3 eye = { m.x - 1.2f, 1.6f, m.z };
        Check(Swarm_MeleeHit(&g_a, eye, 1.0f, 0.0f, 1.5f, 45.0f, 10.0f) == 1 && g_a.health == c.coreHealth - 10.0f, "a swing at it within reach, in front, level: hits and damages");
        Check(Swarm_MeleeHit(&g_a, eye, 1.0f, 0.0f, 0.4f, 45.0f, 10.0f) == 0, "out of reach: misses");
        Check(Swarm_MeleeHit(&g_a, eye, -1.0f, 0.0f, 1.5f, 45.0f, 10.0f) == 0, "facing away: misses");
        Check(Swarm_MeleeHit(&g_a, (Vector3){ m.x - 1.2f, 4.5f, m.z }, 1.0f, 0.0f, 1.5f, 45.0f, 10.0f) == 0, "from far above: misses");
        Check(g_a.health == c.coreHealth - 10.0f, "and the misses took nothing");

        // The bite.
        Walls(); Room(3, 3, 41, 41);
        pl = Player(40.0f, 40.0f);
        Swarm_SpawnAt(&g_a, &c, 36.0f, 40.0f, 5u);
        Run(&g_a, 30 * 12, &pl);
        float dmg = 0.0f; Vector3 from = { 0 };
        int bitesAt[8], nb = 0; float first = -1.0f;
        for (int i = 0; i < 30 * 5; ++i) {
            Run(&g_a, 1, &pl);
            if (Swarm_Bite(&g_a, DT, &pl, &dmg, &from)) { if (nb < 8) bitesAt[nb++] = i; if (first < 0.0f) first = dmg; }
        }
        Check(nb >= 5 && nb <= 11, "a swarm round a player bites again and again");
        Check(nb >= 2 && (bitesAt[1] - bitesAt[0]) >= (int)(c.biteInterval * 30.0f) - 1 && (bitesAt[1] - bitesAt[0]) <= (int)(c.biteInterval * 30.0f) + 1, "a bite_interval apart");
        Check(first > 0.0f && first <= c.biteCap && Dist2D(from, pl.pos) < 1.5f, "each with some damage, at most bite_cap, from where the dots touch");
        SwarmConfig hot = c; hot.biteDamage = 50.0f; hot.biteCap = 7.0f;
        Swarm_SpawnAt(&g_b, &hot, 36.0f, 40.0f, 5u);
        Run(&g_b, 30 * 12, &pl);
        float biggest = 0.0f;
        for (int i = 0; i < 30 * 3; ++i) { Run(&g_b, 1, &pl); if (Swarm_Bite(&g_b, DT, &pl, &dmg, &from) && dmg > biggest) biggest = dmg; }
        Check(biggest == 7.0f, "a bite never takes more than bite_cap, however many dots touch");
        // The windup, from first contact.
        SwarmConfig slow = c; slow.biteWindup = 1.0f;
        Walls(); Room(3, 3, 41, 41);
        Swarm_SpawnAt(&g_b, &slow, 30.0f, 40.0f, 5u);
        SwarmTarget touch = Player(30.0f, 40.0f);                          // standing in the heap
        int firstBite = -1;
        for (int i = 0; i < 90 && firstBite < 0; ++i) { Run(&g_b, 1, &touch); g_b.state = SWARM_HUNTING; if (Swarm_Bite(&g_b, DT, &touch, &dmg, &from)) firstBite = i; }
        Check(firstBite >= 25 && firstBite <= 35, "the first bite comes after bite_windup of contact, not at once");
        Swarm_SpawnAt(&g_b, &c, 10.0f, 10.0f, 5u);
        Run(&g_b, 30 * 3, &pl);
        g_b.state = SWARM_HUNTING;                                         // hunting, but the player is 40 m away
        Check(!Swarm_Bite(&g_b, 1.0f, &pl, &dmg, &from) && !Swarm_Bite(&g_b, 1.0f, &pl, &dmg, &from), "a player out of reach is not bitten, however long the contact is waited for");
        pl = Player(30.0f, 40.0f); pl.alive = false;
        g_b.state = SWARM_HUNTING;
        Check(!Swarm_Bite(&g_b, 1.0f, &pl, &dmg, &from) && !Swarm_Bite(&g_b, 1.0f, &pl, &dmg, &from), "a dead player is not bitten");
        Swarm_SpawnAt(&g_b, &c, 30.0f, 40.0f, 5u);
        pl = Player(30.0f, 40.0f);
        Check(!Swarm_Bite(&g_b, 1.0f, &pl, &dmg, &from) && !Swarm_Bite(&g_b, 1.0f, &pl, &dmg, &from), "a sleeping swarm does not bite");
    }

    printf("\ncost (this machine; a phone is several times slower)\n");
    {
        Walls(); Room(3, 3, 41, 41);
        SwarmConfig c = Cfg();
        SwarmTarget pl = Player(70.0f, 70.0f);
        c.senseRange = 120.0f;
        Swarm_SpawnAt(&g_a, &c, 10.0f, 10.0f, 5u);
        Run(&g_a, 60, &pl);
        float t0 = CpuMs(); Run(&g_a, 300, &pl); const float defMs = (CpuMs() - t0) / 300.0f;
        c.worms = 64; c.dotsPerWorm = 16;
        Swarm_SpawnAt(&g_b, &c, 10.0f, 10.0f, 5u);
        Run(&g_b, 60, &pl);
        t0 = CpuMs(); Run(&g_b, 300, &pl); const float maxMs = (CpuMs() - t0) / 300.0f;
        printf("    (240 dots hunting: %.3f ms a step; 1024 dots: %.3f ms; the step is 33.3 ms)\n", (double)defMs, (double)maxMs);
        Check(defMs < 4.0f, "the swarm as shipped costs under 4 ms a step");
        Check(maxMs < 20.0f, "the largest swarm that fits costs under 20 ms a step");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
