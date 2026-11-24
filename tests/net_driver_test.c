// Test of net_driver: the game's real NetServer and NetClient running over mid-net's driver (the Rust library, through its C interface) over the pretend
// network of net_loopback (delay, loss, doubling, blackouts), and what the driver changes against the plain transport: NO SIGNAL shown, a six second
// outage survived (the plain rule gave up at five), a dead link ended at ten seconds, a goodbye heard at once, a stranger's bytes and a plain client
// refused, a full server saying so. No window, no real network. This needs the Rust library, so it is not one of the *_headless tests: run
//   bash tests/net_driver_check.sh
// which builds the library from the mid-engine checkout (exit 77 = skipped when there is none) and compiles and runs this with the line below.
//   cc -O1 -g -std=c99 -Wall -Wextra -DNET_DRIVER_TEST -DTOWER_NETDRIVER=1 -Isrc -Iraylib/src tests/net_driver_test.c src/net/net_driver.c src/net/net_mid.c src/net/net_wire.c src/net/net_proto.c src/net/net_loopback.c src/net/net_move_sim.c src/net/net_server.c src/net/net_history.c src/net/net_client.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c LIBMIDNET -lm -lpthread -ldl -o net_driver_test
#ifdef NET_DRIVER_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dungeon/dungeon_generator.h"
#include "net/net_client.h"
#include "net/net_driver.h"
#include "net/net_loopback.h"
#include "net/net_proto.h"
#include "net/net_server.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

#define TICK_MS (1000.0 / 30.0)
static NetServer g_srv;
static NetLoopback g_lb[NET_MAX_PLAYERS + 1];
static NetClient g_cl[NET_MAX_PLAYERS + 1];
static int g_nClients, g_slot[NET_MAX_PLAYERS + 1];
static double g_now, g_nextSrvTick;
static bool g_useDriver;

static double Clock(void) { return g_now; }

static NetCmd Script(void *user, uint32_t t)
{
    const int k = (int)(intptr_t)user;
    const int phase = (int)((t / 45u) % 6u);
    float mx = 0.0f, my = -1.0f; uint16_t b = 0;
    if (phase == 1) b |= NET_BTN_SPRINT;
    if (phase == 2) { mx = 1.0f; my = 0.0f; }
    if (phase == 3 && t % 45u == 10u) b |= NET_BTN_JUMP;
    if (phase == 4) { mx = -1.0f; my = 0.5f; }
    return NetCmd_Make(mx, my, 0.7f * (float)k + 0.02f * (float)t, 0.05f * sinf((float)t * 0.05f), b);
}

static NetLinkCfg Link(float latency, float jitter, float loss, float dup) { NetLinkCfg c = { latency, jitter, loss, dup }; return c; }

static void World(int n, NetLinkCfg cfg, uint32_t seed, bool driver)
{
    g_nClients = n; g_now = 1000.0; g_useDriver = driver;
    NetDriver_SetClock(Clock);
    NetServer_Init(&g_srv, 12345u, 1);
    g_nextSrvTick = g_now + TICK_MS;
    for (int k = 0; k < n; ++k) {
        NetLoopback_Init(&g_lb[k], seed + (uint32_t)k * 77u);
        NetLoopback_SetBoth(&g_lb[k], cfg);
        NetLoopback_Advance(&g_lb[k], g_now);
        NetTransport st = NetLoopback_End(&g_lb[k], 1), ct = NetLoopback_End(&g_lb[k], 0);
        if (driver) {
            if (!NetDriver_Server(st, (uint32_t)(k + 1), &st) || !NetDriver_Client(ct, k == 0 ? "steve" : "brou", &ct)) { printf("FATAL: the driver would not start\n"); exit(2); }
        }
        g_slot[k] = NetServer_AddClient(&g_srv, st, g_now);
        NetClient_Init(&g_cl[k], ct, k == 0 ? "steve" : "brou", g_now);
    }
}

