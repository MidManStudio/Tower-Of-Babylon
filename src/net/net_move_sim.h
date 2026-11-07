// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_move_sim.h and net_move_sim.c"
// ============================================================================
#ifndef NET_MOVE_SIM_H
#define NET_MOVE_SIM_H

#include "net_proto.h"
#include "../player/player_controller.h"

// The part of the game the netcode runs so far: a player moving on a floor. Server and client both step a player with exactly this
// function from the same NetCmd, which is what makes prediction work: the client's guess is the server's answer unless something the
// client could not know got in the way. It is the same movement the single-player game uses (PlayerStep and the jump and slide rules).
// When the rest of the rules (SimTick) move out of main.c, they replace this.
void NetSim_Spawn(PlayerController *p, float x, float z);
void NetSim_Step(PlayerController *p, const NetCmd *cmd, const FloorMap *map);      // one fixed step of NET_TICK_SECS

void  NetSim_Capture(const PlayerController *p, NetOwnerState *out);                // the movement state, for the owner
void  NetSim_Apply(PlayerController *p, const NetOwnerState *s);                    // put it back (the view-only parts of the player are left as they are)
float NetSim_Error(const PlayerController *p, const NetOwnerState *s);              // how far apart they are: the position gap in metres, or a large number when anything else differs by enough to matter

void NetSim_Remote(const PlayerController *p, uint8_t id, NetRemote *out);         // what others are told

#endif
