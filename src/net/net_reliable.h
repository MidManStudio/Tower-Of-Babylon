// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_reliable.h and net_reliable.c"
// ============================================================================
#ifndef NET_RELIABLE_H
#define NET_RELIABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Messages that arrive whole, in order, exactly once, over a path that loses, doubles and reorders packets (UDP). One of these per
// connection. It knows nothing of sockets: it hands the bytes it wants sent to an emit function and is given the bytes that arrive, and
// the caller says what time it is, so it is tested over the pretend network with a made-up clock.
//
// A window of NET_REL_WINDOW messages in flight. Each is sent with a sequence number and resent, with a growing wait, until the other
// end says it has it; the other end says so with a cumulative acknowledgement plus a bitmap of the next 32 (so one lost message does not
// make the rest wait to be resent). Messages are delivered in sequence order. The wait is the measured round trip plus four times its
// swing, never shorter than 40 ms or longer than a second. Messages are for the handshake and rare events, not for a stream: a small
// window and a small message size, on purpose.
#define NET_REL_WINDOW  32
#define NET_REL_MAX_MSG 512
#define NET_REL_GIVEUP  12                       // sends of one message without an answer before the connection is called dead

typedef void (*NetRelEmit)(void *user, const uint8_t *pkt, size_t len);

typedef struct {
    uint16_t sendNext, sendBase;                 // the next sequence number to use; the oldest not yet acknowledged
    struct { bool used; uint8_t tries; uint16_t len; double sentAt, firstAt; uint8_t data[NET_REL_MAX_MSG]; } out[NET_REL_WINDOW];
    uint16_t recvNext;                           // the next sequence number to hand to the caller
    struct { bool have; uint16_t len; uint8_t data[NET_REL_MAX_MSG]; } in[NET_REL_WINDOW];
    double   srtt, rttvar, rto;
    bool     haveRtt, dead;
    unsigned sent, retransmits, delivered, duplicates, outOfWindow, acksSent;
} NetRel;

void NetRel_Init(NetRel *r);
// Queues a message and sends it now. False when it is empty or over NET_REL_MAX_MSG, or the window is full.
bool NetRel_Send(NetRel *r, const uint8_t *msg, size_t len, double nowMs, NetRelEmit emit, void *user);
// A packet from the other end (data or acknowledgement). False when it is not one of ours or is malformed.
bool NetRel_OnPacket(NetRel *r, const uint8_t *pkt, size_t len, double nowMs, NetRelEmit emit, void *user);
// Resends what has waited too long. Call often (every few milliseconds is fine).
void NetRel_Tick(NetRel *r, double nowMs, NetRelEmit emit, void *user);
// The next message in order, copied into buf: its length, 0 when none is ready, -1 when it did not fit (it is dropped).
int  NetRel_Recv(NetRel *r, uint8_t *buf, size_t cap);

#endif
