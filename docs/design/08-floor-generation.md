# Floor generation v2

**Status:** plan accepted on 2026-11-18. Nothing in this document is built. The research behind it (the repository audit, the sources, the free assets, the mid-engine toolbox and the measurements) is in `09-floor-generation-research.md`. Marks, as in the other design documents: **yours** (from your notes and answers), **built** (in the game today), **proposal** (mine, overrule freely) and **measured** (a number from a run, with where it ran).

## 1. What this is for

Every floor today is one flat 45 by 45 grid, built by one generator (`docs/dungeon.md`). You asked for floors that are bigger, with a different start for each team, more horror (low walls to take cover behind, floors stacked on top of each other), themes (an office that is slightly wrong in different ways), a safe zone, room for missions and special rooms that are still to be designed, and "the best procedural level generation we can make": fast, the same on every phone and the server, and built with the optimisation techniques of mid-engine that really work. This document is the plan. It is meant to be read before any of the rounds in section 15 is started and changed when the design changes.

## 2. What you decided (yours)

1. **Sizes.** Solo stays 45 tiles per side. Two teams: **64**. No floor is bigger than **128** tiles per side, ever. Three or four teams: not set yet; 81 is my proposal and anything up to 128 fits.
2. **Stacked floors are not common.** A floor with a second level over part of it is the exception. A stack is normally two levels; the **hard limit is four**, and going above two is **extremely rare**. A deck mostly has **one or two holes**, and every hole has a way back up (stairs by default).
3. **The upper level covers most of its zone**, with those holes in it. It is not a gallery ring around one open atrium.
4. **Low walls** that can be used as cover.
5. **The colours stay** (they look better for horror). The tower is an interdimensional hole that looks like a corporate office, and floors can be altered versions of that: **floral, fungus-infected, mechanical zone** are the first ones.
6. **Each team starts in its own place** where the docs say so (the first floor); later floors keep the shared arrival at the stairs, as the docs have it.
7. **A safe zone**, as `05-world-and-lore.md` describes.
8. **Missions and special rooms are still to be brainstormed.** The generator has to leave room for them so they can be added without reworking it.
9. **The best generator we can make:** fast, deterministic from the seed, safe for multiplayer, using every mid-engine technique that works (bit masks, wide types, SIMD where it pays), measured and not assumed.
10. **Accepted with "this works":** a level is 6 m high; cover is 0.45 m and 0.85 m high; the safe zone is a zone with two sentinel gates on random floors; team start rooms exist on floor 1 and on boss arenas; the generator becomes integer-only (so every old seed gives a different floor); the build order in section 15.
11. **Who owns what.** This chat owns the tower, the floor generator and small mechanics. Multiplayer is another chat's work: nothing under `src/net/` changes here (section 12.5 says what that chat needs to know).

## 3. What the other documents already require

| Requirement | Where it comes from | What the generator must give |
|---|---|---|
| Team spawn rooms on floor 1, at the edges, away from the closed stairs; teams are put in them at random | `01-game-loop.md`, `05-world-and-lore.md` | one start room per team, fair distances, a slot-to-point function |
| Stairs on every floor, always in the same place, shut until unlocked | built (`dungeon.md`), `01-game-loop.md` | the fixed stair shell, scaled to any floor size |
| A console that unlocks the stairs, put in the same place from the floor's seed | built (`floor_run.md`) | a console socket; today's rule stays as the fallback |
| Mission sites | `01-game-loop.md`, M1 | mission sockets in sizes small, medium, large |
| A safe zone with a shop and two sentinels, on random floors | `05-world-and-lore.md`, `01-game-loop.md`, M5 | a safe socket with two gates, flags and a no-spawn margin |
| An elevator on every floor, not always active | `05-world-and-lore.md` | an elevator socket |
| A boss every tenth floor: one large arena | `03-monsters-and-senses.md`, `05-world-and-lore.md` | a boss recipe: one arena zone, start rooms around it |
| No two floors alike | `05-world-and-lore.md` | a seeded recipe, themes and zone types |
| The server sends only the floor seed | `06-multiplayer.md` | a pure function of the seed and a few small numbers, the same on every platform |
| The collapse wave from the far edge toward the stairs; a countdown that is longer on bigger floors | built (`floor_run.md`), `01-game-loop.md` | distance fields from the stairs; the floor size class |

## 4. The generator today (built)

The full description is `docs/dungeon.md`. What matters for this plan:

- 45 by 45 tiles of 2 m, walls 6 m, 8 m between floors (`dungeon_types.h`). A fixed shell and two stair alcoves in the east corners are stamped first, then probe rays (16 per probe) drive the partitions, pillar halls and a pocket pass. Up to 8 attempts, then a pillar-hall fallback.
- **Measured (`dungeon.md`):** 3 ms on average and 14 ms at worst per floor on x86. Not measured on the phone.
- Random numbers come from a 32-bit xorshift and a 32-bit integer hash. The layout code also uses floats and libm: `hypotf` (`dungeon_generator.c:160`) and `cosf` and `sinf` for the ray directions (`dungeon_probes.c:75`). No build script sets `-ffp-contract=off`.
- A tile is an 8 byte `MapCell` (`type`, `discovered`, `variant`, `flags`). Solid means "the type is `TILE_WALL`". There is one ceiling for the whole floor.
- Open space is steady at about 84 percent, and the three tile corridor rule is what limits density (`dungeon.md`, Known limits). More maze-like floors need a different rule, which is what the zone types in section 5.5 are for.
- Everyone arrives at one point. The Welcome message carries the floor seed (32 bits), the floor number, the server tick and one spawn (`net_proto.h`).
- The static arrays and one stack array that grow with the map size, and the per-frame work that walks the whole grid, are listed in section 13.4 (they are what has to change before 128 tiles is safe).

