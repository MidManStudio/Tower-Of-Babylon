// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "The server program"
// ============================================================================
// A player with no screen: connects to a server, walks a fixed course for a while, says how it went. For trying a server by hand and as the
// other half of tests/server_smoke.sh. Built only on purpose (scripts/build_server.sh defines TOWER_BOT_MAIN).
//   ./tower_bot [host] [port] [--seconds 10] [--name steve] [--loss 0] [--quiet] [--transport udp|quic]
//   ./tower_bot --find [--wait 5] [--discovery-port 7778] [--probe IP]     look for a game on this network and join the first one that can be joined
//   ./tower_bot --list [--wait 5] [--discovery-port 7778] [--probe IP]     only list what answers (net/net_discovery.h), then stop
// --probe asks one address directly, for a host on another subnet or where broadcasts are filtered.
// Exit code: 0 it was playing at the end and ran about the right number of ticks without once being put back from scratch (with --list: something answered); 1 it was not; 2 bad use; 3 nothing found to join.
#ifdef TOWER_BOT_MAIN

#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../net/net_client.h"
#include "../net/net_dial.h"
#include "../net/net_discovery.h"
#include "../net/net_mid.h"
#include "../net/net_udp.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static NetClient g_cl;
static NetDial g_dial;

static void SleepMs(double ms) { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (long)(ms * 1e6); nanosleep(&ts, NULL); }

// Asks the network for games for up to waitSeconds (or until one that can be joined has answered, with stopAtFirst). Returns how many were heard.
static int Search(NetDiscClient *cl, unsigned discPort, double waitSeconds, const char *probeIp, int stopAtFirst)
{
    if (!NetDiscClient_Open(cl, (uint16_t)discPort)) { fprintf(stderr, "tower_bot: cannot open a UDP socket to search with\n"); return -1; }
    const double start = NetUdp_NowMs();
    double nextProbe = start;
    while (NetUdp_NowMs() - start < waitSeconds * 1000.0) {
        const double now = NetUdp_NowMs();
        NetDiscClient_Search(cl, now);
        if (probeIp && now >= nextProbe) {
            if (!NetDiscClient_Probe(cl, probeIp, (uint16_t)discPort) && nextProbe == start) fprintf(stderr, "tower_bot: cannot send a question to %s\n", probeIp);
            nextProbe = now + NET_DISC_INTERVAL_MS;
        }
        NetDiscClient_Poll(cl, now);
        if (stopAtFirst && NetLobbies_FirstJoinable(&cl->lobbies)) break;
        SleepMs(5.0);
    }
    return NetLobbies_Count(&cl->lobbies);
}

