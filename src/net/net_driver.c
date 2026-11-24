// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_driver.h and net_driver.c"
// ============================================================================
#define _POSIX_C_SOURCE 200809L
#include "net_driver.h"

#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"

#if TOWER_NETDRIVER

#include <stdio.h>
#include <time.h>

#include "mid_net_driver.h"
#include "net_mid.h"
#include "net_proto.h"
#include "net_reliable.h"

// ---- the protocol this layer speaks ---------------------------------------------------------------------------------------------------
// Channels 0 and 1 are the session's own (handshake and goodbye; ping and pong). The game's datagrams are one sequenced channel, its reliable
// messages one reliable channel, each carrying one opaque message type: the bytes the game already makes (net_proto.c).
#define DRV_CH_CTRL_REL   0
#define DRV_CH_CTRL_DG    1
#define DRV_CH_STATE      2
#define DRV_CH_RELIABLE   3
#define DRV_MSG_DATA      1
#define DRV_PRIO_STATE    192        // "high": the owner's state and the commands are what the game cannot do without

#define DRV_DG_RING       32         // received datagrams kept until the game reads them
#define DRV_REL_RING      32         // received reliable messages kept until the game reads them (when full the driver is not drained: it holds, the peer is pushed back)
#define DRV_EARLY_RING    8          // reliable messages sent before the handshake finished
#define DRV_LOWER_RING    256        // reliable chunks waiting for the transport under (130 KB). When they fill, the transport below has taken nothing for that long, the peer is not reading: the adapter says error and the driver ends the connection, so a peer that reads nothing costs a bounded amount of memory
#define DRV_CHUNK         (NET_REL_MAX_MSG - 1)   // what a chunk carries after its stream byte: no transport under is asked for more than a reliable message may be

typedef struct { uint16_t len; uint8_t data[NET_MTU]; } DrvMsg;
typedef struct { uint16_t len; uint8_t data[NET_REL_MAX_MSG]; } DrvChunk;

typedef struct {
    MidNetDriver *drv;
    NetTransport  lower;
    double        lastPollMs;
    bool          polled, closed;
    DrvMsg        dg[DRV_DG_RING];       int dgHead, dgCount;
    DrvMsg        rel[DRV_REL_RING];     int relHead, relCount;
    DrvMsg        early[DRV_EARLY_RING]; int earlyHead, earlyCount;
    DrvMsg        held;                  bool heldValid;     // a reliable message taken from the driver while the ring was full
    DrvChunk      lowerQ[DRV_LOWER_RING]; int lqHead, lqCount;
    NetLinkInfo   info;
    NetDriverStats st;
} NetDrv;

static double (*g_clock)(void);
static MidNetSchema *g_schema;
static uint8_t g_scratch[65536];

