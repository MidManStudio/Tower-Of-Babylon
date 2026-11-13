// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_discovery.h and net_discovery.c"
// ============================================================================
#ifndef NET_DISCOVERY_H
#define NET_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net_proto.h"
#include "net_transport.h"

// Finding a game on the local network without typing an address. The idea (and the phone knowledge in it) comes from the LocalLobbyManager
// of the Unity package com.midmanstudio.netcode: a searching device broadcasts a small "who is hosting?" packet to the network, a host
// that hears it answers the sender directly, and the sender lists what answered and forgets what stops answering. The game itself is not
// touched: the answer says where to connect (the host's address as the packet arrived, the game port) and the game's own handshake
// (net_proto.h Hello/Welcome) does the rest.
//
// The packets and the lobby table need no sockets and are tested without them. The sockets (one for a host, one for a searcher) exist
// only where TOWER_NET_UDP (platform.h); elsewhere they say "no network". A browser has no UDP at all and cannot search: it needs
// an address typed in (NetDisc_RankHostAddresses tells a host which one to show).
//
// IPv4 only. Addresses are host-order numbers: 192.168.1.5 is 0xC0A80105.

#define NET_DISC_PORT        7778          // where a host listens for questions (the game itself uses 7777)
#define NET_DISC_VERSION     1             // of these packets, not of the game's protocol
#define NET_DISC_MAGIC       0x31445754u   // the bytes 'T' 'W' 'D' '1'
#define NET_DISC_ASK_SIZE    64            // a question is this long, padded with zeros, and no answer is longer: nobody can use a host to send more than they sent
#define NET_DISC_NAME_MAX    24            // a lobby's name: 1 to 24 printable ASCII characters
#define NET_DISC_MAX_LOBBIES 16
#define NET_DISC_INTERVAL_MS 2000.0        // between two rounds of questions
#define NET_DISC_TIMEOUT_MS  5000.0        // a lobby not heard from for this long is gone
#define NET_DISC_REPLY_RATE  40.0          // a host answers at most this many questions a second (and a burst of NET_DISC_REPLY_BURST)
#define NET_DISC_REPLY_BURST 20.0

// ---- the two packets ---------------------------------------------------------------------------------------------------------------
typedef struct { uint32_t nonce; } NetDiscAsk;            // a number the searcher picked; the answer must carry it back

typedef struct {
    uint32_t nonce;                        // the question's, echoed
    uint32_t hostId;                       // picked when the host started: a restarted host is told from the old one
    uint16_t protocol;                     // NET_PROTOCOL_VERSION of the host
    uint16_t port;                         // the game's UDP port
    uint8_t  players, maxPlayers;
    uint8_t  transport;                    // NET_TRANSPORT_UDP or NET_TRANSPORT_QUIC (net_transport.h): how to connect to the game port
    char     name[NET_DISC_NAME_MAX + 1];
} NetDiscAnnounce;

bool   NetDisc_NameOk(const char *name);                                                  // 1 to 24 characters, all printable ASCII
// Both encoders return the length written, or 0 if the fields are not valid or the buffer is too small. Both decoders are strict: the
// right magic, version, kind and length, zero padding, a valid name and sane numbers, or the packet is refused. What a decoder accepts
// encodes back to exactly the same bytes.
size_t NetDisc_EncodeAsk(const NetDiscAsk *ask, uint8_t *buf, size_t cap);
bool   NetDisc_DecodeAsk(const uint8_t *buf, size_t len, NetDiscAsk *out);
size_t NetDisc_EncodeAnnounce(const NetDiscAnnounce *a, uint8_t *buf, size_t cap);
bool   NetDisc_DecodeAnnounce(const uint8_t *buf, size_t len, NetDiscAnnounce *out);

// ---- what a searcher has found ------------------------------------------------------------------------------------------------------
typedef struct {
    bool            used, compatible;      // compatible: the host speaks this game's protocol version
    bool            transportSupported;    // this build can connect the way the host wants (NetDisc_TransportSupported)
    uint32_t        addr;                  // where the answer came FROM (never what the host claims about itself)
    unsigned        seq;                   // the order they were first heard in
    double          lastSeenMs;
    NetDiscAnnounce info;
} NetLobby;

typedef struct { NetLobby lobby[NET_DISC_MAX_LOBBIES]; unsigned nextSeq; } NetLobbies;

typedef enum { NETLOBBY_IGNORED = 0, NETLOBBY_NEW, NETLOBBY_CHANGED, NETLOBBY_SAME } NetLobbyResult;

