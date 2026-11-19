// Headless test of the real network transport: UDP sockets on this machine's loopback address, in real time. A server and clients in
// one process talk through actual packets: the reliable layer over real sockets, the handshake, a few seconds of play with prediction,
// packets lost and doubled on the way out (the transport's own debug switch), a full server, closing, and strangers' packets. No window,
// no raylib calls. It takes about fifteen seconds (the clock is the real one). Run from the project root:
//   cc -O2 -std=c99 -DNET_UDP_HEADLESS_TEST -Isrc -Iraylib/src tests/net_udp_headless.c src/net/net_wire.c src/net/net_proto.c src/net/net_reliable.c src/net/net_udp.c src/net/net_move_sim.c src/net/net_server.c src/net/net_history.c src/net/net_client.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o net_udp_headless
#ifdef NET_UDP_HEADLESS_TEST

#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net/net_client.h"
#include "net/net_proto.h"
#include "net/net_server.h"
#include "net/net_udp.h"

// The player controller's debug drawing and the props, as in net_headless.c.
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

static void SleepMs(double ms) { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (long)(ms * 1e6); nanosleep(&ts, NULL); }

#define TICK_MS (1000.0 / 30.0)
static NetServer g_srv;
static NetUdpHost g_srvHost;
static NetUdpHost g_cliHost[NET_MAX_PLAYERS + 2];
static NetClient g_cl[NET_MAX_PLAYERS + 2];
static double g_nextTick;
static int g_nCli;

static NetCmd Script(void *user, uint32_t t)
{
    const int k = (int)(intptr_t)user;
    const int phase = (int)((t / 40u) % 4u);
    uint8_t b = 0; float my = -1.0f, mx = 0.0f;
    if (phase == 1) b |= NET_BTN_SPRINT;
    if (phase == 2) mx = 0.6f;
    if (phase == 3 && t % 40u == 10u) b |= NET_BTN_JUMP;
    return NetCmd_Make(mx, my, 0.5f * (float)k + 0.02f * (float)t, 0.0f, b);
}

// One pass of everything a program would do in its loop: accept new players, read packets, run the server's ticks, run the clients.
static void Pump(void)
{
    const double now = NetUdp_NowMs();
    NetTransport tp;
    while (NetUdpHost_Accept(&g_srvHost, &tp)) NetServer_AddClient(&g_srv, tp, now);
    NetServer_Poll(&g_srv, now);
    while (now >= g_nextTick) { NetServer_Tick(&g_srv, now); g_nextTick += TICK_MS; if (now - g_nextTick > 200.0) g_nextTick = now + TICK_MS; }
    for (int k = 0; k < g_nCli; ++k) { NetClient_Poll(&g_cl[k], now); NetClient_Update(&g_cl[k], now, Script, (void *)(intptr_t)k); }
}
static void RunFor(double ms) { const double end = NetUdp_NowMs() + ms; while (NetUdp_NowMs() < end) { Pump(); SleepMs(1.0); } }

static void StartServer(void)
{
    NetServer_Init(&g_srv, 12345u, 1);
    memset(&g_srvHost, 0, sizeof g_srvHost);
    NetUdpHost_Listen(&g_srvHost, 0);
    g_nextTick = NetUdp_NowMs() + TICK_MS;
    g_nCli = 0;
}
static bool Join(const char *character)
{
    NetTransport tp;
    if (!NetUdpHost_Connect(&g_cliHost[g_nCli], "127.0.0.1", NetUdpHost_Port(&g_srvHost), &tp)) return false;
    NetClient_Init(&g_cl[g_nCli], tp, character, NetUdp_NowMs());
    ++g_nCli;
    return true;
}
static void StopAll(void)
{
    for (int k = 0; k < g_nCli; ++k) NetUdpHost_Close(&g_cliHost[k]);
    NetUdpHost_Close(&g_srvHost);
    g_nCli = 0;
}

// A stranger: a plain UDP socket that sends whatever it likes to the server.
static int Stranger(void) { return socket(AF_INET, SOCK_DGRAM, 0); }
static void SendTo(int fd, uint16_t port, const void *data, size_t len)
{
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(fd, data, len, 0, (struct sockaddr *)&a, sizeof a);
}