static void Step1ms(void)
{
    g_now += 1.0;
    for (int k = 0; k < g_nClients; ++k) NetLoopback_Advance(&g_lb[k], g_now);
    NetServer_Poll(&g_srv, g_now);
    for (int k = 0; k < g_nClients; ++k) NetClient_Poll(&g_cl[k], g_now);
    if (g_now >= g_nextSrvTick) { g_nextSrvTick += TICK_MS; NetServer_Tick(&g_srv, g_now); }
    for (int k = 0; k < g_nClients; ++k) NetClient_Update(&g_cl[k], g_now, Script, (void *)(intptr_t)k);
}
static void Run(double ms) { const double end = g_now + ms; while (g_now < end) Step1ms(); }
static void Finish(void) { for (int k = 0; k < g_nClients; ++k) NetClient_Leave(&g_cl[k]); Run(300.0); for (int i = 0; i < NET_MAX_PLAYERS; ++i) NetServer_Drop(&g_srv, i, NET_DISC_NORMAL); }

static int ClientLinkState(int k) { NetLinkInfo li; return NetClient_Link(&g_cl[k], &li) ? li.state : -1; }
static int ServerLinkState(int k) { NetLinkInfo li; return (g_srv.c[g_slot[k]].used && g_srv.c[g_slot[k]].tp.linkInfo && g_srv.c[g_slot[k]].tp.linkInfo(g_srv.c[g_slot[k]].tp.self, &li)) ? li.state : -1; }