## 5. The pipeline (proposal)

One pipeline, eight stages, always in this order. Each stage reads the output of the earlier ones and has its own random stream (section 12.1), so a stage can be changed or added without moving what the earlier stages made.

```
 recipe -> skeleton -> zone graph -> sockets -> zone fill -> levels -> cover -> dressing -> validate, hash, bake
 (data)    (rects)     (loops)       (roles)    (per zone)   (decks)   (walls)  (props)     (one set of tables)
```

### 5.1 Stage 0: the recipe

Input: the floor seed, the floor number, the generator version and the team count. Output: a `FloorRecipe`: the floor kind (normal, boss; later safe and finale), the theme, the size, the zone mix, the stacked-floor numbers, and the list of sockets the floor needs (kind, how many, which size). The recipe is data in `assets/config/floorgen.mdix`, read and baked like the other config files (`docs/config.md`), so adding a mission type or a special room later is a row, not code.

Placeholder numbers (no playtest, set on the phone): a boss when the floor number is a multiple of ten (yours, from the docs); a safe zone on about one floor in five from floor 3; a stacked zone on about one floor in six; a stack of two levels 95 percent of the time, three 4.5 percent, four 0.5 percent; one hole 50 percent, two 42, three 7, four 1.

### 5.2 Stage 1: the skeleton

The stair shell is fixed and scaled from the floor size `w` by `h`: the east strip starts at x = w - 9, the up alcove takes z 0 to 8, the down alcove the last nine rows, the doors and pads sit at the same offsets from the east and south edges as today (`dungeon_types.h`). Everything west of the strip is a **warped grid of rectangles**: the number of columns and rows is chosen from the size (about 16 tiles to a zone, 12 at the least), each inner boundary is moved a little by the skeleton stream, and neighbouring zones are sometimes merged (two or four into one rectangle) to make big zones for arenas, atriums and the safe zone. Zones are rectangles on purpose: partitions, cubicle rows and pillar lattices are straight, portals are straight segments along a shared border, and a zone can be generated in a small local buffer.

The count is `round((w - 10) / 16)` columns by `round((h - 2) / 16)` rows. For the sizes you gave: 45 gives 2 by 3 zones, 64 gives 3 by 4, 81 gives 4 by 5, 128 gives 7 by 8, each plus the east strip.

### 5.3 Stage 2: the zone graph

Zones are nodes; two zones that share a border of at least three tiles are joined by an edge. The skeleton stream gives each edge a weight, a minimum spanning tree makes everything reachable, then extra edges are added to make **loops**: the edges chosen are the ones whose two ends are far apart in the tree, until the recipe's loop count is met (`09` section 2.1 says why: a second route is what makes a floor playable for several teams). The graph is then checked for **bridges** (Tarjan's low-link method; the graph has 60 nodes at most) and edges are added until every start zone sits on a cycle, so no team can be trapped in a room with one door. A portal (three to five tiles wide, at least two tiles from the corners of the border) is cut for each edge, and a long border gets two.

### 5.4 Stage 3: the sockets

A **socket** is a reserved place with a role. Roles are given out on the graph, before any tile exists:

| Socket | How it is placed |
|---|---|
| start (one per team) | edge zones away from the stair shell, chosen by farthest-point so the teams are far apart, then checked for fairness (below) |
| console | a central zone with at least three portals, far from the starts; today's rule (22 tiles from the foot of the up stairs, relaxed to 12 then anywhere) is the fallback |
| mission sites | farthest-point from the console and the starts, sizes from the recipe |
| safe zone | a zone that is not a cut vertex, whose portals are cut down to two gates (two sentinels, one at each) |
| elevator | next to the stair shell |
| special rooms | whatever the recipe asks for, by kind, size and tags |

A socket is a kind, a footprint, a level, tags and a slot (12 bytes). At most 64 per floor. **Fairness:** the walking distance (zone graph weights) from each start to the console and to the stairs must stay within a ratio the recipe sets (placeholder 1.25); if not, the assignment is rolled again with the next attempt number. This is a graph check on a few dozen nodes, not a tile search.

### 5.5 Stage 4: zone fill

Each zone is filled by one small generator chosen by its type. A zone works in a local buffer, gets its own sub-seed and may retry (attempt 0, 1, 2, ...), and it is **accepted only when every portal and socket inside it is connected** and the lanes are at least three tiles wide. So the floor is connected by construction, and the global flood fill at the end is an assertion, not a search. A zone that fails after its retries falls back to the open pillar hall (which cannot fail). This is the "modify in blocks" idea (`09` section 2.2) applied to levels.

Zone types (data: `type`, which themes use it, weight, size limits). Marked with the round that builds them:

