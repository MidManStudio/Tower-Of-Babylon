// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectile_simulation.h and projectile_simulation.c"
// ============================================================================
#ifndef PROJECTILE_SIMULATION_H
#define PROJECTILE_SIMULATION_H

#include "projectile_types.h"

// Moves every live projectile by dt: remembers where it was (px, py, pz) for the swept hit test, ages it, and ends it at
// its lifetime, its range or the floor. Returns how many ended.
int TickProjectiles(Projectile *projs, int count, float dt);

#endif
