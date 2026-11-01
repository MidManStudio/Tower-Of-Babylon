// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "game_config.h and game_config.c"
// ============================================================================
#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#include <stdbool.h>

#include "mdix_doc.h"

// Loads the weapon and projectile tables from assets/config/*.mdix.
//   1. GameConfig_LoadBaked installs the baked copy of the three files in assets/config/ (weapons, projectiles, flashlight) (config_baked.c): always works, needs nothing.
//   2. GameConfig_Init does that, then (web builds) starts the DixScript wasm in the background; GameConfig_Poll, called
//      once a frame, waits for it, reads the .mdix files from the bundled assets and replaces the baked values with
//      what the files say now. If anything about that goes wrong the baked values stay and the status says why.
// Either way the game is playable from the first frame.

typedef enum { CONFIG_FROM_BAKED = 0, CONFIG_FROM_FILES } ConfigSource;

typedef struct {
    ConfigSource  source;        // where the numbers in use came from
    MdixWasmState wasm;          // the DixScript wasm
    bool          bakedStale;    // the files differ from what config_baked.c was made from (run tools/mdix_bake.mjs)
    char          note[240];     // the first problem found, or ""
} ConfigStatus;

void GameConfig_LoadBaked(void);
void GameConfig_Init(void);
void GameConfig_Poll(void);
const ConfigStatus *GameConfig_Status(void);

// Builds the tables from three documents (weapons, projectiles, flashlight) and installs them when everything checks out. False (and nothing installed)
// when a value is missing, of the wrong type or out of range; err says which. This is what the two above call, and
// what the tests use to try documents of their own.
bool GameConfig_LoadFrom(const MdixDoc *weapons, const MdixDoc *projectiles, const MdixDoc *flashlight, char *err, int errCap);

#endif
