// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "lobby_backend.h"
// ============================================================================
#ifndef LOBBY_BACKEND_H
#define LOBBY_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

// What the lobby screen needs from the network, and nothing else: whether searching is possible here, start and stop a search, and the list of games
// found so far. The screen (lobby.c) never touches a socket: the real backend (lobby_net.c) answers from the LAN discovery of net_discovery.h, and the
// tests answer from a script. Strings are plain ASCII and copied into the Lobby, so a backend may reuse its buffers.
#define LOBBY_NAME_MAX    24         // the same limit as a discovery name (NET_DISC_NAME_MAX)
#define LOBBY_WHY_MAX     24
#define LOBBY_MAX_ENTRIES 16         // the same as NET_DISC_MAX_LOBBIES

typedef struct {
    char name[LOBBY_NAME_MAX + 1];
    int  players, maxPlayers;
    char transport[8];               // "UDP", "QUIC"
    bool joinable;                   // this build can connect to it and it has room
    char why[LOBBY_WHY_MAX + 1];     // when not joinable, in capitals: "FULL", "OTHER VERSION", "NEEDS QUIC"
    char     address[16];            // where to connect: the address the answer came from, dotted (what the host claims about itself is never used)
    uint16_t port;
    int      transportId;            // how to connect: the NET_TRANSPORT_* value (0 plain UDP, 1 QUIC)
} LobbyEntry;

typedef struct {
    bool        canSearch;           // false: a browser (no UDP at all); the screen shows cannotSearchReason instead of a list
    const char *cannotSearchReason;
    bool (*searchStart)(void *user); // false: the search could not start (no network, no permission)
    void (*searchStop)(void *user);
    void (*update)(void *user, double nowMs);                 // once per frame while searching: send questions, take answers
    int  (*count)(void *user);
    bool (*entry)(void *user, int index, LobbyEntry *out);    // false: that one is gone
    void *user;
} LobbyBackend;

#endif
