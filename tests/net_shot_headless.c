// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/testing.md, section "net_shot_headless.c"
// ============================================================================
// Headless test of the foundations for shooting in a networked game: the shot seed (the same pellets on every machine), the server's pose
// history and its rewind, the view lag a client puts on every command, and, end to end on a real (loopback) link, that the pose the server
// rewinds a player to is the pose the shooter's screen showed, and that without the rewind it is not.
// Build and run from the project root (the raylib sources are only read for their headers):
//   cc -O2 -std=c99 -DNET_SHOT_HEADLESS_TEST -Isrc -Iraylib/src tests/net_shot_headless.c src/net/net_wire.c src/net/net_proto.c src/net/net_loopback.c src/net/net_move_sim.c src/net/net_server.c src/net/net_history.c src/net/net_client.c src/net/net_play.c src/net/net_shot.c src/input/input_latch.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o net_shot_headless
#ifdef NET_SHOT_HEADLESS_TEST
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "net/net_history.h"
#include "net/net_loopback.h"
#include "net/net_play.h"
#include "net/net_server.h"
#include "net/net_shot.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }
static int Near(float a, float b, float e) { return fabsf(a - b) <= e; }

#define TICK_MS (1000.0 / 30.0)
static NetServer g_srv;
static NetLoopback g_lb[2];
static NetClient g_cl[2];
static NetPlay g_play[2];
static InputFrame g_in[2];
static int g_slot[2];
static double g_now, g_nextTick, g_nextFrame;
typedef struct { uint32_t tick; bool have; float x, z; } Seen;
static Seen g_seen[256];                          // what client 1's screen showed of player 0 when it made each command

static NetCmd Watch1(void *user, uint32_t tick)   // client 1's fingers, and a note of where it sees player 0 at that moment
{
    (void)user;
    NetCmd c = NetPlay_Take(&g_play[1]);
    Vector3 p; float yaw;
    Seen *s = &g_seen[tick % 256];
    s->tick = tick;
    s->have = NetClient_RemoteView(&g_cl[1], 0, g_now, &p, &yaw);
    s->x = p.x; s->z = p.z;
    return c;
}

static void World(void)
{
    g_now = 1000.0; g_nextTick = g_now + TICK_MS; g_nextFrame = g_now;
    NetServer_Init(&g_srv, 777u, 1);
    for (int k = 0; k < 2; ++k) {
        NetLoopback_Init(&g_lb[k], 90u + (uint32_t)k);
        NetLoopback_Advance(&g_lb[k], g_now);
        g_slot[k] = NetServer_AddClient(&g_srv, NetLoopback_End(&g_lb[k], 1), g_now);
        NetClient_Init(&g_cl[k], NetLoopback_End(&g_lb[k], 0), k == 0 ? "steve" : "brou", g_now);
        NetPlay_Init(&g_play[k], 0.0f);
        memset(&g_in[k], 0, sizeof g_in[k]);
    }
    memset(g_seen, 0, sizeof g_seen);
}

typedef struct { int n; double sumRewound, sumCurrent, maxRewound; int minLag, maxLag; } Stats;
static Stats g_st;

static void Step1ms(bool measure)
{
    g_now += 1.0;
    for (int k = 0; k < 2; ++k) NetLoopback_Advance(&g_lb[k], g_now);
    NetServer_Poll(&g_srv, g_now);
    for (int k = 0; k < 2; ++k) NetClient_Poll(&g_cl[k], g_now);
    if (g_now >= g_nextTick) {
        g_nextTick += TICK_MS;
        const unsigned fed1 = g_srv.c[g_slot[1]].ticksFed;
        NetServer_Tick(&g_srv, g_now);
        const uint32_t T = g_srv.tick;
        const NetServerClient *c1 = &g_srv.c[g_slot[1]];
        const Seen *seen = &g_seen[T % 256];
        Vector3 r; float yaw;
        if (measure && c1->ticksFed > fed1 && seen->tick == T && seen->have && NetServer_RewoundPose(&g_srv, c1->lastCmd.viewLag, 0, &r, &yaw)) {
            const Vector3 cur = g_srv.c[g_slot[0]].player.position;
            const double eR = hypot(r.x - seen->x, r.z - seen->z), eC = hypot(cur.x - seen->x, cur.z - seen->z);
            ++g_st.n; g_st.sumRewound += eR; g_st.sumCurrent += eC;
            if (eR > g_st.maxRewound) g_st.maxRewound = eR;
            if (c1->lastCmd.viewLag < g_st.minLag) g_st.minLag = c1->lastCmd.viewLag;
            if (c1->lastCmd.viewLag > g_st.maxLag) g_st.maxLag = c1->lastCmd.viewLag;
        }
    }
    if (g_now >= g_nextFrame) {
        g_nextFrame += 1000.0 / 60.0;
        for (int k = 0; k < 2; ++k) { NetPlay_Input(&g_play[k], &g_in[k], 400.0f); g_in[k].look = (Vector2){ 0, 0 }; }
    }
    NetClient_Update(&g_cl[0], g_now, NetPlay_CmdFn, &g_play[0]);
    NetClient_Update(&g_cl[1], g_now, Watch1, NULL);
}
static void Run(double ms, bool measure) { const double end = g_now + ms; while (g_now < end) Step1ms(measure); }