| Type | What it is | Round |
|---|---|---|
| open pillar hall | today's pillar hall | R0 (ported) |
| partitions | today's probe-ray partitions and pockets | R0 (ported) |
| cubicle farm | rows of low-wall cubicles with aisles; the cover is the cubicle panel | R2 |
| offices | suites along a corridor spine, full walls and door gaps | R3 |
| storage | shelf rows and cages (mid-height cover, blocks sight) | R3 |
| lounge | open, with scattered cover clusters | R2 |
| arena | boss floor: one big zone with cover clusters | R3 |
| atrium | the stacked zone (section 7) | R4 |
| garden, spore, machinery | the floral, fungal and mechanical zones | R5 |

### 5.6 Stage 5: levels

Section 7.

### 5.7 Stage 6: cover

Section 8.

### 5.8 Stage 7: dressing

Cosmetic props (desks, chairs, plants, cables, stains) chosen by the theme's prop pools and placed by per-zone-type rules, from the dressing stream. They are drawn only, never read by the simulation, and are **not part of the floor hash**. A prop that has to block the player is not a prop: it is a cover wall of height class mid, in the grid. This keeps the server free of meshes and keeps determinism simple. The prop bake tool and the asset rules are in section 11.

### 5.9 Stage 8: validate, hash, bake

Validate: every socket and portal reachable, lanes at least three tiles, cover never in reserved tiles, levels within limits, every deck tile reachable by stairs (section 7.5), start points on open floor. Hash: section 12.3. Bake: build the derived tables the rest of the game reads, once, so no system has to re-derive them:

- the **bit rows** (solid and free by row, and the transposed copy by column) for each level;
- the **zone map** (`zoneIdx` per tile) and the socket list;
- **distance fields** (steps to the up pad and to each start room), which replace the separate breadth-first searches in `pickups.c`, `floor_game.c` and the spider and swarm code, and make a collapse wave that follows the walking distance possible (the unfairness `floor_run.md` notes);
- the **light fixtures** (per level);
- the spawn candidate lists for enemies and pickups, with start rooms and the safe zone left out;
- the generation report (counts and the time of each stage).

## 6. Data model (proposal)

### 6.1 Constants

`MAP_MAX 128` (yours), `LEVELS_MAX 4` (yours), `ZONES_MAX 96`, `SOCKETS_MAX 64`. A floor has its own `w` and `h`, each at most 128, so they fit a byte. A row of 128 tiles is exactly one 128-bit mask (two 64-bit words), which is the shape of `BitMask128` in mid-math (`09` section 4).

Heights: a level is **6 m** (yours, accepted), a deck is 0.4 m thick, the surface of level k is at 6 k, the ceiling above a zone with L levels is at 6 L (6 m for a normal floor, as today; 24 m at most). Walls: full height, **low 0.45 m**, **mid 0.85 m** (yours, accepted). Compare the standing eye at 0.63 m and the lying eye at 0.40 m (`player_controller.h`): a low wall hides someone lying down and lets a standing player shoot over it, a mid wall hides a standing player.

### 6.2 `FloorMap` v2

Struct of arrays, one byte per field, level 0 over the whole floor:

| Array | Meaning |
|---|---|
| `type[z][x]` | the existing `TileType` values keep their numbers; new: void (a hole), stair |
| `variant[z][x]` | wall style (shell, partition, pillar, cover panel, planter, crate, mound, machine) |
| `wallH[z][x]` | 0 none, 1 low, 2 mid, 3 full |
| `flags[z][x]` | the existing reserved bit, plus portal, socket, safe, no-spawn, and a client-only discovered bit |
| `zoneIdx[z][x]` | which zone the tile is in |
| derived bit rows | solid, free (by row) and solid (by column), per level, built by the bake step |

Stacked zones keep their upper levels in a **per-floor arena** (a bump allocator, as `mid-arena`): each stacked zone has `levels - 1` copies of the four byte arrays, sized to the zone rectangle only, so the memory follows the stacked area and not the floor (`09` section 4). `Zone`, `Socket` and `StackedZone` tables sit beside them.

Size: level 0 is about 86 KB at 128 by 128 (four byte arrays and the zone map at 16 KB each, plus 6 KB of bit rows). Simply enlarging today's 8 byte `MapCell` grid to 128 by 128 would be 128 KB, so v2 is smaller than a plain enlargement.

### 6.3 The accessors

The rest of the game stops reading `grid[][]` and `WALL_HEIGHT` directly and asks:

```c
bool  Floor_Solid(const FloorMap *f, int level, int x, int z);          // any wall class
int   Floor_WallClass(const FloorMap *f, int level, int x, int z);      // 0 open, 1 low, 2 mid, 3 full
float Floor_WallTopY(const FloorMap *f, int level, int x, int z);
float Floor_GroundY(const FloorMap *f, float x, float z, float y);      // highest standable surface under y plus the step-up
float Floor_CeilY(const FloorMap *f, float x, float z, float y);       // lowest ceiling above y
bool  Floor_Blocked(const FloorMap *f, float x, float z, float radius, float y, float height);
int   Floor_LevelAt(const FloorMap *f, float x, float z, float y);
bool  Floor_StartPoint(const FloorMap *f, int slot, float *x, float *z, float *yaw);
int   Floor_Sockets(const FloorMap *f, int kind, const Socket **out);
```

