// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "The server program"
// ============================================================================
// A player with no screen: connects to a server, walks a fixed course for a while, says how it went. For trying a server by hand and as the
// other half of tests/server_smoke.sh. Built only on purpose (scripts/build_server.sh defines TOWER_BOT_MAIN).
//   ./tower_bot [host] [port] [--seconds 10] [--name steve] [--loss 0] [--quiet]
// Exit code: 0 it was playing at the end and ran about the right number of ticks without once being put back from scratch; 1 it was not; 2 bad use.
#ifdef TOWER_BOT_MAIN

#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../net/net_client.h"
#include "../net/net_udp.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static NetClient g_cl;
static NetUdpHost g_host;

static void SleepMs(double ms) { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (long)(ms * 1e6); nanosleep(&ts, NULL); }

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
    const char *host = "127.0.0.1", *name = "steve";
    unsigned port = 7777;
    double seconds = 10.0, lossPercent = 0.0;
    int quiet = 0, positional = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--loss") && i + 1 < argc) lossPercent = atof(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (argv[i][0] != '-' && positional == 0) { host = argv[i]; ++positional; }
        else if (argv[i][0] != '-' && positional == 1) { port = (unsigned)atoi(argv[i]); ++positional; }
        else { fprintf(stderr, "usage: %s [host] [port] [--seconds N] [--name steve] [--loss PERCENT] [--quiet]\n", argv[0]); return 2; }
    }
    if (port == 0 || port > 65535) { fprintf(stderr, "bad port\n"); return 2; }
    if (lossPercent > 0.0) NetUdp_SetDebug((float)(lossPercent / 100.0), 0.0f);
    NetTransport tp;
    if (!NetUdpHost_Connect(&g_host, host, (uint16_t)port, &tp)) { fprintf(stderr, "tower_bot: cannot reach %s:%u\n", host, port); return 2; }
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
    for (int i = 0; i < 20; ++i) { NetUdpHost_Pump(&g_host); SleepMs(5.0); }            // let the goodbye go out
    NetUdpHost_Close(&g_host);
    return ok ? 0 : 1;
}
#endif