int main(void)
{
    printf("The shot seed\n");
    {
        Check(NetShot_Seed(1, 100, 0) == 0xc367a821u && NetShot_Seed(0, 1, 0) == 0xa0512387u && NetShot_Seed(3, 4000000000u, 255) == 0x739686c7u, "pinned values (the same on every machine and compiler: a client and a server must agree)");
        int zero = 0, same = 0;
        uint32_t seen[4000]; int n = 0;
        for (int p = 0; p < 4; ++p) for (int t = 1; t <= 500; ++t) for (int i = 0; i < 2; ++i) {
            const uint32_t s = NetShot_Seed((uint8_t)p, (uint32_t)t, (uint8_t)i);
            if (s == 0) ++zero;
            for (int k = 0; k < n; ++k) if (seen[k] == s) { ++same; break; }
            seen[n++] = s;
        }
        Check(zero == 0, "never 0 (the pellet generator needs a non-zero state)");
        Check(same == 0, "4000 different players, ticks and shots give 4000 different seeds");
        Check(NetShot_Seed(0, 0, 0) != 0, "even for all zeros");
    }

    printf("\nThe pose history\n");
    {
        NetHistory h; NetHistory_Init(&h);
        for (uint32_t t = 1; t <= 100; ++t) {
            NetHistory_Begin(&h, t);
            NetHistory_Set(&h, t, 0, (Vector3){ (float)t, 0.0f, 0.0f }, 0.1f * (float)t);
            if (t % 2 == 0) NetHistory_Set(&h, t, 2, (Vector3){ 0.0f, 0.0f, (float)t }, 0.0f);
        }
        Vector3 p; float y;
        Check(NetHistory_PoseAt(&h, 100, 0, &p, &y) && Near(p.x, 100.0f, 1e-6f) && Near(y, 10.0f, 1e-4f), "the newest tick is there with its pose");
        Check(NetHistory_PoseAt(&h, 100 - NET_HIST_FRAMES + 1, 0, &p, NULL) && Near(p.x, (float)(100 - NET_HIST_FRAMES + 1), 1e-6f), "and so is the oldest the ring still holds");
        Check(!NetHistory_PoseAt(&h, 100 - NET_HIST_FRAMES, 0, &p, NULL), "an older tick has been replaced: not there (not somebody else's pose)");
        Check(!NetHistory_PoseAt(&h, 101, 0, &p, NULL) && !NetHistory_PoseAt(&h, 0, 0, &p, NULL), "a tick not yet run, or tick 0: not there");
        Check(NetHistory_PoseAt(&h, 98, 2, &p, NULL) && Near(p.z, 98.0f, 1e-6f) && !NetHistory_PoseAt(&h, 99, 2, &p, NULL), "a player who was there at some ticks and not others: only at those");
        Check(!NetHistory_PoseAt(&h, 98, 1, &p, NULL) && !NetHistory_PoseAt(&h, 98, -1, &p, NULL) && !NetHistory_PoseAt(&h, 98, NET_MAX_PLAYERS, &p, NULL), "a player who was never there, and ids out of range: not there");
        Check(NetHistory_PoseAtF(&h, 50.25, 0, &p, &y) && Near(p.x, 50.25f, 1e-4f) && Near(y, 5.025f, 1e-4f), "between two ticks the pose is blended");
        Check(NetHistory_PoseAtF(&h, 50.0, 0, &p, NULL) && Near(p.x, 50.0f, 1e-6f), "on a tick it is that tick's");
        Check(!NetHistory_PoseAtF(&h, 99.5, 2, &p, NULL) && !NetHistory_PoseAtF(&h, 0.5, 0, &p, NULL), "between a tick with the player and one without: not there; before tick 1: not there");
        NetHistory_Begin(&h, 101);
        NetHistory_Set(&h, 101, 1, (Vector3){ 0, 0, 0 }, 6.2f);
        NetHistory_Begin(&h, 102);
        NetHistory_Set(&h, 102, 1, (Vector3){ 1, 0, 0 }, 0.1f);
        NetHistory_Begin(&h, 103);
        NetHistory_Set(&h, 103, 1, (Vector3){ 2, 0, 0 }, 6.2f);
        float yw = 0.0f;
        Check(NetHistory_PoseAtF(&h, 101.5, 1, NULL, &yw) && (Near(yw, 6.2916f, 0.01f) || Near(yw, 0.0084f, 0.01f)), "a yaw blended across a full turn (6.2 to 0.1) goes the short way, through 0, not backwards through 3");
        Check(NetHistory_PoseAtF(&h, 102.5, 1, NULL, &yw) && (Near(yw, 6.2916f, 0.01f) || Near(yw, 0.0084f, 0.01f)), "and the other way (0.1 to 6.2) as well");
        NetHistory_Begin(&h, 101);
        Check(!NetHistory_PoseAt(&h, 101, 0, &p, NULL), "a frame just begun has nobody in it until they are set");
        NetHistory_Set(&h, 7, 0, (Vector3){ 1, 2, 3 }, 0.0f);
        Check(!NetHistory_PoseAt(&h, 7, 0, &p, NULL), "setting a pose for a tick whose frame is not there does nothing");
        Check(NetHistory_RewindTick(100, 7) == 93 && NetHistory_RewindTick(100, 0) == 100, "a shot made at tick 100 by someone 7 ticks behind is judged at tick 93");
        Check(NetHistory_RewindTick(100, 200) == 100 - NET_REWIND_MAX && NetHistory_RewindTick(100, NET_REWIND_MAX) == 100 - NET_REWIND_MAX, "a claim of more than the limit is judged at the limit: no one gets a longer rewind by asking");
        Check(NetHistory_RewindTick(5, 200) == 1 && NetHistory_RewindTick(3, 3) == 1 && NetHistory_RewindTick(1, 0) == 1, "never before tick 1");
    }

    printf("\nThe view lag a client puts on a command\n");
    {
        static NetClient c; memset(&c, 0, sizeof c);
        Check(NetClient_ViewLag(&c, 5000.0, 100) == 0, "before the first snapshot: 0");
        c.snapCount = 1; c.lastServerTick = 100; c.lastSnapMs = 1000.0;
        Check(NetClient_ViewLag(&c, 1000.0, 106) == 9, "snapshot of tick 100 just arrived, command for tick 106: the others are drawn at 97, so 9 ticks");
        Check(NetClient_ViewLag(&c, 1000.0 + 2.0 * TICK_MS, 106) == 7, "two ticks later the server is at 102, the others at 99, the command is still for 106: 7 ticks");
        Check(NetClient_ViewLag(&c, 1000.0, 90) == 0, "a command for a tick before what is drawn: 0, not negative");
        Check(NetClient_ViewLag(&c, 1000.0, 100000) == 255, "an absurd gap is held at 255");
        Check(NetClient_ViewLag(&c, 1000.0 + 0.3 * TICK_MS, 106) == 9 && NetClient_ViewLag(&c, 1000.0 + 0.7 * TICK_MS, 106) == 8, "a part of a tick rounds to the nearest tick (8.7 ticks is 9, 8.3 is 8)");
    }

    printf("\nLag compensation on a real link\n");
    {
        World();
        g_st.minLag = 255; g_st.maxLag = 0;
        Run(1500.0, false);
        for (int leg = 0; leg < 4; ++leg) {                              // player 0 walks forward and back (the floor has walls); player 1 stands and looks at them
            g_in[0].move = (Vector2){ 0.0f, leg % 2 == 0 ? -1.0f : 1.0f };
            Run(300.0, false);                                           // (let the turn settle: the first ticks of a new direction are the accelerating ones)
            Run(1500.0, true);
        }
        printf("    (%d commands judged; the lag was %d to %d ticks; the biggest miss of the rewound pose %.3f m, the mean %.3f m; the mean miss with no rewind %.3f m)\n",
               g_st.n, g_st.minLag, g_st.maxLag, g_st.maxRewound, g_st.sumRewound / (g_st.n ? g_st.n : 1), g_st.sumCurrent / (g_st.n ? g_st.n : 1));
        Check(g_st.n > 150, "(most of the commands of seven seconds of walking could be judged)");
        Check(g_st.minLag >= 3 && g_st.maxLag <= 12, "the view lag is what it should be: the lead over the server plus the 3 ticks others are drawn behind (3 to 12)");
        Check(g_st.maxRewound < 0.25 && g_st.sumRewound / g_st.n < 0.10, "the pose the server rewinds player 0 to is the pose player 1's screen showed, to within a tick's walk (under 0.25 m at worst)");
        Check(g_st.sumCurrent / g_st.n > 0.60, "and without the rewind the server would have judged against a place about a metre ahead of what was on the screen");
        Check(g_st.sumRewound / g_st.n < 0.2 * (g_st.sumCurrent / g_st.n), "the rewind is more than five times closer on average");
        Vector3 p; float yaw;
        Check(!NetServer_RewoundPose(&g_srv, 7, 3, &p, &yaw), "a player who is not in the game has no rewound pose");
        Check(NetServer_RewoundPose(&g_srv, 7, 0, &p, &yaw) && NetServer_RewoundPose(&g_srv, 250, 0, &p, &yaw), "and a lag of 250 still gets one (at the limit)");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
