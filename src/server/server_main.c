// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "The server program"
// ============================================================================
// The dedicated server: one floor, up to four players, over UDP. Built only on purpose (scripts/build_server.sh defines
// TOWER_SERVER_MAIN), because mray and the Android build compile every .c file and the game has a main() of its own.
//   ./tower_server [--port 7777] [--seed 12345] [--floor 1] [--max-seconds 0] [--loss 0] [--quiet]
#ifdef TOWER_SERVER_MAIN

#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../net/net_server.h"
#include "../net/net_udp.h"

// The player controller draws debug shapes and asks the props where they are; a server does neither. NO PROPS YET: the game's props are
// a list built while the floor is drawn, and the simulation that owns them has not been moved out of main.c (docs/net.md).
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static volatile sig_atomic_t g_stop = 0;
static void OnSignal(int sig) { (void)sig; g_stop = 1; }

static NetServer g_srv;
static NetUdpHost g_host;

static void SleepMs(double ms) { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (long)(ms * 1e6); nanosleep(&ts, NULL); }

int main(int argc, char **argv)
{
    unsigned port = 7777, seed = 12345;
    int floorNumber = 1, quiet = 0;
    double maxSeconds = 0.0, lossPercent = 0.0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--floor") && i + 1 < argc) floorNumber = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-seconds") && i + 1 < argc) maxSeconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--loss") && i + 1 < argc) lossPercent = atof(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else { fprintf(stderr, "usage: %s [--port N] [--seed N] [--floor N] [--max-seconds N] [--loss PERCENT] [--quiet]\n", argv[0]); return 2; }
    }
    if (port > 65535 || floorNumber < 1) { fprintf(stderr, "bad port or floor\n"); return 2; }
    if (lossPercent > 0.0) NetUdp_SetDebug((float)(lossPercent / 100.0), 0.0f);

    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
    NetServer_Init(&g_srv, seed, floorNumber);
    if (!NetUdpHost_Listen(&g_host, (uint16_t)port)) { fprintf(stderr, "tower_server: cannot listen on UDP port %u (is another server using it?)\n", port); return 2; }
    printf("tower_server: floor %d, seed %u, listening on UDP port %u, %d players at most\n", floorNumber, seed, (unsigned)NetUdpHost_Port(&g_host), NET_MAX_PLAYERS);
    fflush(stdout);

    const double tickMs = 1000.0 / (double)NET_TICK_RATE;
    const double start = NetUdp_NowMs();
    double nextTick = start + tickMs, nextStatus = start + 5000.0;
    int peak = 0;
    while (!g_stop) {
        const double now = NetUdp_NowMs();
        NetTransport tp;
        while (NetUdpHost_Accept(&g_host, &tp)) {
            const int slot = NetServer_AddClient(&g_srv, tp, now);
            if (!quiet) printf("  a connection arrived: %s\n", slot >= 0 ? "given a place" : "refused, the server is full");
        }
        NetServer_Poll(&g_srv, now);
        int ran = 0;
        while (now >= nextTick && ran < 5) { NetServer_Tick(&g_srv, now); nextTick += tickMs; ++ran; }
        if (now >= nextTick) nextTick = now + tickMs;                                     // far behind: do not try to catch up
        const int players = NetServer_PlayerCount(&g_srv);
        if (players > peak) peak = players;
        if (!quiet && now >= nextStatus) {
            nextStatus += 5000.0;
            printf("  tick %u, %d player%s, %u snapshots sent\n", g_srv.tick, players, players == 1 ? "" : "s", g_srv.snapshotsSent);
            fflush(stdout);
        }
        if (maxSeconds > 0.0 && now - start >= maxSeconds * 1000.0) break;
        SleepMs(1.0);
    }
    printf("tower_server: stopped after %u ticks; peak_players=%d; snapshots_sent=%u\n", g_srv.tick, peak, g_srv.snapshotsSent);
    NetUdpHost_Close(&g_host);
    return 0;
}
#endif
