// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_server.h and net_server.c"
// ============================================================================
#include "net_server.h"

#include <string.h>

#include "../dungeon/dungeon_generator.h"

static void Send(NetTransport *tp, const NetMsg *m, bool reliable)
{
    uint8_t buf[NET_MTU];
    const size_t n = NetProto_Encode(m, buf, sizeof buf);
    if (n == 0) return;
    if (reliable) tp->sendReliable(tp->self, buf, n); else tp->sendDatagram(tp->self, buf, n);
}

void NetServer_Init(NetServer *s, uint32_t floorSeed, int floorNumber)
{
    memset(s, 0, sizeof *s);
    s->floorSeed = floorSeed;
    s->floorNumber = floorNumber;
    GenerateFloorMap(&s->map, floorNumber, floorSeed);
}

bool NetServer_RewoundPose(const NetServer *s, uint8_t viewLag, int id, Vector3 *pos, float *yaw)
{
    return NetHistory_PoseAt(&s->hist, NetHistory_RewindTick(s->tick, viewLag), id, pos, yaw);
}

int NetServer_PlayerCount(const NetServer *s) { int n = 0; for (int i = 0; i < NET_MAX_PLAYERS; ++i) if (s->c[i].used && s->c[i].welcomed) ++n; return n; }

int NetServer_AddClient(NetServer *s, NetTransport tp, double nowMs)
{
    for (int i = 0; i < NET_MAX_PLAYERS; ++i)
        if (!s->c[i].used) {
            memset(&s->c[i], 0, sizeof s->c[i]);
            s->c[i].used = true; s->c[i].tp = tp; s->c[i].id = (uint8_t)i; s->c[i].lastHeardMs = nowMs; s->c[i].connectedMs = nowMs;
            return i;
        }
    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_DISCONNECT; m.u.disc.reason = NET_DISC_FULL;
    Send(&tp, &m, true);
    if (tp.close) tp.close(tp.self);
    return -1;
}

void NetServer_Drop(NetServer *s, int slot, uint8_t reason)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS || !s->c[slot].used) return;
    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_DISCONNECT; m.u.disc.reason = reason;
    Send(&s->c[slot].tp, &m, true);
    if (s->c[slot].tp.close) s->c[slot].tp.close(s->c[slot].tp.self);
    s->c[slot].used = false;
}

static void OnHello(NetServer *s, NetServerClient *cl, const NetHello *h)
{
    if (cl->welcomed) return;                                                         // a second Hello changes nothing
    if (h->version != NET_PROTOCOL_VERSION) { NetServer_Drop(s, cl->id, NET_DISC_VERSION); return; }
    memcpy(cl->character, h->character, sizeof cl->character);
    NetSim_Spawn(&cl->player, s->map.startX, s->map.startZ);
    cl->welcomed = true;
    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_WELCOME;
    m.u.welcome.version = NET_PROTOCOL_VERSION; m.u.welcome.tickRate = NET_TICK_RATE; m.u.welcome.playerId = cl->id; m.u.welcome.maxPlayers = NET_MAX_PLAYERS;
    m.u.welcome.floorSeed = s->floorSeed; m.u.welcome.floorNumber = s->floorNumber; m.u.welcome.serverTick = s->tick;
    m.u.welcome.spawnX = s->map.startX; m.u.welcome.spawnZ = s->map.startZ;
    Send(&cl->tp, &m, true);
}

static void OnInput(NetServer *s, NetServerClient *cl, const NetInputMsg *in)
{
    if (!cl->welcomed) { ++cl->badPackets; return; }
    const uint32_t next = s->tick + 1;                                                // the tick about to be run
    {
        long m = (long)in->tick - (long)next;
        cl->margin = (int8_t)(m > 127 ? 127 : (m < -128 ? -128 : m));
    }
    uint32_t earliest = 0; bool any = false;
    for (int i = 0; i < in->count; ++i) {
        if (in->tick < (uint32_t)i) break;
        const uint32_t t = in->tick - (uint32_t)i;
        if (t < next) { if (i == 0) ++cl->cmdsLate; continue; }                         // too late for its tick; the copies of older ones in later packets are expected, so only the newest is counted
        if (t > next + NET_MAX_LEAD) { if (i == 0) ++cl->cmdsTooFar; continue; }
        const int slot = (int)(t % NET_INPUT_RING);
        if (cl->ringHave[slot] && cl->ringTick[slot] == t) { if (i == 0) ++cl->cmdsDuplicate; continue; }       // the first to arrive for a tick is the one used
        cl->ring[slot] = in->cmds[i]; cl->ringTick[slot] = t; cl->ringHave[slot] = true;
        ++cl->cmdsAccepted;
        if (!any || t < earliest) { earliest = t; any = true; }
    }
    if (any && cl->startTick == 0) cl->startTick = earliest;
}

