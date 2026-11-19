// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net_shots.md, section "net_history.h and net_history.c"
// ============================================================================
#ifndef NET_HISTORY_H
#define NET_HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "net_proto.h"

// Where every player was at each of the last server ticks: what lag compensation rewinds to. A player shoots at what the screen showed, which is
// where the others were a few ticks ago (the shooter's lead over the server, the network, and the 100 ms the client draws others behind): the server
// asks "where was everyone when this shooter looked" (NetServer_RewoundPose) and judges the shot there, so a hit on the screen is a hit.
// The pose of a tick is the one after that tick's step: exactly what the snapshot of that tick says, which is what the client draws.
#define NET_HIST_FRAMES  64        // 2.1 s of ticks kept
#define NET_REWIND_MAX   20        // 0.67 s: the furthest back a shot is judged; asked for more, it is judged at this (a cheat or a bad link gets no more)

typedef struct {
    uint32_t tick;                 // 0: an unused frame
    uint8_t  present[NET_MAX_PLAYERS];
    Vector3  pos[NET_MAX_PLAYERS];
    float    yaw[NET_MAX_PLAYERS];
} NetPoseFrame;

typedef struct { NetPoseFrame f[NET_HIST_FRAMES]; } NetHistory;

void NetHistory_Init(NetHistory *h);
// Starts the frame of `tick` (every player absent), replacing the frame it takes the place of in the ring.
void NetHistory_Begin(NetHistory *h, uint32_t tick);
void NetHistory_Set(NetHistory *h, uint32_t tick, int id, Vector3 pos, float yaw);
// The pose of player `id` at exactly `tick`; false if that tick is no longer (or not yet) kept, or the player was not there.
bool NetHistory_PoseAt(const NetHistory *h, uint32_t tick, int id, Vector3 *pos, float *yaw);
// The same between two ticks (`tick` may have a fraction), blended; false if either tick lacks the player.
bool NetHistory_PoseAtF(const NetHistory *h, double tick, int id, Vector3 *pos, float *yaw);
// The tick a shot made at `nowTick` by someone who saw the world `viewLag` ticks earlier is judged at: nowTick - viewLag, but never more than
// NET_REWIND_MAX back and never before tick 1.
uint32_t NetHistory_RewindTick(uint32_t nowTick, uint8_t viewLag);

#endif
