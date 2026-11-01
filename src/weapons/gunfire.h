// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "gunfire.h and gunfire.c"
// ============================================================================
#ifndef GUNFIRE_H
#define GUNFIRE_H

#include <stdint.h>

#include "weapons.h"
#include "../dungeon/dungeon_types.h"

// Turns one pull of the trigger into projectiles: where they start, which way each goes, how fast, and what each carries.
// The arsenal says a shot happened and spends the ammo; this fires it. Everything about the shot except the aim comes from
// the weapon's row in weapons.mdix and its projectile config in projectiles.mdix.
//
//   * The shot is aimed at the point the crosshair is on (the first wall along the view ray, at least 2.5 m and at most
//     40 m away), not parallel to it, so what is under the crosshair is what is hit even though the muzzle is off to the
//     side and below the eye.
//   * It starts at the weapon's own muzzle (Aim_MuzzleFor), unless a wall is between the eye and the muzzle (the player is pressed against it), in which
//     case it starts just in front of the wall, so a shot cannot be fired through it.
//   * Pellets are spread by pattern over spreadDeg degrees; `rng` is the random state (the caller owns it).
// map may be NULL (no walls to consider). Returns the number of projectiles fired.
int Gunfire_Shoot(const WeaponDef *def, const AimFrame *aim, const FloorMap *map, float spreadDeg, uint32_t *rng);

#endif