static void Drain(NetServer *s, int slot, double nowMs)
{
    NetServerClient *cl = &s->c[slot];
    uint8_t buf[NET_MTU];
    NetMsg m;
    int n;
    for (int k = 0; k < NET_MAX_PACKETS_PER_POLL && cl->used && (n = cl->tp.recvReliable(cl->tp.self, buf, sizeof buf)) != 0; ++k) {
        if (n < 0 || !NetProto_Decode(buf, (size_t)n, &m)) { ++cl->badPackets; continue; }
        cl->lastHeardMs = nowMs;
        if (m.kind == NET_MSG_HELLO) OnHello(s, cl, &m.u.hello);
        else if (m.kind == NET_MSG_DISCONNECT) { if (cl->tp.close) cl->tp.close(cl->tp.self); cl->used = false; }
        else ++cl->badPackets;                                                                    // a datagram kind on the reliable channel
    }
    for (int k = 0; k < NET_MAX_PACKETS_PER_POLL && cl->used && (n = cl->tp.recvDatagram(cl->tp.self, buf, sizeof buf)) != 0; ++k) {
        if (n < 0 || !NetProto_Decode(buf, (size_t)n, &m)) { ++cl->badPackets; continue; }
        cl->lastHeardMs = nowMs;
        if (m.kind == NET_MSG_INPUT) OnInput(s, cl, &m.u.input);
        else ++cl->badPackets;
    }
}

void NetServer_Poll(NetServer *s, double nowMs)
{
    s->nowMs = nowMs;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        if (!s->c[i].used) continue;
        Drain(s, i, nowMs);
        if (s->c[i].used && nowMs - s->c[i].lastHeardMs > NET_TIMEOUT_MS) NetServer_Drop(s, i, NET_DISC_TIMEOUT);
        else if (s->c[i].used && !s->c[i].welcomed && nowMs - s->c[i].connectedMs > NET_HELLO_TIMEOUT_MS) NetServer_Drop(s, i, NET_DISC_TIMEOUT);
    }
}

// The command for tick t, if it arrived. Takes it out of the ring.
static bool TakeCmd(NetServerClient *cl, uint32_t t, NetCmd *out)
{
    const int slot = (int)(t % NET_INPUT_RING);
    if (!cl->ringHave[slot] || cl->ringTick[slot] != t) return false;
    *out = cl->ring[slot];
    cl->ringHave[slot] = false;
    return true;
}

void NetServer_Tick(NetServer *s, double nowMs)
{
    s->nowMs = nowMs;
    const uint32_t T = ++s->tick;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        NetServerClient *cl = &s->c[i];
        if (!cl->used || !cl->welcomed) continue;
        if (!cl->started && cl->startTick != 0 && T >= cl->startTick) cl->started = true;
        if (!cl->started) continue;
        NetCmd cmd;
        if (TakeCmd(cl, T, &cmd)) {
            cl->lastCmd = cmd; cl->repeats = 0; ++cl->ticksFed;
        } else if (cl->repeats < NET_MAX_REPEAT) {                                           // did not arrive: go on as before, minus one-off presses
            cmd = cl->lastCmd;
            cmd.buttons = (uint16_t)(cmd.buttons & ~(NET_BTN_JUMP | NET_BTN_USE | NET_BTN_SWAP | NET_BTN_LIGHT | NET_BTN_SLIDE | NET_BTN_DIVE | NET_BTN_RELOAD));
            ++cl->repeats; ++cl->ticksRepeated;
        } else {                                                                              // silent for too long: stop, keeping the aim
            cmd = cl->lastCmd; cmd.moveX = 0; cmd.moveY = 0; cmd.buttons = 0;
            ++cl->ticksNeutral;
        }
        NetSim_Step(&cl->player, &cmd, &s->map);
    }
    NetHistory_Begin(&s->hist, T);                                                          // where everyone is after this tick, for the shots of the next ones
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        const NetServerClient *cl = &s->c[i];
        if (cl->used && cl->welcomed && cl->started) NetHistory_Set(&s->hist, T, i, cl->player.position, cl->player.yaw);
    }
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        NetServerClient *cl = &s->c[i];
        if (!cl->used || !cl->welcomed) continue;
        NetMsg m; memset(&m, 0, sizeof m);
        m.kind = NET_MSG_SNAPSHOT;
        m.u.snap.serverTick = T; m.u.snap.started = cl->started ? 1 : 0; m.u.snap.margin = cl->margin;
        NetSim_Capture(&cl->player, &m.u.snap.owner);
        for (int j = 0; j < NET_MAX_PLAYERS; ++j) {
            if (j == i || !s->c[j].used || !s->c[j].welcomed) continue;
            NetSim_Remote(&s->c[j].player, s->c[j].id, &m.u.snap.remote[m.u.snap.nRemote++]);
        }
        Send(&cl->tp, &m, false);
        ++s->snapshotsSent;
    }
}
