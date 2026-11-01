// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectile_patterns.h and projectile_patterns.c"
// ============================================================================
#ifndef PROJECTILE_PATTERNS_H
#define PROJECTILE_PATTERNS_H

#include <stdint.h>

#include "raylib.h"

// How the pellets of one pull of the trigger are spread. The numbers match Pattern in assets/config/weapons.mdix.
typedef enum { SHOT_CONE = 0, SHOT_FAN = 1 } ShotPattern;

// A small deterministic random source (xorshift32); the caller owns the state so tests can seed it.
float Rng01(uint32_t *state);                    // 0 up to, not including, 1

// Fills out[0..count) with unit directions around `forward` (right and up are the other two axes of the same frame):
//   SHOT_CONE  each pellet lands anywhere inside a cone of coneDeg degrees half-angle, evenly over its disc;
//              a single pellet in a cone of 0 degrees goes exactly along forward
//   SHOT_FAN   the pellets are spread evenly from -coneDeg to +coneDeg sideways (along right); one pellet goes straight
void Pattern_Directions(ShotPattern pattern, int count, float coneDeg, Vector3 forward, Vector3 right, Vector3 up,
                        uint32_t *rng, Vector3 *out);

#endif
