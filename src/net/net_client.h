// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_client.h and net_client.c"
// ============================================================================
#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include "net_move_sim.h"
#include "net_proto.h"
#include "net_transport.h"
#include "../dungeon/dungeon_types.h"

// The player's side. It runs the player at once from the player's own commands (prediction) so that nothing waits for the network, sends
// every command to the server (with the last few, in case a packet is lost), and when the server's answer for a tick arrives it compares
// it with what it had predicted for that tick: the same, nothing to do; different, it puts the server's state back and runs the
// commands since again (reconciliation), and shows the difference fading away instead of jumping. Other players are drawn a little in
// the past, between two snapshots (interpolation). Its clock runs a few ticks ahead of the server's so that a command arrives before the
// server needs it, and the server's reports of how early they arrive keep that lead right (a slightly faster or slower clock).
#define NET_HISTORY        64      // ticks of commands and predicted states kept for reconciling
#define NET_SNAPS          16      // snapshots kept for interpolation
#define NET_RECON_EPS      0.02f   // metres: a difference smaller than this is not worth correcting
#define NET_INTERP_TICKS   3.0f    // other players are shown this many ticks (100 ms) behind the newest snapshot
#define NET_SMOOTH_KEEP    0.85f   // how much of a correction's visible jump is left after each tick

typedef enum { NETC_IDLE = 0, NETC_HELLO, NETC_PLAYING, NETC_CLOSED } NetClientState;
typedef NetCmd (*NetCmdFn)(void *user, uint32_t tick);       // the player's wishes for a tick

typedef struct { uint32_t serverTick; double arrivedMs; int n; NetRemote r[NET_MAX_PLAYERS - 1]; } NetClientSnap;

typedef struct {
    NetTransport    tp;
    NetClientState  state;
    uint8_t         closedReason;
    bool            tpClosed;                  // the transport has been closed (once: a transport may free itself)
    char            character[NET_CHARACTER_LEN];

    uint8_t         id, maxPlayers;
    uint32_t        floorSeed;
    int             floorNumber;
    FloorMap        map;                       // made from the seed, the same as the server's
    PlayerController player;                   // the predicted player: the state after the newest tick run

    double          helloSentMs, rttMs;
    uint32_t        startTick;                 // the first tick this client runs
    uint32_t        tick;                      // the next tick to run
    struct { NetCmd cmd; PlayerController state; uint32_t tick; bool valid; } hist[NET_HISTORY];

    NetClientSnap   snaps[NET_SNAPS];          // newest first
    int             snapCount;
    uint32_t        lastServerTick, lastReconciled;
    double          lastSnapMs;

    double          lastUpdateMs, accum;
    float           rate;                      // 1, or a little above or below to move the lead
    int8_t          marginWin[20];
    int             marginN;

    Vector3         errOffset;                 // the visible jump of a correction, fading away: draw the player at position + errOffset
    Vector3         prevPos;                   // where the player was before the newest tick: drawing between the two ticks blends from here

    unsigned        corrections, hardSnaps, snapshotsSeen, snapshotsOld, ticksRun, starved;
    float           lastError, maxError;
} NetClient;

void NetClient_Init(NetClient *c, NetTransport tp, const char *character, double nowMs);   // sends Hello
void NetClient_Poll(NetClient *c, double nowMs);                                            // reads what has arrived
// Runs the ticks that are due by nowMs (asking fn for each tick's command), sending an Input for each. Returns how many ran.
int  NetClient_Update(NetClient *c, double nowMs, NetCmdFn fn, void *user);
void NetClient_Leave(NetClient *c);                                                         // tells the server and closes
// How the connection stands (NETLINK_NO_SIGNAL is what to show as NO SIGNAL), when the transport watches it (net_driver.h): false for a transport that does not.
bool NetClient_Link(const NetClient *c, NetLinkInfo *out);

Vector3 NetClient_ViewPos(const NetClient *c);                                              // where to draw the player: predicted, plus the fading jump
// The same, between the last two ticks: alpha is 0 at the older and 1 at the newest (NetClient_Alpha is how far the next tick has got), so
// a screen faster than the 30 ticks a second still sees smooth movement. A correction does not show as a jump here either.
Vector3 NetClient_ViewPosAt(const NetClient *c, float alpha);
float   NetClient_Alpha(const NetClient *c);
// The viewLag a command made now for `tick` carries (net_proto.h): that tick minus the server tick the others are drawn at on this screen right now
// (the estimated server tick minus NET_INTERP_TICKS), rounded, 0 to 255; 0 before the first snapshot. The server rewinds by it to judge a shot.
uint8_t NetClient_ViewLag(const NetClient *c, double nowMs, uint32_t tick);                                                // 0 to 1: how far the next tick is from the last
// Where another player is drawn, nowMs; false when no snapshot has had them. Between two snapshots, a little in the past; past the newest, held.
bool NetClient_RemoteView(const NetClient *c, uint8_t id, double nowMs, Vector3 *pos, float *yaw);

#endif
