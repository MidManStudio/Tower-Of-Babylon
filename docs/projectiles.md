# Projectiles: flight, hits, tracers

## Overview

A shot is a projectile in a pool of 64. What it does is set by its config in `assets/config/projectiles.mdix` (see `config.md`), picked by the gun that fired it: one config per kind of shot, with speed, drop, range, how far it hurts, whether it pierces, and how it looks. The pool is the old one rebuilt: it used to draw a large wireframe sphere (`DrawSphereWires`), pass through walls, test only where a projectile was at the end of a frame (so a fast one could jump over an enemy), and give every shot the pistol's damage. Now a shot is drawn as a glowing streak, stops at walls, is followed along its whole path each frame, and carries the damage of the gun that fired it, less what distance takes off.

The config format follows the Unity project's `ProjectileConfigSO` (movement type, speed, lifetime, gravity, range, piercing and collisions, trail) and `WeaponDefinitionSO` (a weapon names a projectile config; pellets and spread belong to the weapon). Ported: Straight, Wave and Circular movement (Guided and Teleport are accepted as numbers but fly straight), None and Piercer and Random piercing (Random is treated as Piercer), range, gravity scale, trail length and width, colour. Not ported: damage curves as curves (a start, an end and a minimum instead), headshots and crits, impact prefabs, the spline, ring, V and formula patterns.

## Modules (all in `src/projectiles/`)

### `projectile_types.h`

`ProjectileConfig` (one config) and `Projectile` (one live projectile). The `MOVE_*` and `PIERCE_*` numbers match Unity's enums and the enums in `projectiles.mdix`. A config:

| Field | Meaning |
|---|---|
| `id` | what a weapon calls it |
| `motion` | `MOVE_STRAIGHT` (the only one guns use), `MOVE_WAVE`, `MOVE_CIRCULAR` |
| `speedMin`, `speedMax` | m/s; each pellet picks its own between them |
| `lifetime`, `maxRange` | seconds and metres; it ends at whichever comes first |
| `gravity` | 1 falls like a stone (9.8 m/s per second); 0 flies dead straight |
| `radius` | added to a target's own radius when testing a hit |
| `falloffStart`, `falloffEnd`, `falloffMin` | full damage up to start, fading to `damage * falloffMin` at end and beyond |
| `piercing`, `maxHits` | `PIERCE_NONE` ends at the first hit; `PIERCE_PIERCER` goes on through until `maxHits` targets are hit |
| `trailLength`, `trailWidth` | the streak, metres |
| `colorCore`, `colorGlow` | 0xRRGGBB |

### `projectile_config.h` and `projectile_config.c`

The table of configs, at most 16. `ProjectileConfig_SetAll` is called by the loader (`game_config.c`); a weapon holds the index of its config. It is separate from the pool so the loader and the weapon tests do not need any drawing code.

### `projectile_patterns.h` and `projectile_patterns.c`

How the pellets of one pull of the trigger are spread: `SHOT_CONE` puts each pellet anywhere inside a cone of the given half-angle, evenly over its disc (the angle is the cone's angle times the square root of a random number, so the middle is not crowded); a single pellet in a 0 degree cone goes exactly forward. `SHOT_FAN` spreads pellets evenly from minus to plus the angle, sideways; one pellet goes straight. `Rng01` is a small xorshift32 whose state the caller owns, so tests can seed it. This file used to hold a partial port of the Rust pattern generator (ring, V, spline), which nothing used.

### `projectile_simulation.h` and `projectile_simulation.c`

`TickProjectiles` moves every live projectile by `dt` (straight with gravity, wave, circular), remembers where it was (`px`, `py`, `pz`) for the swept test, adds the distance actually flown to `travelDist`, and ends it at its lifetime, its range, or when it is under the floor. Range is measured by the distance the position really moved, whatever the movement type did.

### `projectiles.h` and `projectiles.c`

- `Projectiles_Spawn(config, origin, dir, speed, damage)` fires one. When the pool is full it replaces the one that has flown longest, so a shot is never dropped.
- `Projectile_FalloffDamage(config, baseDamage, travel)`.
- `ResolveProjectileHits(map, hitFn)`, run after the move each frame. For every live projectile it takes the path from where the projectile was to where it is, and:
  1. casts it against the floor map's walls, floor and ceiling (`Dungeon_Raycast`, the same test the player's collision uses); a wall ends the projectile at the wall, and leaves a spark;
  2. offers points along the path up to that wall to `hitFn(position, radius, damage)`, every 0.25 m (a new projectile also tests the place it left the gun). The first point `hitFn` accepts ends the projectile, or, for a piercer, counts a hit and lets it go on. The damage offered is the falloff damage at the distance that point is from the muzzle.
  So the nearest of a wall and a target wins, a fast round cannot jump over a target between two frames (the test places a rifle round, 1.42 m a frame, so that no frame end is inside the target, and it still hits), and a target behind a wall is not hit.
