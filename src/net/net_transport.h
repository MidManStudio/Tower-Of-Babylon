// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_transport.h"
// ============================================================================
#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// What the game needs from a network, and no more: datagrams that may be lost, duplicated and reordered, and messages that arrive
// whole, in order, once (a QUIC stream). The server and the client talk only through this, so the lossy loopback of the tests and, later, the
// native QUIC and the browser's WebTransport (the Rust mid-net transports, reached over a C interface) are interchangeable.
// recv functions return the length of the packet copied into buf, 0 when none is waiting, or -1 when it did not fit in cap (it is dropped).
typedef struct {
    void *self;
    bool (*sendDatagram)(void *self, const uint8_t *data, size_t len);     // false when refused (too big for NET_MTU, or the link is closed)
    int  (*recvDatagram)(void *self, uint8_t *buf, size_t cap);
    bool (*sendReliable)(void *self, const uint8_t *data, size_t len);
    int  (*recvReliable)(void *self, uint8_t *buf, size_t cap);
} NetTransport;

#endif
