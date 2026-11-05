# Enemies module

Cylinder enemies and procedural spiders for Tower of Babylon, plus the hook that lets projectiles hurt them.

## Overview

Two kinds of enemy share one entry point, `enemy_manager.h`, which `main.c` calls.

Cylinder enemies are tall procedural cylinders. For each floor the manager picks spawn tiles on a lattice, shuffles them from the floor seed so the enemies spread over the whole floor, and varies height, radius, colour and health from a hash of the seed. They stay idle until they see the player, chase while they have line of sight and for three seconds after losing it, and stop at attack range. Line of sight uses the same grid ray caster as the dungeon generator. Movement is split by axis so an enemy slides along a wall instead of sticking to it or passing through it.

Spiders are eight-legged crawlers that walk on floors, walls and the ceiling, built from a locomotion memo: anchor on the surface, front legs touch a new surface before the anchor switches, alternating leg groups, three-ray terrain search, body orientation from the plane of the feet, and two-bone leg IK. They find their way through the maze with a path field computed from the player's tile. They stalk along the ceiling while the player is far, and when the player is within 9 m they commit to coming down a wall to the floor.

Spawn counts: cylinder enemies `6 + 2 * floor` up to 24, spiders `1 + floor / 4` up to 4. Neither kind spawns within 16 tiles (18 for spiders) of either arrival point.

## Modules

### `enemy_types.h`

`Enemy` and `EnemyState`. The fields are `id`, `position` (centre of the base), `radius`, `height`, `tint`, `health`, `state`, `aggroRange`, `attackRange`, `moveSpeed`, `alertTimer` and `active`.

### `enemy_manager.h` and `enemy_manager.c`

The enemy system. `InitEnemiesSystem`, `SpawnEnemiesForFloor(floorMap, tier)`, `UpdateEnemiesSystem(dt, playerPos, floorMap)`, `DrawEnemiesSystem`, `UnloadEnemiesSystem`, `CheckAndHandleEnemyDamage(pos, radius, damage)` and `CountActiveEnemies`. Each call also drives the spider system, so `main.c` never touches spiders directly. A damage check tests cylinders first and then spiders. The pool holds `MAX_FLOOR_ENEMIES` cylinders in static storage.

### Stairs, `Enemies_SpawnAt`, `Enemies_At`, `Enemies_SetRage`
The cylinder enemies now stand on whatever is under them: each frame they follow `Staircase_GroundHeight` (a step up of a quarter metre is nothing, and if the ground drops away they fall at 9 m/s), are blocked by the flight's steps and railings with the player's 0.3 m clearance, and ask `Stair_ChaseWaypoint` where to head when the player or they are on the flight (`dungeon.md`, `stair_collision.c`). `Enemies_SpawnAt(pos, tier, alerted)` adds one enemy at a place the game chooses (the stair clock's console calls monsters with it; -1 when every slot is used; a killed enemy's slot is reused). `Enemies_At(slot)` is a read-only look at a slot. `Enemies_SetRage(m)` makes every monster walk m times faster; a new floor puts it back to 1.

### `enemies.h` and `enemies.c`

Retired. Two competing implementations existed and the build could not link, so everything was merged into `enemy_manager.c`. `enemies.h` is kept as a one-line compatibility include. `enemies.c` is a placeholder because an archive cannot delete a file. Both can be deleted with mray (option 4, then 3).

### `spider_math.h`

Small vector helpers local to the spider code. The notable one is `V3Transport`, which carries a direction across a surface switch by the rotation that turns the old normal into the new one. This is parallel transport: the direction of travel does not flip when the spider goes from a wall onto a ceiling.

### `spider.h`

`Spider` and `SpiderLeg`, the locomotion API (`Spider_Init`, `Spider_Step`, `Spider_BodySpheres`) and the enemy-system API (`Spiders_*`). Locomotion takes a wished direction and speed and knows nothing about the player, which is what lets the tests drive it directly.

### `spider.c`

Locomotion, in the order the memo gives:

