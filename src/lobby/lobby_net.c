// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "lobby_net.h and lobby_net.c"
// ============================================================================
#include "lobby_net.h"

#include <stdio.h>
#include <string.h>

#include "../platform/platform.h"

static NetDiscClient g_client;
static bool          g_open = false;
static uint16_t      g_port = NET_DISC_PORT;

void LobbyNet_UsePort(uint16_t discoveryPort) { g_port = discoveryPort; }

bool LobbyNet_EntryFromLobby(const NetLobby *n, LobbyEntry *out)
{
    memset(out, 0, sizeof *out);
    if (!n || !n->used) return false;
    snprintf(out->name, sizeof out->name, "%s", n->info.name);
    out->players = n->info.players;
    out->maxPlayers = n->info.maxPlayers;
    const char *transport = n->info.transport == NET_TRANSPORT_UDP ? "UDP" : n->info.transport == NET_TRANSPORT_QUIC ? "QUIC" : "?";
    snprintf(out->transport, sizeof out->transport, "%s", transport);
    const char *why = NULL;
    if (!n->compatible)                                   why = "OTHER VERSION";
    else if (!n->transportSupported)                      why = n->info.transport == NET_TRANSPORT_QUIC ? "NEEDS QUIC" : "CANNOT CONNECT";
    else if (n->info.players >= n->info.maxPlayers)       why = "FULL";
    if (why) snprintf(out->why, sizeof out->why, "%s", why);
    out->joinable = why == NULL;
    return true;
}

static bool NetStart(void *user)
{
    (void)user;
    if (g_open) return true;
    memset(&g_client, 0, sizeof g_client);
    g_open = NetDiscClient_Open(&g_client, g_port);
    return g_open;
}

static void NetStop(void *user)
{
    (void)user;
    if (g_open) NetDiscClient_Close(&g_client);
    g_open = false;
}

static void NetUpdate(void *user, double nowMs)
{
    (void)user;
    if (!g_open) return;
    NetDiscClient_Search(&g_client, nowMs);     // asks when a round is due (the first call always does)
    NetDiscClient_Poll(&g_client, nowMs);       // takes the answers and forgets the games that went quiet
}

static int NetCount(void *user)
{
    (void)user;
    return g_open ? NetLobbies_Count(&g_client.lobbies) : 0;
}

static bool NetEntry(void *user, int index, LobbyEntry *out)
{
    (void)user;
    if (!g_open) return false;
    return LobbyNet_EntryFromLobby(NetLobbies_Nth(&g_client.lobbies, index), out);
}

LobbyBackend LobbyNet_Backend(void)
{
    LobbyBackend b;
    memset(&b, 0, sizeof b);
    b.canSearch = TOWER_NET_UDP != 0;
    b.cannotSearchReason = "A BROWSER CANNOT SEARCH THE NETWORK. USE THE APP.";
    b.searchStart = NetStart;
    b.searchStop = NetStop;
    b.update = NetUpdate;
    b.count = NetCount;
    b.entry = NetEntry;
    return b;
}

void LobbyNet_Shutdown(void) { NetStop(NULL); }