`Staircase_GroundHeight` and `Staircase_Blocked` become the stair part of `Floor_GroundY` and `Floor_Blocked`. `Tile_IsSolid` stays as a thin wrapper while call sites are moved, so every round compiles and passes the tests.

### 6.4 The API

```c
typedef struct { uint32_t floorSeed; int32_t floorNumber; uint8_t genVersion, teams; } GenParams;

bool     FloorGen_Run(const GenParams *p, FloorMap *out, FloorGenScratch *scratch);   // blocking
void     FloorGen_Begin(FloorGenJob *j, const GenParams *p, FloorMap *out, FloorGenScratch *scratch);
bool     FloorGen_Step(FloorGenJob *j, uint32_t budgetMicros);                        // true when finished
uint32_t FloorHash(const FloorMap *f);
```

Scratch memory is handed in by the caller instead of living in `static` arrays, so the generator is re-entrant (the server can build the next floor of one match while another runs) and can be **time-sliced**: the stages and the zones are the units, each at most about a millisecond. The three-floor window then builds the floor above in the background during a floor and nothing happens at the door.

## 7. Stacked zones (proposal built on section 2)

### 7.1 What it is

A zone of type atrium (or any large zone the recipe stacks) gets 2, 3 or 4 levels. Level 0 is the ground. Every higher level is a **deck** over the zone, covering most of it, with one or two holes (rarely three or four). The ceiling above the zone is at 6 m times the number of levels. The surrounding single-level zones keep their 6 m ceiling, so the step in the ceiling at the zone's border is what shows you have come to something bigger. Pillars run through every level, so the layouts line up.

### 7.2 Frequency (yours, with placeholder numbers)

Stacks are the exception, not the rule. Section 5.1 holds the placeholders: about one floor in six has one, almost all are two levels, three and four are extremely rare. These are numbers in the recipe, to be tuned by playing.

### 7.3 Holes and the way back up

A hole is a rectangle cut in a deck, at least 3 tiles from the zone border and clear of the portals. Each hole has a **connector** that brings a player from the level below back up. The first connector is a straight stair: riser 0.25 m and tread 0.5 m like the main flight, so 24 steps and 12 m (6 tiles) of run for a 6 m rise, 3 tiles wide, with a clear tile at each end. The stair is built from step boxes in the same way as the main flight (`stair_collision.c`) but from parameters, not constants. A ramp, a ladder or a lift can be added later as other connector kinds. With three or four levels there is at least one connector for each pair of neighbouring levels.

### 7.4 Rules

- **The critical path stays on the ground** (proposal): the start rooms, the console and the stairs are on level 0. Upper levels are for loot, vantage points, mission sites that say they may go up, and flanking. A floor can be finished without ever climbing.
- Each upper level is laid out by the same zone-type generators as level 0, with the lower level's pillars as fixed points.
- An upper level never holds a start room.

### 7.5 Validation

A flood fill over (level, x, z) from the ground portals: every deck tile and every connector end must be reached, and a hole must never leave a deck tile that can only be reached by dropping. Counts: levels at most 4, holes per deck at most 4, a connector for every pair of neighbouring levels. This is the biggest correctness risk of the plan (a player falling through a deck, or stuck in a hole), so it gets the most tests (section 14).

### 7.6 What it changes elsewhere (round R4)

- **Collision and the player:** ground, ceiling and wall queries are level aware (section 6.3). The body tests every level it overlaps.
- **Shots, sight and the monsters:** `Dungeon_Raycast`, `Dungeon_PushOutSphere` and `Dungeon_SurfaceDistance` read heights from the accessors. Ground monsters stay on level 0 at first; spiders crawl over what they are given.
- **Drawing:** the camera's level is drawn in full within the existing radius; another level is drawn only through a hole and within a few tiles of it. A deck drawn from below needs its underside. This keeps the quads per view close to today's (about 1,000 in a view, `dungeon.md`; the phone reported 62 to 63 FPS).
- **Light:** lights are per level; a hole lets some light fall to the level below.
- **The collapse:** every level falls, not only the ground.
- **Falling:** a hole is a drop (gravity is already 14 m/s squared). Whether a 6 m drop hurts, and whether hole edges have rails, are open decisions (section 17).

## 8. Cover (proposal built on section 2)

**The idea.** The probe rays that already decide where a partition goes get a second result. Where a span is long and the zone is open plan, the generator places a **cover cluster** instead of a full partition: you can see over it, you cannot walk through it, and it breaks a sight line.

- **Classes:** low 0.45 m, mid 0.85 m, full. Stored per tile in `wallH` (section 6.2).
- **Clusters:** a small library of shapes kept as tiny bit masks: a 3 or 5 tile segment, an L, a pair with a two tile gap, a U, a 2 by 2 island. Each is tested against a candidate mask built for the whole zone in a few word operations (`09` section 5: 2 to 6 times faster than a loop).
- **Rules:** a lane of at least three tiles stays open between cover and anything else; nothing inside the open radius of the stair doors, a portal or a socket's margin; the density per 100 tiles squared comes from the zone type and the theme.
- **What each class means** (relative to the 0.63 m standing eye): low hides a player who is lying, and a standing player shoots over it; mid hides a standing player, who has to step out or jump to engage. Vaulting is a later mechanic.
- **What has to learn about it (R2):** `Dungeon_Raycast` and the push-out (a sphere centred above the wall top passes), monster sight (`03-monsters-and-senses.md`), the weapons' hit test, the lighting (light passes over cover, so lights need a "walk" mask and a "light" mask), the swarm and spider path fields (cover blocks ground paths for now), and the renderer (a box of the class height, with a top face).
- **Theme styles:** cubicle panel (corporate), planter (floral), mycelium mound (fungal), machine block (mechanical). The same classes, other art.

