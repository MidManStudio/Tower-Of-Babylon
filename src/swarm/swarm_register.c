// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/swarm.md, section "swarm_register.c"
// ============================================================================
#include "swarm_config.h"

#include "../config/game_config.h"

extern const MdixBaked g_bakedSwarm;            // config_baked.c, written by tools/mdix_bake.mjs

// Apart from swarm_config.c so that the tests can read the numbers without linking the whole config layer.
void Swarm_Register(void) { GameConfig_RegisterExtra("assets/config/swarm.mdix", &g_bakedSwarm, SwarmConfig_LoadFrom); }
