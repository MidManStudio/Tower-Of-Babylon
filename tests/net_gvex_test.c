// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/testing.md, section "tests/net_gvex_test.c"
// ============================================================================
// Headless test of the creatures in a session (slice S1 of docs/gvex_session.md): a server with its world of creatures, two clients over a link with
// latency, jitter and loss, and what each client is shown. The same ghouls on both screens, where the server has them (a tick or three behind),
// the same looks as the single-player game gives its cylinders, a creature killed on the server gone from both screens, and the wire format of a
// full snapshot. It needs the Flecs object, so it is built by tests/gvex_check.sh (exit 77, SKIP, where that is not possible; its name ends in _test so that run_tests.sh does not build it with a plain line):
//   cc -O1 -std=c99 -DNET_GVEX_HEADLESS_TEST -DTOWER_GVEX=1 -Isrc -Iraylib/src -Ithird_party/flecs tests/net_gvex_test.c src/net/net_wire.c src/net/net_proto.c src/net/net_loopback.c src/net/net_move_sim.c src/net/net_server.c src/weapons/weapons.c src/config/game_config.c src/config/config_baked.c src/config/mdix_doc.c src/config/mdix_web.c src/projectiles/projectile_config.c src/projectiles/projectile_patterns.c src/flashlight/flashlight.c src/net/net_history.c src/net/net_client.c src/gvex/gvex_world.c third_party/flecs/tob_flecs.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -lpthread -o net_gvex_test
#ifdef NET_GVEX_HEADLESS_TEST
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config/game_config.h"
#include "enemies/enemy_plan.h"
#include "gvex/gvex_world.h"
#include "net/net_client.h"
#include "net/net_loopback.h"
#include "net/net_server.h"
#include "net/net_wire.h"
#include "weapons/weapons.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static int g_failures = 0, g_checks = 0;
static void Check(int ok, const char *what) { ++g_checks; printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

#define TICK_MS (1000.0 / 30.0)
#define FLOOR 3
#define SEED  4242u
static NetServer g_srv;
static NetLoopback g_lb[2];
static NetClient g_cl[2];
static int g_slot[2];
static double g_now, g_nextTick;
static GvexRecord g_hist[4096][GVEX_MAX_CREATURES];       // what the server's world was after each tick (by the server's tick)
static int g_histN[4096];

static int g_sprint[2];                                    // set by the test: the client holds forward and sprint (what the player's fingers would do)
static int g_fire[2];                                      // set by the test: the client holds the fire button
static float g_yaw[2];                                     // and which way it looks (radians)
static NetCmd Cmd(void *user, uint32_t tick)
{
    (void)tick;
    const int k = (int)((int *)user - g_sprint), sprint = *(int *)user;
    return NetCmd_Make(0, sprint ? 1.0f : 0.0f, g_yaw[k], 0, (uint16_t)((sprint ? NET_BTN_SPRINT : 0) | (g_fire[k] ? NET_BTN_FIRE : 0)));
}

static void World(float latency, float jitter, float loss)
{
    g_now = 1000.0; g_nextTick = g_now + TICK_MS;
    memset(g_histN, 0, sizeof g_histN);
    memset(g_sprint, 0, sizeof g_sprint);
    memset(g_fire, 0, sizeof g_fire);
    memset(g_yaw, 0, sizeof g_yaw);
    NetServer_Init(&g_srv, SEED, FLOOR);
    for (int k = 0; k < 2; ++k) {
        NetLoopback_Init(&g_lb[k], 31u + (uint32_t)k);
        NetLinkCfg cfg = { latency, jitter, loss, 0.0f };
        NetLoopback_SetBoth(&g_lb[k], cfg);
        NetLoopback_Advance(&g_lb[k], g_now);
        g_slot[k] = NetServer_AddClient(&g_srv, NetLoopback_End(&g_lb[k], 1), g_now);
        NetClient_Init(&g_cl[k], NetLoopback_End(&g_lb[k], 0), k == 0 ? "steve" : "brou", g_now);
    }
}
static void Step1ms(void)
{
    g_now += 1.0;
    for (int k = 0; k < 2; ++k) NetLoopback_Advance(&g_lb[k], g_now);
    NetServer_Poll(&g_srv, g_now);
    for (int k = 0; k < 2; ++k) NetClient_Poll(&g_cl[k], g_now);
    if (g_now >= g_nextTick) {
        g_nextTick += TICK_MS;
        NetServer_Tick(&g_srv, g_now);
        if (g_srv.tick < 4096) g_histN[g_srv.tick] = GvexWorld_Records(g_srv.gvex, g_hist[g_srv.tick], GVEX_MAX_CREATURES);
    }
    for (int k = 0; k < 2; ++k) NetClient_Update(&g_cl[k], g_now, Cmd, &g_sprint[k]);
}
static void Run(double ms) { const double end = g_now + ms; while (g_now < end) Step1ms(); }
static void Finish(void) { NetServer_Free(&g_srv); }

// A creature (other than skipId) with clear floor to one side of it, and which side: (dx, dz) points from the creature to where a player can stand.
static int FindSide(int *outId, float *dx, float *dz, int skipId)
{
    GvexRecord rec[GVEX_MAX_CREATURES];
    const int n = GvexWorld_Records(g_srv.gvex, rec, GVEX_MAX_CREATURES);
    static const float dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int i = 0; i < n; ++i) {
        if (rec[i].id == skipId) continue;
        for (int k = 0; k < 4; ++k) {
            int clear = 1;
            for (float d = 0.8f; d <= 3.0f; d += 0.2f) {
                const float px = rec[i].x + dirs[k][0] * d, pz = rec[i].z + dirs[k][1] * d;
                if (EnemyWalk_Blocked(&g_srv.map, px, pz, 0.7f) || !EnemyWalk_LineOfSight(&g_srv.map, (Vector3){ rec[i].x, 0, rec[i].z }, (Vector3){ px, 0, pz })) { clear = 0; break; }
            }
            if (clear) { *outId = rec[i].id; *dx = dirs[k][0]; *dz = dirs[k][1]; return 1; }
        }
    }
    return 0;
}
static int RecOf(int id, GvexRecord *out)
{
    GvexRecord rec[GVEX_MAX_CREATURES];
    const int n = GvexWorld_Records(g_srv.gvex, rec, GVEX_MAX_CREATURES);
    for (int i = 0; i < n; ++i) if (rec[i].id == id) { *out = rec[i]; return 1; }
    return 0;
}
// Stands player pl `dist` metres from the creature on the side (dx, dz), looking at it (towards = 1) or away from it (0). The server owns the position, so the test sets it there.
static void Stand(int pl, const GvexRecord *c, float dx, float dz, float dist, int towards)
{
    g_srv.c[pl].player.position = (Vector3){ c->x + dx * dist, c->y, c->z + dz * dist };
    float yaw = towards ? atan2f(-dx, -dz) : atan2f(dx, dz);
    if (yaw < 0.0f) yaw += 6.28318531f;
    g_yaw[pl] = yaw;
}

