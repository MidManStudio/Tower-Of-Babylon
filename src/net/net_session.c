// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_session.h and net_session.c"
// ============================================================================
#include "net_session.h"

#include <stdio.h>
#include <string.h>

#include "../platform/platform.h"
#include "net_driver.h"

bool NetSession_CanHost(void) { return TOWER_NET_UDP != 0; }

const char *NetSession_ReasonText(uint8_t reason, bool wasPlaying)
{
    switch (reason) {
        case NET_DISC_NORMAL:  return wasPlaying ? "THE HOST LEFT" : "THE HOST CLOSED THE GAME";
        case NET_DISC_VERSION: return "OTHER VERSION OF THE GAME";
        case NET_DISC_FULL:    return "THE GAME IS FULL";
        case NET_DISC_TIMEOUT: return wasPlaying ? "CONNECTION LOST" : "NO ANSWER";
        case NET_DISC_BAD:     return "THE HOST REFUSED THE CONNECTION";
        default:               return "DISCONNECTED";
    }
}

static NetCmd StandStill(void *user, uint32_t tick) { (void)user; (void)tick; return NetCmd_Make(0.0f, 0.0f, 0.0f, 0.0f, 0); }

static void Fail(NetSession *s, const char *why)
{
    snprintf(s->error, sizeof s->error, "%s", why);
    s->state = NETS_FAILED;
}

static void Begin(NetSession *s, bool hosting, const char *name, double nowMs)
{
    memset(s, 0, sizeof *s);
    s->hosting = hosting;
    snprintf(s->name, sizeof s->name, "%s", name ? name : "");
    s->startedMs = nowMs;
    s->connectTimeoutMs = NETS_CONNECT_TIMEOUT_MS;
}

static bool Connect(NetSession *s, int transport, const char *address, uint16_t port, const char *character, double nowMs)
{
    NetTransport tp;
    char err[64];
    if (!NetDial_Open(&s->dial, transport, address, port, &tp, err, sizeof err)) { Fail(s, err[0] ? err : "COULD NOT CONNECT"); return false; }
    NetClient_Init(&s->client, tp, character ? character : "", nowMs);
    s->state = NETS_CONNECTING;
    return true;
}

bool NetSession_Host(NetSession *s, int transport, const char *lobbyName, const char *character, uint32_t floorSeed, double nowMs)
{
    Begin(s, true, lobbyName, nowMs);
    if (NetSession_CanHost() && !NetDriver_Available()) { Fail(s, "THIS BUILD HAS NO MULTIPLAYER LIBRARY"); return false; }
    NetHostConfig cfg;
    NetHostConfig_Default(&cfg);
    cfg.transport = transport; cfg.seed = floorSeed; cfg.name = lobbyName;
    if (!NetHost_Open(&s->host, &cfg, nowMs)) { Fail(s, NetSession_CanHost() ? "COULD NOT OPEN A PORT TO HOST ON" : "A BROWSER CANNOT HOST"); return false; }
    if (!Connect(s, transport, "127.0.0.1", s->host.port, character, nowMs)) { NetHost_Close(&s->host, 0); return false; }
    return true;
}

bool NetSession_Join(NetSession *s, int transport, const char *address, uint16_t port, const char *name, const char *character, double nowMs)
{
    Begin(s, false, name, nowMs);
    return Connect(s, transport, address, port, character, nowMs);
}

// Everything is closed (no waiting): the session ended by itself, so there is nobody left to say goodbye to.
static void Teardown(NetSession *s)
{
    NetClient_Leave(&s->client);                             // a connection that ended by itself still has its transport (the driver's, which holds memory and a thread's worth of state) to free; this says nothing to anyone then
    if (s->hosting) NetHost_Close(&s->host, 0);
    NetDial_Close(&s->dial, 0);
}

void NetSession_SetCommands(NetSession *s, NetCmdFn fn, void *user) { s->cmdFn = fn; s->cmdUser = user; }

void NetSession_Update(NetSession *s, double nowMs)
{
    if (s->state != NETS_CONNECTING && s->state != NETS_PLAYING) return;
    if (s->hosting) NetHost_Update(&s->host, nowMs);
    NetClient_Poll(&s->client, nowMs);
    if (s->cmdFn) NetClient_Update(&s->client, nowMs, s->cmdFn, s->cmdUser);
    else NetClient_Update(&s->client, nowMs, StandStill, NULL);

    if (s->client.state == NETC_PLAYING) {
        if (s->state == NETS_CONNECTING) s->state = NETS_PLAYING;
    } else if (s->client.state == NETC_CLOSED) {
        const bool wasPlaying = s->state == NETS_PLAYING;
        snprintf(s->error, sizeof s->error, "%s", NetSession_ReasonText(s->client.closedReason, wasPlaying));
        s->state = wasPlaying ? NETS_ENDED : NETS_FAILED;
        Teardown(s);
    } else if (s->state == NETS_CONNECTING && nowMs - s->startedMs > s->connectTimeoutMs) {
        Fail(s, "NO ANSWER");
        Teardown(s);
    }
}

void NetSession_Leave(NetSession *s)
{
    if (s->state == NETS_CONNECTING || s->state == NETS_PLAYING) {
        NetClient_Leave(&s->client);                         // the goodbye
        if (s->hosting) NetHost_Close(&s->host, 150);        // tells the others the game is over, and gives it a moment to go out
        NetDial_Close(&s->dial, s->hosting ? 0 : 150);
    }
    s->state = NETS_IDLE;
    s->error[0] = 0;
}

int NetSession_Slots(const NetSession *s, NetSessionSlot out[NET_MAX_PLAYERS])
{
    memset(out, 0, sizeof(NetSessionSlot) * NET_MAX_PLAYERS);
    if (s->state != NETS_PLAYING && s->state != NETS_CONNECTING) return 0;
    const bool known = s->client.state == NETC_PLAYING;
    int n = 0;
    if (s->hosting && s->host.open) {
        for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
            const NetServerClient *c = &s->host.server.c[i];
            if (!c->used || !c->welcomed) continue;
            out[n].used = true; out[n].id = c->id; out[n].host = c->id == 0;
            out[n].you = known && c->id == s->client.id;
            snprintf(out[n].character, sizeof out[n].character, "%s", c->character);
            ++n;
        }
        return n;
    }
    if (!known) return 0;
    // A joiner knows itself, and the others from the newest snapshot. The host is the first player (id 0: it joins its own game first).
    NetSessionSlot mine = { true, true, s->client.id == 0, s->client.id, { 0 } };
    snprintf(mine.character, sizeof mine.character, "%s", s->client.character);
    bool placed = false;
    const int others = s->client.snapCount > 0 ? s->client.snaps[0].n : 0;
    for (int id = 0; id < NET_MAX_PLAYERS; ++id) {
        if (id == s->client.id) { out[n++] = mine; placed = true; continue; }
        for (int k = 0; k < others; ++k)
            if (s->client.snaps[0].r[k].id == id) { out[n].used = true; out[n].id = (uint8_t)id; out[n].host = id == 0; ++n; break; }
    }
    if (!placed) out[n++] = mine;
    return n;
}

uint32_t NetSession_NoSignalMs(const NetSession *s)
{
    NetLinkInfo li;
    if (s->state != NETS_PLAYING || !NetClient_Link(&s->client, &li) || li.state != NETLINK_NO_SIGNAL) return 0;
    return li.silentMs > 0 ? li.silentMs : 1;
}

int NetSession_Players(const NetSession *s)
{
    NetSessionSlot slots[NET_MAX_PLAYERS];
    return NetSession_Slots(s, slots);
}
