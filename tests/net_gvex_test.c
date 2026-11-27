// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/testing.md, section "tests/net_gvex_test.c"
// ============================================================================
// Headless test of the creatures in a session (slice S1 of docs/gvex_session.md): a server with its world of creatures, two clients over a link with
// latency, jitter and loss, and what each client is shown. The same ghouls on both screens, where the server has them (a tick or three behind),
// the same looks as the single-player game gives its cylinders, a creature killed on the server gone from both screens, and the wire format of a
// full snapshot. It needs the Flecs object, so it is built by tests/gvex_check.sh (exit 77, SKIP, where that is not possible; its name ends in _test so that run_tests.sh does not build it with a plain line):
//   cc -O1 -std=c99 -DNET_GVEX_HEADLESS_TEST -DTOWER_GVEX=1 -Isrc -Iraylib/src -Ithird_party/flecs tests/net_gvex_test.c src/net/net_wire.c src/net/net_proto.c src/net/net_loopback.c src/net/net_move_sim.c src/net/net_server.c src/net/net_history.c src/net/net_client.c src/gvex/gvex_world.c third_party/flecs/tob_flecs.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -lpthread -o net_gvex_test
#ifdef NET_GVEX_HEADLESS_TEST
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "enemies/enemy_plan.h"
#include "gvex/gvex_world.h"
#include "net/net_client.h"
#include "net/net_loopback.h"
#include "net/net_server.h"
#include "net/net_wire.h"

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
static NetCmd Cmd(void *user, uint32_t tick) { (void)tick; return *(int *)user ? NetCmd_Make(0, 1, 0, 0, NET_BTN_SPRINT) : NetCmd_Make(0, 0, 0, 0, 0); }

static void World(float latency, float jitter, float loss)
{
    g_now = 1000.0; g_nextTick = g_now + TICK_MS;
    memset(g_histN, 0, sizeof g_histN);
    memset(g_sprint, 0, sizeof g_sprint);
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
