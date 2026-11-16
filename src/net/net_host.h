// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_host.h and net_host.c"
// ============================================================================
#ifndef NET_HOST_H
#define NET_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "net_discovery.h"
#include "net_mid.h"
#include "net_server.h"
#include "net_udp.h"

// A hosted game, as a thing that can be opened, updated every frame, and closed: the server (net_server.h) with the socket that gives it
// its players (plain UDP or QUIC) and the announcer that lets others on the network find it (net_discovery.h). It is the loop that was
// in the dedicated server's main(), taken out so the game can host too: tower_server calls it from a loop of its own, the game calls it
// from its frame. The caller supplies the clock (any clock that only goes forward, in milliseconds, the same on every call).
typedef struct {
    uint32_t    seed;                // the floor every player is given
    int         floorNumber;
    const char *name;                // what the lobby list shows (1 to 24 printable characters; anything else becomes "TOWER")
    int         transport;           // NET_TRANSPORT_UDP or NET_TRANSPORT_QUIC
    uint16_t    port;                // the game's port; 0 picks a free one
    bool        anyPortIfBusy;       // port is taken: take a free one instead of failing
    uint16_t    discoveryPort;       // where "who is hosting?" is answered
    bool        discovery;           // answer it at all
} NetHostConfig;

typedef struct {
    NetServer    server;
    NetUdpHost   udp;
    NetMidHost   mid;
    NetDiscHost  disc;
    bool         open, discoveryOpen;
    int          transport;
    uint16_t     port;               // the game port in use (what NetUdpHost_Port or NetMidHost_Port said)
    char         name[NET_DISC_NAME_MAX + 1];
    char         error[96];          // why Open said no
    double       tickMs, nextTickMs;
    int          announced, peak;    // the player count last announced; the most players there were at once
    unsigned     accepted, refused;  // connections given a place; connections turned away (full)
} NetHost;

void NetHostConfig_Default(NetHostConfig *c);                              // seed 12345, floor 1, "TOWER", plain UDP, port 7777 (or any free one), discovery on
bool NetHost_Open(NetHost *h, const NetHostConfig *cfg, double nowMs);     // false: h->error says why (the port is taken, no UDP or QUIC in this build)
// Once per frame (or as often as the caller likes): takes new connections, reads what arrived, runs the ticks that are due (at most 5, then it
// gives up catching up), and keeps the announcement's player count right.
void NetHost_Update(NetHost *h, double nowMs);
int  NetHost_Players(const NetHost *h);
// Tells every player the game is over (a Disconnect), lets it go out for up to lingerMs (the packets of a UDP peer that is closing need the socket
// for a moment more), then closes the socket and the announcer. lingerMs 0 closes at once. Safe to call twice.
void NetHost_Close(NetHost *h, int lingerMs);

#endif