static double Now(void)
{
    if (g_clock) return g_clock();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

void NetDriver_SetClock(double (*nowMs)(void)) { g_clock = nowMs; }
bool NetDriver_Available(void) { return true; }

// ---- the schema and the configuration: the same in every connection ---------------------------------------------------------------------
static bool EnsureSchema(void)
{
    if (g_schema) return true;
    MidNetSchema *s = mid_net_schema_new(8, 4);
    if (!s) return false;
    if (mid_net_schema_channel(s, DRV_CH_CTRL_REL, MID_NET_CHANNEL_RELIABLE_ORDERED) == 0 &&
        mid_net_schema_channel(s, DRV_CH_CTRL_DG, MID_NET_CHANNEL_UNRELIABLE_UNORDERED) == 0 &&
        mid_net_schema_channel(s, DRV_CH_STATE, MID_NET_CHANNEL_UNRELIABLE_SEQUENCED) == 0 &&
        mid_net_schema_channel(s, DRV_CH_RELIABLE, MID_NET_CHANNEL_RELIABLE_ORDERED) == 0 &&
        mid_net_schema_message(s, DRV_MSG_DATA) == 0 &&
        mid_net_schema_seal(s, DRV_CH_CTRL_REL, DRV_CH_CTRL_DG) == 0) {
        g_schema = s;
        return true;
    }
    mid_net_schema_free(s);
    return false;
}

static void MakeConfig(MidNetDriverConfig *c)
{
    char text[64];
    mid_net_driver_config_default(c);
    // two builds that disagree about the protocol are refused in the handshake, by this number
    const int n = snprintf(text, sizeof text, "tower-of-babylon net driver v1 protocol %d", NET_PROTOCOL_VERSION);
    c->schema_hash = mid_net_fnv1a64((const uint8_t *)text, (size_t)(n > 0 ? n : 0));
    c->hello_retry_ms = 0;                       // the transport under is reliable for the handshake: no resending here
    c->tick_rate_hz = NET_TICK_RATE;
    c->reliable_budget = 8192;                   // bytes handed to the transport under per poll: it has a window of its own, and what it will not take waits in DRV_LOWER_RING
    c->max_stream_pending = 65536;               // what the game may have queued in the driver itself (it hands over 8 KB per poll); past it the driver ends the connection
}

// ---- the transport under, as the driver's five callbacks ---------------------------------------------------------------------------------
// A chunk is [stream id][up to DRV_CHUNK bytes]: the transport under carries whole messages, the driver wants a byte stream per channel, and
// a message boundary of the one has nothing to do with a frame boundary of the other.
static void FlushLower(NetDrv *w)
{
    while (w->lqCount > 0) {
        DrvChunk *c = &w->lowerQ[w->lqHead];
        if (!w->lower.sendReliable(w->lower.self, c->data, c->len)) break;
        w->lqHead = (w->lqHead + 1) % DRV_LOWER_RING;
        --w->lqCount;
    }
}

static int32_t L_send_datagram(void *u, const uint8_t *d, size_t n)
{
    NetDrv *w = (NetDrv *)u;
    return w->lower.sendDatagram(w->lower.self, d, n) ? 0 : -1;
}

static int32_t L_recv_datagram(void *u, uint8_t *buf, size_t cap)
{
    NetDrv *w = (NetDrv *)u;
    const int n = w->lower.recvDatagram(w->lower.self, buf, cap);
    return n < 0 ? 0 : n;                        // one that did not fit was dropped by the transport: nothing to say about it
}

static int32_t L_send_reliable(void *u, uint32_t stream, const uint8_t *d, size_t n)
{
    NetDrv *w = (NetDrv *)u;
    size_t off = 0;
    FlushLower(w);
    while (off < n) {
        if (w->lqCount > 0) {                    // chunks are waiting for the transport: top up the last one (of this stream, not yet full) before starting another
            DrvChunk *t = &w->lowerQ[(w->lqHead + w->lqCount - 1) % DRV_LOWER_RING];
            if (t->data[0] == (uint8_t)stream && t->len < NET_REL_MAX_MSG) {
                const size_t room = (size_t)(NET_REL_MAX_MSG - t->len), more = n - off < room ? n - off : room;
                memcpy(t->data + t->len, d + off, more);
                t->len = (uint16_t)(t->len + more);
                off += more;
                continue;
            }
        }
        const size_t take = n - off < DRV_CHUNK ? n - off : DRV_CHUNK;
        uint8_t tmp[NET_REL_MAX_MSG];
        tmp[0] = (uint8_t)stream;
        memcpy(tmp + 1, d + off, take);
        if (!(w->lqCount == 0 && w->lower.sendReliable(w->lower.self, tmp, take + 1))) {
            if (w->lqCount == DRV_LOWER_RING) return -1;                    // the transport under has not taken anything for far too long
            DrvChunk *c = &w->lowerQ[(w->lqHead + w->lqCount) % DRV_LOWER_RING];
            c->len = (uint16_t)(take + 1);
            memcpy(c->data, tmp, take + 1);
            ++w->lqCount;
            if ((unsigned)w->lqCount > w->st.lowerQueuePeak) w->st.lowerQueuePeak = (unsigned)w->lqCount;
        }
        off += take;
    }
    return 0;
}

static int32_t L_recv_reliable(void *u, uint8_t *buf, size_t cap, uint32_t *stream)
{
    NetDrv *w = (NetDrv *)u;
    uint8_t tmp[NET_MTU + 8];
    FlushLower(w);
    const int n = w->lower.recvReliable(w->lower.self, tmp, sizeof tmp);
    if (n < 2) return 0;                          // nothing, a message that did not fit (dropped), or a chunk with no bytes in it
    if ((size_t)(n - 1) > cap) return 0;
    *stream = tmp[0];
    memcpy(buf, tmp + 1, (size_t)(n - 1));
    return n - 1;
}

static int32_t L_is_connected(void *u)
{
    NetDrv *w = (NetDrv *)u;
    const int st = NetMid_TransportState(&w->lower);     // 0 connecting, 1 connected, 2 closed or failed; -1: not a QUIC transport, which is ready when it is made
    return st < 0 ? 1 : st == 1;
}

// ---- the driver, as a NetTransport ----------------------------------------------------------------------------------------------------------
static void PushRing(DrvMsg *ring, int cap, int *head, int *count, const uint8_t *d, size_t n)
{
    DrvMsg *m = &ring[(*head + *count) % cap];
    m->len = (uint16_t)n;
    memcpy(m->data, d, n);
    ++*count;
}

static void Note(NetDrv *w, const MidNetEvent *e)
{
    ++w->st.events;
    if (e->kind == MID_NET_EVENT_FAULT) ++w->st.faults;
}

static void Pump(NetDrv *w, bool force)
{
    const double now = Now();
    if (w->closed) return;
    if (!force && w->polled && now - w->lastPollMs < 0.5) return;
    w->lastPollMs = now; w->polled = true;
    FlushLower(w);
    mid_net_driver_poll(w->drv, (uint64_t)now);
    MidNetEvent ev;
    while (mid_net_driver_next_event(w->drv, &ev) == MID_NET_ST_OK) Note(w, &ev);
    MidNetDriverInfo di;
    if (mid_net_driver_info(w->drv, (uint64_t)now, &di) == MID_NET_ST_OK) {
        w->info.state = di.state;                // MID_NET_STATE_* and NETLINK_* are the same numbers
        w->info.silentMs = di.silent_ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)di.silent_ms;
        w->info.rttMs = (float)di.rtt_ms;
        w->info.endKind = di.end_kind;           // MID_NET_END_* and NETLINK_END_* are the same numbers
        w->info.endPeer = di.end_peer_reason;
    }
    MidNetDriverStats ds;
    if (mid_net_driver_stats(w->drv, &ds) == MID_NET_ST_OK) {
        w->st.datagramsSent = (unsigned)ds.datagrams_sent; w->st.datagramsReceived = (unsigned)ds.datagrams_received;
        w->st.badDatagrams = (unsigned)ds.bad_datagrams; w->st.staleDatagrams = (unsigned)ds.stale_messages;
        w->st.droppedExpired = (unsigned)ds.dropped_expired; w->st.droppedQueueFull = (unsigned)ds.dropped_queue_full;
        w->st.reliableBytesSent = (unsigned)ds.reliable_bytes_sent; w->st.reliableBytesReceived = (unsigned)ds.reliable_bytes_received;
    }
    // what the game sent before the handshake finished goes now, in order
    while (w->earlyCount > 0 && (w->info.state == NETLINK_CONNECTED || w->info.state == NETLINK_NO_SIGNAL)) {
        DrvMsg *m = &w->early[w->earlyHead];
        if (mid_net_driver_send(w->drv, DRV_CH_RELIABLE, 0, DRV_MSG_DATA, m->data, m->len) != MID_NET_ST_OK) break;
        w->earlyHead = (w->earlyHead + 1) % DRV_EARLY_RING;
        --w->earlyCount;
    }
    // what arrived goes to the ring of its kind; a reliable message that has no room waits in one slot, and nothing more is taken until it has
    for (;;) {
        if (w->heldValid) {
            if (w->relCount >= DRV_REL_RING) break;
            PushRing(w->rel, DRV_REL_RING, &w->relHead, &w->relCount, w->held.data, w->held.len);
            w->heldValid = false;
        }
        uint8_t ch = 0; uint16_t id = 0; size_t len = 0;
        if (mid_net_driver_next_message(w->drv, &ch, &id, g_scratch, sizeof g_scratch, &len) != MID_NET_ST_OK) break;
        if (len == 0) continue;
        if (len > NET_MTU) { ++w->st.oversizeDropped; continue; }          // not a message of this game: the rings hold NET_MTU
        if (ch == DRV_CH_STATE) {
            if (w->dgCount >= DRV_DG_RING) { w->dgHead = (w->dgHead + 1) % DRV_DG_RING; --w->dgCount; ++w->st.dgRingDrops; }
            PushRing(w->dg, DRV_DG_RING, &w->dgHead, &w->dgCount, g_scratch, len);
        } else if (ch == DRV_CH_RELIABLE) {
            if (w->relCount < DRV_REL_RING) PushRing(w->rel, DRV_REL_RING, &w->relHead, &w->relCount, g_scratch, len);
            else { w->held.len = (uint16_t)len; memcpy(w->held.data, g_scratch, len); w->heldValid = true; }
        }
    }
}