int main(void)
{
    uint8_t rx[NET_MTU];

    printf("two ends of the transport, by themselves\n");
    {
        NetUdpHost srv, cli;
        Check(NetUdpHost_Listen(&srv, 0) && NetUdpHost_Port(&srv) != 0, "a server binds a port of its own choosing");
        NetUdpHost dup;
        Check(!NetUdpHost_Listen(&dup, NetUdpHost_Port(&srv)), "a second server cannot take the same port (no two servers quietly sharing packets)");
        NetTransport ct, st;
        Check(NetUdpHost_Connect(&cli, "127.0.0.1", NetUdpHost_Port(&srv), &ct), "a client sets up a connection to it");
        Check(!NetUdpHost_Accept(&srv, &st), "the server sees nothing until the client sends something reliable");
        const uint8_t hello[5] = { 'h', 'e', 'l', 'l', 'o' };
        Check(ct.sendReliable(ct.self, hello, 5), "the client sends a reliable message");
        double deadline = NetUdp_NowMs() + 1000.0; bool accepted = false;
        while (NetUdp_NowMs() < deadline && !accepted) { accepted = NetUdpHost_Accept(&srv, &st); SleepMs(1.0); }
        Check(accepted && st.recvReliable(st.self, rx, sizeof rx) == 5 && memcmp(rx, hello, 5) == 0, "the server sees a new connection, and the message arrives whole");
        Check(!NetUdpHost_Accept(&srv, &st), "and the connection is announced once");
        Check(st.sendReliable(st.self, (const uint8_t *)"world", 5), "the server answers reliably");
        int n = 0; deadline = NetUdp_NowMs() + 1000.0;
        while (NetUdp_NowMs() < deadline && n <= 0) { n = ct.recvReliable(ct.self, rx, sizeof rx); SleepMs(1.0); }
        Check(n == 5 && memcmp(rx, "world", 5) == 0, "and it comes back");
        const uint8_t dg[3] = { 1, 2, 3 };
        Check(ct.sendDatagram(ct.self, dg, 3) && st.sendDatagram(st.self, dg, 3), "datagrams go both ways");
        int a = 0, b = 0; deadline = NetUdp_NowMs() + 1000.0;
        while (NetUdp_NowMs() < deadline && (a <= 0 || b <= 0)) { if (a <= 0) a = st.recvDatagram(st.self, rx, sizeof rx); if (b <= 0) b = ct.recvDatagram(ct.self, rx, sizeof rx); SleepMs(1.0); }
        Check(a == 3 && b == 3, "and arrive");
        uint8_t big[NET_MTU + 1] = { 0 };
        Check(!ct.sendDatagram(ct.self, big, NET_MTU + 1) && !ct.sendDatagram(ct.self, big, 0) && ct.sendDatagram(ct.self, big, NET_MTU), "a datagram over the limit, or empty, is refused; one of the full size goes");
        Check(!ct.sendReliable(ct.self, big, NET_REL_MAX_MSG + 1), "a reliable message over its size is refused");
        // More datagrams than the connection keeps, with nobody reading: the oldest are kept, the rest counted and dropped.
        SleepMs(50.0);
        while (st.recvDatagram(st.self, rx, sizeof rx) > 0) { }                                  // what is left from before
        for (int i = 0; i < 100; ++i) { uint8_t d[2] = { 7, (uint8_t)i }; ct.sendDatagram(ct.self, d, 2); }
        NetUdp_NowMs(); SleepMs(100.0);
        NetUdpHost_Pump(&srv);
        NetUdpPeer *sp = (NetUdpPeer *)st.self;
        Check(sp->inCount == NET_UDP_INBOX && sp->inboxDrops >= 60, "100 datagrams at a connection nobody is reading: it keeps no more than its limit and counts the rest");
        int first = -1, count = 0; uint8_t d2[16];
        while (st.recvDatagram(st.self, d2, sizeof d2) > 0) { if (first < 0) first = d2[1]; ++count; }
        Check(count == NET_UDP_INBOX && first == 0, "and what it kept is the first ones, in order");
        NetUdpHost_Close(&cli); NetUdpHost_Close(&srv);
    }

    printf("\na server and two clients, a few seconds of play\n");
    {
        StartServer(); Join("steve"); Join("brou");
        RunFor(800.0);
        Check(g_cl[0].state == NETC_PLAYING && g_cl[1].state == NETC_PLAYING && NetServer_PlayerCount(&g_srv) == 2, "two clients say Hello over real UDP, are welcomed, and are playing within a second");
        Check(g_cl[0].id != g_cl[1].id && g_cl[0].floorSeed == 12345u, "each has its own id, and the floor's seed");
        printf("    (the round trip to Hello and Welcome: %.1f ms)\n", g_cl[0].rttMs);
        RunFor(4000.0);
        const NetServerClient *sc = &g_srv.c[g_cl[0].id];
        printf("    (%u client ticks, %u server ticks run for player 0, %u corrections, %u hard snaps; lead %d ticks)\n", g_cl[0].ticksRun, sc->ticksFed + sc->ticksRepeated, g_cl[0].corrections, g_cl[0].hardSnaps, (int)g_cl[0].tick - (int)g_srv.tick);
        Check(g_cl[0].ticksRun > 120 && g_cl[0].ticksRun < 170, "the client ran its ticks at about 30 a second (in real time)");
        Check(g_cl[0].hardSnaps == 0 && g_cl[1].hardSnaps == 0, "never put back from scratch");
        Check(sc->ticksRepeated * 20 < sc->ticksFed + 1, "almost every command was there when the server needed it");
        Check(g_cl[0].snaps[0].n == 1 && g_cl[1].snaps[0].n == 1, "each player is told about the other");
        const PlayerController *p = &g_srv.c[g_cl[0].id].player;
        Check(hypotf(p->position.x - g_srv.map.startX, p->position.z - g_srv.map.startZ) > 2.0f, "and the player really moved on the server");
        StopAll();
    }

    printf("\nreal packets lost and doubled\n");
    {
        NetUdp_SetDebug(0.15f, 0.05f);                                   // every packet any end sends: 15% never leaves, 5% leave twice
        StartServer(); Join("steve"); Join("brou");
        RunFor(2500.0);
        const bool joined = g_cl[0].state == NETC_PLAYING && g_cl[1].state == NETC_PLAYING;
        const unsigned c0 = g_cl[0].corrections, h0 = g_cl[0].hardSnaps;
        const NetServerClient *sc = &g_srv.c[g_cl[0].id];
        const unsigned fed0 = sc->ticksFed, rep0 = sc->ticksRepeated;
        RunFor(4000.0);
        const unsigned fed = sc->ticksFed - fed0, rep = sc->ticksRepeated - rep0;
        printf("    (at 15%% loss: %u of %u ticks without a command in time; %u corrections; hard snaps %u; Hello took until the clients were playing: %s)\n", rep, fed + rep, g_cl[0].corrections - c0, g_cl[0].hardSnaps - h0, joined ? "yes" : "no");
        Check(joined, "the Hello and Welcome (reliable) get through 15% loss in both directions and the clients are playing");
        Check(g_cl[0].state == NETC_PLAYING && g_cl[0].hardSnaps == h0, "and keep playing, never put back from scratch");
        Check(fed + rep > 100 && rep * 10 < fed + rep, "with under 10% of the commands missing at their tick");
        NetUdp_SetDebug(0.0f, 0.0f);
        StopAll();
    }

    printf("\nfull, leaving, and a stranger's packets\n");
    {
        StartServer();
        for (int i = 0; i < NET_MAX_PLAYERS; ++i) Join(i % 2 ? "brou" : "steve");
        RunFor(1200.0);
        Check(NetServer_PlayerCount(&g_srv) == NET_MAX_PLAYERS, "four clients fill a four-player server");
        Join("khan");
        RunFor(1200.0);
        Check(g_cl[NET_MAX_PLAYERS].state == NETC_CLOSED && g_cl[NET_MAX_PLAYERS].closedReason == NET_DISC_FULL && NetServer_PlayerCount(&g_srv) == NET_MAX_PLAYERS, "a fifth is told the server is full (reliably, over UDP), and the four go on");

        NetClient_Leave(&g_cl[0]);
        RunFor(600.0);
        Check(!g_srv.c[0].used && NetServer_PlayerCount(&g_srv) == NET_MAX_PLAYERS - 1, "a client that says goodbye is dropped at once");
        RunFor(2600.0);
        int live = 0; for (int i = 0; i < NET_UDP_MAX_PEERS; ++i) if (g_srvHost.peers[i].used) ++live;
        Check(live == NET_MAX_PLAYERS - 1, "and its connection is freed once the goodbye has been delivered (the refused fifth's too)");

        const int fd = Stranger();
        const uint16_t port = NetUdpHost_Port(&g_srvHost);
        const unsigned bad0 = g_srvHost.badPackets;
        const unsigned peers0 = (unsigned)live;
        uint8_t junk[64];
        uint32_t rs = 99u;
        for (int i = 0; i < 200; ++i) { const size_t n = 1 + (size_t)(rs = rs * 1664525u + 1013904223u) % 60; for (size_t j = 0; j < n; ++j) junk[j] = (uint8_t)(rs = rs * 1664525u + 1013904223u); SendTo(fd, port, junk, n); }
        const uint8_t tiny[3] = { 1, 2, 3 };
        SendTo(fd, port, tiny, 3);
        uint8_t dgOnly[9] = { 0x11, 0x22, 0x33, 0x44, 0, 'x', 'y', 'z', 0 };                // a datagram from a connection nobody opened
        SendTo(fd, port, dgOnly, sizeof dgOnly);
        RunFor(300.0);
        int live2 = 0; for (int i = 0; i < NET_UDP_MAX_PEERS; ++i) if (g_srvHost.peers[i].used) ++live2;
        Check(g_srvHost.badPackets >= bad0 + 150 && (unsigned)live2 == peers0 && NetServer_PlayerCount(&g_srv) == NET_MAX_PLAYERS - 1, "200 packets of junk and two that are not a connection: counted and thrown away, no connection made, the game goes on");

        // A packet from a stranger that carries a live connection's id is not that connection's.
        unsigned liveId = 0;
        for (int i = 0; i < NET_UDP_MAX_PEERS; ++i) if (g_srvHost.peers[i].used && !g_srvHost.peers[i].closing) liveId = g_srvHost.peers[i].connId;
        const unsigned badSpoof0 = g_srvHost.badPackets;
        uint8_t spoof[9] = { (uint8_t)liveId, (uint8_t)(liveId >> 8), (uint8_t)(liveId >> 16), (uint8_t)(liveId >> 24), 0, 'S', 'P', 'O', 'F' };
        SendTo(fd, port, spoof, sizeof spoof);
        RunFor(200.0);
        Check(liveId != 0 && g_srvHost.badPackets == badSpoof0 + 1, "a datagram that carries a live connection's id from another address is not taken for that connection's");
        // A reliable packet that is not the first of a connection (sequence number 5) does not open one.
        int liveBefore = 0; for (int i = 0; i < NET_UDP_MAX_PEERS; ++i) if (g_srvHost.peers[i].used) ++liveBefore;
        const uint8_t notFirst[8] = { 0x77, 0x77, 0, 0, 1, 5, 0, 'H' };
        SendTo(fd, port, notFirst, sizeof notFirst);
        RunFor(200.0);
        int liveAfter = 0; for (int i = 0; i < NET_UDP_MAX_PEERS; ++i) if (g_srvHost.peers[i].used) ++liveAfter;
        Check(liveAfter == liveBefore, "a reliable packet that is not the first of a connection does not open one");

        // Strangers opening connections: a reliable first packet from each of many addresses fills the server's connection slots and no more.
        int fds[16];
        for (int i = 0; i < 16; ++i) {
            fds[i] = Stranger();
            uint8_t open[4 + 4] = { (uint8_t)(0x1000 + i), 0x10, 0, 0, 1, 0, 0, 'H' };      // connection id, a reliable data packet, sequence 0, one byte
            SendTo(fds[i], port, open, sizeof open);
        }
        RunFor(400.0);
        int live3 = 0; for (int i = 0; i < NET_UDP_MAX_PEERS; ++i) if (g_srvHost.peers[i].used) ++live3;
        Check(live3 <= NET_UDP_MAX_PEERS && NetServer_PlayerCount(&g_srv) == NET_MAX_PLAYERS - 1, "sixteen strangers each starting a connection: no more than the connection slots are taken, and nobody is let into the game");
        for (int i = 0; i < 16; ++i) close(fds[i]);
        close(fd);
        StopAll();
    }

    printf("\nwhen there is no server\n");
    {
        NetUdpHost cli; NetTransport ct;
        NetUdpHost_Connect(&cli, "127.0.0.1", 9, &ct);                    // port 9: nothing there
        NetClient c; static NetClient dummy; (void)dummy;
        NetClient_Init(&c, ct, "steve", NetUdp_NowMs());
        double t0 = NetUdp_NowMs();
        while (NetUdp_NowMs() < t0 + 6000.0 && c.state == NETC_HELLO) { NetClient_Poll(&c, NetUdp_NowMs()); SleepMs(5.0); }
        Check(c.state == NETC_CLOSED && c.closedReason == NET_DISC_TIMEOUT, "a client with nobody to answer gives up after five seconds and says why");
        NetUdpHost_Close(&cli);
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
