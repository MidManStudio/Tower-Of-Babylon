// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectiles.h and projectiles.c"
// ============================================================================
#ifndef PROJECTILES_H
#define PROJECTILES_H

#include "projectile_types.h"
#include "projectile_config.h"
#include "projectile_patterns.h"
#include "projectile_simulation.h"
#include "../dungeon/dungeon_types.h"

// Returns true if the sphere at pos hit something and should count as a hit. The projectile's own radius and its damage
// (after the distance falloff) are passed in.
typedef bool (*ProjectileHitFn)(Vector3 pos, float radius, float damage);

void InitProjectilesSystem(void);
void Projectiles_Clear(void);            // ends every projectile and impact (a new floor)

// Fires one projectile of config `config` (an index into the config table) from origin along dir (unit), at `speed` m/s,
// carrying `damage` before the falloff. When the pool is full the oldest projectile is replaced. False for a bad config.
bool Projectiles_Spawn(int config, Vector3 origin, Vector3 dir, float speed, float damage);

void UpdateProjectilesSystem(float dt);

// The swept hit test, run after the move: every projectile is followed from where it was to where it is, so a fast one cannot
// jump over a target or through a wall. The nearest of a wall (the floor map's solid tiles, floor and ceiling) and a target
// wins. A wall ends the projectile with a spark; a target (offered to `hit` in steps along the way) ends it, or lets a
// piercing one go on. map may be NULL to skip walls. Returns the number of targets hit.
int ResolveProjectileHits(const FloorMap *map, ProjectileHitFn hit);

// The damage a projectile does after flying `travel` metres: full up to the config's falloff start, fading to falloffMin.
float Projectile_FalloffDamage(const ProjectileConfig *cfg, float baseDamage, float travel);

// Draws the live projectiles as glowing streaks and the wall sparks, additively and without writing depth. Call after
// everything opaque in the 3D pass has been drawn.
void DrawProjectilesSystem(Vector3 viewer);
// For drawing between fixed steps: how long ago (seconds) the last step ended, as drawn time runs ahead of it. Rounds are drawn
// that much of their flight behind where the last step left them (never behind where they were fired from).
void Projectiles_SetRenderLag(float seconds);

int Projectiles_Active(void);
const Projectile *Projectiles_Get(int index);     // 0 .. MAX_PROJECTILES - 1; may be dead
int Impacts_Active(void);

void UnloadProjectilesSystem(void);

#endif