static bool U_sendDatagram(void *self, const uint8_t *data, size_t len)
{
    NetDrv *w = (NetDrv *)self;
    if (w->closed || len == 0 || len > NET_MTU) return false;
    Pump(w, false);
    const int32_t r = mid_net_driver_send(w->drv, DRV_CH_STATE, DRV_PRIO_STATE, DRV_MSG_DATA, data, len);
    if (r == MID_NET_ST_OK) Pump(w, true);                            // it goes out now, as a plain datagram does, not at the next frame's poll
    return r == MID_NET_ST_OK || r == MID_NET_ST_NOT_CONNECTED;       // before the handshake a datagram is dropped, as any datagram may be
}

static bool U_sendReliable(void *self, const uint8_t *data, size_t len)
{
    NetDrv *w = (NetDrv *)self;
    if (w->closed || len == 0 || len > NET_MTU) return false;
    Pump(w, false);
    if (w->earlyCount == 0) {
        const int32_t r = mid_net_driver_send(w->drv, DRV_CH_RELIABLE, 0, DRV_MSG_DATA, data, len);
        if (r == MID_NET_ST_OK) { Pump(w, true); return true; }
        if (r != MID_NET_ST_NOT_CONNECTED) return false;
    }
    if (w->earlyCount >= DRV_EARLY_RING) return false;            // the handshake is taking so long that the game has said too much: refused, like a full window
    PushRing(w->early, DRV_EARLY_RING, &w->earlyHead, &w->earlyCount, data, len);
    ++w->st.heldEarly;
    return true;
}