static const GvexRecord *Hist(uint32_t tick, int id) { if (tick >= 4096) return NULL; for (int i = 0; i < g_histN[tick]; ++i) if (g_hist[tick][i].id == id) return &g_hist[tick][i]; return NULL; }
static const NetMonsterView *View(const NetMonsterView *v, int n, int id) { for (int i = 0; i < n; ++i) if (v[i].id == id) return &v[i]; return NULL; }

// How far a creature's shown position is from where the server had it at the tick the client is showing (the client's own arithmetic, redone from the server's history).
static float ShownError(const NetClient *c, const NetMonsterView *v)
{
    const double est = (double)c->lastServerTick + (g_now - c->lastSnapMs) / TICK_MS;
    const double want = est - (double)NET_INTERP_TICKS;
    uint32_t t0 = (uint32_t)floor(want);
    const GvexRecord *a = Hist(t0, v->id), *b = Hist(t0 + 1, v->id);
    if (!a || !b) return 1e9f;
    const float u = (float)(want - (double)t0);
    return hypotf(v->pos.x - (a->x + (b->x - a->x) * u), v->pos.z - (a->z + (b->z - a->z) * u));
}

// How far the shown facing is from the server's at the tick shown (radians, the short way round; the facing changes slowly, so the nearer record will do).
static float ShownYawError(const NetClient *c, const NetMonsterView *v)
{
    const double want = (double)c->lastServerTick + (g_now - c->lastSnapMs) / TICK_MS - (double)NET_INTERP_TICKS;
    const GvexRecord *a = Hist((uint32_t)floor(want), v->id);
    if (!a) return 1e9f;
    float d = fabsf(v->yaw - a->yaw);
    d = fmodf(d, 6.28318531f);
    return d > 3.14159265f ? 6.28318531f - d : d;
}

