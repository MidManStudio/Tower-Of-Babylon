// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_dial.h and net_dial.c"
// ============================================================================
#ifndef NET_DIAL_H
#define NET_DIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net_mid.h"
#include "net_transport.h"
#include "net_udp.h"

// One outgoing connection, over whichever transport the host speaks: the socket that carries it and the NetTransport the client plays through.
// That transport is mid-net's driver (net_driver.h) over the socket's own: every connection of the game is, so a build without the driver library
// cannot connect at all (NetDial_Open says so).
// It is what tower_bot did by hand, taken out so that the game can connect to a game it found in the lobby (and to its own hosted game).
typedef struct {
    NetUdpHost udp;
    NetMidHost mid;
    int        transport;        // NET_TRANSPORT_UDP or NET_TRANSPORT_QUIC
    bool       open;
} NetDial;

// Starts connecting to host (a dotted address or a name) and port; returns at once. False (err says why, in capitals) when this build cannot
// speak that transport (or has no driver library) or the address is not usable. The transport can be used straight away; the first message it sends opens the connection.
bool NetDial_Open(NetDial *d, int transport, const char *host, uint16_t port, NetTransport *out, char *err, size_t errCap);
// Lets what was sent go out for up to lingerMs (a Disconnect needs the socket for a moment), then closes the socket. Safe to call twice.
void NetDial_Close(NetDial *d, int lingerMs);

#endif
