// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_client.h and net_client.c"
// ============================================================================
#include "net_client.h"

#include <math.h>
#include <string.h>

#include "../dungeon/dungeon_generator.h"

#define TICK_MS (1000.0 / (double)NET_TICK_RATE)

static void Send(NetClient *c, const NetMsg *m, bool reliable)
{
    uint8_t buf[NET_MTU];
    const size_t n = NetProto_Encode(m, buf, sizeof buf);
    if (n == 0) return;
    if (reliable) c->tp.sendReliable(c->tp.self, buf, n); else c->tp.sendDatagram(c->tp.self, buf, n);
}

void NetClient_Init(NetClient *c, NetTransport tp, const char *character, double nowMs)
{
    memset(c, 0, sizeof *c);
    c->tp = tp;
    c->rate = 1.0f;
    {
        size_t n = 0;
        while (character && character[n] && n < NET_CHARACTER_LEN - 1) { c->character[n] = character[n]; ++n; }
    }
    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_HELLO; m.u.hello.version = NET_PROTOCOL_VERSION;
    memcpy(m.u.hello.character, c->character, sizeof m.u.hello.character);
    Send(c, &m, true);
    c->state = NETC_HELLO;
    c->helloSentMs = nowMs;
}

static void OnWelcome(NetClient *c, const NetWelcome *w, double nowMs)
{
    if (c->state != NETC_HELLO) return;
    if (w->version != NET_PROTOCOL_VERSION || w->tickRate != NET_TICK_RATE) { c->state = NETC_CLOSED; c->closedReason = NET_DISC_VERSION; return; }
    c->id = w->playerId; c->maxPlayers = w->maxPlayers; c->floorSeed = w->floorSeed; c->floorNumber = w->floorNumber;
    GenerateFloorMap(&c->map, w->floorNumber, w->floorSeed);
    NetSim_Spawn(&c->player, w->spawnX, w->spawnZ);
    c->rttMs = nowMs - c->helloSentMs;
    // The server is about rtt/ticks ahead of the Welcome by the time a command sent now reaches it; start that far plus a margin ahead.
    c->startTick = w->serverTick + (uint32_t)ceil(c->rttMs / TICK_MS) + 3;
    c->tick = c->startTick;
    c->lastUpdateMs = nowMs; c->accum = 0.0;
    c->lastSnapMs = nowMs;
    c->prevPos = c->player.position;
    c->state = NETC_PLAYING;
}

static void AddSnapshot(NetClient *c, const NetSnapshot *s, double nowMs)
{
    NetClientSnap e; e.serverTick = s->serverTick; e.arrivedMs = nowMs; e.n = s->nRemote;
    memcpy(e.r, s->remote, sizeof e.r);
    int at = 0;                                                         // newest first, by tick
    while (at < c->snapCount && c->snaps[at].serverTick > e.serverTick) ++at;
    if (at < c->snapCount && c->snaps[at].serverTick == e.serverTick) return;       // a doubled datagram
    if (c->snapCount < NET_SNAPS) ++c->snapCount;
    if (at >= NET_SNAPS) return;
    for (int i = c->snapCount - 1; i > at; --i) c->snaps[i] = c->snaps[i - 1];
    c->snaps[at] = e;
}

static void Reconcile(NetClient *c, const NetSnapshot *s)
{
    if (!s->started) return;
    const uint32_t T = s->serverTick;
    if (T <= c->lastReconciled) { ++c->snapshotsOld; return; }                      // a late or doubled snapshot: the newer one already did this
    c->lastReconciled = T;
    if (T < c->startTick) return;
    if (T >= c->tick || c->tick - T >= NET_HISTORY) {                               // far ahead of us or far behind: no tick to compare with. Take the server's word.
        NetSim_Apply(&c->player, &s->owner);
        for (int i = 0; i < NET_HISTORY; ++i) c->hist[i].valid = false;
        if (T >= c->tick) c->tick = T + 3;
        c->startTick = c->tick;
        c->errOffset = (Vector3){ 0 };
        c->prevPos = c->player.position;
        ++c->hardSnaps;
        return;
    }
    const int hi = (int)(T % NET_HISTORY);
    if (!c->hist[hi].valid || c->hist[hi].tick != T) return;
    const float err = NetSim_Error(&c->hist[hi].state, &s->owner);
    c->lastError = err;
    if (err > c->maxError) c->maxError = err;
    if (err <= NET_RECON_EPS) return;

    PlayerController st = c->hist[hi].state;                                        // the server's state at T, then our own commands since
    NetSim_Apply(&st, &s->owner);
    c->hist[hi].state = st;
    for (uint32_t t = T + 1; t < c->tick; ++t) {
        const int i = (int)(t % NET_HISTORY);
        if (!c->hist[i].valid || c->hist[i].tick != t) break;
        NetSim_Step(&st, &c->hist[i].cmd, &c->map);
        c->hist[i].state = st;
    }
    const Vector3 before = c->player.position;
    c->player = st;
    c->errOffset.x += before.x - st.position.x; c->errOffset.y += before.y - st.position.y; c->errOffset.z += before.z - st.position.z;
    c->prevPos.x -= before.x - st.position.x; c->prevPos.y -= before.y - st.position.y; c->prevPos.z -= before.z - st.position.z;   // the blend between ticks moves with the correction, so the fading jump is the only change on screen
    if (c->errOffset.x * c->errOffset.x + c->errOffset.y * c->errOffset.y + c->errOffset.z * c->errOffset.z > 4.0f) c->errOffset = (Vector3){ 0 };   // over two metres: not worth smoothing
    ++c->corrections;
}

