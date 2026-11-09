// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_udp.h and net_udp.c"
// ============================================================================
#ifndef NET_UDP_H
#define NET_UDP_H

#include <stdbool.h>
#include <stdint.h>

#include "net_proto.h"
#include "net_reliable.h"
#include "net_transport.h"

// The real network: UDP sockets under the NetTransport interface. Datagrams go straight out as UDP packets; reliable messages go through
// NetRel (net_reliable.h), which resends them until they are acknowledged. Every packet starts with a 32-bit connection id that the
// client picks, so one server socket serves many players and tells them apart. Only where TOWER_NET_UDP (platform.h): elsewhere every
// function here says "no network".
//
// This is for a LAN or a server with an open port, in plain text: nothing here proves who is who (the connection id is a random number,
// not a secret) or hides what is sent. The QUIC transports are the way to do that; they go behind the same NetTransport.
#define NET_UDP_MAX_PEERS 8
#define NET_UDP_INBOX     32           // datagrams kept per peer until the game reads them; more than that, the newest are dropped

typedef struct NetUdpHost NetUdpHost;

typedef struct {
    bool        used, closing, announced;
    uint32_t    connId;
    uint8_t     addr[32];              // the other end's address (an IPv4 sockaddr), opaque here
    int         addrLen;
    NetRel      rel;
    struct { uint16_t len; uint8_t data[NET_MTU]; } inbox[NET_UDP_INBOX];
    int         inHead, inCount;
    double      lastHeardMs, closeAtMs;
    NetUdpHost *host;
    unsigned    inboxDrops;
} NetUdpPeer;

struct NetUdpHost {
    int         fd;
    bool        open, isServer;
    uint16_t    port;
    NetUdpPeer  peers[NET_UDP_MAX_PEERS];
    unsigned    badPackets;
};

// Server: listens on a UDP port (0 picks a free one; NetUdpHost_Port says which), on every address of this machine.
bool NetUdpHost_Listen(NetUdpHost *h, uint16_t port);
uint16_t NetUdpHost_Port(const NetUdpHost *h);
// Server: a player has appeared since the last call (their first reliable message, the Hello, arrived): the transport to give NetServer_AddClient.
bool NetUdpHost_Accept(NetUdpHost *h, NetTransport *out);
// Client: a connection to a server at an IPv4 address or name and port. Nothing is sent yet; the first message the game sends opens it.
bool NetUdpHost_Connect(NetUdpHost *h, const char *host, uint16_t port, NetTransport *out);
// Reads the socket and runs the resend timers. The transport's own receive functions do this too, so a game that only polls its
// transports needs no more than NetUdpHost_Accept on a server.
void NetUdpHost_Pump(NetUdpHost *h);
void NetUdpHost_Close(NetUdpHost *h);

double NetUdp_NowMs(void);             // a clock that only goes forward, in milliseconds
// For tests and for trying the game on a bad network: each datagram and reliable packet sent is lost, or sent twice, with these chances (0 to 1).
void NetUdp_SetDebug(float lossChance, float dupChance);

#endif