1. Steering projects the wish onto the current surface and turns at a capped rate.
2. The anchor advances along the surface and is re-attached by firing a ray back along the normal. If a move would leave the surface, it slides along the two in-plane axes separately, so the edge of a wall acts as a boundary.
3. Corner detection. Concave: a forward ray meets a surface that is turned away by more than 60 degrees and faces the spider head-on. Convex: a short step ahead the surface is gone, so a ray fired backward from under the edge finds the surface that wraps around it. The spider slows to stop 0.73 m from a wall (head reach plus a 0.28 m gap) and 0.18 m from an edge.
4. Front legs first. When a switch starts, the two front legs reach for the new surface without waiting for the gait. The landing spot is the ideal landing projected onto the new surface and pushed 0.5 m further along it, snapped to real geometry, fixed for the whole swing, and lifted higher than a normal step. The anchor switches only when both front feet have landed. If they have not after 1.4 s the switch is forced and counted as a failsafe.
5. Gait. Legs alternate in two groups (L1 R2 L3 R4 against R1 L2 R3 L4) with a small phase shift toward the rear. Frequency is `1.4 + 1.15 * speed / 2.2` Hz up to 4.6 Hz. A foot steps when a cycle ends and it is 0.10 m off its ideal spot, when it reaches 98% of leg length, or when standing still and 0.75 m off. A leg lifts only when its left-right partner and its front and back neighbours are all planted. The landing target leads the ideal spot by the distance the body covers during the swing plus half the stance. Swings use smoothstep with a sine lift along a blend of takeoff normal, landing normal and body up.
6. Terrain search for each landing: a ray from the leg root to a slightly extended target (a wall in the way gives footing), then straight down from above the target, then horizontally from beneath the target back toward the body (the underside of an edge). If none hit, the foot folds under the root and the leg retries every frame.
7. Body. Up is 35% weighted support normal plus 65% normal of the plane through the feet, found with Newell's method over the ring of foot tips. Support normals from surfaces close to the anchor's surface weigh more. A swinging leg counts at its takeoff point until it lands. Position blends anchor and foot centroid 35:65 and is followed with a critically damped spring. A landing adds a small downward impulse. Three spheres on head, thorax and abdomen are pushed out of the terrain.
8. Two-bone IK from hip to ankle, with the knee up and outward, and a short tarsus from ankle to foot tip.

Not implemented from the memo: shifting the centre of mass toward support legs, tilting with inertia, the underdamped landing bounce, Verlet chains for tufts and palps, and feet that ride on a moving box. None apply yet, since nothing in the tower moves.

### `spider_ai.c`

The enemy system. It keeps a path-distance field from the player's tile (a breadth-first search, refreshed when the player changes tile or every 0.5 s) and chooses a wish each frame:

- Floor: head for the centre of the neighbouring tile closest to the player by path.
- Ceiling: the same, unless the player is close or the ceiling ends ahead, in which case head for the nearest wall so a concave corner takes it down.
- Wall: climb while the player is far, descend when the player is close, and drift along the wall toward the path.

"Close" is 9 m horizontally and is latched: once a spider commits to coming down it stays committed until it is on the floor and the player is more than 15 m away. Spiders beyond 34 path steps stay still. Within 2.4 m of the player a spider stops.

### `spider_draw.c`

Head, thorax and abdomen spheres, two red eyes and three cylinders per leg. Sphere and cylinder detail is low on purpose. All joint positions are computed during the update, so drawing does no maths beyond issuing calls.

### Chase speed and hearing

Cylinder enemies chase at `ENEMY_CHASE_SPEED_BASE` (3.6 m/s) plus 0.3 per tier (mod 4), so up to 4.5, the speed of a walking player; spiders at `SPIDER_CHASE_SPEED` (4.4). Before, they were 2.2 to 2.8 (spiders 2.8), slower than a walk. `Enemies_SetPlayerNoise(metres)` sets how far the player can be heard (see `player.md`, "The cost of running"): a cylinder enemy within that distance is alerted without line of sight, and spiders hunt out to `HUNT_BASE_TILES` (12) of path distance plus one tile per metre of noise (`Spiders_SetNoise`). With no call the cylinder noise is 0 (sight only, as before) and the spider noise is 22 m, which keeps the old 34-tile hunt for tests. `enemies_headless.c` checks that a quiet player behind a wall is not noticed (no enemy moves) and a loud one is (most set off).