static void AdjustClock(NetClient *c, int8_t margin)
{
    const int n = (int)(sizeof c->marginWin / sizeof c->marginWin[0]);
    c->marginWin[c->marginN % n] = margin;
    ++c->marginN;
    if (c->marginN < n) return;                                                      // a window of snapshots first
    int lowest = 127;
    for (int i = 0; i < n; ++i) if (c->marginWin[i] < lowest) lowest = c->marginWin[i];
    // The worst margin lately: below 1 a command can be late, above 4 the lead is costing needless delay. A few percent faster or slower moves it.
    c->rate = lowest < 1 ? 1.05f : (lowest > 4 ? 0.95f : 1.0f);
}

void NetClient_Poll(NetClient *c, double nowMs)
{
    if (c->state == NETC_IDLE || c->state == NETC_CLOSED) return;
    uint8_t buf[NET_MTU];
    NetMsg m;
    int n;
    for (int k = 0; k < 64 && (n = c->tp.recvReliable(c->tp.self, buf, sizeof buf)) != 0; ++k) {
        if (n < 0 || !NetProto_Decode(buf, (size_t)n, &m)) continue;
        if (m.kind == NET_MSG_WELCOME) OnWelcome(c, &m.u.welcome, nowMs);
        else if (m.kind == NET_MSG_DISCONNECT) { c->state = NETC_CLOSED; c->closedReason = m.u.disc.reason; return; }
    }
    for (int k = 0; k < 64 && (n = c->tp.recvDatagram(c->tp.self, buf, sizeof buf)) != 0; ++k) {
        if (n < 0 || !NetProto_Decode(buf, (size_t)n, &m) || m.kind != NET_MSG_SNAPSHOT || c->state != NETC_PLAYING) continue;
        ++c->snapshotsSeen;
        if (m.u.snap.serverTick > c->lastServerTick) { c->lastServerTick = m.u.snap.serverTick; c->lastSnapMs = nowMs; }
        AddSnapshot(c, &m.u.snap, nowMs);
        Reconcile(c, &m.u.snap);
        AdjustClock(c, m.u.snap.margin);
    }
    if ((c->state == NETC_PLAYING && nowMs - c->lastSnapMs > 5000.0) || (c->state == NETC_HELLO && nowMs - c->helloSentMs > 5000.0)) {
        c->state = NETC_CLOSED; c->closedReason = NET_DISC_TIMEOUT;
    }
}

static void RunTick(NetClient *c, NetCmd cmd)
{
    const int i = (int)(c->tick % NET_HISTORY);
    c->prevPos = c->player.position;
    NetSim_Step(&c->player, &cmd, &c->map);
    c->hist[i].cmd = cmd; c->hist[i].state = c->player; c->hist[i].tick = c->tick; c->hist[i].valid = true;
    c->errOffset.x *= NET_SMOOTH_KEEP; c->errOffset.y *= NET_SMOOTH_KEEP; c->errOffset.z *= NET_SMOOTH_KEEP;

    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_INPUT; m.u.input.tick = c->tick; m.u.input.ackServerTick = c->lastServerTick;
    int count = 0;
    for (int k = 0; k < NET_INPUT_REDUNDANCY && c->tick >= c->startTick + (uint32_t)k; ++k) {
        const int j = (int)((c->tick - (uint32_t)k) % NET_HISTORY);
        if (!c->hist[j].valid || c->hist[j].tick != c->tick - (uint32_t)k) break;
        m.u.input.cmds[count++] = c->hist[j].cmd;
    }
    m.u.input.count = (uint8_t)count;
    if (count > 0) Send(c, &m, false);
    ++c->tick;
    ++c->ticksRun;
}