static NetCmd Course(void *user, uint32_t t)                         // walk, sprint, strafe, jump, turning all the while
{
    (void)user;
    const int phase = (int)((t / 40u) % 4u);
    uint8_t b = 0; float mx = 0.0f;
    if (phase == 1) b |= NET_BTN_SPRINT;
    if (phase == 2) mx = 0.6f;
    if (phase == 3 && t % 40u == 10u) b |= NET_BTN_JUMP;
    return NetCmd_Make(mx, -1.0f, 0.02f * (float)t, 0.0f, b);
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1", *name = "steve", *probeIp = NULL;
    unsigned port = 7777, discPort = NET_DISC_PORT;
    double seconds = 10.0, lossPercent = 0.0, waitSeconds = 5.0;
    int quiet = 0, positional = 0, find = 0, list = 0, useQuic = -1;
    char foundHost[16];
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--loss") && i + 1 < argc) lossPercent = atof(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--find")) find = 1;
        else if (!strcmp(argv[i], "--transport") && i + 1 < argc) { ++i; useQuic = !strcmp(argv[i], "quic") ? 1 : !strcmp(argv[i], "udp") ? 0 : -2; }
        else if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "--wait") && i + 1 < argc) waitSeconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--discovery-port") && i + 1 < argc) discPort = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc) probeIp = argv[++i];
        else if (argv[i][0] != '-' && positional == 0) { host = argv[i]; ++positional; }
        else if (argv[i][0] != '-' && positional == 1) { port = (unsigned)atoi(argv[i]); ++positional; }
        else { fprintf(stderr, "usage: %s [host] [port] [--seconds N] [--name steve] [--loss PERCENT] [--quiet] [--transport udp|quic]\n       %s --find|--list [--wait N] [--discovery-port N] [--probe IP]\n", argv[0], argv[0]); return 2; }
    }
    if (port == 0 || port > 65535 || discPort == 0 || discPort > 65535 || waitSeconds <= 0.0) { fprintf(stderr, "bad port or wait\n"); return 2; }
    if ((find || list) && positional) { fprintf(stderr, "give a host and port, or --find/--list, not both\n"); return 2; }
    if (probeIp) { uint32_t pip; if (!NetDisc_ParseIp(probeIp, &pip)) { fprintf(stderr, "--probe wants an address like 192.168.1.5\n"); return 2; } }
    if (find || list) {
        static NetDiscClient cl;
        const int heard = Search(&cl, discPort, waitSeconds, probeIp, find);
        if (heard < 0) return 2;
        for (int i = 0; i < heard; ++i) {
            const NetLobby *l = NetLobbies_Nth(&cl.lobbies, i);
            char s[16];
            NetDisc_FormatIp(l->addr, s);
            printf("  %d. \"%s\" at %s:%u, %d of %d players, %s%s%s\n", i + 1, l->info.name, s, (unsigned)l->info.port, l->info.players, l->info.maxPlayers,
                   l->info.transport == NET_TRANSPORT_QUIC ? "QUIC" : l->info.transport == NET_TRANSPORT_UDP ? "plain UDP" : "an unknown transport",
                   l->compatible ? "" : " (another version of the game)", l->transportSupported ? "" : " (this build cannot connect that way)");
        }
        if (list) { NetDiscClient_Close(&cl); if (heard == 0) { printf("tower_bot: no game found\n"); return 3; } return 0; }
        const NetLobby *pick = NetLobbies_FirstJoinable(&cl.lobbies);
        if (!pick) { printf(heard ? "tower_bot: found %d game%s but none can be joined (full, or another version)\n" : "tower_bot: no game found\n", heard, heard == 1 ? "" : "s"); NetDiscClient_Close(&cl); return 3; }
        NetDisc_FormatIp(pick->addr, foundHost);
        host = foundHost;
        port = pick->info.port;
        useQuic = pick->info.transport == NET_TRANSPORT_QUIC;                                  // the lobby says how to connect
        printf("tower_bot: found \"%s\" at %s:%u, joining\n", pick->info.name, host, port);
        NetDiscClient_Close(&cl);
    }
    if (useQuic == -2) { fprintf(stderr, "--transport is udp or quic\n"); return 2; }
    if (useQuic < 0) useQuic = NetMid_Available() ? 1 : 0;                                    // mid-net's QUIC whenever this build has it
    if (useQuic && !NetMid_Available()) { fprintf(stderr, "tower_bot: this build has no QUIC (it was built without the mid-engine library: see docs/net.md)\n"); return 2; }
    if (lossPercent > 0.0) {
        if (useQuic) fprintf(stderr, "tower_bot: --loss only applies to plain UDP; ignored\n");
        else NetUdp_SetDebug((float)(lossPercent / 100.0), 0.0f);
    }
    NetTransport tp;
    char dialErr[64];
    if (!NetDial_Open(&g_dial, useQuic ? NET_TRANSPORT_QUIC : NET_TRANSPORT_UDP, host, (uint16_t)port, &tp, dialErr, sizeof dialErr)) { fprintf(stderr, "tower_bot: %s (%s:%u)\n", dialErr, host, port); return 2; }
    if (!quiet) printf("tower_bot: connecting to %s:%u over %s, through mid-net's driver\n", host, port, useQuic ? "QUIC (mid-net)" : "plain UDP");
    NetClient_Init(&g_cl, tp, name, NetUdp_NowMs());

    const double start = NetUdp_NowMs();
    double nextStatus = start + 2000.0;
    unsigned ticksAtStart = 0;
    double playingSince = -1.0;
    while (NetUdp_NowMs() - start < seconds * 1000.0 && g_cl.state != NETC_CLOSED) {
        const double now = NetUdp_NowMs();
        NetClient_Poll(&g_cl, now);
        NetClient_Update(&g_cl, now, Course, NULL);
        if (g_cl.state == NETC_PLAYING && playingSince < 0.0) { playingSince = now; ticksAtStart = g_cl.ticksRun; if (!quiet) printf("tower_bot: playing as player %d, round trip %.1f ms\n", g_cl.id, g_cl.rttMs); }
        if (!quiet && now >= nextStatus) {
            nextStatus += 2000.0;
            printf("  position %.1f %.1f, %u corrections, lead %d ticks, %d others in view\n", g_cl.player.position.x, g_cl.player.position.z, g_cl.corrections, (int)g_cl.tick - (int)g_cl.lastServerTick, g_cl.snapCount ? g_cl.snaps[0].n : 0);
            fflush(stdout);
        }
        SleepMs(1.0);
    }
    const double played = playingSince < 0.0 ? 0.0 : (NetUdp_NowMs() - playingSince) / 1000.0;
    const unsigned ticks = g_cl.ticksRun - ticksAtStart;
    const int ok = g_cl.state == NETC_PLAYING && played > 0.5 && (double)ticks >= played * (double)NET_TICK_RATE * 0.8 && g_cl.hardSnaps == 0;
    printf("tower_bot: %s; played %.1f s, %u ticks, %u corrections, %u hard snaps, state %d\n", ok ? "ok" : "NOT ok", played, ticks, g_cl.corrections, g_cl.hardSnaps, (int)g_cl.state);
    NetClient_Leave(&g_cl);
    NetDial_Close(&g_dial, 150);                                                                  // lets the goodbye go out, then closes the socket
    return ok ? 0 : 1;
}
#endif