int main(void)
{
    GameConfig_LoadBaked();                      // the hands' numbers, as the server program installs them
    printf("the wire format\n");
    {
        NetMsg m; memset(&m, 0, sizeof m);
        m.kind = NET_MSG_SNAPSHOT; m.u.snap.serverTick = 77; m.u.snap.started = 1; m.u.snap.nRemote = 3; m.u.snap.nMonster = NET_MAX_MONSTERS;
        for (int i = 0; i < 3; ++i) m.u.snap.remote[i].id = (uint8_t)(i + 1);
        for (int i = 0; i < NET_MAX_MONSTERS; ++i)
            m.u.snap.monster[i] = (NetMonster){ (uint16_t)(1000 + i), 0, NET_MON_FLAGS(i % 3, (i / 3) % 3), -12345 + i, i, 987654 - i, (uint16_t)(65535 - i), (uint8_t)(255 - i) };
        uint8_t buf[NET_MTU];
        const size_t n = NetProto_Encode(&m, buf, sizeof buf);
        NetMsg back;
        Check(n > 0 && n < NET_MTU && NetProto_Decode(buf, n, &back), "a snapshot with 3 other players and 32 creatures fits in a datagram and decodes");
        int same = back.u.snap.nMonster == NET_MAX_MONSTERS;
        for (int i = 0; same && i < NET_MAX_MONSTERS; ++i) {
            const NetMonster *x = &back.u.snap.monster[i], *y = &m.u.snap.monster[i];
            same = x->id == y->id && x->kind == y->kind && x->flags == y->flags && x->px == y->px && x->py == y->py && x->pz == y->pz && x->yaw == y->yaw && x->health == y->health;
        }
        Check(same, "and every creature comes back exactly as it went");
        printf("  (that snapshot is %zu bytes of the %d a datagram may hold)\n", n, NET_MTU);
        NetMsg none; memset(&none, 0, sizeof none); none.kind = NET_MSG_SNAPSHOT;
        const size_t n0 = NetProto_Encode(&none, buf, sizeof buf);
        Check(n0 > 0, "(an empty snapshot encodes)");
        Check(n - n0 == 3u * 18u + 19u * NET_MAX_MONSTERS, "a creature costs 19 bytes and another player 18");

        NetMsg bad = m;
        bad.u.snap.nMonster = 1; bad.u.snap.monster[0].kind = NET_MON_KINDS;
        size_t nb = NetProto_Encode(&bad, buf, sizeof buf);
        Check(nb > 0 && !NetProto_Decode(buf, nb, &back), "a creature of a kind that does not exist is refused");
        bad.u.snap.monster[0].kind = 0; bad.u.snap.monster[0].flags = 3;                 // state 3: nothing
        nb = NetProto_Encode(&bad, buf, sizeof buf);
        Check(nb > 0 && !NetProto_Decode(buf, nb, &back), "a creature in a state that does not exist is refused");
        bad.u.snap.monster[0].flags = 0x10;
        nb = NetProto_Encode(&bad, buf, sizeof buf);
        Check(nb > 0 && !NetProto_Decode(buf, nb, &back), "and one with stray flag bits");
        bad.u.snap.nMonster = NET_MAX_MONSTERS + 1;
        Check(NetProto_Encode(&bad, buf, sizeof buf) == 0, "more creatures than a snapshot may carry cannot be encoded");
        const size_t full = NetProto_Encode(&m, buf, sizeof buf);
        buf[full - 1 - 19u * (NET_MAX_MONSTERS) + 18u] = 0;                                // (a touch of garbage in the middle must not crash the decoder)
        NetProto_Decode(buf, full, &back);
        Check(!NetProto_Decode(buf, full - 5, &back), "a snapshot cut short is refused");
    }

    printf("the same ghouls on both screens\n");
    {
        World(40.0f, 10.0f, 0.0f);
        Run(1500.0);
        EnemySpawn plan[GVEX_MAX_CREATURES];
        const int want = EnemyPlan_Spawns(&g_srv.map, FLOOR, plan, GVEX_MAX_CREATURES);
        NetMonsterView va[NET_MAX_MONSTERS], vb[NET_MAX_MONSTERS];
        const int na = NetClient_MonsterViews(&g_cl[0], g_now, va, NET_MAX_MONSTERS), nb = NetClient_MonsterViews(&g_cl[1], g_now, vb, NET_MAX_MONSTERS);
        Check(want == 6 + 2 * FLOOR && na == want && nb == want, "both clients are shown every creature the floor has (6 + 2 * floor)");
        int placed = 1, kinds = 1;
        for (int i = 0; i < want; ++i) {
            const NetMonsterView *a = View(va, na, i), *b = View(vb, nb, i);
            placed &= a && b && hypotf(a->pos.x - plan[i].x, a->pos.z - plan[i].z) < 0.002f && hypotf(b->pos.x - plan[i].x, b->pos.z - plan[i].z) < 0.002f;
            kinds &= a && b && a->kind == GVEX_KIND_GHOUL && a->state == GVEX_STATE_IDLE && a->variant == b->variant && a->health > 0.99f;
        }
        Check(placed, "each where the single-player game would put its cylinder enemy on that floor (the same seed on every machine)");
        Check(kinds, "all idle Ghouls at full health, with the same grade on both screens");
        Check(g_cl[0].floorSeed == SEED && g_cl[0].map.seed == g_srv.map.seed && EnemyHash32(g_cl[0].map.seed + 5u * 0x9e3779b1U) == plan[5].hash,
              "and a client can work out a creature's looks itself: its hash is the floor's seed (the map the client made, the server's) and the id");
        Finish();
    }

    printf("they chase, and both screens follow\n");
    {
        World(40.0f, 10.0f, 0.0f);
        Run(600.0);
        const GvexRecord *r0 = Hist(g_srv.tick, 0);
        const GvexRecord g0 = *r0;
        // put player 0 in clear view of ghoul 0, a few metres away (the server's player: the clients' own predictions are corrected to it)
        float sx = 0, sz = 0; int found = 0;
        for (int tz = 1; tz < g_srv.map.h - 1 && !found; ++tz)
            for (int tx = 1; tx < g_srv.map.w - 1 && !found; ++tx) {
                const float px = (float)tx * TILE_SIZE, pz = (float)tz * TILE_SIZE, d = hypotf(px - g0.x, pz - g0.z);
                if (d < 7.0f || d > 10.0f || EnemyWalk_Blocked(&g_srv.map, px, pz, 0.5f)) continue;
                if (!EnemyWalk_LineOfSight(&g_srv.map, (Vector3){ g0.x, 0, g0.z }, (Vector3){ px, 0, pz })) continue;
                sx = px; sz = pz; found = 1;
            }
        Check(found, "(a spot in clear view of ghoul 0 was found)");
        g_srv.c[g_slot[0]].player.position.x = sx; g_srv.c[g_slot[0]].player.position.z = sz;
        {   // a player who has not started the game is nobody to the creatures: nothing wakes while it stands in clear view
            const uint32_t startTick = g_srv.c[g_slot[0]].startTick;                // the server starts a player at its start tick; hold that back for a while
            g_srv.c[g_slot[0]].started = false; g_srv.c[g_slot[0]].startTick = 0xFFFFFFFFu;          // (0 would be set again by the next command)
            Run(300.0);
            Check(Hist(g_srv.tick, 0)->state == GVEX_STATE_IDLE, "a player who has not started is not noticed, though it stands in clear view");
            g_srv.c[g_slot[0]].started = true; g_srv.c[g_slot[0]].startTick = startTick;
        }
        int stateOk = 1, stateSamples = 0;
        for (int ms = 0; ms < 300; ++ms) {                                          // the wake-up, sampled every millisecond: the state shown is the later snapshot's
            Step1ms();
            NetMonsterView q[NET_MAX_MONSTERS];
            const int nq = NetClient_MonsterViews(&g_cl[0], g_now, q, NET_MAX_MONSTERS);
            const NetMonsterView *x = View(q, nq, 0);
            const double want = (double)g_cl[0].lastServerTick + (g_now - g_cl[0].lastSnapMs) / TICK_MS - (double)NET_INTERP_TICKS;
            const GvexRecord *later = Hist((uint32_t)floor(want) + 1, 0), *earlier = Hist((uint32_t)floor(want), 0);
            if (x && later && earlier && later->state != earlier->state) { ++stateSamples; stateOk &= x->state == later->state; }
        }
        Check(stateSamples > 0 && stateOk, "while it changes from idle to chasing, each screen shows the later snapshot's state in between (as the other players' flags are)");
        NetMonsterView va[NET_MAX_MONSTERS], vb[NET_MAX_MONSTERS];
        int na = NetClient_MonsterViews(&g_cl[0], g_now, va, NET_MAX_MONSTERS), nb = NetClient_MonsterViews(&g_cl[1], g_now, vb, NET_MAX_MONSTERS);
        const NetMonsterView *a = View(va, na, 0), *b = View(vb, nb, 0);
        Check(a && b && a->state != GVEX_STATE_IDLE && b->state != GVEX_STATE_IDLE, "the ghoul that saw a player is shown chasing (or attacking) on both screens");
        float worstA = 0, worstB = 0, apart = 0, worstYaw = 0; int samples = 0, others = 1;
        for (int i = 0; i < 90; ++i) {                                              // three seconds, sampled every tick
            Run(TICK_MS);
            na = NetClient_MonsterViews(&g_cl[0], g_now, va, NET_MAX_MONSTERS); nb = NetClient_MonsterViews(&g_cl[1], g_now, vb, NET_MAX_MONSTERS);
            for (int id = 0; id < 8; ++id) {
                const NetMonsterView *x = View(va, na, id), *y = View(vb, nb, id);
                if (!x || !y) { others = 0; continue; }
                const float ea = ShownError(&g_cl[0], x), eb = ShownError(&g_cl[1], y);
                if (ea < 1e8f && ea > worstA) worstA = ea;
                if (eb < 1e8f && eb > worstB) worstB = eb;
                if (id == 0) { const float ey = ShownYawError(&g_cl[0], x); if (ey < 1e8f && ey > worstYaw) worstYaw = ey; }
                const float d = hypotf(x->pos.x - y->pos.x, x->pos.z - y->pos.z);
                if (d > apart) apart = d;
                ++samples;
            }
        }
        Check(samples > 600 && others, "every creature is on both screens at every sample");
        Check(worstA < 0.03f && worstB < 0.03f, "each screen shows each creature where the server had it at the tick it is showing (the interpolation, redone from the server's history)");
        Check(worstYaw < 0.1f, "and the chasing ghoul is shown facing the way the server has it (to within 0.1 rad)");
        Check(apart < 0.35f, "and the two screens agree with each other to within a third of a metre (they are a few milliseconds apart in time)");
        const GvexRecord *now0 = Hist(g_srv.tick, 0);
        Check(hypotf(now0->x - g0.x, now0->z - g0.z) > 1.0f, "(ghoul 0 really did walk at the players)");
        Finish();
    }

    printf("noise reaches the creatures\n");
    {
        // the server feeds each started player's noise (PlayerNoise) to the creatures: a player standing out of sight 6 m from a ghoul is not heard (3 m),
        // the same player sprinting is (20 m), through the wall between them
        float hx = 0, hz = 0; int found = 0;
        World(40.0f, 10.0f, 0.0f);
        Run(600.0);
        const GvexRecord g0 = *Hist(g_srv.tick, 0);
        for (int tz = 1; tz < g_srv.map.h - 1 && !found; ++tz)
            for (int tx = 1; tx < g_srv.map.w - 1 && !found; ++tx) {
                const float px = (float)tx * TILE_SIZE, pz = (float)tz * TILE_SIZE, d = hypotf(px - g0.x, pz - g0.z);
                if (d < 6.0f || d > 9.0f || EnemyWalk_Blocked(&g_srv.map, px, pz, 0.5f)) continue;
                if (EnemyWalk_LineOfSight(&g_srv.map, (Vector3){ g0.x, 0, g0.z }, (Vector3){ px, 0, pz })) continue;
                hx = px; hz = pz; found = 1;
            }
        Check(found, "(a spot 6 to 9 m from ghoul 0 with a wall between was found)");
        g_srv.c[g_slot[0]].player.position.x = hx; g_srv.c[g_slot[0]].player.position.z = hz;
        Run(500.0);
        const GvexRecord *quiet = Hist(g_srv.tick, 0);
        Check(quiet->state == GVEX_STATE_IDLE, "standing there, silent, it is not heard");
        Finish();
        World(40.0f, 10.0f, 0.0f);
        Run(600.0);
        g_srv.c[g_slot[0]].player.position.x = hx; g_srv.c[g_slot[0]].player.position.z = hz;
        int heard = 0;
        g_sprint[0] = 1;                                                                // the client holds forward and sprint
        for (int i = 0; i < 30 && !heard; ++i) { Run(TICK_MS); heard = Hist(g_srv.tick, 0)->state != GVEX_STATE_IDLE; }
        Check(heard, "while sprinting (noise 20 m) it is heard through the wall and wakes");
        Finish();
    }

    printf("a bad link\n");
    {
        World(80.0f, 40.0f, 0.10f);
        Run(600.0);
        const GvexRecord g0 = *Hist(g_srv.tick, 1);
        float sx = g0.x + 6.0f, sz = g0.z;
        g_srv.c[g_slot[1]].player.position.x = sx; g_srv.c[g_slot[1]].player.position.z = sz;
        float worst = 0; int samples = 0, shown = 1;
        for (int i = 0; i < 150; ++i) {
            Run(TICK_MS);
            NetMonsterView v[NET_MAX_MONSTERS];
            const int n = NetClient_MonsterViews(&g_cl[0], g_now, v, NET_MAX_MONSTERS);
            if (n != 6 + 2 * FLOOR) shown = 0;
            for (int k = 0; k < n; ++k) { const float e = ShownError(&g_cl[0], &v[k]); if (e < 1e8f) { if (e > worst) worst = e; ++samples; } }
        }
        Check(shown, "with 80 ms latency, 40 ms jitter and 10% loss every creature is still on the screen at every sample");
        Check(samples > 1000 && worst < 0.6f, "and none is more than 0.6 m from where the server had it at the tick shown (a lost snapshot is a longer straight line)");
        Finish();
    }

    printf("killed on the server\n");
    {
        World(40.0f, 10.0f, 0.0f);
        Run(800.0);
        const int n0 = GvexWorld_Count(g_srv.gvex);
        Check(GvexWorld_Damage(g_srv.gvex, 2, 1000.0f), "(creature 2 is killed on the server)");
        Run(150.0);
        {
            NetMonsterView q[NET_MAX_MONSTERS];
            const int nq = NetClient_MonsterViews(&g_cl[0], g_now, q, NET_MAX_MONSTERS);
            Check(!View(q, nq, 2), "it is gone from the screen within 150 ms of the kill: a creature the server no longer lists is not kept for the interpolation delay");
        }
        Run(250.0);
        NetMonsterView va[NET_MAX_MONSTERS], vb[NET_MAX_MONSTERS];
        const int na = NetClient_MonsterViews(&g_cl[0], g_now, va, NET_MAX_MONSTERS), nb = NetClient_MonsterViews(&g_cl[1], g_now, vb, NET_MAX_MONSTERS);
        Check(na == n0 - 1 && nb == n0 - 1 && !View(va, na, 2) && !View(vb, nb, 2), "it is gone from both screens");
        Check(View(va, na, 1) && View(va, na, 3) && View(vb, nb, 1) && View(vb, nb, 3), "and the creatures with the ids on either side of it are still there");
        Finish();
    }

    printf("fists: the server runs the players' hands\n");
    {
        const WeaponDef *hands = Weapon_Def(WEAPON_HANDS);
        Check(hands->damage > 0.0f && hands->reach > 0.0f && hands->cooldown > 0.0f && hands->kind == WEAPON_KIND_UNARMED, "(set up: the baked config gives the hands a punch)");
        World(30.0f, 4.0f, 0.0f);
        Run(600.0);
        int id = -1; float dx = 0.0f, dz = 0.0f;
        Check(FindSide(&id, &dx, &dz, -1), "(set up: a creature with clear floor to one side was found)");
        GvexRecord c;
        RecOf(id, &c);
        const float maxHp = c.maxHealth;
        Check(Arsenal_OwnedCount(&g_srv.c[0].arsenal) == 1 && g_srv.c[0].arsenal.current == WEAPON_HANDS && Arsenal_EquippedCount(&g_srv.c[0].arsenal) == 0 && g_srv.c[1].arsenal.current == WEAPON_HANDS, "a player in a session starts with their hands and nothing else, in hand");
        Stand(0, &c, dx, dz, 1.2f, 1);
        Run(1500.0);
        RecOf(id, &c);
        Check(c.health == maxHp, "a player standing in front of a creature, not firing, does it no harm (1.5 s)");

        g_fire[0] = 1; Run(2000.0); g_fire[0] = 0; Run(500.0);
        Check(RecOf(id, &c), "(it is still alive after five blows or so)");
        const float lost = maxHp - c.health, blows = lost / hands->damage;
        Check(fabsf(blows - roundf(blows)) < 1e-3f && blows >= 4.0f && blows <= 5.0f, "holding fire for two seconds: four or five blows landed, each the hands' damage, and no blow in between (a blow every 0.45 s, the first after its windup)");
        {
            NetMonsterView va[NET_MAX_MONSTERS], vb[NET_MAX_MONSTERS];
            const int na = NetClient_MonsterViews(&g_cl[0], g_now, va, NET_MAX_MONSTERS), nb = NetClient_MonsterViews(&g_cl[1], g_now, vb, NET_MAX_MONSTERS);
            const NetMonsterView *a = View(va, na, id), *b = View(vb, nb, id);
            Check(a && b && fabsf(a->health - c.health / maxHp) < 0.008f && fabsf(b->health - c.health / maxHp) < 0.008f, "the puncher's screen and the other player's both show the creature with the health the server left it");
            Check(a && b && hypotf(a->pos.x - c.x, a->pos.z - c.z) < 1.0f, "and it is still where the punches left it (knocked back and closing in, not thrown away)");
            const float away = hypotf(c.x - g_srv.c[0].player.position.x, c.z - g_srv.c[0].player.position.z);
            Check(away > 1.35f && away < 2.6f, "the blows knocked it back from the 1.2 m it started at (and it closes up to its reach again: not flung away)");
            Vector3 hp3;
            Check(NetMonHistory_PosAt(&g_srv.mhist, g_srv.tick, id, &hp3) && fabsf(hp3.x - c.x) < 0.002f && fabsf(hp3.z - c.z) < 0.002f, "the server's history of the creature has where it is (to the millimetre the snapshot carries)");
        }

        g_fire[0] = 1;
        int gone = 0;
        for (int i = 0; i < 80 && !gone; ++i) { Run(100.0); gone = !RecOf(id, &c); }
        g_fire[0] = 0;
        Check(gone, "held on, the creature dies (it has 100 health: ten blows)");
        Run(300.0);
        {
            NetMonsterView va[NET_MAX_MONSTERS], vb[NET_MAX_MONSTERS];
            const int na = NetClient_MonsterViews(&g_cl[0], g_now, va, NET_MAX_MONSTERS), nb = NetClient_MonsterViews(&g_cl[1], g_now, vb, NET_MAX_MONSTERS);
            Check(!View(va, na, id) && !View(vb, nb, id) && na == GvexWorld_Count(g_srv.gvex) && nb == na, "and it is gone from both screens, the others still there");
        }
        Finish();

        // behind the player, close enough to be in reach but not in the arc: nothing
        World(30.0f, 4.0f, 0.0f);
        Run(600.0);
        Check(FindSide(&id, &dx, &dz, -1), "(set up again)");
        RecOf(id, &c);
        Stand(0, &c, dx, dz, 1.7f, 0);
        g_fire[0] = 1; Run(1500.0); g_fire[0] = 0; Run(200.0);
        GvexRecord c2;
        Check(RecOf(id, &c2) && c2.health == maxHp, "a creature 1.7 m behind a player who is punching the other way is not hit");
        Finish();

        // nobody firing: a player can walk through a crowd of creatures and the hands do nothing (the arsenal only works on the fire button)
        World(30.0f, 4.0f, 0.0f);
        Run(600.0);
        g_sprint[0] = 1; Run(3000.0);
        int whole = 1; GvexRecord all[GVEX_MAX_CREATURES]; const int n = GvexWorld_Records(g_srv.gvex, all, GVEX_MAX_CREATURES);
        for (int i = 0; i < n; ++i) whole &= all[i].health == all[i].maxHealth;
        Check(whole, "a player sprinting about for three seconds without the fire button hurts nobody");
        Finish();
    }

    printf("fists are judged where the puncher saw the creature\n");
    {
        World(30.0f, 4.0f, 0.0f);
        Run(2500.0);
        int id = -1; float dx = 0.0f, dz = 0.0f;
        Check(FindSide(&id, &dx, &dz, -1), "(set up: a creature with clear floor to one side was found)");
        GvexRecord c;
        RecOf(id, &c);
        const WeaponDef *hands = Weapon_Def(WEAPON_HANDS);
        const Vector3 P = { c.x, c.y, c.z };
        const Vector3 farX = { P.x + 12.0f, P.y, P.z }, farZ = { P.x, P.y, P.z + 12.0f };
        const uint32_t T = g_srv.tick;
        Check(T >= 60, "(set up: the server has run long enough to have history to rewind into)");
        for (uint32_t t = T - 40; t < T; ++t) {                               // far along z until 21 ticks ago, far along x until 8 ago, here since
            NetMonHistory_Begin(&g_srv.mhist, t);
            NetMonHistory_Set(&g_srv.mhist, t, id, t < T - 20 ? farZ : (t < T - 7 ? farX : P));
        }
        const float yaw = atan2f(-dx, -dz);                                   // looking at the creature from one metre away on its side
        const Vector3 nearP = { P.x + dx, P.y + 1.0f, P.z + dz };
        const Vector3 nearFarX = { farX.x + dx, farX.y + 1.0f, farX.z + dz };
        uint16_t ids[8];
        Check(NetServer_MeleeTargets(&g_srv, 0, nearP, yaw, hands, ids, 8) == 1 && ids[0] == id, "with no lag the creature is judged where it is now (there is no frame for this tick)");
        Check(NetServer_MeleeTargets(&g_srv, 3, nearP, yaw, hands, ids, 8) == 1, "a lag of 3 ticks: it was here then too: hit");
        Check(NetServer_MeleeTargets(&g_srv, 10, nearP, yaw, hands, ids, 8) == 0, "a lag of 10 ticks: it was 12 m away then, and the screen showed it there: miss");
        Check(NetServer_MeleeTargets(&g_srv, 10, nearFarX, yaw, hands, ids, 8) == 1 && ids[0] == id, "a puncher whose screen showed it 12 m along x ten ticks ago and who swung at it there: hit, though it is not there now");
        Check(NetServer_MeleeTargets(&g_srv, 0, nearFarX, yaw, hands, ids, 8) == 0, "(the same swing with no lag misses: the rewind is what makes it land)");
        Check(NetServer_MeleeTargets(&g_srv, 30, nearFarX, yaw, hands, ids, 8) == 1, "a lag of 30 is held to NET_REWIND_MAX (20 ticks): it is judged at 20 ticks ago (along x), not at 30 (along z)");
        Check(NetServer_MeleeTargets(&g_srv, 255, nearFarX, yaw, hands, ids, 8) == 1, "and a lag of 255 is the same: a cheat or a bad link gets no further back");
        const Vector3 farther = { P.x + dx * 1.7f, P.y + 1.0f, P.z + dz * 1.7f };        // 1.7 m from its centre: its surface is over half a metre away at any size
        Check(NetServer_MeleeTargets(&g_srv, 3, farther, yaw, hands, ids, 8) == 1, "(from 1.7 m, looking at it: hit)");
        Check(NetServer_MeleeTargets(&g_srv, 3, farther, yaw + 3.14159265f, hands, ids, 8) == 0, "and looking the other way from there: miss"); 
        Check(NetServer_MeleeTargets(&g_srv, 3, nearP, yaw, hands, ids, 0) == 0 && NetServer_MeleeTargets(&g_srv, 3, nearP, yaw, NULL, ids, 8) == 0 && NetServer_MeleeTargets(&g_srv, 3, nearP, yaw, hands, NULL, 8) == 0, "no room for results, no weapon, or nowhere to write: nothing, and no crash");
        Check(GvexWorld_Damage(g_srv.gvex, id, 10000.0f) && NetServer_MeleeTargets(&g_srv, 3, nearP, yaw, hands, ids, 8) == 0, "a creature that has died since is not a target, however far back the puncher's screen shows it alive");
        Finish();
    }

    printf("the server frees its world\n");
    {
        NetServer s;
        NetServer_Init(&s, SEED, FLOOR);
        Check(s.gvex != NULL && GvexWorld_Count(s.gvex) == 6 + 2 * FLOOR, "a server made with TOWER_GVEX has its creatures");
        NetServer_Free(&s);
        Check(s.gvex == NULL, "and gives them up, once (the sanitizer says whether it all came back)");
        NetServer_Free(&s);
        Check(1, "twice is harmless");
    }

    printf("\n%s (%d failures of %d checks)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_checks);
    return g_failures ? 1 : 0;
}
#endif
