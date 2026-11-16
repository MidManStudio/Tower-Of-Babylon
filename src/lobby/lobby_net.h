// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "lobby_net.h and lobby_net.c"
// ============================================================================
#ifndef LOBBY_NET_H
#define LOBBY_NET_H

#include <stdbool.h>
#include <stdint.h>

#include "lobby_backend.h"
#include "../net/net_discovery.h"

// The lobby's backend over the LAN discovery (net_discovery.h): searching opens a NetDiscClient, asks every couple of seconds, and lists what answers.
// Where there is no UDP (a browser) it says so and the screen shows why. There is one search at a time (a static client), which is all one lobby needs.
LobbyBackend LobbyNet_Backend(void);
void         LobbyNet_Shutdown(void);                       // closes the search if one is open (safe to call at any time)
void         LobbyNet_UsePort(uint16_t discoveryPort);      // where questions go; NET_DISC_PORT unless a test runs its own host

// One game found, as the screen shows it: false for an entry that is not in use. joinable is: a version this game speaks, a transport this build has,
// and room for one more; otherwise why says in capitals what is wrong (OTHER VERSION, NEEDS QUIC, CANNOT CONNECT, FULL). It also says where and
// how to connect: the address the answer came from, the game's port, and the transport.
bool LobbyNet_EntryFromLobby(const NetLobby *found, LobbyEntry *out);

#endif
