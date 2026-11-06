// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/characters.md, section "characters_register.c"
// ============================================================================
#include "characters.h"

#include "../config/game_config.h"

extern const MdixBaked g_bakedCharacters;            // config_baked.c, written by tools/mdix_bake.mjs

// Apart from characters.c so that the tests can read the roster without linking the whole config layer.
void Characters_Register(void) { GameConfig_RegisterExtra("assets/config/characters.mdix", &g_bakedCharacters, Characters_LoadFrom); }
