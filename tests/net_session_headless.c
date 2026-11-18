// Headless test of NetSession: real games over real UDP sockets on this machine's loopback address, in real time. A session hosts (the shared
// host module with the host playing as a client of its own game), others join by address, the lobby's announcement is read back by a real
// searcher, a fifth player is turned away, players leave, the host leaves, and the failures say why. No window, no raylib calls. Takes a few
// seconds (the clock is the real one). Run from the project root:
//   cc -O2 -std=c99 -DNET_SESSION_HEADLESS_TEST -Isrc -Iraylib/src tests/net_session_headless.c src/net/net_session.c src/net/net_host.c src/net/net_dial.c src/net/net_discovery.c src/net/net_mid.c src/net/net_wire.c src/net/net_proto.c src/net/net_reliable.c src/net/net_udp.c src/net/net_move_sim.c src/net/net_server.c src/net/net_client.c src/net/net_play.c src/input/input_latch.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o net_session_headless
#ifdef NET_SESSION_HEADLESS_TEST

#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "net/net_play.h"
#include "net/net_session.h"
#include "platform/platform.h"

// The player controller draws debug shapes and asks the props where they are; a session does neither (as in the server program).
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

static void SleepMs(int ms) { struct timespec ts = { 0, ms * 1000 * 1000 }; nanosleep(&ts, NULL); }

// Runs the sessions (all of them, every 2 ms) until done() says so or ms of real time has passed. Returns whether done() became true.
typedef bool (*Done)(void *ctx);
static NetSession *g_set[8];
static int g_setN = 0;
static bool Pump(int ms, Done done, void *ctx)
{
    const double start = NetUdp_NowMs();
    while (NetUdp_NowMs() - start < (double)ms) {
        const double now = NetUdp_NowMs();
        for (int i = 0; i < g_setN; ++i) NetSession_Update(g_set[i], now);
        if (done && done(ctx)) return true;
        SleepMs(2);
    }
    return done ? done(ctx) : false;
}
static void Watch(NetSession *a, NetSession *b, NetSession *c, NetSession *d, NetSession *e)
{
    NetSession *all[] = { a, b, c, d, e };
    g_setN = 0;
    for (int i = 0; i < 5; ++i) if (all[i]) g_set[g_setN++] = all[i];
}

typedef struct { NetSession *s; NetSessionState state; int players; } Want;
static bool WantState(void *ctx) { const Want *w = ctx; return w->s->state == w->state; }
static bool WantPlayers(void *ctx) { const Want *w = ctx; return w->s->state == NETS_PLAYING && NetSession_Players(w->s) == w->players; }
static bool Never(void *ctx) { (void)ctx; return false; }

static NetSession host, a, b, c, extra;

// What a real searcher on this machine is told about the game on `port`: the players it announces, or -1 if nothing was heard (the discovery port can be
// busy on a machine that already runs a server, so a -1 is "not checked", not a failure).
static int Announced(uint16_t port)
{
    NetDiscClient cl;
    if (!NetDiscClient_Open(&cl, NET_DISC_PORT)) return -1;
    int players = -1;
    for (int i = 0; i < 400 && players < 0; ++i) {
        const double now = NetUdp_NowMs();
        for (int k = 0; k < g_setN; ++k) NetSession_Update(g_set[k], now);
        NetDiscClient_Search(&cl, now); NetDiscClient_Poll(&cl, now);
        for (int k = 0; k < NetLobbies_Count(&cl.lobbies); ++k) {
            const NetLobby *l = NetLobbies_Nth(&cl.lobbies, k);
            if (l->info.port == port) players = l->info.players;
        }
        SleepMs(10);
    }
    NetDiscClient_Close(&cl);
    return players;
}

