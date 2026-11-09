// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_loopback.h and net_loopback.c"
// ============================================================================
#include "net_loopback.h"

#include <string.h>

static float Rnd01(NetLoopback *l)                      // 0 up to (not including) 1
{
    uint32_t x = l->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    l->rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

void NetLoopback_Init(NetLoopback *l, uint32_t seed)
{
    memset(l, 0, sizeof *l);
    l->ep[0].link = l; l->ep[0].side = 0; l->ep[1].link = l; l->ep[1].side = 1;
    l->rng = seed ? seed : 0x2545f491u;
    l->blackoutFrom = l->blackoutTo = -1.0;
}
void NetLoopback_SetBoth(NetLoopback *l, NetLinkCfg cfg) { l->cfg[0] = cfg; l->cfg[1] = cfg; }
void NetLoopback_Advance(NetLoopback *l, double toMs) { if (toMs > l->now) l->now = toMs; }
void NetLoopback_Blackout(NetLoopback *l, double fromMs, double toMs) { l->blackoutFrom = fromMs; l->blackoutTo = toMs; }

static bool InBlackout(const NetLoopback *l) { return l->now >= l->blackoutFrom && l->now < l->blackoutTo; }

static bool Push(NetLoopback *l, NetLbPkt *q, int dir, double at, const uint8_t *data, size_t len)
{
    for (int i = 0; i < NET_LB_QUEUE; ++i)
        if (!q[i].used) {
            q[i].used = true; q[i].at = at; q[i].seq = l->seq++; q[i].len = (uint16_t)len; memcpy(q[i].data, data, len);
            return true;
        }
    ++l->overflowed[dir];
    return false;
}

static int Pop(NetLoopback *l, NetLbPkt *q, int dir, uint8_t *buf, size_t cap)
{
    int best = -1;
    for (int i = 0; i < NET_LB_QUEUE; ++i)
        if (q[i].used && q[i].at <= l->now && (best < 0 || q[i].at < q[best].at || (q[i].at == q[best].at && q[i].seq < q[best].seq))) best = i;
    if (best < 0) return 0;
    q[best].used = false;
    if (q[best].len > cap) return -1;
    memcpy(buf, q[best].data, q[best].len);
    ++l->delivered[dir];
    return q[best].len;
}

static bool SendDatagram(void *self, const uint8_t *data, size_t len)
{
    NetLbEnd *e = (NetLbEnd *)self; NetLoopback *l = (NetLoopback *)e->link; const int d = e->side;
    if (len == 0 || len > NET_MTU) { ++l->oversize[d]; return false; }
    ++l->sent[d];
    if (InBlackout(l) || Rnd01(l) < l->cfg[d].loss) { ++l->lost[d]; return true; }          // sent, and gone
    const NetLinkCfg *c = &l->cfg[d];
    Push(l, l->dg[d], d, l->now + c->latencyMs + c->jitterMs * Rnd01(l), data, len);
    if (Rnd01(l) < c->dup) { ++l->doubled[d]; Push(l, l->dg[d], d, l->now + c->latencyMs + c->jitterMs * Rnd01(l), data, len); }
    return true;
}
static int RecvDatagram(void *self, uint8_t *buf, size_t cap) { NetLbEnd *e = (NetLbEnd *)self; NetLoopback *l = (NetLoopback *)e->link; return Pop(l, l->dg[1 - e->side], 1 - e->side, buf, cap); }

static bool SendReliable(void *self, const uint8_t *data, size_t len)
{
    NetLbEnd *e = (NetLbEnd *)self; NetLoopback *l = (NetLoopback *)e->link; const int d = e->side;
    if (len == 0 || len > NET_MTU) { ++l->oversize[d]; return false; }
    const NetLinkCfg *c = &l->cfg[d];
    double at = l->now + c->latencyMs + c->jitterMs * Rnd01(l);
    if (InBlackout(l) && at < l->blackoutTo + c->latencyMs) at = l->blackoutTo + c->latencyMs;
    if (at < l->lastRelAt[d]) at = l->lastRelAt[d];                                          // in order: behind the one before
    l->lastRelAt[d] = at;
    return Push(l, l->rel[d], d, at, data, len);
}
static int RecvReliable(void *self, uint8_t *buf, size_t cap) { NetLbEnd *e = (NetLbEnd *)self; NetLoopback *l = (NetLoopback *)e->link; return Pop(l, l->rel[1 - e->side], 1 - e->side, buf, cap); }

NetTransport NetLoopback_End(NetLoopback *l, int side)
{
    NetTransport t;
    t.self = &l->ep[side ? 1 : 0];
    t.sendDatagram = SendDatagram; t.recvDatagram = RecvDatagram; t.sendReliable = SendReliable; t.recvReliable = RecvReliable; t.close = NULL;
    return t;
}
