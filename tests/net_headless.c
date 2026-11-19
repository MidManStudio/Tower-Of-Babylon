// Headless test of the multiplayer netcode: the wire format and the protocol (and that a stranger's bytes cannot hurt them), the
// pretend network (delay, loss, doubling, reordering, blackouts), the handshake, and the real thing running through that network:
// a server and up to four clients moving players with the game's own movement, client prediction, reconciliation against the server,
// the clock lead, interpolation of other players, and what the server does with commands that are late, early, doubled, forged or
// flooding. No window, no raylib calls, no real network. Run from the project root:
//   cc -O2 -std=c99 -DNET_HEADLESS_TEST -Isrc -Iraylib/src tests/net_headless.c src/net/net_wire.c src/net/net_proto.c src/net/net_loopback.c src/net/net_move_sim.c src/net/net_server.c src/net/net_history.c src/net/net_client.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o net_headless
#ifdef NET_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dungeon/dungeon_generator.h"
#include "net/net_client.h"
#include "net/net_loopback.h"
#include "net/net_proto.h"
#include "net/net_server.h"
#include "net/net_wire.h"

// The player controller's debug drawing, which this test does not draw (as in movement_headless.c).
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }

bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }       // no props on this floor (they are a module-global list in the game; docs/net.md says what that means)

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

// ---- a world: one server, up to four clients, a link for each, and a clock --------------------------------------------------
#define TICK_MS (1000.0 / 30.0)
static NetServer g_srv;
static NetLoopback g_lb[NET_MAX_PLAYERS];
static NetClient g_cl[NET_MAX_PLAYERS];
static int g_nClients, g_slot[NET_MAX_PLAYERS];
static double g_now, g_nextSrvTick;
static PlayerController g_srvHist[NET_MAX_PLAYERS][256];       // each player on the server, after every tick (to compare with what a client had predicted)
static bool g_quiet[NET_MAX_PLAYERS];                          // a client that is made to say nothing
static unsigned g_airTicks, g_sprintTicks, g_slideTicks, g_diveTicks;   // what the first player did on the server, to prove the script really jumps, sprints, slides and dives
static float g_topSpeed, g_topHeight;

static NetCmd Script(void *user, uint32_t t)
{
    const int k = (int)(intptr_t)user;
    const int phase = (int)((t / 45u) % 10u), at = (int)(t % 45u);
    float mx = 0.0f, my = 0.0f; uint16_t b = 0;
    switch (phase) {
    case 0: my = -1.0f; break;
    case 1: my = -1.0f; b |= NET_BTN_SPRINT; break;
    case 2: my = -1.0f; b |= NET_BTN_SPRINT; if (at == 3 || at == 20 || at == 36) b |= NET_BTN_SLIDE; break;       // tried a few times: a slide needs a sprint that has got going, and a wall can stop one
    case 3: mx = 1.0f; break;
    case 4: my = 1.0f; break;
    case 5: my = -1.0f; if (at == 10) b |= NET_BTN_JUMP; break;
    case 6: break;
    case 7: mx = 1.0f; b |= NET_BTN_SPRINT; if (at == 20 || at == 32 || at == 42) b |= NET_BTN_DIVE; break;      // a sprint to the right, then a dive from the ground (tried a few times, like the slides)
    case 8: my = -1.0f; b |= NET_BTN_SPRINT; if (at == 12) b |= NET_BTN_JUMP; if (at == 30 || at == 42) b |= NET_BTN_SLIDE; break;
    default: my = -1.0f; break;
    }
    return NetCmd_Make(mx, my, 0.7f * (float)k + 0.02f * (float)t, 0.1f * sinf((float)t * 0.05f), b);
}

static NetCmdFn g_script = Script;                 // what the clients' fingers do (a test may swap it)
static uint32_t g_oneShotTick;
static NetCmd OneShot(void *user, uint32_t t)      // walk to the right; at one tick press sprint and dive together, then go quiet for good
{
    (void)user;
    if (t < g_oneShotTick) return NetCmd_Make(1.0f, 0.0f, 0.0f, 0.0f, 0);
    g_quiet[0] = true;
    return NetCmd_Make(1.0f, 0.0f, 0.0f, 0.0f, NET_BTN_SPRINT | NET_BTN_DIVE);
}

static void World(int n, NetLinkCfg cfg, uint32_t seed)
{
    g_nClients = n; g_now = 1000.0;
    NetServer_Init(&g_srv, 12345u, 1);
    memset(g_srvHist, 0, sizeof g_srvHist);
    g_nextSrvTick = g_now + TICK_MS;
    g_airTicks = g_sprintTicks = g_slideTicks = g_diveTicks = 0; g_topSpeed = g_topHeight = 0.0f;
    for (int k = 0; k < n; ++k) {
        NetLoopback_Init(&g_lb[k], seed + (uint32_t)k * 77u);
        NetLoopback_SetBoth(&g_lb[k], cfg);
        NetLoopback_Advance(&g_lb[k], g_now);
        g_slot[k] = NetServer_AddClient(&g_srv, NetLoopback_End(&g_lb[k], 1), g_now);
        NetClient_Init(&g_cl[k], NetLoopback_End(&g_lb[k], 0), k == 0 ? "steve" : "brou", g_now);
        g_quiet[k] = false;
    }
}

static void Step1ms(void)
{
    g_now += 1.0;
    for (int k = 0; k < g_nClients; ++k) NetLoopback_Advance(&g_lb[k], g_now);
    NetServer_Poll(&g_srv, g_now);
    for (int k = 0; k < g_nClients; ++k) NetClient_Poll(&g_cl[k], g_now);
    if (g_now >= g_nextSrvTick) {
        g_nextSrvTick += TICK_MS;
        NetServer_Tick(&g_srv, g_now);
        for (int k = 0; k < g_nClients; ++k) if (g_srv.c[g_slot[k]].used) g_srvHist[k][g_srv.tick % 256] = g_srv.c[g_slot[k]].player;
        if (g_nClients > 0 && g_srv.c[g_slot[0]].used && g_srv.c[g_slot[0]].started) {
            const PlayerController *q = &g_srv.c[g_slot[0]].player;
            if (!q->isGrounded) ++g_airTicks;
            if (q->sprinting) ++g_sprintTicks;
            if (q->sliding) ++g_slideTicks;
            if (q->diving || q->rolling) ++g_diveTicks;
            const float sp = hypotf(q->moveVel.x, q->moveVel.z);
            if (sp > g_topSpeed) g_topSpeed = sp;
            if (q->position.y > g_topHeight) g_topHeight = q->position.y;
        }
    }
    for (int k = 0; k < g_nClients; ++k) if (!g_quiet[k]) NetClient_Update(&g_cl[k], g_now, g_script, (void *)(intptr_t)k);
}
static void Run(double ms) { const double end = g_now + ms; while (g_now < end) Step1ms(); }

// How far the client's own record of tick T is from the server's at tick T (metres); -1 when either is missing.
static float PredictionGap(int k, uint32_t T)
{
    const int i = (int)(T % NET_HISTORY);
    if (!g_cl[k].hist[i].valid || g_cl[k].hist[i].tick != T || T > g_srv.tick || g_srv.tick - T >= 250) return -1.0f;
    const PlayerController *s = &g_srvHist[k][T % 256], *c = &g_cl[k].hist[i].state;
    return hypotf(hypotf(s->position.x - c->position.x, s->position.y - c->position.y), s->position.z - c->position.z);
}