- A piercing projectile skips hit tests for the next 1.6 m after a hit, which is more than the longest way through a target of up to 0.7 m radius, so one target is not hit twice. (A first value of 1.0 m was short of the 1.2 m path through a 0.5 m target with a 0.1 m round; found while writing the test for it, before it ran.)
- **Wall sparks.** A projectile that hits a wall, floor or ceiling makes an impact: a short flash straight out of the surface and four sparks that fly out and fade in 0.22 s. A pool of 24; when full the oldest is replaced.
- `DrawProjectilesSystem(viewer)`: each projectile is three strips that turn about their own axis to face the viewer: a wide faint glow (3.4 times the width), a bright line that fades from solid at the head to nothing at the tail, and a short fat blob that is the bullet itself. The streak is cut to the distance flown, so a round that has just left the gun does not draw a tail behind the muzzle. Drawn additively (`BLEND_ADDITIVE`), with depth testing on and depth writes off, culling off, and **after everything opaque** (`main.c` calls it after the stairs): light that adds to what is behind it must come last, or an enemy drawn later would paint over the streak in front of it. Seen from behind, a round flying straight away is seen end-on and shrinks to a dot of its width with a foreshortened tail, which is how a tracer looks.
- `Projectiles_Clear` is called on every floor change and on respawn so nothing flies from one floor into the next. `Projectiles_Active`, `Projectiles_Get` and `Impacts_Active` are for tests and the overlay.

### `tests/projectiles_headless.c`

Build line in the file. Checks, with stubs that collect the drawn quads as triangles: the random source (range, same seed same numbers), cone and fan geometry (all unit vectors, none outside the cone, spread over it, a fan symmetric and reaching its edge exactly); the falloff slope (full to the start, 80% halfway, the minimum from the end on; buckshot at 15% by 22 m); flight (40 m/s for half a second goes 20 m; buckshot drops by half g t squared scaled by its gravity; it ends at its lifetime; a rifle round ends at its 70 m range after about 50 frames, before its lifetime; one under the floor ends); the swept test (the jump-over case above, full damage at 6 m, a pistol round at 50 m doing about 69%, a wall hiding a target, a target in front of a wall hit first and the wall not sparked, a shot into the floor and into the ceiling, a target at point-blank range); piercing (a piercer with two hits goes through the first target, hits the second and stops, each target hit once); the pool (full, then the oldest replaced); drawing (nothing when empty, three strips per projectile, strips square to the flight direction and turned toward the viewer, the streak cut to the distance flown and then the full 1.8 m, bright head and faded tail, the render state put back); and `Gunfire_Shoot` (see `weapons.md`).

## Fixes and Problems

- The old draw call drew a 0.3 m wireframe sphere per shot (`DrawSphereWires`), which is what showed up on the phone. Replaced by the streaks.
- A shot used to be tested only where it was at the end of a frame, against a radius of 0.25 m. At the old 22 m/s that was 0.37 m a frame, nearly enough; the new speeds (55 to 85 m/s) are 0.9 to 1.4 m a frame and would have gone through enemies. Hence the swept test.
- Shots used to pass through walls and the floor was their only end. They now stop at walls, floor and ceiling.
- Every shot used to do the pistol's 25. A shot now carries its weapon's damage.
- The pool was 32; a shotgun blast is 8 projectiles and a rifle fires 8 shots a second with a 1.2 s lifetime, so it is 64.

## Tuning

Everything about a shot is in `projectiles.mdix`. In code: `HIT_STEP` (0.25 m) and `PIERCE_SKIP` (1.6 m) in `projectiles.c`; the spark count and life (`IMPACT_SPARKS`, `IMPACT_SECS`); the glow width multiplier (3.4) and the blob in `DrawProjectilesSystem`.

## Known limits

- The stairs (the blocks between floors) are not part of the wall test, which is the floor map's grid plus the floor and ceiling planes, so a shot on the stairs passes through the steps.
- A hit is a sphere against an enemy's cylinder; there are no headshots.
- Piercing assumes targets under 0.7 m in radius.
- Enemies do not shoot yet, so the pool is the player's only.
- The tracers are not lit by the light map (they glow by themselves).
- How the streaks look and cost on the phone is not verified. Each projectile is 12 vertices; with a rifle at full auto about ten are alive at once.