## 9. Sockets, templates, missions and special rooms (proposal)

The generator knows **where** and **how big**. It does not know what a mission is.

1. A floor's recipe asks for sockets by kind, size and tags (section 5.4).
2. A **template** is a small hand-made room: an ASCII grid with a legend (wall, floor, low cover, mid cover, door, void, and marker characters for a console, a sentinel, a loot spot and so on), the sides its doors may be on, its tags, its weight and the first floor it may appear on. Templates live in `assets/config/` as `.mdix` and are baked.
3. After the zone is filled, the socket's footprint is stamped with a template chosen from the socket's own random stream, the doors are joined to the zone's lanes, and the zone is **validated again** (a stamped template can break the path a generator guaranteed; `09` section 2.3).
4. Gameplay reads `Floor_Sockets(kind)` and the template's marker cells. A mission module owns its own logic.

Adding a special room or a mission later is then: pick or add a socket kind (new kinds go at the end of the enum), write one or more templates, add a recipe row (how many, which floors, which themes), write the gameplay module, add the tests (a template is valid, it is placed on N seeds, the zone stays connected). No change to the generator for the usual case.

A sketch of the data (not final syntax; the real files follow `docs/config.md` and the existing `.mdix` files in `assets/config/`):

```
@DATA(
  config_version<int> = 1
  size_tiles:
    solo = 45, two_teams = 64, three_four_teams = 81, max = 128
  stacked:
    floor_one_in = 6, levels_2 = 950, levels_3 = 45, levels_4 = 5,    // per mille
    holes_1 = 500, holes_2 = 420, holes_3 = 70, holes_4 = 10
)
```

## 10. Team starts, the safe zone, boss floors, the elevator (proposal on top of the docs)

- **Start rooms:** floor 1 only (and boss arenas, where the teams share the fight). A start room is 9 by 9 tiles with 7 by 7 inside and one door three tiles wide (a second door is a recipe option), on the edge of an edge zone. There is one per team, and the team count is part of the generator's input, so every client builds the same rooms. Which team gets which room is decided by the server at random; `Floor_StartPoint(map, slot)` turns a slot into a point.
- **Later floors:** everyone arrives at the stairs together, as today and as the docs want ("everyone enters at the same moment").
- **Safe zone:** a socket of the large size with two gates and two sentinel spots. Inside, the `safe` flag; around it, a `no-spawn` margin so no monster is placed against the gate. What "safe" does (no fighting, shop) is the simulation's, which reads the flag. Whether it can also be a whole floor is open (section 17).
- **Boss floor:** a recipe with one arena zone (the merged area west of the shell), cover clusters, and the start rooms around the arena. The stairs unlock when the boss falls (`01-game-loop.md`).
- **Elevator:** a small socket next to the shell. What it does is not specified in the docs yet (section 17).

## 11. Themes and assets (proposal)

A **theme** is data: a palette shift off today's yellow (corporate is exactly today's colours; the other themes move away from them a little), light-mood ratios (on, dim, flicker, dead), zone weights, prop pools, a cover style, hazards, and later an ambience. The first three variants are floral, fungal and mechanical. Mostly they are rows, plus a few zone types: planter beds for the garden, cellular-automaton blobs for the fungus (done with bit masks, which are good at it), catwalk decks and machine blocks for the machinery. The renderer's colour constants become a `ThemePalette` table in R5, with entry 0 equal to today's values and a test that proves it.

**Assets.** The researched free packs are in `09` section 3. The rules:

- CC0 sources first (Kenney, Quaternius, KayKit). Other packs only after the licence on their own page has been read. A ledger (`assets/props/LICENSES.md`) lists every model that is used, its source and its licence.
- The renderer is vertex lit per tile corner and has no textures, so a **bake tool** (`tools/prop_bake.mjs`, in R5) turns a glTF or OBJ model into a small vertex-coloured prop: textures are ignored, the colour of each triangle is sampled from the model's colour atlas or material, and each material is mapped to a palette slot so the theme can recolour it. Props are drawn through the same lit-vertex path as the walls.
- Budget (placeholder, to be set from the phone): about 120 triangles for a normal prop. A few models reused with different rotation and scale are better than many models.
- Props do not collide. The only things that block are grid cells.

## 12. Determinism and the multiplayer contract

### 12.1 Rules (they apply to everything under `src/floorgen/`)

