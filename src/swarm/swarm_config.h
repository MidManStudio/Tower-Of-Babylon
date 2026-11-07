// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/swarm.md, section "swarm_config.h and swarm_config.c"
// ============================================================================
#ifndef SWARM_CONFIG_H
#define SWARM_CONFIG_H

#include <stdbool.h>

#include "../config/mdix_doc.h"

// Every dot of every worm of a swarm lives in fixed arrays of this size.
#define SWARM_MAX_DOTS  1024
#define SWARM_MAX_WORMS 64

// The swarm's numbers, read from assets/config/swarm.mdix (a baked copy, or the file itself on the web build when the DixScript wasm
// is up). Until a document is read, and when one is refused, the defaults below (the same as the file) are used.
typedef struct {
    int   worms, dotsPerWorm, iterations;
    int   firstFloor, everyNFloors, showCore;
    float dotRadius, linkLength, crawlSpeed, speedSpread, weave, gravity, damping, squirm;
    float senseRange, spawnMinDist;
    float coreHealth, coreHalf, deathSecs;
    float biteDamage, biteWindup, biteInterval, biteCap, biteReach;
} SwarmConfig;

void SwarmConfig_Defaults(SwarmConfig *c);
const SwarmConfig *SwarmConfig_Get(void);

// Reads the document. Installs it only when every value is there and in range (and worms x dots_per_worm fits the arrays);
// otherwise nothing changes and err says why.
bool SwarmConfig_LoadFrom(const MdixDoc *doc, char *err, int errCap);

// Hands swarm.mdix to the config layer (the baked copy at once, the real file when the wasm is up). Before GameConfig_Init.
void Swarm_Register(void);

#endif