static NetLinkCfg Link(float latency, float jitter, float loss, float dup) { NetLinkCfg c = { latency, jitter, loss, dup }; return c; }
static uint32_t Rng(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

int main(void)
{
    uint8_t buf[NET_MTU];

    printf("the wire\n");
    {
        NetW w; NetW_Init(&w, buf, 32);
        NetW_U8(&w, 0xAB); NetW_U16(&w, 0x1234); NetW_U32(&w, 0xDEADBEEFu); NetW_I8(&w, -5); NetW_I16(&w, -300); NetW_I32(&w, -70000); NetW_F32(&w, 1.5f);
        Check(!w.err && w.len == 1 + 2 + 4 + 1 + 2 + 4 + 4, "numbers go in, 18 bytes");
        Check(buf[1] == 0x34 && buf[2] == 0x12 && buf[3] == 0xEF && buf[6] == 0xDE, "little-endian, whatever the machine");
        NetR r; NetR_Init(&r, buf, w.len);
        const int ok = NetR_U8(&r) == 0xAB && NetR_U16(&r) == 0x1234 && NetR_U32(&r) == 0xDEADBEEFu && NetR_I8(&r) == -5 && NetR_I16(&r) == -300 && NetR_I32(&r) == -70000 && NetR_F32(&r) == 1.5f;
        Check(ok && NetR_Done(&r), "and come out the same, with nothing left over");
        NetR_Init(&r, buf, 3); NetR_U32(&r);
        Check(r.err && NetR_U8(&r) == 0 && !NetR_Done(&r), "a read past the end is an error, reads zeros and stays an error");
        NetR_Init(&r, buf, 4); NetR_U8(&r);
        Check(!NetR_Done(&r), "bytes left over are not a finished packet");
        uint8_t small[3]; NetW_Init(&w, small, 3); NetW_U16(&w, 1); NetW_U16(&w, 2);
        Check(w.err && w.len == 2, "a write that does not fit is an error and writes nothing of it");
        small[2] = 0x7E; NetW_Init(&w, small, 2); NetW_U32(&w, 5);
        Check(w.err && small[2] == 0x7E, "and never writes outside the buffer");
    }

    printf("\nthe protocol\n");
    {
        NetCmd c = NetCmd_Make(0.0f, -1.0f, 1.0f, 0.2f, NET_BTN_SPRINT);
        float x, y; NetCmd_Move(&c, &x, &y);
        Check(x == 0.0f && y == -1.0f && fabsf(NetCmd_Yaw(&c) - 1.0f) < 1e-3f && fabsf(NetCmd_Pitch(&c) - 0.2f) < 1e-3f, "a command keeps what it was given (to the wire's precision)");
        c.moveX = 127; c.moveY = 127; NetCmd_Move(&c, &x, &y);
        Check(hypotf(x, y) <= 1.0001f, "a forged (127, 127) is held to length 1: no faster walk");
        c.moveX = -128; c.moveY = -128; NetCmd_Move(&c, &x, &y);
        Check(hypotf(x, y) <= 1.0001f && x >= -1.0f, "so is (-128, -128)");
        c = NetCmd_Make(NAN, INFINITY, NAN, NAN, 0); NetCmd_Move(&c, &x, &y);
        Check(x == 0.0f && y <= 1.0f && NetCmd_Yaw(&c) == 0.0f, "NaN in a command becomes nothing");
        c = NetCmd_Make(0, 0, -1.0f, 9.0f, 0);
        Check(NetCmd_Yaw(&c) > 5.0f && NetCmd_Yaw(&c) < 6.3f && NetCmd_Pitch(&c) < 1.56f, "yaw wraps into a turn; pitch stops a little short of straight up");
        c = NetCmd_Make(0, 0, 100.0f, 0, 0);
        Check(fabsf(NetCmd_Yaw(&c) - fmodf(100.0f, 6.2831853f)) < 1e-3f, "a huge yaw wraps the same way");

        NetMsg msgs[6]; memset(msgs, 0, sizeof msgs);
        msgs[0].kind = NET_MSG_HELLO; msgs[0].u.hello.version = 1; strcpy(msgs[0].u.hello.character, "totour_khan2");
        msgs[1].kind = NET_MSG_WELCOME; msgs[1].u.welcome = (NetWelcome){ 1, 30, 2, 4, 0xCAFEF00Du, 7, 123456u, 73.5f, 13.5f };
        msgs[2].kind = NET_MSG_DISCONNECT; msgs[2].u.disc.reason = NET_DISC_FULL;
        msgs[3].kind = NET_MSG_INPUT; msgs[3].u.input.tick = 1000; msgs[3].u.input.ackServerTick = 990; msgs[3].u.input.count = 4;
        for (int i = 0; i < 4; ++i) msgs[3].u.input.cmds[i] = NetCmd_Make(0.5f, -0.5f, (float)i, 0.1f, (uint16_t)(i * 211));
        msgs[4].kind = NET_MSG_SNAPSHOT; msgs[4].u.snap.serverTick = 555; msgs[4].u.snap.started = 1; msgs[4].u.snap.margin = -3;
        msgs[4].u.snap.owner = (NetOwnerState){ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, NET_OWN_GROUNDED | NET_OWN_SLIDING | NET_OWN_DIVING | NET_OWN_LYING | NET_OWN_JUMPED_SPRINTING };
        msgs[4].u.snap.nRemote = 3;
        for (int i = 0; i < 3; ++i) msgs[4].u.snap.remote[i] = (NetRemote){ (uint8_t)(i + 1), 1000 * i, -2000, 30000 + i, (uint16_t)(1000 * i), (int16_t)(-100 * i), NET_REM_SPRINTING };
        msgs[5].kind = NET_MSG_SNAPSHOT; msgs[5].u.snap.owner.px = 1.0f;
        int roundtrip = 1;
        size_t lens[6];
        for (int i = 0; i < 6; ++i) {
            lens[i] = NetProto_Encode(&msgs[i], buf, sizeof buf);
            NetMsg back;
            if (lens[i] == 0 || !NetProto_Decode(buf, lens[i], &back) || back.kind != msgs[i].kind) { roundtrip = 0; continue; }
            uint8_t again[NET_MTU];
            if (NetProto_Encode(&back, again, sizeof again) != lens[i] || memcmp(again, buf, lens[i]) != 0) roundtrip = 0;
        }
        Check(roundtrip, "every kind of message encodes, decodes, and encodes to the very same bytes");
        printf("    (sizes: Hello %zu, Welcome %zu, Disconnect %zu, Input %zu, Snapshot with 3 others %zu, with none %zu bytes)\n", lens[0], lens[1], lens[2], lens[3], lens[4], lens[5]);
        Check(lens[3] < 64 && lens[4] < 200 && lens[4] < NET_MTU, "an Input is small, and a full Snapshot is far under the datagram limit");

        int cut = 1;                                                       // every shorter prefix of every message is refused
        for (int i = 0; i < 6; ++i) {
            NetProto_Encode(&msgs[i], buf, sizeof buf);
            for (size_t n = 0; n < lens[i]; ++n) { NetMsg o; if (NetProto_Decode(buf, n, &o)) cut = 0; }
        }
        Check(cut, "a packet cut short at any length is refused");
        int extra = 1;
        for (int i = 0; i < 6; ++i) { NetProto_Encode(&msgs[i], buf, sizeof buf); buf[lens[i]] = 0; NetMsg o; if (NetProto_Decode(buf, lens[i] + 1, &o)) extra = 0; }
        Check(extra, "so is one with a byte too many");
        NetProto_Encode(&msgs[3], buf, sizeof buf);
        NetMsg o;
        uint8_t keep = buf[0]; buf[0] = 'X';
        Check(!NetProto_Decode(buf, lens[3], &o), "wrong magic is refused"); buf[0] = keep;
        keep = buf[2]; buf[2] = 99;
        Check(!NetProto_Decode(buf, lens[3], &o), "an unknown kind is refused"); buf[2] = keep;
        keep = buf[11]; buf[11] = 0;
        Check(!NetProto_Decode(buf, lens[3], &o), "an Input with no commands is refused"); buf[11] = 9;
        Check(!NetProto_Decode(buf, lens[3], &o), "and one with more than the limit"); buf[11] = keep;
        NetProto_Encode(&msgs[4], buf, sizeof buf);
        uint8_t save[NET_MTU]; memcpy(save, buf, lens[4]);
        const float nan = NAN; memcpy(buf + 3 + 4 + 1 + 1, &nan, 4);
        Check(!NetProto_Decode(buf, lens[4], &o), "a Snapshot with a NaN in the owner's state is refused"); memcpy(buf, save, lens[4]);
        buf[3 + 4 + 1 + 1 + 80 + 1 + 1 + 1 + 12 + 4] = 0xF0;                           // the first other player's flags byte (the owner's eight flag bits all mean something now)
        Check(!NetProto_Decode(buf, lens[4], &o), "unknown flag bits on another player are refused"); memcpy(buf, save, lens[4]);
        buf[3 + 4 + 1 + 1 + 80 + 1] = 9;
        Check(!NetProto_Decode(buf, lens[4], &o), "more remote players than there are slots is refused"); memcpy(buf, save, lens[4]);
        NetMsg bad = msgs[0]; strcpy(bad.u.hello.character, "Steve!");
        Check(NetProto_Encode(&bad, buf, sizeof buf) == 0, "a character id with capitals or marks is not even sent");
        NetProto_Encode(&msgs[0], buf, sizeof buf); buf[5] = 'Q';
        Check(!NetProto_Decode(buf, lens[0], &o), "nor accepted when it arrives so");
        NetMsg big = msgs[4]; big.u.snap.nRemote = 9;
        Check(NetProto_Encode(&big, buf, sizeof buf) == 0, "a message that is not valid is not sent");
        Check(NetProto_Encode(&msgs[4], buf, 20) == 0, "nor one that does not fit");

        // Strangers' bytes: random packets, and good packets with a byte spoilt. Nothing may crash, and what is accepted must be what it says.
        uint32_t rs = 987654321u;
        long accepted = 0; int stable = 1;
        for (long it = 0; it < 300000; ++it) {
            uint8_t p[256]; size_t n;
            if (it % 2 == 0) { n = Rng(&rs) % 96; for (size_t i = 0; i < n; ++i) p[i] = (uint8_t)Rng(&rs); if (n >= 3 && it % 4 == 0) { p[0] = 'T'; p[1] = 'B'; p[2] = (uint8_t)(Rng(&rs) % 3 + 1); } }
            else { const int which = (int)(Rng(&rs) % 6); n = NetProto_Encode(&msgs[which], p, sizeof p); const int flips = 1 + (int)(Rng(&rs) % 3); for (int f = 0; f < flips && n; ++f) p[Rng(&rs) % n] = (uint8_t)Rng(&rs); if (Rng(&rs) % 8 == 0 && n) n -= 1 + Rng(&rs) % n; }
            NetMsg d;
            if (NetProto_Decode(p, n, &d)) {
                ++accepted;
                uint8_t again[256];
                const size_t m2 = NetProto_Encode(&d, again, sizeof again);
                if (m2 != n || memcmp(again, p, n) != 0) stable = 0;
            }
        }
        Check(stable, "300000 random and spoilt packets: none crashed it, and every one it accepted re-encodes to the same bytes");
        printf("    (%ld of them were accepted as valid messages)\n", accepted);
    }

    printf("\nthe pretend network\n");
    {
        static NetLoopback L;
        NetLoopback_Init(&L, 5u);
        NetLoopback_SetBoth(&L, Link(50.0f, 20.0f, 0.0f, 0.0f));
        NetTransport cli = NetLoopback_End(&L, 0), srv = NetLoopback_End(&L, 1);
        uint8_t pk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, rx[NET_MTU];
        cli.sendDatagram(cli.self, pk, 8);
        NetLoopback_Advance(&L, 49.0);
        Check(srv.recvDatagram(srv.self, rx, sizeof rx) == 0, "nothing arrives before the latency has passed");
        NetLoopback_Advance(&L, 71.0);
        Check(srv.recvDatagram(srv.self, rx, sizeof rx) == 8 && memcmp(rx, pk, 8) == 0 && srv.recvDatagram(srv.self, rx, sizeof rx) == 0, "it arrives whole, once, within latency + jitter");
        Check(!cli.sendDatagram(cli.self, rx, NET_MTU + 1) && !cli.sendDatagram(cli.self, rx, 0) && L.oversize[0] == 2, "a datagram over the limit, or empty, is refused");
        Check(!cli.sendReliable(cli.self, rx, NET_MTU + 1), "so is a reliable message");

        NetLoopback_Init(&L, 9u); NetLoopback_SetBoth(&L, Link(10.0f, 0.0f, 0.2f, 0.0f));
        cli = NetLoopback_End(&L, 0); srv = NetLoopback_End(&L, 1);
        int got = 0;
        for (int i = 0; i < 10000; ++i) { cli.sendDatagram(cli.self, pk, 8); NetLoopback_Advance(&L, L.now + 1.0); while (srv.recvDatagram(srv.self, rx, sizeof rx) > 0) ++got; }
        NetLoopback_Advance(&L, L.now + 50.0); while (srv.recvDatagram(srv.self, rx, sizeof rx) > 0) ++got;
        printf("    (20%% loss: %d of 10000 arrived)\n", got);
        Check(got > 7700 && got < 8300, "a loss of 20% loses about a fifth of the datagrams");

        NetLoopback_Init(&L, 11u); NetLoopback_SetBoth(&L, Link(10.0f, 0.0f, 0.0f, 0.3f));
        cli = NetLoopback_End(&L, 0); srv = NetLoopback_End(&L, 1);
        got = 0;
        for (int i = 0; i < 5000; ++i) { cli.sendDatagram(cli.self, pk, 8); NetLoopback_Advance(&L, L.now + 1.0); while (srv.recvDatagram(srv.self, rx, sizeof rx) > 0) ++got; }
        NetLoopback_Advance(&L, L.now + 50.0); while (srv.recvDatagram(srv.self, rx, sizeof rx) > 0) ++got;
        Check(got > 5000 * 1.25 && got < 5000 * 1.35, "30% doubling gives about a third more");

        NetLoopback_Init(&L, 13u); NetLoopback_SetBoth(&L, Link(20.0f, 60.0f, 0.0f, 0.0f));
        cli = NetLoopback_End(&L, 0); srv = NetLoopback_End(&L, 1);
        for (uint8_t i = 0; i < 100; ++i) { pk[0] = i; cli.sendDatagram(cli.self, pk, 8); NetLoopback_Advance(&L, L.now + 2.0); }
        NetLoopback_Advance(&L, L.now + 200.0);
        int outOfOrder = 0, last = -1, count = 0;
        while (srv.recvDatagram(srv.self, rx, sizeof rx) > 0) { if ((int)rx[0] < last) ++outOfOrder; last = rx[0]; ++count; }
        Check(count == 100 && outOfOrder > 5, "jitter reorders datagrams, as a real network does");

        NetLoopback_Init(&L, 15u); NetLoopback_SetBoth(&L, Link(30.0f, 80.0f, 0.5f, 0.2f));
        cli = NetLoopback_End(&L, 0); srv = NetLoopback_End(&L, 1);
        for (uint8_t i = 0; i < 100; ++i) { pk[0] = i; cli.sendReliable(cli.self, pk, 8); NetLoopback_Advance(&L, L.now + 3.0); }
        NetLoopback_Advance(&L, L.now + 1000.0);
        int inOrder = 1; last = -1; count = 0;
        while (srv.recvReliable(srv.self, rx, sizeof rx) > 0) { if ((int)rx[0] != last + 1) inOrder = 0; last = rx[0]; ++count; }
        Check(count == 100 && inOrder, "the reliable channel loses none and keeps the order, whatever the loss and jitter");

        NetLoopback_Init(&L, 17u); NetLoopback_SetBoth(&L, Link(10.0f, 0.0f, 0.0f, 0.0f)); NetLoopback_Blackout(&L, 100.0, 300.0);
        cli = NetLoopback_End(&L, 0); srv = NetLoopback_End(&L, 1);
        NetLoopback_Advance(&L, 150.0); cli.sendDatagram(cli.self, pk, 8); cli.sendReliable(cli.self, pk, 8);
        NetLoopback_Advance(&L, 250.0);
        Check(srv.recvDatagram(srv.self, rx, sizeof rx) == 0 && srv.recvReliable(srv.self, rx, sizeof rx) == 0, "in a blackout the datagram is lost and the reliable message waits");
        NetLoopback_Advance(&L, 320.0);
        Check(srv.recvReliable(srv.self, rx, sizeof rx) == 8 && srv.recvDatagram(srv.self, rx, sizeof rx) == 0, "the message arrives when it ends; the datagram never does");

        static NetLoopback A, B;
        NetLoopback_Init(&A, 21u); NetLoopback_Init(&B, 21u); NetLoopback_SetBoth(&A, Link(20.0f, 30.0f, 0.1f, 0.1f)); NetLoopback_SetBoth(&B, Link(20.0f, 30.0f, 0.1f, 0.1f));
        NetTransport ca = NetLoopback_End(&A, 0), sa = NetLoopback_End(&A, 1), cb = NetLoopback_End(&B, 0), sb = NetLoopback_End(&B, 1);
        int same = 1;
        for (int i = 0; i < 500; ++i) {
            pk[0] = (uint8_t)i; ca.sendDatagram(ca.self, pk, 8); cb.sendDatagram(cb.self, pk, 8);
            NetLoopback_Advance(&A, A.now + 2.0); NetLoopback_Advance(&B, B.now + 2.0);
            uint8_t r1[8], r2[8]; int n1 = sa.recvDatagram(sa.self, r1, 8), n2 = sb.recvDatagram(sb.self, r2, 8);
            if (n1 != n2 || (n1 > 0 && memcmp(r1, r2, 8))) same = 0;
        }
        Check(same, "the same seed gives the same network, every time");
    }

    printf("\nthe handshake\n");
    {
        World(2, Link(25.0f, 5.0f, 0.0f, 0.0f), 3u);
        Run(400.0);
        Check(g_cl[0].state == NETC_PLAYING && g_cl[1].state == NETC_PLAYING && NetServer_PlayerCount(&g_srv) == 2, "two clients say Hello, are welcomed, and are playing");
        Check(g_cl[0].id != g_cl[1].id && g_cl[0].id == g_slot[0] && g_cl[1].id == g_slot[1], "each is given its own player id");
        int sameFloor = g_cl[0].floorSeed == g_srv.floorSeed && g_cl[0].floorNumber == g_srv.floorNumber;
        for (int z = 0; z < MAP_HEIGHT && sameFloor; ++z) for (int x = 0; x < MAP_WIDTH; ++x) if (g_cl[0].map.grid[z][x].type != g_srv.map.grid[z][x].type) sameFloor = 0;
        Check(sameFloor && g_cl[0].map.startX == g_srv.map.startX, "the client makes the same floor from the seed as the server has");
        Check(strcmp(g_srv.c[g_slot[0]].character, "steve") == 0 && strcmp(g_srv.c[g_slot[1]].character, "brou") == 0, "the server knows which character each chose");
        Check(g_cl[0].rttMs > 45.0 && g_cl[0].rttMs < 70.0, "the client measured the round trip");
        Check(!Tile_IsSolid(&g_srv.map, (int)floorf((g_srv.map.startX + 1.0f) / 2.0f), (int)floorf((g_srv.map.startZ + 1.0f) / 2.0f)), "the players are put down on open floor");

        // A client of another version.
        static NetLoopback L; NetLoopback_Init(&L, 2u); NetLoopback_SetBoth(&L, Link(5.0f, 0.0f, 0.0f, 0.0f));
        NetServer_Init(&g_srv, 12345u, 1);
        const int slot = NetServer_AddClient(&g_srv, NetLoopback_End(&L, 1), 0.0);
        NetTransport cli = NetLoopback_End(&L, 0);
        NetMsg h; memset(&h, 0, sizeof h); h.kind = NET_MSG_HELLO; h.u.hello.version = 99;
        size_t n = NetProto_Encode(&h, buf, sizeof buf);
        cli.sendReliable(cli.self, buf, n);
        NetLoopback_Advance(&L, 10.0); NetServer_Poll(&g_srv, 10.0); NetLoopback_Advance(&L, 20.0);
        uint8_t rx[NET_MTU]; NetMsg m;
        const int rn = cli.recvReliable(cli.self, rx, sizeof rx);
        Check(rn > 0 && NetProto_Decode(rx, (size_t)rn, &m) && m.kind == NET_MSG_DISCONNECT && m.u.disc.reason == NET_DISC_VERSION && !g_srv.c[slot].used, "a client of another version is told so and dropped");

        // A server of another version.
        NetClient_Init(&g_cl[0], NetLoopback_End(&L, 0), "steve", 30.0);
        NetMsg w; memset(&w, 0, sizeof w); w.kind = NET_MSG_WELCOME; w.u.welcome = (NetWelcome){ 7, 30, 0, 4, 1, 1, 0, 1.0f, 1.0f };
        n = NetProto_Encode(&w, buf, sizeof buf);
        NetTransport srv = NetLoopback_End(&L, 1);
        srv.sendReliable(srv.self, buf, n);
        NetLoopback_Advance(&L, 60.0); NetClient_Poll(&g_cl[0], 60.0);
        Check(g_cl[0].state == NETC_CLOSED && g_cl[0].closedReason == NET_DISC_VERSION, "a client given a Welcome of another version closes");

        // A full server.
        World(4, Link(5.0f, 0.0f, 0.0f, 0.0f), 4u);
        static NetLoopback extra; NetLoopback_Init(&extra, 6u); NetLoopback_SetBoth(&extra, Link(5.0f, 0.0f, 0.0f, 0.0f));
        const int fifth = NetServer_AddClient(&g_srv, NetLoopback_End(&extra, 1), g_now);
        NetClient_Init(&g_cl[0], NetLoopback_End(&extra, 0), "steve", g_now);
        NetLoopback_Advance(&extra, g_now + 20.0); NetClient_Poll(&g_cl[0], g_now + 20.0);
        Check(fifth == -1 && g_cl[0].state == NETC_CLOSED && g_cl[0].closedReason == NET_DISC_FULL, "a fifth client on a four-player server is told it is full");
    }

    printf("\nthe dive in the movement state\n");
    {
        static FloorMap fm;
        GenerateFloorMap(&fm, 1, 4242u);
        PlayerController a; NetSim_Spawn(&a, fm.startX, fm.startZ);
        NetCmd go = NetCmd_Make(1.0f, 0.0f, 0.0f, 0.0f, NET_BTN_SPRINT), dive = NetCmd_Make(1.0f, 0.0f, 0.0f, 0.0f, NET_BTN_SPRINT | NET_BTN_DIVE);
        for (int i = 0; i < 40; ++i) NetSim_Step(&a, &go, &fm);
        Check(PlayerDiveAvailable(&a), "(a sprint to the right: the dive is there)");
        NetSim_Step(&a, &dive, &fm);
        Check(a.diving && !a.isGrounded, "the dive button starts a dive from the ground, on the command alone");
        for (int i = 0; i < 6; ++i) NetSim_Step(&a, &go, &fm);
        NetOwnerState o; NetSim_Capture(&a, &o);
        Check((o.flags & NET_OWN_DIVING) && o.diveT > 0.1f && fabsf(o.diveDirX) > 0.5f, "mid-dive the owner's state says so: diving, how long, and which way");
        PlayerController b; NetSim_Spawn(&b, 0.0f, 0.0f);
        NetSim_Apply(&b, &o);
        float worst = 0.0f;
        for (int i = 0; i < 80; ++i) {
            NetSim_Step(&a, &go, &fm); NetSim_Step(&b, &go, &fm);
            const float d = hypotf(a.position.x - b.position.x, a.position.z - b.position.z) + fabsf(a.position.y - b.position.y);
            if (d > worst) worst = d;
        }
        Check(worst < 1e-5f && a.lying == b.lying && a.rolling == b.rolling, "a player put back from that state goes on exactly like the original: through the landing, the roll and the lying down");
        Check(NetSim_Error(&a, &o) > 5.0f, "(and a state that differs in the dive is a large error: it is a different second from now)");
        PlayerController c; NetSim_Spawn(&c, fm.startX, fm.startZ);
        NetOwnerState oc; NetSim_Capture(&c, &oc);
        PlayerController d2 = c; d2.lying = true; d2.lieStick = (Vector2){ 0.5f, 0.5f };
        NetOwnerState od; NetSim_Capture(&d2, &od);
        PlayerController e; NetSim_Spawn(&e, fm.startX, fm.startZ);
        NetSim_Apply(&e, &od);
        Check(e.lying && e.lieStick.x == 0.5f && e.lieStick.y == 0.5f && NetSim_Error(&c, &od) > 5.0f && NetSim_Error(&e, &od) < 1e-4f, "lying down, and the stick it must be moved from, travel too");
        PlayerController r3; NetSim_Spawn(&r3, fm.startX, fm.startZ);         // put back in the middle of the ROLL: how long it has rolled matters too
        for (int i = 0; i < 40; ++i) NetSim_Step(&r3, &go, &fm);
        NetSim_Step(&r3, &dive, &fm);
        for (int i = 0; i < 200 && !(r3.rolling && r3.diveT > 0.15f); ++i) NetSim_Step(&r3, &go, &fm);
        NetOwnerState orl; NetSim_Capture(&r3, &orl);
        PlayerController r4; NetSim_Spawn(&r4, 0.0f, 0.0f);
        NetSim_Apply(&r4, &orl);
        float worstRoll = 0.0f; int stateSame = 1;
        for (int i = 0; i < 60; ++i) {
            NetSim_Step(&r3, &go, &fm); NetSim_Step(&r4, &go, &fm);
            const float d = hypotf(r3.position.x - r4.position.x, r3.position.z - r4.position.z);
            if (d > worstRoll) worstRoll = d;
            if (r3.rolling != r4.rolling || r3.lying != r4.lying) stateSame = 0;
        }
        Check((orl.flags & NET_OWN_ROLLING) && orl.diveT > 0.15f && worstRoll < 1e-5f && stateSame, "put back in the middle of the roll it rolls for as long as the original and gets up (or lies) at the same moment");
        NetCmd s = NetCmd_Make(0.0f, 0.0f, 0.0f, 0.0f, NET_BTN_DIVE | NET_BTN_RELOAD);
        Check(s.buttons == (NET_BTN_DIVE | NET_BTN_RELOAD) && s.buttons > 255, "the buttons are 16 bits: dive and reload are past the first eight");
    }

    printf("\na press that is replayed for a command that did not arrive\n");
    {
        World(1, Link(0.0f, 0.0f, 0.0f, 0.0f), 21u);
        g_script = OneShot;
        g_oneShotTick = 0xFFFFFFFFu;
        Run(1500.0);                                                         // joined and started
        g_oneShotTick = g_cl[0].tick + 45;                                   // walks right for a second and a half (no sprint), then sprint and dive at once, then silence
        NetServerClient *sc = &g_srv.c[g_slot[0]];
        bool everDived = false, couldHave = false;
        for (int i = 0; i < 2500; ++i) {
            Step1ms();
            if (g_quiet[0] && PlayerDiveAvailable(&sc->player)) couldHave = true;
            if (g_quiet[0] && (sc->player.diving || sc->player.rolling || sc->player.lying)) everDived = true;
        }
        Check(g_quiet[0] && sc->ticksRepeated >= 3, "(the client went quiet right after that command: the server replayed it)");
        Check(couldHave, "(and a moment later the player WAS sprinting sideways, where a dive is possible: a stale press would have worked)");
        Check(!everDived, "the dive was a press: replayed for a command that did not arrive it is not pressed again, so no dive starts on it");
        g_script = Script;
    }

    printf("\nprediction with no delay at all\n");
    {
        World(1, Link(0.0f, 0.0f, 0.0f, 0.0f), 7u);
        Run(1500.0);
        const unsigned warm = g_cl[0].corrections;
        Run(14000.0);
        printf("    (%u ticks run; %u corrections at the start, %u after; the client leads the server by %d ticks)\n", g_cl[0].ticksRun, warm, g_cl[0].corrections - warm, (int)g_cl[0].tick - (int)g_srv.tick);
        Check(g_cl[0].corrections == warm, "once running, the server's answer never differs from the client's guess: nothing to correct");
        float worst = 0.0f; int compared = 0;
        for (uint32_t T = g_srv.tick - 60; T < g_srv.tick; ++T) { const float g = PredictionGap(0, T); if (g >= 0.0f) { ++compared; if (g > worst) worst = g; } }
        Check(compared > 40 && worst < 1e-4f, "the client's record of each tick matches the server's to a ten-thousandth of a metre");
        const PlayerController *p = &g_srv.c[g_slot[0]].player;
        Check(hypotf(p->position.x - g_srv.map.startX, p->position.z - g_srv.map.startZ) > 5.0f, "and the player really went somewhere (a test of standing still proves nothing)");
        Check(g_srv.c[g_slot[0]].ticksRepeated == 0 && g_srv.c[g_slot[0]].cmdsLate == 0, "no command was missing or late");
        printf("    (on the server the player spent %u ticks in the air, %u sprinting, %u sliding; top speed %.2f m/s, highest point %.2f m)\n", g_airTicks, g_sprintTicks, g_slideTicks, (double)g_topSpeed, (double)g_topHeight);
        Check(g_airTicks > 10 && g_topHeight > 0.5f, "the script's jumps really happen on the server");
        Check(g_sprintTicks > 30 && g_topSpeed > PLAYER_WALK_SPEED + 1.0f, "and its sprints (faster than a walk)");
        Check(g_slideTicks > 5, "and its slides");
        printf("    (and %u ticks diving or rolling)\n", g_diveTicks);
        Check(g_diveTicks > 10, "and its dives from the ground (the dive is on the wire now, and its state with it)");
    }

    printf("\nthe view between ticks, through a correction\n");
    {
        World(1, Link(0.0f, 0.0f, 0.0f, 0.0f), 11u);
        Run(1500.0);
        const unsigned c0 = g_cl[0].corrections;
        g_srv.c[g_slot[0]].player.position.x += 0.6f;                            // the server moves the player: its answers now differ from the client's guess
        g_srv.c[g_slot[0]].player.position.z -= 0.4f;
        float worst = 0.0f; Vector3 last = NetClient_ViewPosAt(&g_cl[0], NetClient_Alpha(&g_cl[0]));
        for (int i = 0; i < 600; ++i) {
            Step1ms();
            if (i % 16 == 0) {
                const Vector3 v = NetClient_ViewPosAt(&g_cl[0], NetClient_Alpha(&g_cl[0]));
                const float st = hypotf(hypotf(v.x - last.x, v.y - last.y), v.z - last.z);
                if (st > worst) worst = st;
                last = v;
            }
        }
        Check(g_cl[0].corrections > c0, "(the client took the server's word)");
        printf("    (biggest step of the view in a frame through a 0.72 m correction: %.3f m)\n", (double)worst);
        Check(worst < 0.30f, "and the view did not jump: the 0.72 m is spread over the next ticks, not seen as one step");
        const Vector3 end = NetClient_ViewPosAt(&g_cl[0], 1.0f), now = NetClient_ViewPos(&g_cl[0]);
        Check(fabsf(end.x - now.x) < 1e-5f && fabsf(end.z - now.z) < 1e-5f, "at alpha 1 the blended view is the newest tick's, with the fading jump");
        Check(fabsf(NetClient_ViewPosAt(&g_cl[0], 0.0f).x - (g_cl[0].prevPos.x + g_cl[0].errOffset.x)) < 1e-6f && NetClient_Alpha(&g_cl[0]) >= 0.0f && NetClient_Alpha(&g_cl[0]) <= 1.0f, "at alpha 0 it is the tick before, and alpha stays between 0 and 1");
    }

    printf("\nprediction over a bad network\n");
    {
        World(1, Link(50.0f, 20.0f, 0.05f, 0.02f), 8u);                 // 100 ms round trip, 40 ms swing, 5% loss
        Run(2000.0);
        const unsigned corr0 = g_cl[0].corrections, rep0 = g_srv.c[g_slot[0]].ticksRepeated, fed0 = g_srv.c[g_slot[0]].ticksFed, late0 = g_srv.c[g_slot[0]].cmdsLate;
        float maxView = 0.0f; double sumGap = 0.0; int gaps = 0;
        float maxFrameStep = 0.0f; Vector3 lastView = NetClient_ViewPosAt(&g_cl[0], NetClient_Alpha(&g_cl[0])); int nonMonotone = 0;       // the view drawn every 16 ms, between the ticks
        for (int i = 0; i < 20000; ++i) {
            Step1ms();
            if (i % 16 == 0) {
                const Vector3 v = NetClient_ViewPosAt(&g_cl[0], NetClient_Alpha(&g_cl[0]));
                const float st = hypotf(hypotf(v.x - lastView.x, v.y - lastView.y), v.z - lastView.z);
                if (st > maxFrameStep) maxFrameStep = st;
                if (!isfinite(v.x) || !isfinite(v.z)) nonMonotone = 1;
                lastView = v;
            }
            if (i % 33 == 0) {
                const uint32_t T = g_srv.tick - 6;
                const float g = PredictionGap(0, T);
                if (g >= 0.0f) { sumGap += g; ++gaps; }
                const Vector3 v = NetClient_ViewPos(&g_cl[0]);
                const float vd = hypotf(v.x - g_cl[0].player.position.x, v.z - g_cl[0].player.position.z);
                if (vd > maxView) maxView = vd;
            }
        }
        const NetServerClient *sc = &g_srv.c[g_slot[0]];
        const unsigned ticks = sc->ticksFed - fed0 + sc->ticksRepeated - rep0;
        const double starvedShare = (double)(sc->ticksRepeated - rep0) / (double)ticks;
        printf("    (%u server ticks: %.1f%% had no command in time; %u corrections; %u hard snaps; late commands %u; lead now %d ticks; mean gap %.4f m; biggest visible offset %.3f m)\n",
               ticks, starvedShare * 100.0, g_cl[0].corrections - corr0, g_cl[0].hardSnaps, sc->cmdsLate - late0, (int)g_cl[0].tick - (int)g_srv.tick, gaps ? sumGap / gaps : -1.0, (double)maxView);
        Check(g_cl[0].state == NETC_PLAYING && g_cl[0].hardSnaps == 0, "it stays connected, and never has to be put back from scratch");
        printf("    (the view drawn every 16 ms never stepped more than %.3f m; the player's top speed is 9.5 m/s, 0.152 m per frame)\n", (double)maxFrameStep);
        Check(maxFrameStep < 0.30f && !nonMonotone, "over a bad network the view between ticks moves smoothly, a step per frame no bigger than the top speed allows");
        Check(starvedShare < 0.02, "with 5% loss, a command is missing at its tick under 2% of the time (the repeats in every packet cover the rest)");
        Check((g_cl[0].corrections - corr0) < ticks / 20, "so the client's guess is wrong, and corrected, in under 5% of the ticks");
        Check(gaps > 100 && sumGap / gaps < 0.15, "on average the client's record of a tick is within 15 cm of the server's");
        Check(g_cl[0].maxError < 3.0f && maxView < 1.0f, "and a correction never shows as a jump larger than a metre");
    }

    printf("\nprediction over a very bad network\n");
    {
        World(1, Link(100.0f, 40.0f, 0.2f, 0.05f), 9u);                 // 200 ms round trip, 80 ms swing, 20% loss, 5% doubled
        Run(3000.0);
        const NetServerClient *sc = &g_srv.c[g_slot[0]];
        const unsigned rep0 = sc->ticksRepeated, fed0 = sc->ticksFed, hs0 = g_cl[0].hardSnaps;
        Run(20000.0);
        const unsigned ticks = sc->ticksFed - fed0 + sc->ticksRepeated - rep0;
        printf("    (%u ticks: %.1f%% without a command in time; hard snaps %u; max error %.2f m; lead %d ticks)\n", ticks, 100.0 * (sc->ticksRepeated - rep0) / ticks, g_cl[0].hardSnaps - hs0, (double)g_cl[0].maxError, (int)g_cl[0].tick - (int)g_srv.tick);
        Check(g_cl[0].state == NETC_PLAYING && g_cl[0].hardSnaps == hs0, "20% loss and a 200 ms round trip: still playing, no restart from scratch");
        Check(100.0 * (sc->ticksRepeated - rep0) / ticks < 8.0, "a command is missing at its tick under 8% of the time");
        float worst = 0.0f; int n = 0;
        for (uint32_t T = g_srv.tick - 40; T < g_srv.tick - 10; ++T) { const float g = PredictionGap(0, T); if (g >= 0.0f) { ++n; if (g > worst) worst = g; } }
        Check(n > 10 && worst < 2.0f, "and the client's picture of a recent tick is never far from the server's");
    }

    printf("\nreconciliation\n");
    {
        World(1, Link(40.0f, 8.0f, 0.0f, 0.0f), 10u);
        Run(4000.0);
        const unsigned before = g_cl[0].corrections;
        g_srv.c[g_slot[0]].player.position.x += 0.8f;                    // the server moves the player, as a knockback or a rule the client did not know would
        g_srv.c[g_slot[0]].player.position.z -= 0.6f;
        Vector3 view0 = NetClient_ViewPos(&g_cl[0]), predicted0 = g_cl[0].player.position;
        float maxStep = 0.0f, maxJumpUnsmoothed = 0.0f; Vector3 prevView = view0, prevPred = predicted0;
        for (int i = 0; i < 700; ++i) {
            Step1ms();
            const Vector3 v = NetClient_ViewPos(&g_cl[0]), p = g_cl[0].player.position;
            if (i % 33 == 32) {
                const float stepView = hypotf(v.x - prevView.x, v.z - prevView.z), stepPred = hypotf(p.x - prevPred.x, p.z - prevPred.z);
                if (stepView > maxStep) maxStep = stepView;
                if (stepPred > maxJumpUnsmoothed) maxJumpUnsmoothed = stepPred;
                prevView = v; prevPred = p;
            }
        }
        printf("    (the predicted player moved at most %.2f m in a tick when corrected; the drawn one at most %.2f m)\n", (double)maxJumpUnsmoothed, (double)maxStep);
        Check(g_cl[0].corrections > before && g_cl[0].hardSnaps == 0, "a difference the server made is noticed and corrected by putting the server's state back and running the commands since");
        float worst = 0.0f; for (uint32_t T = g_srv.tick - 30; T < g_srv.tick - 6; ++T) { const float g = PredictionGap(0, T); if (g > worst) worst = g; }
        Check(worst < 0.01f, "afterwards the client agrees with the server again, to a centimetre");
        Check(maxJumpUnsmoothed > 0.8f, "(the correction really was a jump of most of a metre in the predicted player)");
        Check(maxStep < maxJumpUnsmoothed * 0.7f && maxStep < 0.6f, "but the drawn player moved much less in a tick than that: the jump is shown as a glide");
        Run(700.0);
        const Vector3 v = NetClient_ViewPos(&g_cl[0]);
        Check(hypotf(v.x - g_cl[0].player.position.x, v.z - g_cl[0].player.position.z) < 0.02f, "and after a moment the glide has finished");

        // A shove too big to glide (a teleport, a respawn) is shown as it is: the player is simply there.
        const unsigned c1 = g_cl[0].corrections;
        g_srv.c[g_slot[0]].player.position.x -= 6.0f;
        float gapAfter = 99.0f;
        for (int i = 0; i < 400; ++i) { Step1ms(); if (g_cl[0].corrections > c1 && gapAfter > 50.0f) { const Vector3 w = NetClient_ViewPos(&g_cl[0]); gapAfter = hypotf(w.x - g_cl[0].player.position.x, w.z - g_cl[0].player.position.z); } }
        Check(g_cl[0].corrections > c1 && gapAfter < 0.05f, "a shove of six metres is not glided over: the player is drawn where the server put them");

        // A difference that is not in the position yet: the server has the player more tired than the client thinks. It would be a different sprint a second from now.
        Run(500.0);
        const unsigned c2 = g_cl[0].corrections;
        g_srv.c[g_slot[0]].player.stamina -= 5.0f;                     // a little: no change to the player's speed to give it away
        Run(500.0);
        Check(g_cl[0].corrections > c2, "a difference in stamina alone is noticed and corrected too");
    }

    printf("\nwhat the server does with commands it should not trust\n");
    {
        static NetLoopback L; NetLoopback_Init(&L, 31u); NetLoopback_SetBoth(&L, Link(5.0f, 0.0f, 0.0f, 0.0f));
        NetServer_Init(&g_srv, 12345u, 1);
        const int slot = NetServer_AddClient(&g_srv, NetLoopback_End(&L, 1), 0.0);
        NetTransport cli = NetLoopback_End(&L, 0);
        NetServerClient *sc = &g_srv.c[slot];
        NetMsg in; memset(&in, 0, sizeof in); in.kind = NET_MSG_INPUT; in.u.input.tick = 5; in.u.input.count = 1; in.u.input.cmds[0] = NetCmd_Make(0, -1.0f, 0, 0, 0);
        size_t n = NetProto_Encode(&in, buf, sizeof buf);
        cli.sendDatagram(cli.self, buf, n);
        NetLoopback_Advance(&L, 10.0); NetServer_Poll(&g_srv, 10.0);
        Check(sc->cmdsAccepted == 0 && sc->badPackets == 1 && !sc->started, "a command from a client that has not said Hello is ignored");
        NetMsg h; memset(&h, 0, sizeof h); h.kind = NET_MSG_HELLO; h.u.hello.version = NET_PROTOCOL_VERSION; strcpy(h.u.hello.character, "steve");
        n = NetProto_Encode(&h, buf, sizeof buf); cli.sendReliable(cli.self, buf, n); cli.sendReliable(cli.self, buf, n);
        NetLoopback_Advance(&L, 20.0); NetServer_Poll(&g_srv, 20.0);
        Check(sc->welcomed && NetServer_PlayerCount(&g_srv) == 1, "after Hello it is welcomed (a second Hello changes nothing)");

        // A connection that never says Hello is not left holding a place.
        static NetLoopback L2; NetLoopback_Init(&L2, 32u); NetLoopback_SetBoth(&L2, Link(5.0f, 0.0f, 0.0f, 0.0f));
        const int lurker = NetServer_AddClient(&g_srv, NetLoopback_End(&L2, 1), 100.0);
        NetServer_Poll(&g_srv, 2000.0);
        const int stillThere = g_srv.c[lurker].used;
        NetServer_Poll(&g_srv, 3200.0);
        Check(lurker >= 0 && stillThere && !g_srv.c[lurker].used, "a connection that never says Hello keeps its place for a few seconds, then loses it");

        in.u.input.tick = g_srv.tick + 5000;
        n = NetProto_Encode(&in, buf, sizeof buf); cli.sendDatagram(cli.self, buf, n);
        NetLoopback_Advance(&L, 30.0); NetServer_Poll(&g_srv, 30.0);
        Check(sc->cmdsTooFar == 1 && sc->cmdsAccepted == 0 && !sc->started, "a command thousands of ticks ahead is refused (it could not be used, and it would fill the queue)");

        in.u.input.tick = g_srv.tick + 3;
        n = NetProto_Encode(&in, buf, sizeof buf);
        for (int i = 0; i < 10; ++i) cli.sendDatagram(cli.self, buf, n);
        NetLoopback_Advance(&L, 40.0); NetServer_Poll(&g_srv, 40.0);
        Check(sc->cmdsAccepted == 1 && sc->cmdsDuplicate == 9, "ten copies of one tick's command are one command: sending more does not move the player more");

        for (int t = 0; t < 4; ++t) { NetServer_Tick(&g_srv, 50.0 + t); }
        const unsigned acceptedBefore = sc->cmdsAccepted;
        in.u.input.tick = g_srv.tick - 2;
        n = NetProto_Encode(&in, buf, sizeof buf); cli.sendDatagram(cli.self, buf, n);
        NetLoopback_Advance(&L, 60.0); NetServer_Poll(&g_srv, 60.0);
        Check(sc->cmdsLate == 1 && sc->margin < 0 && sc->cmdsAccepted == acceptedBefore, "a command for a tick already run is late: refused (not kept), and the client is told by a negative margin");

        // Speed: a forged full-speed diagonal for many ticks moves the player no faster than the game allows.
        World(1, Link(0.0f, 0.0f, 0.0f, 0.0f), 33u);
        Run(300.0);
        NetServerClient *c0 = &g_srv.c[g_slot[0]];
        const NetCmd forged = (NetCmd){ 127, -127, 0, 0, NET_BTN_SPRINT };
        for (int t = 0; t < 120; ++t) {
            const uint32_t next = g_srv.tick + 3;
            NetMsg f; memset(&f, 0, sizeof f); f.kind = NET_MSG_INPUT; f.u.input.tick = next; f.u.input.count = 1; f.u.input.cmds[0] = forged;
            n = NetProto_Encode(&f, buf, sizeof buf);
            g_lb[0].ep[0].link = &g_lb[0];
            NetTransport t0 = NetLoopback_End(&g_lb[0], 0); t0.sendDatagram(t0.self, buf, n);
            g_quiet[0] = true;
            Run(TICK_MS);
        }
        float fastest = 0.0f; Vector3 last = g_srvHist[0][(g_srv.tick - 100) % 256].position;
        for (uint32_t T = g_srv.tick - 99; T <= g_srv.tick; ++T) { const Vector3 p = g_srvHist[0][T % 256].position; const float d = hypotf(p.x - last.x, p.z - last.z); if (d > fastest) fastest = d; last = p; }
        printf("    (the fastest the forged player moved in one tick: %.3f m = %.1f m/s; a sprint is %.1f)\n", (double)fastest, (double)(fastest * 30.0f), (double)PLAYER_SPRINT_SPEED);
        Check(fastest * 30.0f <= PLAYER_SPRINT_SPEED * 1.35f + 0.5f, "a forged full-speed diagonal is no faster than the game's own sprint (to the slide's burst at most)");
        (void)c0;

        // A flood: thousands of datagrams in one go are read a bounded number at a time.
        World(1, Link(1.0f, 0.0f, 0.0f, 0.0f), 35u);
        Run(300.0);
        const unsigned seen0 = g_srv.c[g_slot[0]].cmdsAccepted + g_srv.c[g_slot[0]].cmdsDuplicate + g_srv.c[g_slot[0]].cmdsLate + g_srv.c[g_slot[0]].cmdsTooFar;
        NetMsg fl; memset(&fl, 0, sizeof fl); fl.kind = NET_MSG_INPUT; fl.u.input.count = 1; fl.u.input.cmds[0] = NetCmd_Make(0, -1.0f, 0, 0, 0);
        NetTransport t0 = NetLoopback_End(&g_lb[0], 0);
        int sentOk = 0;
        for (int i = 0; i < 1000; ++i) { fl.u.input.tick = g_srv.tick + 4 + (uint32_t)(i % 10); n = NetProto_Encode(&fl, buf, sizeof buf); if (t0.sendDatagram(t0.self, buf, n)) ++sentOk; }
        NetLoopback_Advance(&g_lb[0], g_now + 5.0); g_now += 5.0;
        NetServer_Poll(&g_srv, g_now);
        const unsigned seen1 = g_srv.c[g_slot[0]].cmdsAccepted + g_srv.c[g_slot[0]].cmdsDuplicate + g_srv.c[g_slot[0]].cmdsLate + g_srv.c[g_slot[0]].cmdsTooFar;
        Check(sentOk > 150 && seen1 - seen0 <= NET_MAX_PACKETS_PER_POLL, "a flood of datagrams: one poll reads at most the set number of them, so the server's tick is never held up");

        // Garbage.
        const unsigned bad0 = g_srv.c[g_slot[0]].badPackets;
        uint32_t rs = 4242u;
        for (int i = 0; i < 30; ++i) { uint8_t junk[40]; for (int j = 0; j < 40; ++j) junk[j] = (uint8_t)Rng(&rs); t0.sendDatagram(t0.self, junk, 40); }
        for (int i = 0; i < 12; ++i) { NetLoopback_Advance(&g_lb[0], g_now + 5.0); g_now += 5.0; NetServer_Poll(&g_srv, g_now); }     // the rest of the flood first, then the junk
        Check(g_srv.c[g_slot[0]].badPackets >= bad0 + 25 && g_srv.c[g_slot[0]].used, "random junk is counted and thrown away, and the client is not dropped for it");
    }

    printf("\nother players, seen a little in the past\n");
    {
        World(2, Link(40.0f, 15.0f, 0.05f, 0.0f), 12u);
        Run(3000.0);
        float worst = 0.0f, biggestJump = 0.0f; int samples = 0; Vector3 prev = { 0 }; bool havePrev = false;
        for (int i = 0; i < 12000; ++i) {
            Step1ms();
            if (i % 33 == 0) {
                Vector3 pos; float yaw;
                if (!NetClient_RemoteView(&g_cl[0], g_cl[1].id, g_now, &pos, &yaw)) continue;
                const double est = (double)g_cl[0].lastServerTick + (g_now - g_cl[0].lastSnapMs) / TICK_MS - NET_INTERP_TICKS;   // the tick it is meant to show
                const uint32_t T0 = (uint32_t)floor(est);
                if (T0 + 1 > g_srv.tick || g_srv.tick - T0 >= 200) continue;
                const Vector3 a = g_srvHist[1][T0 % 256].position, b = g_srvHist[1][(T0 + 1) % 256].position;
                const float u = (float)(est - T0);
                const Vector3 truth = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u };
                const float e = hypotf(pos.x - truth.x, pos.z - truth.z);
                if (e > worst) worst = e;
                if (havePrev) { const float j = hypotf(pos.x - prev.x, pos.z - prev.z); if (j > biggestJump) biggestJump = j; }
                prev = pos; havePrev = true; ++samples;
            }
        }
        printf("    (%d samples; the other player is drawn at most %.3f m from where the server had them at that moment; the biggest move between two draws a tick apart %.3f m)\n", samples, (double)worst, (double)biggestJump);
        Check(samples > 300 && worst < 0.2f, "drawn between snapshots, the other player is within 20 cm of where the server had them at the time it shows (5% loss, 30 ms of swing)");
        Check(biggestJump < 0.6f, "and moves smoothly from one draw to the next: never a jump");
        Vector3 own; float yaw;
        Check(!NetClient_RemoteView(&g_cl[0], g_cl[0].id, g_now, &own, &yaw) && !NetClient_RemoteView(&g_cl[0], 3, g_now, &own, &yaw), "a player is never shown a copy of themselves, nor one that is not there");

        // A half second of silence: the other player is held where the last snapshot had them (they really did move on, so there is a step to catch up
        // when it ends), and a moment later is drawn where the server has them again.
        const double blackoutStart = g_now + 100.0;
        NetLoopback_Blackout(&g_lb[0], blackoutStart, blackoutStart + 500.0);
        float heldMove = 0.0f, errBack = 99.0f; Vector3 pv = { 0 }; bool hp = false;
        for (int i = 0; i < 1500; ++i) {
            Step1ms();
            Vector3 pos; float y;
            if (i % 33 == 0 && NetClient_RemoteView(&g_cl[0], g_cl[1].id, g_now, &pos, &y)) {
                const double t = g_now - blackoutStart;
                if (hp && t > 200.0 && t < 520.0) { const float j = hypotf(pos.x - pv.x, pos.z - pv.z); if (j > heldMove) heldMove = j; }     // from when the interpolation has run out of snapshots to just before they come back (about 540 ms)
                if (t > 1100.0) {
                    const double est = (double)g_cl[0].lastServerTick + (g_now - g_cl[0].lastSnapMs) / TICK_MS - NET_INTERP_TICKS;
                    const uint32_t T0 = (uint32_t)floor(est);
                    if (T0 + 1 <= g_srv.tick && g_srv.tick - T0 < 200) {
                        const Vector3 a = g_srvHist[1][T0 % 256].position, b = g_srvHist[1][(T0 + 1) % 256].position; const float u = (float)(est - T0);
                        const float e = hypotf(pos.x - (a.x + (b.x - a.x) * u), pos.z - (a.z + (b.z - a.z) * u));
                        if (errBack > 50.0f || e > errBack) errBack = e;                   // the worst over the time, once there is a first one
                    }
                }
                pv = pos; hp = true;
            }
        }
        printf("    (during the silence the drawn player moved at most %.3f m between draws; a second after it ended it was %.3f m from the server's)\n", (double)heldMove, (double)errBack);
        Check(heldMove < 0.05f, "when the network goes silent for half a second the other player is held where they were last seen, not drifting");
        Check(errBack < 0.2f, "and once the snapshots come back they are drawn where the server has them again");
    }

    printf("\nleaving and going quiet\n");
    {
        World(2, Link(20.0f, 5.0f, 0.0f, 0.0f), 14u);
        Run(1500.0);
        Check(g_cl[0].snaps[0].n == 1 && g_cl[1].snaps[0].n == 1, "each of two players is told about the other");
        NetClient_Leave(&g_cl[1]);
        Run(300.0);
        Check(!g_srv.c[g_slot[1]].used && NetServer_PlayerCount(&g_srv) == 1, "a client that says goodbye is dropped at once");
        Check(g_cl[0].snaps[0].n == 0, "and the other player stops being told about it");
        static NetLoopback L; NetLoopback_Init(&L, 3u); NetLoopback_SetBoth(&L, Link(20.0f, 5.0f, 0.0f, 0.0f));
        const int again = NetServer_AddClient(&g_srv, NetLoopback_End(&L, 1), g_now);
        Check(again == g_slot[1], "the freed slot can be used again");

        World(1, Link(20.0f, 5.0f, 0.0f, 0.0f), 18u);
        Run(1500.0);
        g_quiet[0] = true;                                                               // the client goes silent (it is still there)
        Run(1500.0);
        const NetServerClient *quiet = &g_srv.c[g_slot[0]];
        const float speedNow = hypotf(quiet->player.moveVel.x, quiet->player.moveVel.z);
        Check(quiet->ticksNeutral > 0 && speedNow < 0.5f && quiet->used, "a client that goes silent: its player carries on for a few ticks, then stops where they are (and is not dropped yet)");

        World(2, Link(20.0f, 5.0f, 0.0f, 0.0f), 15u);
        Run(1000.0);
        g_quiet[1] = true; g_lb[1].cfg[0].loss = 1.0f;                                   // client 1's packets stop arriving
        Run(NET_TIMEOUT_MS + 1500.0);
        Check(!g_srv.c[g_slot[1]].used && g_cl[0].state == NETC_PLAYING, "a client not heard from for five seconds is dropped; the other goes on");
        Run(100.0);
        Check(g_cl[1].state == NETC_CLOSED, "and the one that was cut off finds the server gone: its client closes (told, or after five silent seconds)");

        World(1, Link(20.0f, 5.0f, 0.0f, 0.0f), 16u);
        Run(1000.0);
        g_lb[0].cfg[1].loss = 1.0f;                                                       // the server's snapshots stop arriving
        Run(NET_TIMEOUT_MS + 500.0);
        Check(g_cl[0].state == NETC_CLOSED && g_cl[0].closedReason == NET_DISC_TIMEOUT, "a client that hears nothing from the server for five seconds closes by itself");
    }

    printf("\nthe clock lead\n");
    {
        World(1, Link(15.0f, 3.0f, 0.0f, 0.0f), 17u);                  // a 30 ms round trip
        Run(5000.0);
        const NetServerClient *sc = &g_srv.c[g_slot[0]];
        const unsigned late0 = sc->cmdsLate;
        for (int k = 0; k < 4; ++k) g_lb[0].cfg[k < 2 ? k : 0] = Link(120.0f, 30.0f, 0.0f, 0.0f);   // the route changes: now a 240 ms round trip, and 60 ms of swing
        g_lb[0].cfg[1] = Link(120.0f, 30.0f, 0.0f, 0.0f);
        Run(3000.0);
        const unsigned lateDuring = sc->cmdsLate - late0;
        const unsigned late1 = sc->cmdsLate;
        Run(10000.0);
        const unsigned lateAfter = sc->cmdsLate - late1;
        printf("    (right after the route got slower %u commands were late; in the next ten seconds %u; the lead is now %d ticks)\n", lateDuring, lateAfter, (int)g_cl[0].tick - (int)g_srv.tick);
        Check(lateDuring > 0, "(the slower route really did make commands late at first)");
        Check(lateAfter <= 3, "and then the lead settled: in the next ten seconds, almost none were late");
        Check((int)g_cl[0].tick - (int)g_srv.tick >= 7 && (int)g_cl[0].tick - (int)g_srv.tick < 25, "with the client running about a round trip and a margin ahead of the server");
        g_lb[0].cfg[0] = Link(15.0f, 3.0f, 0.0f, 0.0f); g_lb[0].cfg[1] = Link(15.0f, 3.0f, 0.0f, 0.0f);
        Run(14000.0);
        printf("    (and when it got fast again the lead came back down to %d ticks)\n", (int)g_cl[0].tick - (int)g_srv.tick);
        Check((int)g_cl[0].tick - (int)g_srv.tick < 12, "when the route is fast again the lead comes back down, so the player is not kept waiting for no reason");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
