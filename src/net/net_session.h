// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_session.h and net_session.c"
// ============================================================================
#ifndef NET_SESSION_H
#define NET_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "net_client.h"
#include "net_dial.h"
#include "net_host.h"

// One player's part in a networked game, whether they host it or joined it: a state machine that owns what is needed and says where things
// stand in words the lobby can show. HOSTING runs a NetHost (the server, its socket, its announcer) and the host plays as one more client of
// its own game, over a real connection to 127.0.0.1 (the same path every other player takes, so there is one way to play, not two). JOINING
// dials a game found in the lobby. The caller calls NetSession_Update once per frame and looks at `state`.
// The player's commands come from whoever NetSession_SetCommands names (the walking screen: net_play.h); until one is named they are all "stand still".
#define NETS_NAME_MAX           24
#define NETS_CONNECT_TIMEOUT_MS 6000.0     // a join that has no answer by then fails ("NO ANSWER")

typedef enum { NETS_IDLE = 0, NETS_CONNECTING, NETS_PLAYING, NETS_FAILED, NETS_ENDED } NetSessionState;

typedef struct { bool used, you, host; uint8_t id; char character[NET_CHARACTER_LEN]; } NetSessionSlot;

typedef struct {
    NetSessionState state;
    bool            hosting;
    char            name[NETS_NAME_MAX + 1];     // the lobby's name: ours when hosting, the game joined otherwise
    char            error[64];                   // why the session is FAILED or ENDED, in capitals, for the screen
    double          startedMs, connectTimeoutMs;
    NetHost         host;
    NetDial         dial;
    NetClient       client;
    NetCmdFn        cmdFn;                       // where the player's commands come from (NetSession_SetCommands); stand still when not set
    void           *cmdUser;
} NetSession;

bool NetSession_CanHost(void);                   // this platform has UDP (a browser does not)
// Open a game for others to join, and join it as its first player. False (state FAILED, error filled) when it cannot be done.
bool NetSession_Host(NetSession *s, int transport, const char *lobbyName, const char *character, uint32_t floorSeed, double nowMs);
// Join the game at address:port. Returns at once with state CONNECTING; the answer shows in later updates (PLAYING, or FAILED with a reason).
bool NetSession_Join(NetSession *s, int transport, const char *address, uint16_t port, const char *name, const char *character, double nowMs);
// Who gives the player's command for each tick: NetPlay_CmdFn with a NetPlay walks (net_play.h). Not set (the default after Host and Join), the
// player stands still. Set it after Host or Join returned: both start the session from nothing.
void NetSession_SetCommands(NetSession *s, NetCmdFn fn, void *user);
void NetSession_Update(NetSession *s, double nowMs);            // once per frame
void NetSession_Leave(NetSession *s);                           // say goodbye and close everything (a moment of waiting for the goodbye to go out); the state is IDLE after; safe to call at any time
int  NetSession_Slots(const NetSession *s, NetSessionSlot out[NET_MAX_PLAYERS]);   // who is in the game, in id order
int  NetSession_Players(const NetSession *s);
// What a Disconnect reason means to the player, in capitals ("THE GAME IS FULL"); connecting says whether it was the join that failed.
const char *NetSession_ReasonText(uint8_t reason, bool wasPlaying);

#endif
