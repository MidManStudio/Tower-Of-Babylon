// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_driver.h and net_driver.c"
// ============================================================================
#ifndef NET_DRIVER_H
#define NET_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "net_proto.h"
#include "net_transport.h"

// mid-net's driver (the Rust crate in the mid-engine repository, crates/mid-net/src/driver.rs, reached through its C interface) between the
// game and any other NetTransport (plain UDP, QUIC, the pretend network of the tests). What it adds, and what the game's own code then no longer
// has to do: a handshake that refuses a build with a different protocol, a keepalive that tells NO SIGNAL (a second and a half of silence) from
// a dead connection (ten seconds, or the other side's goodbye), the round trip and the server's clock, datagrams that go in priority order
// within a byte budget and expire when they are late, and a goodbye that says why.
//
// The game's server and client are not changed: this is a NetTransport made of a NetTransport. Datagrams (Input, Snapshot) travel on a sequenced
// channel (a snapshot that arrives after a newer one is dropped), reliable messages (Hello, Welcome, Disconnect) on a reliable one, and the
// result says how the connection stands through NetTransport.linkInfo, which makes NetServer and NetClient leave their own 5 s silence rules to
// the link.
//
// Both ends must use it: it is a different wire format from the plain one. Only where TOWER_NETDRIVER (platform.h): elsewhere every function says no.
// The Rust library comes with the QUIC one (mid-net-transport-quinn-ffi exports both) or alone (mid-net).

bool NetDriver_Available(void);                                   // TOWER_NETDRIVER

// The client's end of a connection over `lower` (a transport that has just been made, nothing sent on it yet). `*out` is the transport for NetClient_Init.
bool NetDriver_Client(NetTransport lower, const char *name, NetTransport *out);
// The server's end of one client's connection (the transport NetUdpHost_Accept or NetMidHost_Accept gave): `*out` is for NetServer_AddClient.
// sessionId is what the client is told (any number: the slot, say).
bool NetDriver_Server(NetTransport lower, uint32_t sessionId, NetTransport *out);

// Why a transport that has ended says it did, as a Disconnect reason of the game (NET_DISC_*): the other side's goodbye keeps its reason (full, a
// different version), a silence is a timeout. For any transport whose linkInfo says ENDED; header-only: needs no library (and so no extra source in a build that does not use the driver).
static inline uint8_t NetDriver_EndToDisc(const NetLinkInfo *li)
{
    switch (li->endKind) {
    case NETLINK_END_LOCAL: return NET_DISC_NORMAL;
    case NETLINK_END_PEER:
    case NETLINK_END_REJECTED:                      // the numbers are mid-net's DisconnectReason codes
        switch (li->endPeer) {
        case 0: return NET_DISC_NORMAL;
        case 1: case 2: return NET_DISC_VERSION;    // the version, or the schema (the protocol version is in its hash)
        case 3: return NET_DISC_FULL;
        case 4: case 5: return NET_DISC_TIMEOUT;
        case 6: return NET_DISC_NORMAL;             // sent away: the game has no such reason, the leaving was ordered
        default: return NET_DISC_BAD;
        }
    case NETLINK_END_PROTOCOL: return NET_DISC_BAD;
    default: return NET_DISC_TIMEOUT;               // a silence, or a connection that was never made
    }
}


// The clock the driver uses, in milliseconds that only go forward. NULL (the default) is the real one; a test hands in its pretend one.
void NetDriver_SetClock(double (*nowMs)(void));

typedef struct {
    unsigned datagramsSent, datagramsReceived, badDatagrams, staleDatagrams, droppedExpired, droppedQueueFull;   // the driver's own counts
    unsigned reliableBytesSent, reliableBytesReceived;
    unsigned dgRingDrops;          // received datagrams the game did not read in time (the oldest went)
    unsigned heldEarly;            // reliable messages sent before the handshake finished (they wait, and go when it has)
    unsigned lowerQueuePeak;       // the most reliable chunks that waited for the transport under (it was full)
    unsigned oversizeDropped;      // messages from the other side bigger than the game ever sends (a peer that speaks the driver but not the game): dropped
    unsigned faults;               // times the driver ended the connection itself (a transport error, bytes that are not a message ...)
    unsigned events;               // events seen (connected, no signal, recovered, ended, fault ...)
} NetDriverStats;

// False for a transport that is not from this module.
bool NetDriver_Stats(const NetTransport *t, NetDriverStats *out);

#ifdef NET_DRIVER_TEST
// Test only: sends `len` bytes on the driver channel (2 the game's datagrams, 3 its reliable messages) past the checks the game's own side makes, as a hostile peer would.
bool NetDriver_TestInject(const NetTransport *t, int channel, const uint8_t *data, size_t len);
#endif

#endif