### Attacks on the player

`EnemiesAttackPlayer(dt, playerPos, playerHeight, &damage, &from)` in `enemy_manager.c` advances every attack timer and reports at most one blow landing that frame, with its damage and where it came from (the player controller turns that into knockback). A cylinder is "in reach" when its state is `ENEMY_STATE_ATTACK` (within `attackRange`, 1.8 m) and the player's body overlaps its height, so a player on the stairs above it is safe. In reach it counts down `ENEMY_WINDUP_SECS` (0.35 s), strikes for `CYLINDER_DAMAGE` (10), then strikes again every `ENEMY_COOLDOWN_SECS` (1.1 s). Leaving reach resets the wind-up, and a fresh enemy starts in wind-up.

`Spiders_AttackPlayer` in `spider_ai.c` does the same for spiders. A spider is touching when any of its three body spheres is within `SPIDER_REACH` (0.30 m) of the player's body, treated as a vertical capsule of radius 0.35 m. Wind-up `SPIDER_WINDUP_SECS` (0.30 s), damage `SPIDER_DAMAGE` (15), cooldown `SPIDER_COOLDOWN_SECS` (1.2 s). `EnemiesAttackPlayer` calls it, so `main.c` makes one call. The player has a 0.9 s grace period after any hit, so a crowd of enemies cannot stack blows within it.

### Spider bite range and attack animation

The spider used to stop 2.4 m from the player (`ATTACK_DIST`) while its bite reached about 0.8 m, so it stood and never hit. It now closes to 1.35 m, and the bite reaches `SPIDER_REACH` (0.60 m) past the touching distance, so any spider that arrives is in range. The headless hunt test now keeps the player still for 8 more seconds after the spider arrives and requires a bite in at least 80% of runs (24 of 24).

The attack has three visible parts, all drawn in `spider_draw.c` and none of it touching the locomotion code:

1. **Rear** (the 0.45 s wind-up, `SPIDER_WINDUP_SECS`): the head and front of the body lift and lean back, the front pair of legs rise, the eyes grow and turn amber.
2. **Lunge** (`SPIDER_LUNGE_SECS`, 0.25 s): at the bite the whole body and the front legs throw forward about half a metre.
3. **Hurt** (`SPIDER_HURT_SECS`, 0.18 s after a player shot lands): the body and legs flash near white and shudder, and the spider's speed drops to a fifth, so a hit staggers it.

### Cylinder enemy animation and hit feedback

Before a blow a cylinder crouches (10% shorter), swells (22% wider) and turns warm orange for the last 0.35 s; at the blow it lunges half a metre toward the player and stretches 6% taller for 0.25 s. When a shot lands it flashes near white for 0.15 s and stops moving for that time. Its wire outline is drawn 2% larger than the body, so it no longer fights the surface for depth. `EnemyHitsLanded()` and `EnemyKillsLanded()` are running totals of shots that did damage (a probe with zero damage is not counted); `main.c` watches them to show the hit marker. `DrawEnemiesSystem(viewer)` and `Spiders_Draw(viewer)` skip anything farther than 40 m.

### Melee hits

`EnemiesMeleeHit(floorMap, eye, fx, fz, reach, arcDeg, damage, knockback)` is the player's melee blow (see `docs/weapons.md`). It hits every cylinder enemy within `reach` of its edge, inside `arcDeg` either side of the aim direction (anything within 0.5 m is hit whatever the angle), and level with the eye (the eye must be within its height plus 0.3 m, so a player on the stairs does not hit the floor below). Struck cylinders flash and stagger for 0.24 s and are pushed back `knockback` metres along the line from the player, one axis at a time so they slide along walls. `Spiders_MeleeHit` does the same for spiders but in 3D, since a spider can be on a wall or the ceiling: any of its three body spheres within reach counts, and a struck spider staggers for 0.35 s. Hits and kills go into the same `EnemyHitsLanded` and `EnemyKillsLanded` totals as shots, so the hit marker works for melee too.

### `projectiles.h` and `projectiles.c`

