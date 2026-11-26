# The worm swarm (`src/swarm/`)

## Overview

A swarm is many worms crawling at the player as one pile, and it is the game's first enemy that is not a body with a single health bar. The technique comes from a performance write-up on a swarm of 500 worms (spheres joined by joints and colliders would stop a CPU; points joined by distance limits do not): **each worm is a chain of dots, a dot is only a position, a grid finds each dot's neighbours, the level keeps dots out of walls, and every dot has one goal, the player.**

- **A swarm** is `worms` worms of `dots_per_worm` dots each (24 of 10 as shipped, 240 dots; at most 1024 dots in all).
- **A dot** has a position and where it was a step ago (that difference is its motion), a radius shared by all dots, and no collider of its own.
- **Health** is a single box in the middle of the swarm (the centre of all the dots), as the owner asked for now. Shots and melee hit the box, not the dots; a shot that passes through the swarm without touching the box does nothing. The box takes `core_health` damage and the swarm dies; the dead lie limp for `death_secs` and are then gone. The box is drawn as a thin red frame with a small glowing cube (`show_core`), so there is something to aim at.
- **It sleeps** in a heap until the player is within `sense_range` by the way a walker would go (not through walls), or until it is shot. Then it hunts for good.
- **It bites**: dots touching the player bite together, after `bite_windup` seconds of contact and then every `bite_interval`, for `bite_damage` per dot up to `bite_cap`, through the same `DamagePlayer` as every other blow.
- One swarm per floor from `first_floor`, then every `every_n_floors`, placed from the floor's own seed (so the same floor always has it in the same place), at least `spawn_min_dist` from both arrivals, in an open place with a tile of clearance, never in an alcove.

Everything is numbers in `assets/config/swarm.mdix`, read like the other config files (below). Nothing about the swarm needs JSON.

## How it works, and what was decided

**One step** (`Swarm_Step`, at the game's fixed 30 Hz):
1. Move each dot by the motion it already has (gravity, being pushed), damped, with no dot allowed to travel more than 0.2 m in a step (less than two radii, so none jumps a wall or another dot).
2. When hunting, move each dot toward the goal. The goal moves a dot **without giving it momentum**, so the crawl speed is the speed (a goal that fed the motion would be amplified ten times by the carry-over). Each worm has its own speed (`speed_spread`) and weaves a little (`weave`), so a pile looks like worms and not a flood.
3. A few rounds (`iterations`, 3): each link back to its length; overlapping dots pushed apart (found by the grid); then every dot pushed out of walls, floor, ceiling, the stairs and the player.
4. The health box moves to the middle of the dots (a little behind it, half way each step, so a shot is not jittered by every wriggle).

**The grid.** Cells are two radii wide; each dot looks at its own cell and the 26 round it. The cells are hashed into a table of 2048 heads (`head[]`, `next[]`), rebuilt each step, so a swarm of 1000 dots costs 1000 cell lookups of 27 and not a million pair checks. Two dots in the same worm, side by side, are skipped (the chain sets their distance).

**The world, with no baked distance field.** The write-up bakes the level into a 3D grid where each cell knows the distance to the nearest surface, so a dot does one lookup. Here the level is already a grid of tiles, so a dot's test is *exact* with no baking: the nine tiles round it, and for each solid one the nearest point of its square, pushing the dot out when it is closer than a radius (or out through the nearest face when its middle is inside). That is nine reads of the map and a few multiplies, with no memory to hold a field, no cost on a new floor and no resolution to blur corners. If the swarm ever has to live among geometry that is not tiles, a baked field is what to add; the stairs are the one thing that is not tiles, and they use `Stair_PushOutSphere` (the flight's boxes), only for dots in the up alcove.

