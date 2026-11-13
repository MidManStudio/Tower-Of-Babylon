// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_mid.h and net_mid.c"
// ============================================================================
#ifndef NET_MID_H
#define NET_MID_H

#include <stdbool.h>
#include <stdint.h>

#include "net_transport.h"

// mid-net's native transport (QUIC, from the mid-engine repository's crate mid-net-transport-quinn-ffi) under the game's NetTransport seam.
// The same shape as net_udp.h, so a server or a client picks one or the other and nothing above it changes: Listen and Accept on a server,
// Connect on a client, one NetTransport per connection. Where TOWER_MIDNET is 0 (platform.h: the build did not link the Rust library) every
// function exists and says no.
//
// Unlike plain UDP, QUIC encrypts the connection and delivers the reliable messages itself (there is no net_reliable.c under this), and a
// connection knows when the other end has closed it. What it does not do yet: the client does not check the server's certificate.

typedef struct {
    void    *server;                       // the Rust handle (MidNetQuicServer *)
    bool     open;
    uint16_t port;
} NetMidHost;

bool     NetMid_Available(void);                                                          // TOWER_MIDNET
bool     NetMidHost_Listen(NetMidHost *h, uint16_t port);                                 // 0 picks a free port; every IPv4 address
uint16_t NetMidHost_Port(const NetMidHost *h);
bool     NetMidHost_Accept(NetMidHost *h, NetTransport *out);                             // a client that has connected, if any
// Starts connecting and returns at once: the transport can be used straight away (a reliable message sent now waits in a small outbox until the
// handshake is through; a datagram sent now is dropped, as any datagram may be).
bool     NetMidHost_Connect(NetMidHost *h, const char *host, uint16_t port, NetTransport *out);
void     NetMidHost_Pump(NetMidHost *h);                                                  // nothing to do (the library does its I/O on its own threads); here so a program can treat both hosts alike
void     NetMidHost_Close(NetMidHost *h);

// Why a transport failed to connect or was closed, as text (empty when nothing to say). False for a transport that is not from this module.
bool     NetMid_TransportError(const NetTransport *t, char *buf, size_t cap);
// 0 connecting, 1 connected, 2 closed or failed, -1 not a transport from this module.
int      NetMid_TransportState(const NetTransport *t);

#endif