static int U_recvDatagram(void *self, uint8_t *buf, size_t cap)
{
    NetDrv *w = (NetDrv *)self;
    Pump(w, false);
    if (w->dgCount == 0) return 0;
    DrvMsg *m = &w->dg[w->dgHead];
    const int n = (size_t)m->len > cap ? -1 : (int)m->len;
    if (n > 0) memcpy(buf, m->data, m->len);
    w->dgHead = (w->dgHead + 1) % DRV_DG_RING;
    --w->dgCount;
    return n;
}

static int U_recvReliable(void *self, uint8_t *buf, size_t cap)
{
    NetDrv *w = (NetDrv *)self;
    Pump(w, false);
    if (w->relCount == 0) return 0;
    DrvMsg *m = &w->rel[w->relHead];
    const int n = (size_t)m->len > cap ? -1 : (int)m->len;
    if (n > 0) memcpy(buf, m->data, m->len);
    w->relHead = (w->relHead + 1) % DRV_REL_RING;
    --w->relCount;
    return n;
}

static bool U_linkInfo(void *self, NetLinkInfo *out)
{
    NetDrv *w = (NetDrv *)self;
    if (!out) return false;
    Pump(w, false);
    *out = w->info;
    return true;
}

// The connection is finished with: what the game sent (a Disconnect) goes out, then the goodbye of the driver, then the transport under is
// closed (a real one lets those get out, then frees itself), then everything here is freed.
// A Disconnect the game sent before the handshake had finished (a full server says so this way) never got out, as the link was not yet up: its reason
// goes into the driver's goodbye instead, which is heard even by a client that is still connecting. Anything else is a normal goodbye.
static uint8_t GoodbyeReason(const NetDrv *w)
{
    for (int i = 0; i < w->earlyCount; ++i) {
        const DrvMsg *m = &w->early[(w->earlyHead + i) % DRV_EARLY_RING];
        NetMsg d;
        if (!NetProto_Decode(m->data, m->len, &d) || d.kind != NET_MSG_DISCONNECT) continue;
        switch (d.u.disc.reason) {
        case NET_DISC_VERSION: return 1;
        case NET_DISC_FULL: return 3;
        case NET_DISC_TIMEOUT: return 4;
        case NET_DISC_BAD: return 7;
        default: return 0;
        }
    }
    return 0;
}