int main(void)
{
    printf("=== net_driver: the game's server and client over mid-net's driver ===\n");
    Check(NetDriver_Available(), "the driver is linked in this build");

    printf("\n-- the handshake, and the game over it --\n");
    World(1, Link(30.0f, 10.0f, 0.0f, 0.0f), 5u, true);
    Run(100.0);
    NetDriverStats st0;
    Check(NetDriver_Stats(&g_cl[0].tp, &st0) && st0.heldEarly >= 1, "the client's Hello, sent before the handshake had finished, was held and sent when it had");
    Check(ClientLinkState(0) == NETLINK_CONNECTING || ClientLinkState(0) == NETLINK_CONNECTED, "the client's link is connecting or connected after 100 ms");
    Run(2900.0);
    Check(g_cl[0].state == NETC_PLAYING, "the client is playing (the game's Hello and Welcome went through the driver)");
    Check(ClientLinkState(0) == NETLINK_CONNECTED && ServerLinkState(0) == NETLINK_CONNECTED, "both ends say the link is connected");
    NetLinkInfo li;
    Check(NetClient_Link(&g_cl[0], &li) && li.rttMs > 40.0f && li.rttMs < 200.0f, "the round trip is measured (30 ms each way plus jitter)");
    Check(g_cl[0].snapshotsSeen > 60, "snapshots arrive");
    Finish();

    printf("\n-- over a bad network it plays as it does without the driver --\n");
    unsigned seenPlain = 0, seenDrv = 0, hardPlain = 0, hardDrv = 0;
    for (int pass = 0; pass < 2; ++pass) {
        World(1, Link(40.0f, 15.0f, 0.10f, 0.05f), 9u, pass == 1);
        Run(20000.0);
        if (pass == 0) { seenPlain = g_cl[0].snapshotsSeen; hardPlain = g_cl[0].hardSnaps; }
        else { seenDrv = g_cl[0].snapshotsSeen; hardDrv = g_cl[0].hardSnaps; }
        Check(g_cl[0].state == NETC_PLAYING && g_srv.c[g_slot[0]].used, pass == 0 ? "plain: still playing after 20 s at 10% loss" : "driver: still playing after 20 s at 10% loss");
        Finish();
    }
    printf("      snapshots seen: plain %u, driver %u; hard snaps: plain %u, driver %u\n", seenPlain, seenDrv, hardPlain, hardDrv);
    Check(seenDrv * 100 >= seenPlain * 80, "the driver loses no more than a fifth of what plain UDP delivers (it also drops a snapshot that comes after a newer one)");
    Check(hardDrv <= hardPlain + 1, "and the game's prediction is not shaken more");

    printf("\n-- an outage of six seconds: NO SIGNAL, then back (the plain rule gives up at five) --\n");
    for (int pass = 0; pass < 2; ++pass) {
        World(1, Link(30.0f, 5.0f, 0.0f, 0.0f), 3u, pass == 1);
        Run(2000.0);
        NetLoopback_Blackout(&g_lb[0], g_now + 100.0, g_now + 6100.0);
        const double start = g_now;
        double noSignalAt = -1.0;
        int sawNoSignal = 0;
        for (int i = 0; i < 70; ++i) {
            Run(100.0);
            if (pass == 1 && ClientLinkState(0) == NETLINK_NO_SIGNAL && noSignalAt < 0.0) noSignalAt = g_now - start;
            if (pass == 1 && ClientLinkState(0) == NETLINK_NO_SIGNAL) sawNoSignal = 1;
        }
        Run(2000.0);
        if (pass == 0) {
            Check(g_cl[0].state == NETC_CLOSED && g_cl[0].closedReason == NET_DISC_TIMEOUT, "plain: the client gave up in the outage (the five second rule)");
        } else {
            Check(sawNoSignal && noSignalAt > 1300.0 && noSignalAt < 2300.0, "driver: NO SIGNAL showed about a second and a half into the outage");
            Check(g_cl[0].state == NETC_PLAYING && g_srv.c[g_slot[0]].used, "driver: the client is still playing and the server kept the player");
            Check(ClientLinkState(0) == NETLINK_CONNECTED && ServerLinkState(0) == NETLINK_CONNECTED, "driver: both ends say connected again");
        }
        Finish();
    }

    printf("\n-- a dead link ends at ten seconds, on both ends --\n");
    World(1, Link(30.0f, 5.0f, 0.0f, 0.0f), 3u, true);
    Run(2000.0);
    NetLoopback_Blackout(&g_lb[0], g_now + 100.0, g_now + 60000.0);
    const double lastHeard = g_now + 100.0;
    double clientEnd = -1.0, serverEnd = -1.0;
    for (int i = 0; i < 140; ++i) {
        Run(100.0);
        if (clientEnd < 0.0 && g_cl[0].state == NETC_CLOSED) clientEnd = g_now - lastHeard;
        if (serverEnd < 0.0 && !g_srv.c[g_slot[0]].used) serverEnd = g_now - lastHeard;
    }
    printf("      the client closed %.1f s and the server dropped the player %.1f s after the last packet\n", clientEnd / 1000.0, serverEnd / 1000.0);
    Check(clientEnd > 9500.0 && clientEnd < 11800.0 && g_cl[0].closedReason == NET_DISC_TIMEOUT, "the client closed at about ten seconds, with the timeout reason");
    Check(serverEnd > 9500.0 && serverEnd < 11800.0, "the server dropped the player at about ten seconds");
    Finish();

    printf("\n-- leaving --\n");
    World(1, Link(20.0f, 5.0f, 0.0f, 0.0f), 3u, true);
    Run(1500.0);
    Check(NetServer_PlayerCount(&g_srv) == 1, "one player");
    NetClient_Leave(&g_cl[0]);
    { NetLinkInfo gone; Check(!NetClient_Link(&g_cl[0], &gone), "a client that has left no longer asks its (freed) transport how it stands"); }
    Run(300.0);
    Check(NetServer_PlayerCount(&g_srv) == 0 && !g_srv.c[g_slot[0]].used, "the server heard the goodbye and freed the place within 300 ms");
    NetClient_Leave(&g_cl[0]);
    Check(g_cl[0].state == NETC_CLOSED, "leaving again does nothing (and frees nothing twice)");
    Finish();

    printf("\n-- a client that was timed out by its own link still frees its transport when it leaves --\n");
    World(1, Link(30.0f, 5.0f, 0.0f, 0.0f), 3u, true);
    Run(1500.0);
    NetLoopback_Blackout(&g_lb[0], g_now + 50.0, g_now + 60000.0);
    Run(12500.0);
    Check(g_cl[0].state == NETC_CLOSED && !g_cl[0].tpClosed, "closed by the link, transport not yet freed");
    NetClient_Leave(&g_cl[0]);
    Check(g_cl[0].tpClosed, "Leave frees it (and under the address sanitizer a leak or a double free would be found)");
    NetClient_Leave(&g_cl[0]);
    Finish();

    printf("\n-- four players, and a fifth turned away --\n");
    World(NET_MAX_PLAYERS + 1, Link(25.0f, 10.0f, 0.03f, 0.02f), 21u, true);
    Run(8000.0);
    int playing = 0;
    for (int k = 0; k < NET_MAX_PLAYERS; ++k) if (g_cl[k].state == NETC_PLAYING) ++playing;
    Check(playing == NET_MAX_PLAYERS && NetServer_PlayerCount(&g_srv) == NET_MAX_PLAYERS, "four players are in and playing over lossy links");
    Check(g_slot[NET_MAX_PLAYERS] == -1, "the fifth was told there is no place");
    Check(g_cl[NET_MAX_PLAYERS].state == NETC_CLOSED && g_cl[NET_MAX_PLAYERS].closedReason == NET_DISC_FULL, "and its client says THE GAME IS FULL (a Disconnect through the driver, from a transport that was about to be closed)");
    Finish();

    printf("\n-- the reliable path: a burst the transport under cannot take at once --\n");
    {
        // 800 messages of 1 to 500 bytes (200 KB) at two a millisecond over a link with 150 ms of delay: more chunks in flight than the pretend network holds (192)
        NetLoopback L; NetLoopback_Init(&L, 33u); NetLoopback_SetBoth(&L, Link(150.0f, 5.0f, 0.0f, 0.0f));
        g_now = 1000.0; NetDriver_SetClock(Clock); NetLoopback_Advance(&L, g_now);
        NetTransport a, b;
        Check(NetDriver_Client(NetLoopback_End(&L, 0), "flood", &a) && NetDriver_Server(NetLoopback_End(&L, 1), 7u, &b), "a client end and a server end, without the game on top");
        for (int i = 0; i < 1000; ++i) { g_now += 1.0; NetLoopback_Advance(&L, g_now); uint8_t t[8]; a.recvReliable(a.self, t, sizeof t); b.recvReliable(b.self, t, sizeof t); }
        NetLinkInfo ai;
        Check(a.linkInfo(a.self, &ai) && ai.state == NETLINK_CONNECTED, "connected");
        int sent = 0, ok = 1, got = 0, inOrder = 1, intact = 1;
        uint8_t msg[NET_MTU];
        for (int i = 0; i < 40000 && got < 800 && ok; ++i) {
            g_now += 1.0; NetLoopback_Advance(&L, g_now);
            for (int k = 0; k < 2 && sent < 800; ++k) {
                const size_t n = 1 + (size_t)((sent * 37) % 500);
                for (size_t j = 0; j < n; ++j) msg[j] = (uint8_t)(sent + (int)j);
                if (!a.sendReliable(a.self, msg, n)) { ok = 0; break; }
                ++sent;
            }
            uint8_t tmp[8]; a.recvReliable(a.self, tmp, sizeof tmp);
            uint8_t buf[NET_MTU]; int n;
            while ((n = b.recvReliable(b.self, buf, sizeof buf)) > 0) {
                const size_t want = 1 + (size_t)((got * 37) % 500);
                if ((size_t)n != want) inOrder = 0;
                for (int j = 0; j < n && intact; ++j) if (buf[j] != (uint8_t)(got + j)) intact = 0;
                ++got;
            }
        }
        NetDriverStats sa;
        NetDriver_Stats(&a, &sa);
        printf("      sent %d, received %d, lower queue peak %u, faults %u, link state %d, overflowed %u/%u\n", sent, got, sa.lowerQueuePeak, sa.faults, a.linkInfo(a.self, &ai) ? ai.state : -1, L.overflowed[0], L.overflowed[1]);
        Check(ok && sent == 800, "all 800 were accepted");
        Check(got == 800 && inOrder && intact, "all 800 arrive, once, in order, byte for byte");
        Check(NetDriver_Stats(&a, &sa) && sa.lowerQueuePeak > 20 && sa.lowerQueuePeak < 256 && sa.faults == 0, "some had to wait for room in the transport under (and did, within what is kept for it), and nothing went wrong");
        a.close(a.self); b.close(b.self);
    }

    printf("\n-- the transport on its own: before the handshake, a reader that falls behind, a peer that never reads --\n");
    {
        NetLoopback L; NetLoopback_Init(&L, 44u); NetLoopback_SetBoth(&L, Link(10.0f, 2.0f, 0.0f, 0.0f));
        g_now = 1000.0; NetDriver_SetClock(Clock); NetLoopback_Advance(&L, g_now);
        NetTransport a, b;
        NetDriver_Client(NetLoopback_End(&L, 0), "early", &a); NetDriver_Server(NetLoopback_End(&L, 1), 5u, &b);
        uint8_t d[40]; memset(d, 7, sizeof d);
        Check(a.sendDatagram(a.self, d, sizeof d), "a datagram sent before the handshake is accepted (and dropped, as any datagram may be)");
        int accepted = 0;
        for (int i = 0; i < 12; ++i) { uint8_t m[4] = { (uint8_t)i, 1, 2, 3 }; if (a.sendReliable(a.self, m, sizeof m)) ++accepted; }
        Check(accepted == 8, "eight reliable messages wait for the handshake, the ninth and the ones after it are refused");
        int order[16], n = 0;
        for (int i = 0; i < 1500; ++i) {
            g_now += 1.0; NetLoopback_Advance(&L, g_now);
            uint8_t t[16]; int r;
            a.recvReliable(a.self, t, sizeof t);
            while ((r = b.recvReliable(b.self, t, sizeof t)) > 0) if (n < 16) order[n++] = t[0];
        }
        int inOrder = (n == 8);
        for (int i = 0; i < n && i < 8; ++i) if (order[i] != i) inOrder = 0;
        Check(inOrder, "they went when the handshake had finished: all eight, in the order they were sent");
        NetDriverStats sa;
        Check(NetDriver_Stats(&a, &sa) && sa.heldEarly == 8, "and the driver counted eight held");

        // a reader that falls behind: 100 datagrams and 100 reliable messages arrive while the game reads nothing
        for (int i = 0; i < 100; ++i) {
            g_now += 1.0; NetLoopback_Advance(&L, g_now);
            uint8_t m[8] = { (uint8_t)i, 0xAA, 0xBB, 0xCC };
            a.sendDatagram(a.self, m, sizeof m); a.sendReliable(a.self, m, sizeof m);
            uint8_t t[16]; a.recvReliable(a.self, t, sizeof t);
        }
        g_now += 400.0; NetLoopback_Advance(&L, g_now);
        int dgs = 0, firstDg = -1, lastDg = -1, rels = 0, relOrder = 1;
        for (int i = 0; i < 400; ++i) {
            g_now += 1.0; NetLoopback_Advance(&L, g_now);
            uint8_t t[16]; int r;
            while ((r = b.recvDatagram(b.self, t, sizeof t)) > 0) { if (firstDg < 0) firstDg = t[0]; lastDg = t[0]; ++dgs; }
            while ((r = b.recvReliable(b.self, t, sizeof t)) > 0) { if (t[0] != rels) relOrder = 0; ++rels; }
        }
        NetDriverStats sb;
        NetDriver_Stats(&b, &sb);
        printf("      %d datagrams read (%d..%d, %u dropped for want of room), %d reliable messages read\n", dgs, firstDg, lastDg, sb.dgRingDrops, rels);
        Check(rels == 100 && relOrder, "all 100 reliable messages were kept, in order, though the game read nothing while they came (none is ever dropped)");
        Check(dgs == 32 && lastDg == 99 && firstDg >= 55 && sb.dgRingDrops >= 40, "of the datagrams that arrived (the sequenced channel drops a few that came after a newer one) the game read the 32 newest: the older ones went, and were counted");
        // a peer that speaks the driver but not this game: messages far bigger than the game ever sends
        {
            NetLoopback L3; NetLoopback_Init(&L3, 46u); NetLoopback_SetBoth(&L3, Link(10.0f, 0.0f, 0.0f, 0.0f));
            NetLoopback_Advance(&L3, g_now);
            NetTransport p, q;
            NetDriver_Client(NetLoopback_End(&L3, 0), "hostile", &p); NetDriver_Server(NetLoopback_End(&L3, 1), 8u, &q);
            for (int i = 0; i < 600; ++i) { g_now += 1.0; NetLoopback_Advance(&L3, g_now); uint8_t t[8]; p.recvReliable(p.self, t, sizeof t); q.recvReliable(q.self, t, sizeof t); }
            static uint8_t huge[9000]; memset(huge, 0xEE, sizeof huge);
            Check(NetDriver_TestInject(&p, 3, huge, 5000) && NetDriver_TestInject(&p, 3, huge, 9000 > 16000 ? 1 : 8000), "two messages of 5000 and 8000 bytes were put on the reliable channel");
            uint8_t okm[4] = { 9, 8, 7, 6 };
            p.sendReliable(p.self, okm, sizeof okm);
            int got = 0, firstByte = -1;
            for (int i = 0; i < 400; ++i) {
                g_now += 1.0; NetLoopback_Advance(&L3, g_now);
                uint8_t t[NET_MTU]; int r; uint8_t pump[8];
                p.recvReliable(p.self, pump, sizeof pump);
                while ((r = q.recvReliable(q.self, t, sizeof t)) > 0) { ++got; if (firstByte < 0) firstByte = t[0]; }
            }
            NetDriverStats sq; NetLinkInfo qi;
            NetDriver_Stats(&q, &sq);
            Check(got == 1 && firstByte == 9 && sq.oversizeDropped == 2, "the game reads only the real message; the two oversize ones were dropped and counted, not copied into anything");
            Check(q.linkInfo(q.self, &qi) && qi.state == NETLINK_CONNECTED && sq.faults == 0, "and the connection is unharmed");
            p.close(p.self); q.close(q.self);
        }
        a.close(a.self); b.close(b.self);

        // a peer that never reads: the sender's driver ends the connection instead of growing without bound
        NetLoopback L2; NetLoopback_Init(&L2, 45u); NetLoopback_SetBoth(&L2, Link(10.0f, 0.0f, 0.0f, 0.0f));
        NetLoopback_Advance(&L2, g_now);
        NetTransport c, e;
        NetDriver_Client(NetLoopback_End(&L2, 0), "mute", &c); NetDriver_Server(NetLoopback_End(&L2, 1), 6u, &e);
        for (int i = 0; i < 600; ++i) { g_now += 1.0; NetLoopback_Advance(&L2, g_now); uint8_t t[8]; c.recvReliable(c.self, t, sizeof t); e.recvReliable(e.self, t, sizeof t); }
        uint8_t big[500]; memset(big, 5, sizeof big);
        int pushed = 0;
        for (int i = 0; i < 4000; ++i) {
            g_now += 1.0; NetLoopback_Advance(&L2, g_now);
            if (!c.sendReliable(c.self, big, sizeof big)) break;       // the server end is never polled again: it reads nothing
            ++pushed;
        }
        NetDriverStats sc; NetLinkInfo ci;
        NetDriver_Stats(&c, &sc);
        printf("      %d messages of 500 bytes were accepted before the driver refused (%u chunks waited at most, %u fault)\n", pushed, sc.lowerQueuePeak, sc.faults);
        Check(pushed > 300 && pushed < 2000, "a peer that reads nothing is refused after a bounded amount (what the transport below and the driver may hold), not after memory has been eaten");
        Check(sc.faults == 1 && sc.lowerQueuePeak == 256 && c.linkInfo(c.self, &ci) && ci.state == NETLINK_ENDED, "the driver ended the connection with one fault when the transport below had taken nothing for 256 chunks, and never held more than that");
        c.close(c.self); e.close(e.self);
    }

    printf("\n-- latency: what is sent leaves at once, what arrives is readable at once --\n");
    {
        NetLoopback L; NetLoopback_Init(&L, 47u); NetLoopback_SetBoth(&L, Link(20.0f, 0.0f, 0.0f, 0.0f));
        g_now = 1000.0; NetDriver_SetClock(Clock); NetLoopback_Advance(&L, g_now);
        NetTransport p, q;
        NetDriver_Client(NetLoopback_End(&L, 0), "lat", &p); NetDriver_Server(NetLoopback_End(&L, 1), 4u, &q);
        for (int i = 0; i < 800; ++i) { g_now += 1.0; NetLoopback_Advance(&L, g_now); uint8_t t[8]; p.recvReliable(p.self, t, sizeof t); q.recvReliable(q.self, t, sizeof t); }
        for (int dir = 0; dir < 2; ++dir) {
            NetTransport *from = dir == 0 ? &p : &q, *to = dir == 0 ? &q : &p;
            for (int kind = 0; kind < 2; ++kind) {
                const double t0 = g_now;
                uint8_t m[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
                if (kind == 0) from->sendDatagram(from->self, m, sizeof m); else from->sendReliable(from->self, m, sizeof m);
                double arrive = -1.0;
                for (int i = 0; i < 200 && arrive < 0.0; ++i) {          // only the receiving end is touched after the send: the sender must not need another call to get it out
                    g_now += 1.0; NetLoopback_Advance(&L, g_now);
                    uint8_t t[16];
                    if ((kind == 0 ? to->recvDatagram(to->self, t, sizeof t) : to->recvReliable(to->self, t, sizeof t)) > 0) arrive = g_now - t0;
                }
                char what[120];
                snprintf(what, sizeof what, "%s %s: readable %.0f ms after it was sent (the link's own delay is 20)", dir == 0 ? "client to server" : "server to client", kind == 0 ? "datagram" : "reliable message", arrive);
                Check(arrive >= 20.0 && arrive <= 22.0, what);
                g_now += 100.0; NetLoopback_Advance(&L, g_now);
                uint8_t t[16]; from->recvReliable(from->self, t, sizeof t); to->recvReliable(to->self, t, sizeof t);
            }
        }
        p.close(p.self); q.close(q.self);
    }

    printf("\n-- why a link ended, as the game says it --\n");
    {
        const struct { int kind, peer; uint8_t want; const char *name; } T[] = {
            { NETLINK_END_LOCAL, 0, NET_DISC_NORMAL, "we closed it" },
            { NETLINK_END_PEER, 0, NET_DISC_NORMAL, "the other side said goodbye" },
            { NETLINK_END_PEER, 1, NET_DISC_VERSION, "refused: another version" },
            { NETLINK_END_PEER, 2, NET_DISC_VERSION, "refused: another schema" },
            { NETLINK_END_PEER, 3, NET_DISC_FULL, "the game is full" },
            { NETLINK_END_PEER, 4, NET_DISC_TIMEOUT, "the other side timed out" },
            { NETLINK_END_PEER, 5, NET_DISC_TIMEOUT, "the other side could not connect" },
            { NETLINK_END_PEER, 6, NET_DISC_NORMAL, "sent away" },
            { NETLINK_END_PEER, 7, NET_DISC_BAD, "the other side saw a protocol error" },
            { NETLINK_END_PEER, 99, NET_DISC_BAD, "a reason this build does not know" },
            { NETLINK_END_REJECTED, 2, NET_DISC_VERSION, "the handshake was refused for the schema" },
            { NETLINK_END_REJECTED, 3, NET_DISC_FULL, "the handshake was refused: full" },
            { NETLINK_END_TIMEOUT, 0, NET_DISC_TIMEOUT, "ten seconds of silence" },
            { NETLINK_END_CONNECT_TIMEOUT, 0, NET_DISC_TIMEOUT, "never connected" },
            { NETLINK_END_PROTOCOL, 0, NET_DISC_BAD, "a protocol error" },
        };
        int ok = 1;
        for (size_t i = 0; i < sizeof T / sizeof T[0]; ++i) {
            NetLinkInfo li; memset(&li, 0, sizeof li); li.state = NETLINK_ENDED; li.endKind = T[i].kind; li.endPeer = T[i].peer;
            if (NetDriver_EndToDisc(&li) != T[i].want) { ok = 0; printf("      wrong for: %s\n", T[i].name); }
        }
        Check(ok, "every way a link can end gives the right Disconnect reason");
    }

    printf("\n-- a stranger's bytes, and a client that does not use the driver --\n");
    {
        World(1, Link(20.0f, 5.0f, 0.0f, 0.0f), 3u, true);
        Run(1000.0);
        NetTransport junk = NetLoopback_End(&g_lb[0], 0);        // the stranger writes straight onto the client's end of the link: datagrams the server's driver must refuse
        uint32_t r = 12345u;
        for (int i = 0; i < 2000; ++i) {
            uint8_t b[200]; const size_t n = 1 + (r = r * 1664525u + 1013904223u) % 199u;
            for (size_t k = 0; k < n; ++k) b[k] = (uint8_t)((r = r * 1664525u + 1013904223u) >> 24);
            if (i % 3 == 0) { b[0] = 0; b[1] = (uint8_t)i; b[2] = 200; }          // a packet number and a count that cannot be true
            junk.sendDatagram(junk.self, b, n);
            if (i % 10 == 0) Run(1.0);
        }
        Run(1000.0);
        NetDriverStats ss;
        Check(NetDriver_Stats(&g_srv.c[g_slot[0]].tp, &ss) && ss.badDatagrams > 100, "the server's driver counted the garbage datagrams");
        Check(g_cl[0].state == NETC_PLAYING && g_srv.c[g_slot[0]].used, "and the player that is really there was not disturbed");
        Finish();

        World(1, Link(20.0f, 5.0f, 0.0f, 0.0f), 3u, false);      // a client without the driver, on a server with it
        NetServer_Drop(&g_srv, g_slot[0], NET_DISC_NORMAL);
        NetTransport st = NetLoopback_End(&g_lb[0], 1);
        Check(NetDriver_Server(st, 9u, &st), "the server end made for a plain client");
        g_slot[0] = NetServer_AddClient(&g_srv, st, g_now);
        Run(1500.0);
        Check(!g_srv.c[g_slot[0]].used && NetServer_PlayerCount(&g_srv) == 0, "the plain client is refused: its Hello is not the driver's, the link ends, the place is free again");
        Check(g_cl[0].state == NETC_HELLO || g_cl[0].state == NETC_CLOSED, "and it never got to play");
        for (int k = 0; k < g_nClients; ++k) NetClient_Leave(&g_cl[k]);
    }

    printf("\n%s: %d failure(s)\n", g_failures ? "FAILED" : "all passed", g_failures);
    return g_failures ? 1 : 0;
}

#endif
