// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectile_config.h and projectile_config.c"
// ============================================================================
#ifndef PROJECTILE_CONFIG_H
#define PROJECTILE_CONFIG_H

#include "projectile_types.h"

// The table of projectile configs. Filled by the config loader (src/config/game_config.c) from projectiles.mdix; a weapon
// holds an index into it. Kept apart from the projectile pool so the loader and the weapon tests need no drawing code.
void ProjectileConfig_SetAll(const ProjectileConfig *configs, int count);
int  ProjectileConfig_Count(void);
const ProjectileConfig *ProjectileConfig_Get(int index);   // NULL when out of range
int  ProjectileConfig_Find(const char *id);                // -1 when there is none

#endif