int NetClient_Update(NetClient *c, double nowMs, NetCmdFn fn, void *user)
{
    if (c->state != NETC_PLAYING) return 0;
    double dt = nowMs - c->lastUpdateMs;
    c->lastUpdateMs = nowMs;
    if (dt < 0.0) dt = 0.0;
    c->accum += dt * (double)c->rate;
    int ran = 0;
    while (c->accum >= TICK_MS) {
        c->accum -= TICK_MS;
        if (ran >= 5) { c->accum = 0.0; break; }                                     // a long stall: do not run a burst to catch up
        NetCmd cmd = fn(user, c->tick);
        cmd.viewLag = NetClient_ViewLag(c, nowMs, c->tick);
        RunTick(c, cmd);
        ++ran;
    }
    return ran;
}

void NetClient_Leave(NetClient *c)
{
    if (c->state == NETC_IDLE || c->state == NETC_CLOSED) return;
    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_DISCONNECT; m.u.disc.reason = NET_DISC_NORMAL;
    Send(c, &m, true);
    if (c->tp.close) c->tp.close(c->tp.self);
    c->state = NETC_CLOSED;
}

Vector3 NetClient_ViewPos(const NetClient *c)
{
    return (Vector3){ c->player.position.x + c->errOffset.x, c->player.position.y + c->errOffset.y, c->player.position.z + c->errOffset.z };
}

uint8_t NetClient_ViewLag(const NetClient *c, double nowMs, uint32_t tick)
{
    if (c->snapCount == 0) return 0;
    const double est = (double)c->lastServerTick + (nowMs - c->lastSnapMs) / TICK_MS;       // the server's tick, about now (the same as NetClient_RemoteView)
    const double shown = est - (double)NET_INTERP_TICKS;                                     // the tick the others are drawn at
    double lag = (double)tick - shown + 0.5;
    if (lag < 0.0) lag = 0.0;
    if (lag > 255.0) lag = 255.0;
    return (uint8_t)lag;
}

float NetClient_Alpha(const NetClient *c)
{
    float a = (float)(c->accum / TICK_MS);
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return a;
}

Vector3 NetClient_ViewPosAt(const NetClient *c, float alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    return (Vector3){ c->prevPos.x + (c->player.position.x - c->prevPos.x) * alpha + c->errOffset.x,
                      c->prevPos.y + (c->player.position.y - c->prevPos.y) * alpha + c->errOffset.y,
                      c->prevPos.z + (c->player.position.z - c->prevPos.z) * alpha + c->errOffset.z };
}

static float LerpAngle(float a, float b, float u)                                    // the short way round a turn
{
    float d = b - a;
    while (d > 3.14159265f) d -= 6.28318531f;
    while (d < -3.14159265f) d += 6.28318531f;
    return a + d * u;
}

static const NetRemote *Find(const NetClientSnap *s, uint8_t id) { for (int i = 0; i < s->n; ++i) if (s->r[i].id == id) return &s->r[i]; return NULL; }
static Vector3 RemotePos(const NetRemote *r) { return (Vector3){ (float)r->px / 1000.0f, (float)r->py / 1000.0f, (float)r->pz / 1000.0f }; }
static float RemoteYaw(const NetRemote *r) { return (float)r->yaw / 65536.0f * 6.28318531f; }

bool NetClient_RemoteView(const NetClient *c, uint8_t id, double nowMs, Vector3 *pos, float *yaw)
{
    if (c->snapCount == 0) return false;
    const double est = (double)c->lastServerTick + (nowMs - c->lastSnapMs) / TICK_MS;       // the server's tick, about now
    const double want = est - (double)NET_INTERP_TICKS;
    const NetClientSnap *after = NULL, *before = NULL;                                      // the snapshots round `want` that have this player
    for (int i = 0; i < c->snapCount; ++i) {                                                // newest first
        if (!Find(&c->snaps[i], id)) continue;
        if ((double)c->snaps[i].serverTick > want) after = &c->snaps[i];
        else { before = &c->snaps[i]; break; }
    }
    if (before && after) {
        const NetRemote *a = Find(before, id), *b = Find(after, id);
        const double span = (double)after->serverTick - (double)before->serverTick;
        const float u = (float)(((double)want - (double)before->serverTick) / span);
        const Vector3 pa = RemotePos(a), pb = RemotePos(b);
        *pos = (Vector3){ pa.x + (pb.x - pa.x) * u, pa.y + (pb.y - pa.y) * u, pa.z + (pb.z - pa.z) * u };
        *yaw = LerpAngle(RemoteYaw(a), RemoteYaw(b), u);
        return true;
    }
    const NetClientSnap *hold = before ? before : after;                                   // nothing newer yet (or nothing older): hold what there is
    if (!hold) return false;
    const NetRemote *r = Find(hold, id);
    *pos = RemotePos(r); *yaw = RemoteYaw(r);
    return true;
}
