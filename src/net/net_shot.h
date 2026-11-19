// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net_shots.md, section "net_shot.h and net_shot.c"
// ============================================================================
#ifndef NET_SHOT_H
#define NET_SHOT_H

#include <stdint.h>

// The seed of one shot, the same on every machine that works it out: from who fired, at which command tick, and which shot of that tick it was
// (two weapons, or a burst). Gunfire_Shoot's pellet spread and speeds come from this one number (its `rng` argument), so the shooter's screen
// (which predicts the shot at once) and the server (which makes the real one) throw the same pellets without a seed or a direction travelling.
// Never 0 (the generator needs a non-zero state).
uint32_t NetShot_Seed(uint8_t playerId, uint32_t tick, uint8_t shotIndex);

#endif