static void U_close(void *self)
{
    NetDrv *w = (NetDrv *)self;
    if (!w || w->closed) return;
    Pump(w, true);
    mid_net_driver_close(w->drv, GoodbyeReason(w));
    Pump(w, true);
    FlushLower(w);
    w->closed = true;
    if (w->lower.close) w->lower.close(w->lower.self);
    mid_net_driver_free(w->drv);
    free(w);
}

static bool Make(NetTransport lower, bool server, uint32_t sessionId, const char *name, NetTransport *out)
{
    if (!out || !lower.sendDatagram || !lower.recvDatagram || !lower.sendReliable || !lower.recvReliable) return false;
    if (!EnsureSchema()) return false;
    NetDrv *w = (NetDrv *)calloc(1, sizeof *w);
    if (!w) return false;
    w->lower = lower;
    MidNetTransportVtable vt;
    memset(&vt, 0, sizeof vt);
    vt.user = w;
    vt.send_datagram = L_send_datagram; vt.recv_datagram = L_recv_datagram;
    vt.send_reliable = L_send_reliable; vt.recv_reliable = L_recv_reliable;
    vt.is_connected = L_is_connected;
    MidNetDriverConfig cfg;
    MakeConfig(&cfg);
    const double now = Now();
    int32_t r;
    if (server) r = mid_net_driver_server_end(g_schema, &cfg, &vt, sessionId, (uint64_t)now, &w->drv);
    else r = mid_net_driver_client(g_schema, &cfg, &vt, name ? name : "", (uint32_t)((uint64_t)(now * 1000.0) ^ (uint64_t)(uintptr_t)w), (uint64_t)now, &w->drv);
    if (r != MID_NET_ST_OK || !w->drv) { free(w); return false; }
    w->info.state = NETLINK_CONNECTING;
    out->self = w;
    out->sendDatagram = U_sendDatagram; out->recvDatagram = U_recvDatagram;
    out->sendReliable = U_sendReliable; out->recvReliable = U_recvReliable;
    out->close = U_close; out->linkInfo = U_linkInfo;
    return true;
}

bool NetDriver_Client(NetTransport lower, const char *name, NetTransport *out) { return Make(lower, false, 0, name, out); }
bool NetDriver_Server(NetTransport lower, uint32_t sessionId, NetTransport *out) { return Make(lower, true, sessionId, NULL, out); }

bool NetDriver_Stats(const NetTransport *t, NetDriverStats *out)
{
    if (!t || !out || t->close != U_close || !t->self) return false;
    *out = ((NetDrv *)t->self)->st;
    return true;
}

#ifdef NET_DRIVER_TEST
bool NetDriver_TestInject(const NetTransport *t, int channel, const uint8_t *data, size_t len)
{
    if (!t || t->close != U_close || !t->self) return false;
    NetDrv *w = (NetDrv *)t->self;
    const int32_t r = mid_net_driver_send(w->drv, (uint8_t)channel, 0, DRV_MSG_DATA, data, len);
    Pump(w, true);
    return r == MID_NET_ST_OK;
}
#endif

#else   // the build did not link the driver library: every function says no

bool NetDriver_Available(void) { return false; }
void NetDriver_SetClock(double (*nowMs)(void)) { (void)nowMs; }
bool NetDriver_Client(NetTransport lower, const char *name, NetTransport *out) { (void)lower; (void)name; (void)out; return false; }
bool NetDriver_Server(NetTransport lower, uint32_t sessionId, NetTransport *out) { (void)lower; (void)sessionId; (void)out; return false; }
bool NetDriver_Stats(const NetTransport *t, NetDriverStats *out) { (void)t; (void)out; return false; }

#endif
