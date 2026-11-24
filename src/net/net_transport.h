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
// Which transport a server speaks (announced in discovery, net_discovery.h, so a searcher only offers what its build can join).
#define NET_TRANSPORT_UDP  0               // plain UDP with the game's own reliable layer (net_udp.h)
#define NET_TRANSPORT_QUIC 1               // mid-net's QUIC (net_mid.h)

// How a connection stands, for a transport that watches its own (net_driver.h): NO SIGNAL is a silence of a second and a half, and the end
// comes later (ten seconds) or at once when the other side says goodbye. A transport that does not watch (plain UDP, the pretend network)
// leaves linkInfo NULL and the game's own silence rules (net_server.h, net_client.c) apply.
enum { NETLINK_CONNECTING = 0, NETLINK_CONNECTED = 1, NETLINK_NO_SIGNAL = 2, NETLINK_ENDED = 3 };
enum { NETLINK_END_NONE = 0, NETLINK_END_LOCAL = 1, NETLINK_END_PEER = 2, NETLINK_END_TIMEOUT = 3, NETLINK_END_CONNECT_TIMEOUT = 4, NETLINK_END_REJECTED = 5, NETLINK_END_PROTOCOL = 6 };
typedef struct {
    int      state;          // NETLINK_*
    uint32_t silentMs;       // how long nothing has been heard from the other side
    float    rttMs;          // the round trip, smoothed (0 until measured)
    int      endKind;        // NETLINK_END_*, once the state is ENDED
    int      endPeer;        // the other side's reason code, when it closed the connection (0 is a normal goodbye)
} NetLinkInfo;

typedef struct {
    void *self;
    bool (*sendDatagram)(void *self, const uint8_t *data, size_t len);     // false when refused (too big for NET_MTU, or the link is closed)
    int  (*recvDatagram)(void *self, uint8_t *buf, size_t cap);
    bool (*sendReliable)(void *self, const uint8_t *data, size_t len);
    int  (*recvReliable)(void *self, uint8_t *buf, size_t cap);
    void (*close)(void *self);                                               // optional (may be NULL): the connection is finished with; a real transport lets queued messages (a Disconnect) go out and frees the connection
    bool (*linkInfo)(void *self, NetLinkInfo *out);                          // optional (NULL: this transport does not watch its connection): fills *out, true
} NetTransport;

#endif