**The goal is a flow field, not a straight line.** The write-up's dots crawl straight at the player and rely on the pile climbing walls in open 3D. This tower is a maze of walled rooms on a floor, so a straight line ends with the whole swarm pressed against the nearest wall. Instead each step the swarm knows how many open tiles each tile is from the player's tile (a breadth-first search over 45 by 45 tiles, rebuilt only when the player's tile changes: about 2000 tiles), and a dot crawls *downhill* on it (summing the eight neighbours that are closer, never cutting the corner of a wall), or straight at the player once in the player's tile. The same field decides when it wakes: the swarm wakes when its middle is within `sense_range` metres of the player by that walk (tile steps times 2 m), so the other side of a wall is far. This is a deliberate step past "one goal": the goal is still the player, the field is how to get there.

**Determinism.** The layout, the squirm and everything else random come from one small generator seeded at spawn, in a fixed order, and positions are plain floats stepped at the fixed rate: the same game gives the same swarm bit for bit (tested), which a server and its clients will need.

**A struct, not module globals.** The swarm is a `Swarm` struct that the simulation owns (`GameSim.swarm`, `sim.md`); nothing in `swarm.c` is static state, so a server can run one swarm per room, and `swarm.c` calls no raylib function (it uses raylib's `Vector3` type only).

## Modules

### `swarm_config.h` and `swarm_config.c`
**What it does:** `SwarmConfig` and its defaults (the same values as the file; a test pins that they agree), `SwarmConfig_Get`, and `SwarmConfig_LoadFrom(doc, err, cap)`, which installs the numbers only when every one is present and inside its range (worms 1 to 64, dots 3 to 16, a crawl of at most 8 m/s, a link of 0.03 to 0.5 m, and so on, each range in the loader). Otherwise nothing changes and the message names the value (`swarm.worms: 0 is outside 1 to 64`). `worms x dots_per_worm` is also held to the 1024 dots the arrays hold (the ranges happen to make that always true; the check stays so that widening a range cannot overrun the arrays).

### `swarm_register.c`
**What it does:** one line, `Swarm_Register()`, handing the baked table and the loader to `GameConfig_RegisterExtra`, like `FloorGame_Register`. Called in `main()` before `GameConfig_Init`. Its own file so the tests link `swarm_config.c` without the config layer.

### `swarm.h` and `swarm.c`
**What it does:** everything above. `Swarm_SpawnAt` (a heap round a point; the worms start in a spiral so that their dots do not overlap on the first step), `Swarm_Spawn` (chooses the place on a floor), `Swarm_BeginTick` (remembers where every dot is, for drawing between steps), `Swarm_Step`, `Swarm_Bite`, `Swarm_Hit` (a sphere against the box, for shots), `Swarm_MeleeHit` (reach, arc and level with the eye, like the monsters' melee), `Swarm_Alive`, `Swarm_Active`, and the hit and kill totals. Damage of zero or less is a hit that costs and counts nothing, like the monsters'.

### `swarm_draw.c`
**What it does:** draws the dots (near ones as small low-poly spheres, far ones as cubes), pale and thinning toward the tail with a darker red head, dimmed by how dark the place is, blended between the last two steps by the render alpha; the dead sink out of sight as they go; the box as described. No test can run it (it needs a window).

### In the simulation (`src/sim/game_sim.c`; `Swarm_Draw` is in `main.c`)
`GameSim.swarm`; `SpawnFloorSwarm` beside every `SpawnEnemiesForFloor`; `HitAnything` (the monsters, then the swarm) as the shot test; `Swarm_MeleeHit` added to the melee count; `Swarm_Step` after the monsters' step and `Swarm_Bite` beside their blow; `Swarm_Draw` after the monsters. The hit and kill totals the hit marker watches are the monsters' plus the swarm's, with the swarm's carried over from floor to floor so that leaving a floor does not fire a false hit marker. The stats popup counts a living swarm as one enemy.

## `assets/config/swarm.mdix`

One object table, `swarm:`, read through typed getters (a whole number where it says whole, a float where it says float), baked by `tools/mdix_bake.mjs` like `floor.mdix`. Every number is described in the file. Tuning is editing numbers: more worms for a bigger pile (cost grows with dots), `crawl_speed` below the player's 4.5, `core_health` against the guns' damage, `sense_range` for how soon it wakes.

## Tests: `tests/swarm_headless.c`

The shipped numbers load and equal the defaults; ten refusals of bad numbers (named, and the numbers in use unchanged byte for byte after them). Spawning: counts, in the open, lying on the floor, whole worms; refusals; on a floor, far enough from both arrivals, with clearance, never in an alcove, the same for the same seed, different for different seeds, none when there is no place, the farthest half on a small floor. Waking: far away it sleeps for 20 s; near by the walk it wakes; across a wall it sleeps while a short walk wakes it; a dead player does not wake it; a shot does. Crawling: the speed (measured 2.8 m/s against 3.0), straight at the player, arrival, stopping when the player dies, and finding its way round a wall to the player by the long way (about 43 s on the test map) with no dot ever in a wall and no link past 1.6 times its length, every dot, every step. Bodies: the heap's dots not pressed into each other, two dots on top of each other and two across a grid cell boundary pushed apart, a worm below the floor or above the ceiling put back, a dot in a pillar or its corner put out, a dot in a stair step put out, gravity (a fall that is not instant), the swarm gathering round a player who stands still without one dot inside the player's body, the health box never in the floor. Drawing: `BeginTick`. Determinism: two swarms, 400 steps, the same to the bit; a different seed differs. The box: always inside the spread of the dots; shots (centre, a face just touched, just short, above), damage counted, zero damage, death once, shots passing through the dead, the dead lying still then gone; melee (hit, out of reach, facing away, from far above); the bite (repeated, an interval apart, capped, after the windup, not out of reach, not on a dead player, not by a sleeper). Cost on this machine.

**Mutation tests (34).** Each rule was broken on purpose and the test had to fail: walls, floor and ceiling ignored; the flow field ignored; links not enforced; dots never pushed apart or the grid looking in one column; waking at any distance, a shot that does not wake, a shot that hits from anywhere, zero health that does not kill, the dead that crawl on; every bite rule (cap, windup, interval, reach); dots through the player or the stairs; the seed ignored; the melee's arc, reach and height; the health box not following; `BeginTick` doing nothing; spawning in an alcove, next to the arrival or against a wall; damage costing nothing and zero damage counting; gravity upside down; double speed; and three loader rules. **Three survived and were fixed in the tests**, not the code: the ceiling clamp (the test pulled one dot of a chain high and the chain dragged it down, so a whole worm is now put above the ceiling), "bites from out of reach" (the swarm was asleep, so it never reached the reach check; it is now hunting with the player far away), and a first version of the gravity test that was itself wrong (the chain dragged the dot, and the fall had finished by the time it looked).

## Cost

Measured on the build machine (so a phone is several times slower; the A13 has not been tried): 240 dots hunting take **0.2 ms a step**, the largest swarm that fits (1024 dots) **1.4 ms**, against a step of 33 ms. The test only asserts under 4 ms and 20 ms. The drawing is the other cost and is unmeasured: near dots are 40-triangle spheres, so about a hundred of them in view is a few thousand triangles.

## Not verified

- **Nothing here has been seen on a screen or felt on the phone.** Whether 24 worms of 10 dots read as a swarm and not as noodles; whether the pale colour reads in the flashlight's beam and in the dark; the size of the dots; whether 3 m/s is frightening (the player walks at 4.5 and the fastest monster is 3.6+); whether `sense_range` 30 wakes it too soon or too late; whether the frame rate holds with the swarm in view on the A13 (turn `worms` down first if it does not).
- The bite's pacing and size (1.5 a dot, at most 18, every half second with a 0.3 s windup) are a first guess against the player's 100 health and the 0.9 s grace after any hit.
- The health box's size (0.9 m a side) and health (150) are guesses against the guns' damage in `weapons.mdix`; the stair, console and floor-collapse interplay (the swarm during the stair clock) has not been played.
- `Swarm_Draw` and `main.c` compile for web, Android and desktop (`tests/platform_check.sh`) but have not run. The glue that moved into `src/sim/game_sim.c` runs headless: `tests/game_sim_headless.c` covers it, and the differential test in `sim.md` showed it identical to the old `main.c` (a swarm fight until death among its scenarios).

## Known limits

- **It does not climb.** Stairs are solid to the dots, so a swarm chasing a player up the flight piles at its foot; walls are not climbed (the write-up's pile does, in open 3D). The flow field is over tiles, so the stairs' alcove is just open floor to it.
- Dots do not collide with monsters, props or pickups, and monsters do not collide with dots.
- A shot that crosses the swarm without touching the box does nothing, and shots do not stop at dots; the dots do not react to being shot (no scatter, no gore); no sound at all (no crawl, no wake-up, no death).
- A floor you leave and come back to gets a fresh swarm, with fresh health, like the monsters; one swarm per floor.
- The wake test measures the walk in whole tiles of 2 m (a tile count, not a smooth distance), and a dot's heading is the same everywhere in a tile, so a worm turns a corner in a few visible steps.
- Not networked: each machine would run its own copy (deterministic, so it would agree, but nothing sends it).

## Fixes and Problems

- **The first step tore the worms.** The test measuring the longest link found 0.33 m against a rest of 0.16 (twice) at the very first step: the worms all started radiating from one point, so their dots overlapped and the first push apart was violent. Invisible in play (the player is far away at the spawn) but wrong; the worms now start in a spiral, each a little farther out than the last, and the longest link over a long run is 0.24 m.
- The test for waking in a long way round first used a map where the way round was short; it was rebuilt with a U that is really long.
- Three tests were too weak and a fourth was wrong (see the mutation tests).

**Floors of any size (round R1a).** `Swarm.flow` is `MAP_MAX` squared and the swarm remembers the size of the floor its field was made for (`flowW`, `flowH`; 0 before the first field). `FlowUpdate`'s two queue arrays are static `int16_t` arrays: as `int` arrays on the stack they would be 128 KB on a 128 by 128 floor and the web build's stack is 64 KB. The spawn candidates use the floor's shell for the alcoves. A field over a whole 128 by 128 floor takes about 0.3 ms on x86, only when the player changes tile; no radius was added (`floorgen.md`, "Round R1a").
