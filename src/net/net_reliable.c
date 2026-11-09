// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_reliable.h and net_reliable.c"
// ============================================================================
#include "net_reliable.h"

#include <math.h>
#include <string.h>

#define K_DATA 1
#define K_ACK  2

static int16_t Diff(uint16_t a, uint16_t b) { return (int16_t)(uint16_t)(a - b); }       // a - b round the end of the sequence numbers

void NetRel_Init(NetRel *r) { memset(r, 0, sizeof *r); r->rto = 250.0; }

static void EmitData(NetRel *r, int i, uint16_t seq, NetRelEmit emit, void *user)
{
    uint8_t pkt[3 + NET_REL_MAX_MSG];
    pkt[0] = K_DATA; pkt[1] = (uint8_t)seq; pkt[2] = (uint8_t)(seq >> 8);
    memcpy(pkt + 3, r->out[i].data, r->out[i].len);
    emit(user, pkt, 3u + r->out[i].len);
}

bool NetRel_Send(NetRel *r, const uint8_t *msg, size_t len, double nowMs, NetRelEmit emit, void *user)
{
    if (len == 0 || len > NET_REL_MAX_MSG) return false;
    if (Diff(r->sendNext, r->sendBase) >= NET_REL_WINDOW) return false;
    const int i = r->sendNext % NET_REL_WINDOW;
    r->out[i].used = true; r->out[i].tries = 1; r->out[i].len = (uint16_t)len; r->out[i].sentAt = nowMs; r->out[i].firstAt = nowMs;
    memcpy(r->out[i].data, msg, len);
    EmitData(r, i, r->sendNext, emit, user);
    ++r->sendNext;
    ++r->sent;
    return true;
}

static void RttSample(NetRel *r, double rtt)
{
    if (!r->haveRtt) { r->srtt = rtt; r->rttvar = rtt / 2.0; r->haveRtt = true; }
    else { r->rttvar = 0.75 * r->rttvar + 0.25 * fabs(r->srtt - rtt); r->srtt = 0.875 * r->srtt + 0.125 * rtt; }
    r->rto = r->srtt + 4.0 * r->rttvar;
    if (r->rto < 40.0) r->rto = 40.0;
    if (r->rto > 1000.0) r->rto = 1000.0;
}

static void Acked(NetRel *r, int i, double nowMs)
{
    if (!r->out[i].used) return;
    if (r->out[i].tries == 1) RttSample(r, nowMs - r->out[i].firstAt);              // only a message sent once says how long the round trip was
    r->out[i].used = false;
}

static void OnAck(NetRel *r, uint16_t next, uint32_t mask, double nowMs)
{
    if (Diff(next, r->sendBase) > 0 && Diff(next, r->sendNext) <= 0) {              // everything before `next` has arrived
        for (uint16_t s = r->sendBase; s != next; ++s) Acked(r, s % NET_REL_WINDOW, nowMs);
        r->sendBase = next;
    }
    for (int b = 0; b < 32; ++b)                                                      // and these of the ones after it
        if (mask & (1u << b)) {
            const uint16_t s = (uint16_t)(next + 1 + b);
            if (Diff(s, r->sendBase) >= 0 && Diff(r->sendNext, s) > 0) Acked(r, s % NET_REL_WINDOW, nowMs);
        }
    while (r->sendBase != r->sendNext && !r->out[r->sendBase % NET_REL_WINDOW].used) ++r->sendBase;
}

static void SendAck(NetRel *r, NetRelEmit emit, void *user)
{
    int n = 0;
    while (n < NET_REL_WINDOW && r->in[(uint16_t)(r->recvNext + n) % NET_REL_WINDOW].have) ++n;
    const uint16_t next = (uint16_t)(r->recvNext + n);                              // the first one not received
    uint32_t mask = 0;
    for (int b = 0; b < 32; ++b) {
        const uint16_t s = (uint16_t)(next + 1 + b);
        if (Diff(s, r->recvNext) < NET_REL_WINDOW && r->in[s % NET_REL_WINDOW].have) mask |= 1u << b;
    }
    uint8_t pkt[7] = { K_ACK, (uint8_t)next, (uint8_t)(next >> 8), (uint8_t)mask, (uint8_t)(mask >> 8), (uint8_t)(mask >> 16), (uint8_t)(mask >> 24) };
    emit(user, pkt, sizeof pkt);
    ++r->acksSent;
}

static void OnData(NetRel *r, uint16_t seq, const uint8_t *payload, size_t len, NetRelEmit emit, void *user)
{
    const int16_t d = Diff(seq, r->recvNext);
    if (d < 0) ++r->duplicates;                                                       // one already handed over: say again that we have it
    else if (d >= NET_REL_WINDOW) ++r->outOfWindow;                                   // too far ahead for the room we have: it will be resent
    else {
        const int i = seq % NET_REL_WINDOW;
        if (r->in[i].have) ++r->duplicates;
        else { memcpy(r->in[i].data, payload, len); r->in[i].len = (uint16_t)len; r->in[i].have = true; }
    }
    SendAck(r, emit, user);
}

bool NetRel_OnPacket(NetRel *r, const uint8_t *pkt, size_t len, double nowMs, NetRelEmit emit, void *user)
{
    if (len < 1) return false;
    if (pkt[0] == K_DATA) {
        if (len < 4 || len > 3 + NET_REL_MAX_MSG) return false;
        OnData(r, (uint16_t)(pkt[1] | (pkt[2] << 8)), pkt + 3, len - 3, emit, user);
        return true;
    }
    if (pkt[0] == K_ACK) {
        if (len != 7) return false;
        OnAck(r, (uint16_t)(pkt[1] | (pkt[2] << 8)), (uint32_t)pkt[3] | ((uint32_t)pkt[4] << 8) | ((uint32_t)pkt[5] << 16) | ((uint32_t)pkt[6] << 24), nowMs);
        return true;
    }
    return false;
}

void NetRel_Tick(NetRel *r, double nowMs, NetRelEmit emit, void *user)
{
    for (uint16_t s = r->sendBase; s != r->sendNext; ++s) {
        const int i = s % NET_REL_WINDOW;
        if (!r->out[i].used) continue;
        const int backoff = r->out[i].tries - 1 < 4 ? r->out[i].tries - 1 : 4;
        double wait = r->rto * (double)(1 << backoff);                              // each resend waits longer, up to a second
        if (wait > 1000.0) wait = 1000.0;
        if (nowMs - r->out[i].sentAt < wait) continue;
        if (r->out[i].tries >= NET_REL_GIVEUP) { r->dead = true; continue; }
        ++r->out[i].tries; r->out[i].sentAt = nowMs; ++r->retransmits;
        EmitData(r, i, s, emit, user);
    }
}

int NetRel_Recv(NetRel *r, uint8_t *buf, size_t cap)
{
    const int i = r->recvNext % NET_REL_WINDOW;
    if (!r->in[i].have) return 0;
    const int len = r->in[i].len;
    r->in[i].have = false;
    ++r->recvNext;
    ++r->delivered;
    if ((size_t)len > cap) return -1;
    memcpy(buf, r->in[i].data, (size_t)len);
    return len;
}
