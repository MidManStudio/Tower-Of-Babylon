// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_loopback.h and net_loopback.c"
// ============================================================================
#ifndef NET_LOOPBACK_H
#define NET_LOOPBACK_H

#include "net_proto.h"
#include "net_transport.h"

// A pretend network between two ends (0 the client, 1 the server), with time that only moves when the test says so and conditions it
// sets per direction: delay, jitter, loss, duplication. Datagrams are lost, delayed by latency plus up to jitter (which reorders them)
// and sometimes doubled; the reliable channel never loses anything and keeps order (a late message holds up the ones behind it). The
// "random" is a fixed generator, so a run is the same every time.
#define NET_LB_QUEUE 192

typedef struct { float latencyMs, jitterMs, loss, dup; } NetLinkCfg;       // loss and dup are chances, 0 to 1

typedef struct { double at; uint32_t seq; uint16_t len; bool used; uint8_t data[NET_MTU]; } NetLbPkt;

typedef struct { void *link; int side; } NetLbEnd;       // what a transport's self points at: the link and which end

typedef struct {
    NetLbEnd    ep[2];
    double      now;
    uint32_t    rng, seq;
    NetLinkCfg  cfg[2];                          // [0]: client to server, [1]: server to client
    NetLbPkt    dg[2][NET_LB_QUEUE], rel[2][NET_LB_QUEUE];
    double      lastRelAt[2];
    double      blackoutFrom, blackoutTo;        // while now is inside, datagrams sent are lost and reliable messages wait for the end
    unsigned    sent[2], lost[2], doubled[2], delivered[2], overflowed[2], oversize[2];
} NetLoopback;

void NetLoopback_Init(NetLoopback *l, uint32_t seed);
void NetLoopback_SetBoth(NetLoopback *l, NetLinkCfg cfg);
void NetLoopback_Advance(NetLoopback *l, double toMs);             // the time now (never backwards)
void NetLoopback_Blackout(NetLoopback *l, double fromMs, double toMs);
NetTransport NetLoopback_End(NetLoopback *l, int side);             // 0 for the client's end, 1 for the server's

#endif