1. **Integer only.** No `float`, no `double`, no libm in anything that decides a layout. Distances are compared squared or in the Chebyshev or Manhattan metric; where a ramp is needed it is fixed point (Q16.16, the same discipline as mid-math's `fixed` module, which exists for exactly this reason). Floats appear only at the very end, converting a tile position to world metres (a whole tile times 2.0 is exact in a float).
2. **PCG32 streams.** `Pcg32` as in mid-math (`state`, `inc`, the same output function) with one stream per stage, zone and socket. The floor seed (32 bits on the wire) is widened to 64 bits with SplitMix64 mixed with the generator version; the stream number is `(stage << 32) | (index << 8) | attempt`. Adding a stage or a socket kind uses a new stream, so it does not move anything that was already generated. Ranges use Lemire's method with the 32-bit threshold. Test vectors are in `09` section 4.
3. **A fixed order.** No iteration over a hash table or by pointer, no `qsort` (it is unstable and the order of equal keys is not defined): sorts are written out with an index tie-break.
4. **No uninitialised reads.** Scratch is zeroed or fully written before it is read.
5. **No dependence on the player count except through `GenParams.teams`**, which every client knows.

### 12.2 The generator version

`GEN_VERSION` goes up whenever the output for any seed changes. It travels with the seed. A client and a server with different versions must not play together.

### 12.3 The floor hash

A 32-bit FNV-1a over the floor's dimensions, theme and kind, the level 0 arrays in a fixed order, the stacked zones in zone order, the zone and socket tables and the start points. Not in it: the discovered bit, the props, the lights. Tests keep **golden hashes** for a set of (seed, floor, size, teams) values, run natively and in the WASM build under Node (`09` section 2.5 on why a single architecture does not prove anything).

### 12.4 What a floor needs from the network

Only `GenParams`. The Welcome message already carries the seed and the floor number; the generator version, the team count (or size class) and the hash are what is missing.

### 12.5 For the multiplayer chat (nothing here touches `src/net/`)

- Generation is a pure function of `(floorSeed, floorNumber, genVersion, teams)`.
- Welcome should gain the generator version, the floor hash and a start slot. A different version means refuse to join; a different hash after generation means a desync error, not a silent mismatch. The spawn can stay in Welcome (it will come from `Floor_StartPoint`). The protocol already has a version (`NET_PROTOCOL_VERSION`, 3 at the time of writing, checked in Hello); the generator version can be folded into it or sent on its own, which is the other chat's call.
- Positions are computed on both ends from the same `Floor_*` queries, so decks should need no new wire fields; this is to be checked against `net_move_sim` when R4 starts.
- Cosmetic data is not shared and not hashed. The safe and no-spawn flags are read by the server's simulation.
- The server builds the next floor in slices during a floor (`FloorGen_Step`), so the stairs never wait for it.

## 13. Performance plan

### 13.1 Method (your rules, from mid-engine `platform-optimization.md`)

Measure before changing anything; an optimisation that cannot be measured does not exist; only release numbers count; publish the numbers. Tier 1 first (plain code, data layout, explicit loops, properties of the arithmetic); Tier 2 (vector code) only with a scalar fallback and a measured gain of at least 10 percent at scale; no assembly for arithmetic. In C: "release" is `-O2` as the build scripts use it, and every comparison is also run in the WASM build under Node and on the phone. A plain loop version of every primitive stays in the tests as the oracle for a differential test (the fast version must give the same answer as the loop on random grids).

### 13.2 Budgets

No budget is set yet, because the phone has never been measured for generation. **R0 measures first** (per-stage microseconds in the generation report, native, WASM under Node, and the phone through the diagnostics line) and the budgets are written down from those numbers. Proposed shape: no slice of generation takes more than about 3 ms of a frame, and a whole floor at 128 tiles must be ready before the stairs open.

### 13.3 The technique map

Evidence is from the prototype in `09` section 5 (x86-64 sandbox, synthetic floors, one shared core, so the order matters more than the exact figures).

| Where | Operation | Technique | Prototype result | Decision |
|---|---|---|---|---|
| probes, partitions | free run from a tile in four directions | bit rows plus count-trailing and leading zeros (Tier 1) | 1.7 to 2.7 times faster | adopt in R0 and R1 |
| partitions, cover | is this band of rows and columns all free | OR the rows, AND a range mask | 2.4 to 5.9 times | adopt |
| pillars, cover, props | every position where a pattern fits | shift and AND whole rows, population count | 2.1 to 5.7 times, more at larger sizes | adopt |
| validation, distance fields | flood fill | queue search by default; a bit wavefront (a whole row of the frontier moves at once) at the largest size | without hardware population count the wavefront lost or tied at 45 and 64, was mixed at 81 and won 1.8 to 2.4 times at 128; with it, it won 1.5 to 5 times at every size | queue up to 81; bit wavefront at 128, and earlier if the phone has hardware population count |
| zone graph | spanning tree, bridges | plain C on about 60 nodes | n/a | nothing to optimise |
| dressing, light mood | per-tile integer hash | 4 wide vector (a C mirror of `u32x4`) | 1.4 to 2.7 times, but 0.5 to 15 microseconds for a whole floor | leave scalar until a profile says otherwise |
| lighting update (every frame) | tone curve and corner means over the whole grid | update only a window around the viewer | 19 microseconds at 45, 217 at 128; a 37 by 37 window is about 9 to 10 at any size | window in R1; vector code only if a profile asks |
| nav and flow fields | breadth-first search over the whole grid on a tile change | bounded radius | not measured | bound in R1 |

What the wide types are for: **batch arithmetic over many tiles** (hash fills, the lighting bake, later per-vertex lighting in the renderer). The generator's own hot loops are scans and fits, and bit rows do those better than wide registers do. In C the wide types are a small header (`src/core/wide.h`) with the same names and meaning as mid-math's `u32x4` and friends, built on the compiler's vector extensions, which compile to SSE2 on x86, NEON on ARM and simd128 in WASM when the target has it and to plain scalar code when it does not. The WASM build needs `-msimd128` to get vector code (`mray.py` does not pass it today); whether to turn it on is decided by the benchmark, not by this plan. AVX2 stays an optional server-only step behind a runtime check, as in mid-math's wide integers.

**Mirror, do not link.** The game takes the algorithms and the test vectors of mid-math (`Pcg32`, the bit mask shape, fixed point discipline, the arena) as small C files. Linking the Rust crate would add a Rust toolchain to the Termux, Emscripten and NDK builds for a few tiny operations. The side benefit: the same floors could be generated by a Rust server later, because the algorithms are integer only and the vectors are shared.

### 13.4 What has to change before 128 tiles is safe (found in the audit, all in R1)

- **A stack overflow in the WASM build.** `FlowUpdate` in `src/swarm/swarm.c:42` keeps two `int` arrays of `MAP_WIDTH * MAP_HEIGHT` on the stack. At 45 by 45 that is 16 KB; at 128 by 128 it is 128 KB. Emscripten's default stack has been 64 KB since version 3.1.27 (`09` section 2.6), and `tools/mray.py` does not set `-sSTACK_SIZE`. The arrays move to static storage (or `uint16_t` pairs).
- **`Lighting_Update` walks every tile every frame** (an `expf` per lit tile and a four tap mean per corner). Measured on x86: 19 microseconds at 45 by 45, 217 at 128 by 128. It gets a window around the viewer.
- **`NavCompute` (spiders) and `FlowUpdate` (swarm) are whole-grid searches** each time the target changes tile. They get a radius.
- **Static arrays sized by the map:** about 150 KB at 45 by 45 and about 1.2 MB at 128 by 128 (lighting 275 KB, spider nav 128 KB, pickups 128 KB, the console candidate list 64 KB, the swarm's static lists 192 KB, the three floors of the window 384 KB, and the generator's own 32 KB; counted from the declarations, not measured). Fine with `ALLOW_MEMORY_GROWTH=1`, but it goes in the memory budget.
- **The stair tables are written for 45 by 45** (`stair_layout.h`): the down alcove moves with the south edge, so it needs an offset of `(h - 45) * TILE_SIZE` or a function of the size.

### 13.5 Drawing

The renderer is immediate mode with per-corner lighting and about 1,000 quads per view (`dungeon.md`). The draw radius (18 tiles) does not change with the floor size, so bigger floors cost the same to draw. Stacked zones are the exception, because a hole shows another level; the rule in section 7.6 (draw the other level only through a hole) is what keeps it bounded. New geometry must follow the phone rules already written (`phone-rendering.md`, "Rules for anything new"): nothing laid over another surface within a few centimetres (build it into the surface or stand it off by 1.5 cm or more), and the highp shader stays on. Floors up to 254 m across also put geometry further from the origin than before: the same document gives a 32-bit float as good to about 8 micrometres at 90 m, and it is about 15 micrometres at 254 m. That is still fine for what is drawn, but nothing small should be built far out.

## 14. Tests (planned)

Each uses the `tests/*_headless.c` pattern and is added to `tools/run_tests.sh` and `docs/testing.md` in the round that builds it. As in the other modules, each new rule is also **broken on purpose** and the test has to fail.

| Suite | What it checks |
|---|---|
| `floorgen_rng_headless.c` | the vectors of `09` section 4; stream independence; unbiased ranges (a wrong threshold must fail) |
| `floorgen_bits_headless.c` | every bit row operation against the plain loop on random grids: shifts across the 64-bit word boundary, runs, band fits, pattern masks, population counts (dropping the carry between words must fail) |
| `floorgen_skeleton_headless.c` | zones tile the hall exactly with no gap or overlap; minimum sizes; the graph is connected; loops exist; every start zone is on a cycle; the stair shell is right for every size from 45 to 128 |
| `floorgen_sockets_headless.c` | counts match the recipe; fairness inside the ratio; distance rules; the safe zone is not a cut vertex and has two gates |
| `floorgen_zones_headless.c` | for each zone type, over hundreds of seeds and all sizes: portals connected, lanes at least three tiles, counts in bounds |
| `floorgen_levels_headless.c` | at most four levels; every deck tile reachable; a connector for each pair of levels; hole margins; the frequency tables followed over thousands of rolls; falling through a hole lands on the floor below |
| `floorgen_cover_headless.c` | lanes at least three tiles; no cover in reserved tiles; shots stop at the right height; the push-out and the lighting treat classes correctly |
| `floorgen_hash_headless.c` | golden hashes; the same seed twice is identical; neighbouring seeds differ |
| `tests/floorgen_wasm_check.mjs` | the same golden hashes from the WASM build under Node |
| `tools/bench_floorgen.c` | per-stage timings, native and WASM; a workflow step publishes them as a Step Summary (the workflow file is delivered separately, never in a replacements tarball) |

The old suites keep passing in every round (the migration goes through the wrappers in section 6.3). R0 adds a **statistical equivalence check**: over 500 seeds the ported generator's route length, walkable fraction, partition and pillar counts must match the old generator's within a tolerance, since the new random streams change every floor.

## 15. Build order

Sizes as in the roadmap: S a few small changes, M one delivery like the last ones, L several. Every round ends with something you can try on the phone, with its docs updated (a module document, `docs/floorgen.md`, starts in R0) and a replacements tarball.

| Round | What | Size | Done when |
|---|---|---|---|
| R0 | **Foundation, nothing changes on screen.** `src/floorgen/` with `Pcg32`, SplitMix64, the bit rows, the floor hash. The current generator ported to integer only (the 16 probe directions become a table of integer vectors). `FloorMap` v2 behind the accessors; call sites moved. The benchmark harness and per-stage timers in the generation report. | M | all existing suites pass; the new suites pass; golden hashes equal natively and in WASM; the equivalence check passes; the report shows microseconds per stage |
| R1 | **Bigger floors.** `MAP_MAX 128`; sizes 45, 64 (81); the stair shell scaled; skeleton and zone graph; start, console and elevator sockets; `Floor_StartPoint`; the lighting window, bounded nav and flow, the swarm stack fix. | L | 1,000 seeds at every size pass every invariant; floor 1 has one start room per team inside the fairness ratio; the frame time at 64 by 64 on the phone is no worse than at 45 by 45 |
| R2 | **Cover.** The wall classes end to end (collision, shots, sight, light, drawing); the cluster library; the cubicle farm and lounge zone types. | M | you can take cover behind a low wall and shoot over it, and a mid wall hides you; the tests of section 14 pass |
| R3 | **Sockets and templates.** The template format and registry; the safe zone with two gates and sentinel spots; the boss arena recipe; mission-site and special-room sockets as stubs; the offices and storage zone types. | L | a template can be added by data only; a safe zone appears on the planned floors; a boss floor builds |
| R4 | **Stacked zones.** Levels up to four; holes and stairs; level aware accessors, drawing, light and collapse. | L | one stacked floor can be walked, shot across, fallen through and climbed back; the validation and falling tests pass |
| R5 | **Themes.** `ThemePalette` at run time (corporate equal to today); garden, spore and machinery zones; the prop bake tool, the licence ledger and the first prop set. | L | a floral, a fungal and a mechanical floor can be told apart on the phone, and the corporate floor looks as before |

## 16. Risks

| Risk | Likelihood | Impact | What reduces it |
|---|---|---|---|
| Layouts drift between platforms | low after R0 | a desync from the first frame | integer only, one stream per stage, golden hashes on two architectures, the hash in the handshake |
| Falling through a deck, or being stuck in a hole | medium | a broken floor, a stuck team | the accessor layer, the level flood fill, sweep tests, mutation tests, conservative step-up |
| Phone performance with bigger floors | medium | unplayable | R0 measures; the window and radius fixes; budgets per round; level culling |
| Scope: six rounds | high | the work stalls | each round ships and stops safely; R2 and R3 are useful on their own |
| Unfair placement | medium | teams feel cheated | the fairness check, re-rolled by attempt |
| Too much content to make (zone types by themes by templates) | high | slow | data driven; few types first; templates are data |
| Asset licences | medium | cannot ship | CC0 first, a ledger, no pack without its page read |
| Colliding with the multiplayer chat | medium | rework | section 12.5; nothing under `src/net/` is touched here |
| An old assumption of 45 left somewhere | medium | a crash at 128 | the audit list in section 13.4; a test that builds a 128 floor and runs every module on it |

## 17. Open decisions

Defaults are used if you do not say.

1. **Size for three or four teams.** *(default: 81; the cap is 128)*
2. **Hole edges:** railings, or open so players can drop? And does a 6 m drop hurt? *(default: open edges, no fall damage, a loud landing noise; needs the senses system)*
3. **Upper levels optional:** the critical path stays on level 0. *(default: yes)*
4. **Frequency numbers** in section 5.1. *(default: the placeholders, tuned by playing)*
5. **Other connectors** besides the stair: a ramp, a ladder, a lift. Which first? *(default: the stair only)*
6. **The elevator:** where it goes and what it does when active. *(default: a socket next to the stairs, behaviour unspecified until you say)*
7. **A safe floor:** a whole floor that is a safe zone, or only a zone on a normal floor? *(default: a zone; a safe floor is another recipe kind later)*
8. **The collapse on stacked floors and a wave that follows walking distance** instead of a circle. *(default: every level falls; the wave becomes path based once the distance field exists, in R4)*
9. **Do themes change monsters and hazards?** *(default: yes, through the recipe, designed with M5)*
10. **A daily seed** shared by everyone (mentioned in the research of the old generator). *(default: not planned)*

## 18. Not verified

- Nothing in this plan is built or has been seen on a screen. Every number marked placeholder is a guess to be tuned.
- The measurements in `09` section 5 were taken on an x86-64 sandbox core, with gcc, on synthetic floors, and with one shared core; single runs differed by up to about 1.7 times, so the tables use medians of five runs and only the order and the order of magnitude should be trusted. Nothing was measured on the phone, in the WASM build or on ARM.
- The mid-math `Pcg32` was read and re-implemented twice (in C and in Python, which agree); the Rust crate itself was not run.
- Licences of the assets are the ones shown on listings and search results at the time of research; each must be read on its own page when a model is taken.
