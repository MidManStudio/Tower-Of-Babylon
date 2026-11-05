// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "floor_config.h and floor_config.c"
// ============================================================================
#ifndef FLOOR_CONFIG_H
#define FLOOR_CONFIG_H

#include <stdbool.h>

#include "floor_run.h"
#include "../config/mdix_doc.h"

// The stair clock's numbers, read from assets/config/floor.mdix (a baked copy, or the file itself on the web build when the
// DixScript wasm is up). Until a document is read, and when one is refused, the defaults of FloorRun_DefaultConfig are used.
const FloorRunConfig *FloorConfig_Get(void);

// Reads the document. Installs it only when every value is there and in range; otherwise nothing changes and err says why.
bool FloorConfig_LoadFrom(const MdixDoc *doc, char *err, int errCap);

#endif