The existing projectile system with one addition, `ResolveProjectileHits(hit, radius, damage)`. It offers every live projectile to a callback and removes the ones that hit, so the projectile module never includes the enemy module. `main.c` passes `CheckAndHandleEnemyDamage`.

### `enemies_headless.c` and `spider_headless.c`

Tests that need no window. `enemies_headless.c` now also covers the attacks: the wind-up, the cooldown, reach, a player on another level, and spider bites. Each sits inside its own macro so mray's build, which compiles every `.c` file, does not see a second `main()`. They stub the few raylib draw calls they link against. Build lines are in each file header.

## CI and Workflows

None.

## Fixes and Problems

### The state the enemy code arrived in

- Nine lines of `main.c` held pasted text of the form `[span_N](start_span)`, from another tool's citation format. They caused the "expected expression" errors and, through them, the "undeclared identifier player" cascade. They were removed.
- `main.c` called `DrawFloorMap` with one argument, but the renderer takes the viewer position and a height offset.
- `enemies.c` and `enemy_manager.c` both defined the same functions and both owned a static pool. The headers declared a third and fourth set of names. The `Enemy` struct lacked fields both files used (`id`, `colorTint`, `speed`, `aiState`) and the `AI_STATE_*` constants did not exist. These were merged into one module that matches the names `main.c` calls.
- `CheckAndHandleEnemyDamage` was never called, so enemies could not be killed. The projectile hook fixes that.
- Enemies walked straight at the player through walls, and spawned on a fixed lattice that could land on the arrival point. Movement now respects walls and spawns keep clear of both arrival points.

### Problems met while building the spider

- The anchor can run ahead of the body. A limiter slowed the anchor when the lag was large, but it also stopped the gait phase, so the legs that needed to close the gap never stepped. The limiter now has a floor and a looser threshold.
- Placing the new anchor from the body after a convex switch put it off the surface, because the body hovers 0.5 m out from the face it is leaving. The projection of the old anchor onto the new plane is on the surface, and the contact point found by the ray is the fallback.
- A wish that was mostly "down" with a little sideways drift walked the anchor off the end of a wall. The move failed every frame and the spider froze. A failed move now slides along the surface axes.
- Any wall within the look-ahead counted as a corner, so oblique walls started accidental climbs. A wall now has to be turned away by more than 60 degrees and face the spider head-on.
- A spider near the 9 m threshold climbed and descended over and over. The decision now latches.

### Verified so far

All on the build machine (x86, not a phone), with no rendering.

- Cylinder enemies, 48 floors: counts follow the formula, nothing spawns near either arrival point or inside a wall, spawning is deterministic, 5 of 6 enemies with a clear line closed the distance, none ended inside a wall, hits reduce and then remove an enemy.
- Spider, scripted: floor to wall to ceiling to wall to floor in four switches with no failsafes, and 6 switches around a pillar with no failsafes. Body spheres never penetrated geometry in either run. Planted feet were on a surface in all but 19 of 2477 foot-frames around the pillar (worst 0.6 m).
- Spider, hunting on generated floors: 24 of 24 runs reached a player 18 to 28 path steps away, in 25.7 s on average, using the ceiling in all 24 and a wall in all 24. Body spheres stayed clear of geometry in all 37037 frames.
- Cost: four spiders cost 0.015 ms per frame on the build machine.
- Every source compiles with `-Wall -Wextra` against the raylib headers inside the project, and no symbol is defined twice.

Not verified: how the spider looks and moves on screen, speed on the phone, and the WASM build.

### Known limits

- Attacks are contact only: no ranged attack, and a monster beside the flight can bite a player on it through a railing (it cannot walk through one). The cylinder wind-up is a crouch and swell, not a proper limb animation (the cylinders have no limbs yet).
- A monster walks to the player by line of sight, and up the flight by the route; across the rest of the floor it does not path-find, so it still gets stuck behind a wall that is not on the way.
- Cylinder enemies chase by line of sight and a three second memory, not by path, so they can lose the player around a corner. Spiders use the path field.
- Spiders do not avoid each other and can overlap.
- A spider on the ceiling cannot cross into a stair alcove, since there is no ceiling there. It comes down a wall first.
- The first feet-off-surface count above comes from front-foot probes around pillar corners, where the landing spot can fall outside the face.