int main(void)
{
    printf("the words\n");
    {
        Check(strcmp(NetSession_ReasonText(NET_DISC_FULL, false), "THE GAME IS FULL") == 0, "FULL: THE GAME IS FULL");
        Check(strcmp(NetSession_ReasonText(NET_DISC_VERSION, false), "OTHER VERSION OF THE GAME") == 0, "VERSION: OTHER VERSION OF THE GAME");
        Check(strcmp(NetSession_ReasonText(NET_DISC_NORMAL, true), "THE HOST LEFT") == 0 && strcmp(NetSession_ReasonText(NET_DISC_NORMAL, false), "THE HOST CLOSED THE GAME") == 0, "NORMAL: the host left (in play) or closed the game (while joining)");
        Check(strcmp(NetSession_ReasonText(NET_DISC_TIMEOUT, true), "CONNECTION LOST") == 0 && strcmp(NetSession_ReasonText(NET_DISC_TIMEOUT, false), "NO ANSWER") == 0, "TIMEOUT: connection lost (in play) or no answer (while joining)");
        Check(strcmp(NetSession_ReasonText(NET_DISC_BAD, false), "THE HOST REFUSED THE CONNECTION") == 0 && strcmp(NetSession_ReasonText(99, false), "DISCONNECTED") == 0, "BAD and anything unknown have words too");
        Check(NetSession_CanHost() == (TOWER_NET_UDP != 0), "a session can host exactly where the platform has UDP");
        NetSession idle; memset(&idle, 0, sizeof idle);
        NetSessionSlot slots[NET_MAX_PLAYERS];
        NetSession_Update(&idle, 100.0); NetSession_Leave(&idle);
        Check(idle.state == NETS_IDLE && NetSession_Slots(&idle, slots) == 0 && NetSession_Players(&idle) == 0, "an idle session: updating or leaving it does nothing, and nobody is in it");
    }

#if !TOWER_NET_UDP
    printf("  (no UDP on this platform: the network cases are skipped)\n");
#else
    printf("\nfailures that need no network\n");
    {
        NetSession f;
        const bool ok = NetSession_Join(&f, 7, "127.0.0.1", 9, "x", "steve", NetUdp_NowMs());
        Check(!ok && f.state == NETS_FAILED && strcmp(f.error, "UNKNOWN WAY TO CONNECT") == 0, "joining over a way that does not exist fails at once, with the reason");
        NetSession_Leave(&f);
        Check(f.state == NETS_IDLE && f.error[0] == 0, "leaving a failed session makes it idle and clears the reason");
#if !TOWER_MIDNET
        const bool q = NetSession_Join(&f, NET_TRANSPORT_QUIC, "127.0.0.1", 9, "x", "steve", NetUdp_NowMs());
        Check(!q && f.state == NETS_FAILED && strcmp(f.error, "THIS GAME HAS NO QUIC") == 0, "joining a QUIC game in a build without QUIC fails at once, with the reason");
        NetSession_Leave(&f);
        NetSession h;
        Check(!NetSession_Host(&h, NET_TRANSPORT_QUIC, "X", "steve", 1, NetUdp_NowMs()) && h.state == NETS_FAILED, "hosting over QUIC in a build without QUIC fails, and nothing is left open");
        NetSession_Leave(&h);
#endif
    }

    printf("\nhosting\n");
    {
        const double t0 = NetUdp_NowMs();
        const bool ok = NetSession_Host(&host, NET_TRANSPORT_UDP, "STEVE'S TOWER", "steve", 4242, t0);
        Check(ok && host.state == NETS_CONNECTING && host.hosting, "hosting opens the game and starts joining it (the host plays as one more client)");
        Check(host.host.open && host.host.port != 0 && strcmp(host.name, "STEVE'S TOWER") == 0, "the host is open on a port, under the name given");
        Watch(&host, NULL, NULL, NULL, NULL);
        Want w = { &host, NETS_PLAYING, 1 };
        Check(Pump(3000, WantState, &w) && host.client.state == NETC_PLAYING, "the host's own client is welcomed: the session is PLAYING");
        Check(host.client.floorSeed == 4242 && host.client.id == 0, "it plays on the floor of the seed given, as the first player (id 0)");
        NetSessionSlot s[NET_MAX_PLAYERS];
        const int n = NetSession_Slots(&host, s);
        Check(n == 1 && s[0].used && s[0].you && s[0].host && s[0].id == 0 && strcmp(s[0].character, "steve") == 0, "the host sees one player: itself, the host, with its character");
        Check(NetSession_Players(&host) == 1, "one player");

        NetDiscClient cl;
        if (NetDiscClient_Open(&cl, NET_DISC_PORT) && host.host.discoveryOpen) {
            double now = NetUdp_NowMs();
            int found = 0;
            for (int i = 0; i < 100 && !found; ++i) {
                NetSession_Update(&host, NetUdp_NowMs());
                NetDiscClient_Search(&cl, now); NetDiscClient_Poll(&cl, now);
                for (int k = 0; k < NetLobbies_Count(&cl.lobbies); ++k) {
                    const NetLobby *l = NetLobbies_Nth(&cl.lobbies, k);
                    if (l->info.port == host.host.port && strcmp(l->info.name, "STEVE'S TOWER") == 0) found = 1;
                }
                SleepMs(10); now = NetUdp_NowMs();
            }
            Check(found, "a real searcher on this machine finds the game by its name and port");
            NetDiscClient_Close(&cl);
            const int announced = Announced(host.host.port);
            Check(announced == 1, "and is told one player (the host, playing in its own game)");
        } else printf("  (the discovery port is busy on this machine: the announcement is not checked)\n");
    }

    printf("\njoining\n");
    {
        const bool ok = NetSession_Join(&a, NET_TRANSPORT_UDP, "127.0.0.1", host.host.port, "STEVE'S TOWER", "bob", NetUdp_NowMs());
        Check(ok && a.state == NETS_CONNECTING && !a.hosting, "joining returns at once, connecting");
        Watch(&host, &a, NULL, NULL, NULL);
        Want wa = { &a, NETS_PLAYING, 2 }, wh = { &host, NETS_PLAYING, 2 };
        Check(Pump(3000, WantPlayers, &wa), "the joiner is welcomed and sees two players");
        Check(Pump(3000, WantPlayers, &wh), "the host sees two players");
        NetSessionSlot s[NET_MAX_PLAYERS];
        int n = NetSession_Slots(&host, s);
        Check(n == 2 && s[0].id == 0 && s[0].you && s[0].host && s[1].id == 1 && !s[1].you && !s[1].host && strcmp(s[1].character, "bob") == 0, "the host's list: itself (id 0, host), then bob (id 1) with his character");
        n = NetSession_Slots(&a, s);
        Check(n == 2 && s[0].id == 0 && !s[0].you && s[0].host && s[1].id == 1 && s[1].you && !s[1].host && strcmp(s[1].character, "bob") == 0, "the joiner's list: the host (id 0), then itself (id 1)");
        Check(a.client.floorSeed == 4242, "the joiner plays on the host's floor");
        if (host.host.discoveryOpen) {
            Watch(&host, &a, NULL, NULL, NULL);
            const int announced = Announced(host.host.port);
            Check(announced == 2, "a searcher is now told two players: the announcement follows the game");
        }
    }

    printf("\nwalking together\n");
    {
        static NetPlay hostPlay, joinPlay;
        NetPlay_Init(&hostPlay, 0.0f); NetPlay_Init(&joinPlay, 0.0f);
        NetSession_SetCommands(&host, NetPlay_CmdFn, &hostPlay);
        NetSession_SetCommands(&a, NetPlay_CmdFn, &joinPlay);
        Watch(&host, &a, NULL, NULL, NULL);
        const Vector3 h0 = host.client.player.position, j0 = a.client.player.position;
        InputFrame fwd; memset(&fwd, 0, sizeof fwd);
        fwd.move = (Vector2){ 0.0f, -1.0f };
        InputFrame none; memset(&none, 0, sizeof none);
        const double start = NetUdp_NowMs();
        while (NetUdp_NowMs() - start < 2000.0) {                       // 2 s of the host's stick forward, the joiner standing, a 60 Hz screen
            NetPlay_Input(&hostPlay, &fwd, 90.0f); NetPlay_Input(&joinPlay, &none, 90.0f);
            const double now = NetUdp_NowMs();
            NetSession_Update(&host, now); NetSession_Update(&a, now);
            SleepMs(16);
        }
        const Vector3 h1 = host.client.player.position, j1 = a.client.player.position;
        Check(hypotf(h1.x - h0.x, h1.z - h0.z) > 3.0f, "the host walked: its commands came from NetSession_SetCommands, not from standing still");
        Check(hypotf(j1.x - j0.x, j1.z - j0.z) < 0.05f, "the joiner, who pushed nothing, stayed");
        Vector3 seen; float yaw;
        const bool sees = NetClient_RemoteView(&a.client, 0, NetUdp_NowMs(), &seen, &yaw);
        Check(sees && hypotf(seen.x - h0.x, seen.z - h0.z) > 2.0f && hypotf(seen.x - h1.x, seen.z - h1.z) < 2.0f, "and the joiner sees the host over there, where it walked to, through a real socket");
        const bool seesJ = NetClient_RemoteView(&host.client, 1, NetUdp_NowMs(), &seen, &yaw);
        Check(seesJ && hypotf(seen.x - j1.x, seen.z - j1.z) < 0.3f, "and the host sees the joiner where it stands");
        Check(host.client.corrections < 3 && a.client.corrections < 3, "with next to no corrections: what each side guessed was what the server did");
        NetSession_SetCommands(&host, NULL, NULL);
        NetSession_SetCommands(&a, NULL, NULL);
        const Vector3 hs = host.client.player.position;
        const double st2 = NetUdp_NowMs();
        while (NetUdp_NowMs() - st2 < 1200.0) { const double now = NetUdp_NowMs(); NetSession_Update(&host, now); NetSession_Update(&a, now); SleepMs(16); }
        Check(hypotf(host.client.player.position.x - hs.x, host.client.player.position.z - hs.z) < 1.5f, "with no one naming the commands again they are stand-still: the player slows to a stop (the default is kept)");
    }

    printf("\nfilling up, being turned away, leaving\n");
    {
        NetSession_Join(&b, NET_TRANSPORT_UDP, "127.0.0.1", host.host.port, "X", "cy", NetUdp_NowMs());
        NetSession_Join(&c, NET_TRANSPORT_UDP, "127.0.0.1", host.host.port, "X", "dee", NetUdp_NowMs());
        Watch(&host, &a, &b, &c, NULL);
        Want w4 = { &host, NETS_PLAYING, 4 };
        Check(Pump(4000, WantPlayers, &w4), "four players are in the game");
        NetSession_Join(&extra, NET_TRANSPORT_UDP, "127.0.0.1", host.host.port, "X", "eve", NetUdp_NowMs());
        Watch(&host, &a, &b, &c, &extra);
        Want we = { &extra, NETS_FAILED, 0 };
        Check(Pump(4000, WantState, &we) && strcmp(extra.error, "THE GAME IS FULL") == 0, "a fifth player is turned away: FAILED, THE GAME IS FULL");
        Check(NetSession_Players(&host) == 4 && host.host.refused >= 1, "and the game is as it was (four players, one turned away)");
        NetSession_Leave(&extra);

        NetSession_Leave(&c);
        Check(c.state == NETS_IDLE, "a player who leaves is idle");
        Watch(&host, &a, &b, NULL, NULL);
        Want w3 = { &host, NETS_PLAYING, 3 };
        Check(Pump(4000, WantPlayers, &w3), "and the host sees three players again");
        NetSession_Leave(&b);
        Want w2 = { &a, NETS_PLAYING, 2 };
        Watch(&host, &a, NULL, NULL, NULL);
        Check(Pump(4000, WantPlayers, &w2), "another leaves: the first joiner sees two (the host and itself)");
    }

    printf("\nthe host leaves\n");
    {
        NetSession_Leave(&host);
        Check(host.state == NETS_IDLE && !host.host.open, "the host's session is idle and its socket is closed");
        Watch(&a, NULL, NULL, NULL, NULL);
        Want w = { &a, NETS_ENDED, 0 };
        Check(Pump(4000, WantState, &w), "the joiner finds out: the session ENDED");
        Check(strcmp(a.error, "THE HOST LEFT") == 0, "and is told why: THE HOST LEFT");
        NetSession_Leave(&a);
        Check(a.state == NETS_IDLE, "leaving it makes it idle");
    }

    printf("\njoining nobody\n");
    {
        NetSession_Join(&a, NET_TRANSPORT_UDP, "127.0.0.1", 9, "NOBODY", "bob", NetUdp_NowMs());
        a.connectTimeoutMs = 500.0;
        Watch(&a, NULL, NULL, NULL, NULL);
        Want w = { &a, NETS_FAILED, 0 };
        Check(Pump(5000, WantState, &w) && strcmp(a.error, "NO ANSWER") == 0, "a join that nobody answers fails after the timeout: NO ANSWER");
        NetSession_Leave(&a); NetSession_Leave(&a);
        Check(a.state == NETS_IDLE, "leaving twice is harmless");
        NetSession_Join(&a, NET_TRANSPORT_UDP, "127.0.0.1", 9, "NOBODY", "bob", NetUdp_NowMs());
        NetSession_Leave(&a);
        Check(a.state == NETS_IDLE, "cancelling a join that is still connecting works");
    }

    printf("\ntwo hosts on one machine\n");
    {
        NetSession h1, h2;
        Check(NetSession_Host(&h1, NET_TRANSPORT_UDP, "ONE", "steve", 1, NetUdp_NowMs()) && NetSession_Host(&h2, NET_TRANSPORT_UDP, "TWO", "bob", 2, NetUdp_NowMs()), "a second game can be hosted while the first is open");
        Check(h1.host.port != h2.host.port, "on a different port (the default is taken, so a free one is used)");
        Watch(&h1, &h2, NULL, NULL, NULL);
        Want w1 = { &h1, NETS_PLAYING, 1 }, w2 = { &h2, NETS_PLAYING, 1 };
        Check(Pump(3000, WantPlayers, &w1) && Pump(3000, WantPlayers, &w2), "both are playing, each with its own floor seed");
        Check(h1.client.floorSeed == 1 && h2.client.floorSeed == 2, "and each player is on the floor of their own game");
        NetSession_Leave(&h1); NetSession_Leave(&h2);
    }

    (void)Never;
#endif

    printf("\n%s\n", g_failures == 0 ? "all passed" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
#endif
