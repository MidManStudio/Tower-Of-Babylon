// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "The server program"
// ============================================================================
// The dedicated server: one floor, up to four players, over UDP. Built only on purpose (scripts/build_server.sh defines
// TOWER_SERVER_MAIN), because mray and the Android build compile every .c file and the game has a main() of its own.
//   ./tower_server [--port 7777] [--seed 12345] [--floor 1] [--max-seconds 0] [--loss 0] [--quiet]
//                  [--name tower] [--discovery-port 7778] [--no-discovery] [--transport udp|quic]
// --transport: plain UDP, or mid-net's QUIC (net/net_mid.h; only in a build that linked it, and then the default). --loss applies to plain UDP only.
// By default it also answers "who is hosting?" on UDP port 7778 (net/net_discovery.h), so players on the same network find it without typing
// an address, and it prints the addresses it can be reached at.
#ifdef TOWER_SERVER_MAIN

#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../net/net_host.h"

// The player controller draws debug shapes and asks the props where they are; a server does neither. NO PROPS YET: the game's props are
// a list built while the floor is drawn, and the simulation that owns them has not been moved out of main.c (docs/net.md).
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static volatile sig_atomic_t g_stop = 0;
static void OnSignal(int sig) { (void)sig; g_stop = 1; }

static NetHost g_host;       // the server, its socket and its announcer (net/net_host.h: the same loop the game hosts with)

static void SleepMs(double ms) { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (long)(ms * 1e6); nanosleep(&ts, NULL); }

int main(int argc, char **argv)
{
    unsigned port = 7777, seed = 12345, discPort = NET_DISC_PORT;
    const char *lobbyName = "tower";
    int floorNumber = 1, quiet = 0, discovery = 1, useQuic = -1;
    double maxSeconds = 0.0, lossPercent = 0.0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--floor") && i + 1 < argc) floorNumber = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-seconds") && i + 1 < argc) maxSeconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--loss") && i + 1 < argc) lossPercent = atof(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) lobbyName = argv[++i];
        else if (!strcmp(argv[i], "--discovery-port") && i + 1 < argc) discPort = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-discovery")) discovery = 0;
        else if (!strcmp(argv[i], "--transport") && i + 1 < argc) { ++i; useQuic = !strcmp(argv[i], "quic") ? 1 : !strcmp(argv[i], "udp") ? 0 : -2; }
        else { fprintf(stderr, "usage: %s [--port N] [--seed N] [--floor N] [--max-seconds N] [--loss PERCENT] [--quiet] [--name TEXT] [--discovery-port N] [--no-discovery] [--transport udp|quic]\n", argv[0]); return 2; }
    }
    if (port > 65535 || discPort > 65535 || floorNumber < 1) { fprintf(stderr, "bad port or floor\n"); return 2; }
    if (useQuic == -2) { fprintf(stderr, "--transport is udp or quic\n"); return 2; }
    if (useQuic < 0) useQuic = NetMid_Available() ? 1 : 0;                                    // mid-net's QUIC whenever this build has it
    if (useQuic && !NetMid_Available()) { fprintf(stderr, "tower_server: this build has no QUIC (it was built without the mid-engine library: see docs/net.md)\n"); return 2; }
    if (lossPercent > 0.0) {
        if (useQuic) fprintf(stderr, "tower_server: --loss only applies to plain UDP; ignored\n");
        else NetUdp_SetDebug((float)(lossPercent / 100.0), 0.0f);
    }

    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
    NetHostConfig cfg;
    NetHostConfig_Default(&cfg);
    cfg.seed = seed; cfg.floorNumber = floorNumber; cfg.name = lobbyName; cfg.port = (uint16_t)port; cfg.anyPortIfBusy = false;
    cfg.transport = useQuic ? NET_TRANSPORT_QUIC : NET_TRANSPORT_UDP;
    cfg.discoveryPort = (uint16_t)discPort; cfg.discovery = discovery != 0;
    if (!NetHost_Open(&g_host, &cfg, NetUdp_NowMs())) { fprintf(stderr, "tower_server: %s\n", g_host.error); return 2; }
    const uint16_t gamePort = g_host.port;
    printf("tower_server: floor %d, seed %u, listening on UDP port %u (%s), %d players at most\n", floorNumber, seed, (unsigned)gamePort, useQuic ? "QUIC, mid-net, through its driver" : "plain UDP, through mid-net's driver", NET_MAX_PLAYERS);
    {   // where this machine can be reached, best first (a browser player, who cannot search, types one of these)
        NetDiscIface ifs[16];
        uint32_t addrs[16];
        const int na = NetDisc_RankHostAddresses(ifs, NetDisc_ListInterfaces(ifs, 16), addrs, 16);
        for (int i = 0; i < na; ++i) { char s[16]; NetDisc_FormatIp(addrs[i], s); printf("  address: %s:%u\n", s, (unsigned)gamePort); }
        if (na == 0) printf("  no network address found on this device (is Wi-Fi or the hotspot on?); this device itself can use 127.0.0.1\n");
    }
    if (discovery) {
        if (g_host.discoveryOpen) printf("tower_server: announcing \"%s\" on UDP port %u (players on this network can find it; --no-discovery turns this off)\n", g_host.disc.info.name, (unsigned)NetDiscHost_Port(&g_host.disc));
        else printf("tower_server: discovery is off: UDP port %u could not be opened (is another server announcing on it? give --discovery-port)\n", discPort);
    }
    fflush(stdout);

    const double start = NetUdp_NowMs();
    double nextStatus = start + 5000.0;
    while (!g_stop) {
        const double now = NetUdp_NowMs();
        const unsigned accepted0 = g_host.accepted, refused0 = g_host.refused;
        NetHost_Update(&g_host, now);
        if (!quiet) {
            for (unsigned i = accepted0; i < g_host.accepted; ++i) printf("  a connection arrived: given a place\n");
            for (unsigned i = refused0; i < g_host.refused; ++i) printf("  a connection arrived: refused, the server is full\n");
        }
        if (!quiet && now >= nextStatus) {
            nextStatus += 5000.0;
            const int players = NetHost_Players(&g_host);
            printf("  tick %u, %d player%s, %u snapshots sent\n", g_host.server.tick, players, players == 1 ? "" : "s", g_host.server.snapshotsSent);
            fflush(stdout);
        }
        if (maxSeconds > 0.0 && now - start >= maxSeconds * 1000.0) break;
        SleepMs(1.0);
    }
    if (g_host.discoveryOpen) printf("tower_server: discovery answered %u of %u questions\n", g_host.disc.replies, g_host.disc.received);
    printf("tower_server: stopped after %u ticks; peak_players=%d; snapshots_sent=%u\n", g_host.server.tick, g_host.peak, g_host.server.snapshotsSent);
    NetHost_Close(&g_host, 0);
    return 0;
}
#endif