void           NetLobbies_Init(NetLobbies *t);
// A lobby is told apart by its game port plus either where it answered from or its host id: a host that restarted keeps its place, and a
// host that answers from two of its addresses is one lobby (listed at the address first heard). NEW when first heard, CHANGED when its name, player count, host id
// or version differ from last time (the Unity package never reported a changing player count), SAME when nothing differs, IGNORED when
// the table is full. Every call refreshes the time it was last heard.
NetLobbyResult NetLobbies_Update(NetLobbies *t, uint32_t srcAddr, const NetDiscAnnounce *a, double nowMs);
int            NetLobbies_Expire(NetLobbies *t, double nowMs, double timeoutMs);          // returns how many were removed
int            NetLobbies_Count(const NetLobbies *t);
const NetLobby *NetLobbies_Nth(const NetLobbies *t, int n);                              // in the order they were first heard; NULL past the end
// The earliest heard that is compatible, speaks a transport this build has, and is not full.
const NetLobby *NetLobbies_FirstJoinable(const NetLobbies *t);
// Whether this build can connect over a transport: plain UDP wherever there is UDP (TOWER_NET_UDP), QUIC where the build linked mid-net (TOWER_MIDNET).
bool            NetDisc_TransportSupported(uint8_t transport);

// ---- addresses ------------------------------------------------------------------------------------------------------------------------
typedef struct { uint32_t ip, mask; char name[16]; bool up, loopback, canBroadcast; } NetDiscIface;

// A name that can carry a game on a local network: Wi-Fi, Ethernet, a hotspot, Wi-Fi Direct. Not the loopback, not mobile data (rmnet, ccmni,
// clat), not tunnels and the like.
bool     NetDisc_IfaceNameUsable(const char *name);
uint32_t NetDisc_DirectedBroadcast(uint32_t ip, uint32_t mask);                          // ip | ~mask: 192.168.1.37 /24 -> 192.168.1.255
// The subnets phones give their own hotspot or Wi-Fi Direct: 192.168.43.x (Android), 192.168.49.x (Wi-Fi Direct), 172.20.10.x (iOS),
// 192.168.137.x (Windows).
bool     NetDisc_IsHotspotRange(uint32_t ip);
// The addresses a host can tell people to connect to, best first: ordinary Wi-Fi/Ethernet addresses, then hotspot ranges. Interfaces that
// are down, loopback or unusable by name are left out. Returns how many were written (0: no usable network).
int      NetDisc_RankHostAddresses(const NetDiscIface *ifs, int n, uint32_t *out, int maxOut);
void     NetDisc_FormatIp(uint32_t ip, char out[16]);
bool     NetDisc_ParseIp(const char *text, uint32_t *ip);                                // dotted quad only

// This machine's usable IPv4 interfaces; 0 where there is no network. Fills at most max entries.
int      NetDisc_ListInterfaces(NetDiscIface *out, int max);

// ---- a host: answers questions ---------------------------------------------------------------------------------------------------------
typedef struct {
    int      fd;
    bool     open;
    uint16_t port;
    uint32_t hostId;
    NetDiscAnnounce info;                  // what is announced; the nonce is filled in per question
    double   tokens, lastRefillMs;
    unsigned received, ignored, replies, throttled;
} NetDiscHost;

// Binds the question port (0 picks a free one). There is deliberately no address reuse: a second host on the same machine fails here
// instead of silently sharing the port and answering twice.
bool     NetDiscHost_Open(NetDiscHost *h, uint16_t port);
uint16_t NetDiscHost_Port(const NetDiscHost *h);
// What to say. The name is made safe (anything not printable becomes '?', cut at 24, empty becomes "tower"). Call it again whenever the
// player count changes.
void     NetDiscHost_SetInfo(NetDiscHost *h, const char *name, uint16_t gamePort, int players, int maxPlayers, uint8_t transport);
int      NetDiscHost_Poll(NetDiscHost *h, double nowMs);                                  // answers what has come in; returns the answers sent
void     NetDiscHost_Close(NetDiscHost *h);

// ---- a searcher: asks and listens -----------------------------------------------------------------------------------------------------
typedef struct {
    int        fd;
    bool       open;
    uint16_t   port;                       // where the questions go
    uint32_t   nonce;
    double     lastAskMs;
    bool       asked;
    NetLobbies lobbies;
    unsigned   asksSent, answersSeen, ignored;
} NetDiscClient;

// The searcher's socket takes any free port, so a searcher and a host can run on the same machine.
bool     NetDiscClient_Open(NetDiscClient *c, uint16_t discoveryPort);
// Asks, if a round is due (the first call always asks): to the limited broadcast address, to the directed broadcast address of every usable
// interface, and to this machine itself (a host on the same device). Returns how many destinations it sent to; errors on any one of them
// (a network with no broadcast route) are ignored.
int      NetDiscClient_Search(NetDiscClient *c, double nowMs);
// Asks one address directly, which works across subnets and where broadcasts are filtered, and is how to find a host whose address is known.
bool     NetDiscClient_Probe(NetDiscClient *c, const char *ipv4, uint16_t port);
// Takes what has come in and drops the lobbies that went quiet. Returns how many lobbies were new or changed.
int      NetDiscClient_Poll(NetDiscClient *c, double nowMs);
void     NetDiscClient_Close(NetDiscClient *c);

#endif
