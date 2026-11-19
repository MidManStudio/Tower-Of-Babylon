# Floor generation: what was found out

Written on 2026-11-18, from `main` at commit `5514e4e`. This is the evidence behind `08-floor-generation.md`: what the repository does today, what the sources say, which assets are free to use, what mid-engine offers and what a first measurement showed. Marks used here: **built** (checked in the repository), **excerpt** (the text of a search result or a listing was read, not the whole page), **read** (the source itself was read) and **measured** (a run, with where it ran). Nothing here has been tried on the phone.

How it was done, in the order of your standing rules: both skill repositories were cloned and the relevant skills read as instructions (search before building, deep research with sources, the benchmark method); the game repository, the design documents, the generator, the renderer notes and the mid-engine repository were read; the web was searched for generation methods, determinism, free assets and one toolchain question; a prototype measured the techniques that were proposed.

## 1. The repository audit (built)

### 1.1 The generator

Everything in `docs/dungeon.md` still holds (that document is accurate and is not repeated). The facts that decide this plan, with where they were found:

| Fact | Where |
|---|---|
| 45 by 45 tiles, 2 m tiles, 6 m walls, 8 m between floors, corridors at least 3 tiles | `src/dungeon/dungeon_types.h` |
| the stair shell is constants for 45 by 45: wall at x = 36, up alcove z 0 to 8, down alcove z 36 to 44, doors at z 3 to 5 and 39 to 41, pads and arrival points at x 40 and 38, approach points at x 35 | `dungeon_types.h` |
| a tile is `MapCell`: `TileType type` (an enum, 4 bytes), `discovered`, `variant`, `flags`: 8 bytes | `dungeon_types.h` |
| solid means `type == TILE_WALL`, and out of bounds is solid | `Tile_IsSolid` |
| random numbers: xorshift32 and a 32-bit integer hash; the floor seed is the tower seed mixed with the floor number | `dungeon_generator.c` |
| floats and libm in layout decisions: `hypotf` at line 160 (the ramp of the span target from the stair doors), `cosf` and `sinf` at line 75 of `dungeon_probes.c` (the 16 ray directions) | `dungeon_generator.c`, `dungeon_probes.c` |
| up to 8 attempts, then a pillar-hall fallback; the generator keeps its working state in `static` arrays, so it is not re-entrant | `dungeon_generator.c` |
| 3.0 ms on average and 14 ms at worst per floor on x86; not measured on the phone | `dungeon.md` |
| the floor window keeps three floors; a climb builds exactly one | `floor_window.c`, `dungeon.md` |
| the console is placed from the floor's seed: open ground a metre clear all round, away from both alcoves, at least 22 tiles from the foot of the up stairs (relaxed to 12, then anywhere), 8 from the arrival | `floor_run.md`, `src/floor/floor_game.c` |
| the collapse is a circle around the door; `floor_run.md` itself notes that a wave that follows walking distance would be fairer | `floor_run.md` |
| every player arrives at one point; Welcome carries `floorSeed` (32 bits), `floorNumber`, `serverTick` and one `spawnX`, `spawnZ` | `src/net/net_proto.h`, `net_server.c` |
| per-corner tile lighting, ceiling lights on a lattice of one every 3 tiles (6 m) and a draw radius of 18 tiles keep a view at about 1,000 quads; the phone reported 62 to 63 FPS | `dungeon.md`, `lighting.h` (`LIGHT_LATTICE_TILES`), `dungeon_renderer.c` (`DRAW_RADIUS_TILES`) |

### 1.2 What reads the floor (the migration to accessors)

References to the grid API (`grid[`, `Tile_IsSolid`, `TILE_WALL`): the generator 25, the renderer 8, `spider_ai.c` 6, `swarm.c` 5, `dungeon_types.h` 5, `pickups.c` 4, `floor_game.c` 4, `enemy_manager.c` 3, `dungeon_raycast.c` 3, `dungeon_probes.c` 3, `player_controller.c` 2, `lighting.c` 1; and 13 test files. So moving everything to accessors means touching 11 source files outside the generator. References to `WALL_HEIGHT` and `FLOOR_HEIGHT` outside `dungeon_types.h`: `dungeon_raycast.c` 9, `staircase.c` 7, `dungeon_renderer.c` 2, and one each in `swarm.c`, `spider_ai.c`, `floor_window.h` and `dungeon_raycast.h`: 22 in 7 files.

### 1.3 What grows with the map

Arrays sized by `MAP_WIDTH` and `MAP_HEIGHT` (declarations, found by search; the totals are counted, not measured):

| Where | What | At 45 by 45 | At 128 by 128 |
|---|---|---|---|
| `lighting.c` | `g_tileI`, `g_tile` (float), `g_open` (bool), `g_panelAt` (int), `g_corner` | 35 KB | 275 KB |
| `spider_ai.c` | `g_nav` and `g_navQueue` (int) | 16 KB | 128 KB |
| `pickups.c` | `dist` and `queue` (int, static in a function) | 16 KB | 128 KB |
| `floor_game.c` | `cand` (int) | 8 KB | 64 KB |
| `swarm.c` | static `cx`, `cz`, `cd`; and `Swarm.flow` (int16) per swarm | 24 KB | 192 KB (+32 KB per swarm) |
| `FloorMap.grid` (8 bytes a tile), three floors in the window | | 49 KB | 384 KB |
| `dungeon_generator.c` | `prot` and `hall` (bool) | 4 KB | 32 KB |
| **Total** | | **about 150 KB** | **about 1.2 MB** |

`tools/mray.py` passes `-s ALLOW_MEMORY_GROWTH=1`, so memory is not a hard limit. One array is not static: `FlowUpdate` in `src/swarm/swarm.c:42` declares `int qx[MAP_WIDTH * MAP_HEIGHT], qz[MAP_WIDTH * MAP_HEIGHT]` on the stack: 16 KB now, 128 KB at 128 by 128. See 2.6 for why that matters in the web build.

### 1.4 Work that walks the whole grid

- `Lighting_Update(t)` runs every frame: it clears the sums, adds each lit fixture's pool, applies the tone curve (`1 - expf(-gain * I)`) to every tile and averages the four tiles around every corner. At 128 by 128 that is 16,384 tiles and 16,641 corners per frame. Measured in 5: 19 microseconds at 45 by 45, 217 at 128 by 128, against about 9 to 10 for a 37 by 37 window around the viewer at any size.
- `NavCompute` (spiders, `spider_ai.c`) is a breadth-first search over the whole grid, redone when the target changes tile or after `NAV_REFRESH_SECS`.
- `FlowUpdate` (swarm, `swarm.c`) does the same for the swarm's flow field.
- `Pickups_SpawnForFloor` and `FloorGame_NewFloor` search the whole grid once per floor (cheap, at floor build).

### 1.5 Build flags

`tools/mray.py` sets `ALLOW_MEMORY_GROWTH=1`. None of the build scripts checked (`tools/`, `scripts/`, `android/`, `.github/`) sets `-ffp-contract`, `-ffast-math` or a stack size.

## 2. Techniques surveyed (excerpts)

The text of search results was read; the long articles were not read in full. Each entry says what it claims, what the plan takes and what it leaves.

### 2.1 Cyclic generation (Dormans; the game *Unexplored*)

*Claim.* Dormans' cyclic generator builds a level from cycles, so there is more than one route and a loop back to the start, and the cycle becomes the backbone that gives the level a meaningful arc and pacing. Locks and keys sit inside the cycles. Plain graph rewriting tends to give branching trees, which is what the cyclic approach was made to avoid.
*Taken.* A zone graph with extra edges for loops, every start zone on a cycle (`08` 5.3); the console and the stairs as the lock and the key.
*Left.* The full mission-graph grammar. The mission content is still to be designed (`08` section 9).
Sources: Boris the Brave, *Dungeon Generation in Unexplored*; Game Developer, *Unexplored's secret: cyclic dungeon generation*; Boris the Brave, *Graph Rewriting for Procedural Level Generation*.

### 2.2 Wave Function Collapse and model synthesis

*Claim.* WFC has no global structure: its output looks like the input only locally, so connectivity and pacing have to come from somewhere else. On a contradiction the plain algorithm restarts, backtracking can stall, and "modifying in blocks" restarts only the block that failed (with known-good fallbacks). Tools such as DeBroglie add path constraints and backtracking.
*Taken.* Per-zone retries with sub-seeds and a safe fallback zone (`08` 5.5). WFC-style rules may be used for prop dressing and for cellular-automaton zones later.
*Left.* WFC for the macro layout.
Sources: Boris the Brave, *Wave Function Collapse tips and tricks*; *Model Synthesis and Modifying in Blocks*; the DeBroglie documentation.

### 2.3 Templates around a guaranteed path (Spelunky)

*Claim.* A level is a 4 by 4 grid of rooms with a guaranteed route through it; each room type has 8 to 16 templates made of fixed tiles, random tiles and obstacle blocks. A paper on the generator notes that a filled template can break the route the coarse path promised.
*Taken.* Special rooms and missions as templates stamped into reserved sockets, with the zone validated again after the stamp (`08` section 9).
Sources: Darius Kazemi's Spelunky generator write-ups; Smith et al., *Quality of Play* (University of Washington, 2013).

### 2.4 Building interiors

*Claim.* Lay a skeleton hallway through the building first and fill rooms around it (after Dahl and Rinde, Chalmers). Pitfalls reported: an atrium too close to the edge leaves a narrow or missing hallway and unreachable rooms; a flood fill is the way to check connectivity.
*Taken.* Voids keep a margin from the zone border; level connectivity is checked by a flood fill over (level, x, z) (`08` 7.5). The "skeleton first" idea is the zone graph and its portals.
Sources: the *Subveillance* devlog (parts 1 and 3); Lars Rinde and Alexander Dahl, *Procedural Generation of Indoor Environments* (Chalmers).

### 2.5 Determinism across platforms

*Claim.* Box2D's author: determinism is brittle and needs continuous testing; fused multiply-add must be turned off (`-ffp-contract=off` on Clang and GCC); trigonometric functions come from each compiler's own C library and differ. Another source: a test that passes on one architecture's CI does not show cross-platform determinism; at least two architectures are needed; integer arithmetic is deterministic everywhere. Glenn Fiedler's *Floating Point Determinism* is the usual reference.
*Taken.* Integer-only layout (`08` 12.1) and golden hashes run natively and in WASM (`08` 12.3). The risk is concrete here: the current generator calls `hypotf`, `cosf` and `sinf` and nothing turns fused multiply-add off, and the server, the WASM client and the Android client each build the floor from the seed.
Sources: box2d.org, *Determinism* (2024); bugnet.io, *How to debug floating point determinism across platforms*; gafferongames.com, *Floating Point Determinism* (title seen).

### 2.6 The web build's default stack

*Claim.* Emscripten 3.1.27 (29 November 2022) cut the default `STACK_SIZE` from 5 MB to 64 KB; programs that use more must pass `-sSTACK_SIZE` at link time.
*Consequence.* `FlowUpdate`'s two 128 KB stack arrays at 128 by 128 overflow a 64 KB stack. Which Emscripten version the Termux toolchain has was not checked; the array moves to static storage either way (`08` 13.4).
Sources: the Emscripten ChangeLog for 3.1.27 and pull request 18191.

### 2.7 Already in `dungeon.md`

The survey of van der Linden and others (2014) and the two articles cited there remain the background for the probe-ray design; nothing found changes that.

## 3. Free assets (excerpts and listings)

What the sources say about licences: **CC0 means no conditions, and that covers everything from Kenney, Quaternius, KayKit, Poly Haven and AmbientCG**; OpenGameArt and Freesound mix CC0, CC-BY and CC-BY-SA **per file**, so each page has to be read before shipping (from a guide to free furniture models, excerpt). Kenney does not require credit.

| Pack | Licence as listed | What is in it | Use here | Caveat |
|---|---|---|---|---|
| Kenney, Furniture Kit (kenney.nl/assets/furniture-kit) | CC0 | 140 files: chairs, sofas, tables, bookcases, kitchen, bathroom | office and lounge dressing | residential look; recolour by palette |
| Quaternius, Modular Sci-Fi MEGAKIT (quaternius.com/packs/modularscifimegakit.html) | CC0 | 270 plus modular pieces made to a grid, 190 in the free version; glTF, OBJ, FBX | the mechanical theme: pipes, panels, gantries | check the grid size against the 2 m tile |
| KayKit, Furniture Bits (kaylousberg.itch.io/furniture-bits) | CC0 | low-poly furniture | office and lounge | one gradient atlas per pack |
| KayKit, Dungeon Remastered (kaylousberg.itch.io/kaykit-dungeon-remastered) | CC0 | 200 plus modular pieces, with modular stair pieces added in a later version | reference for the secondary stairs, crates and railings | fantasy styling |
| KayKit, Halloween Bits (kaylousberg.itch.io/halloween-bits) | CC0 (part of the Bits bundle) | spooky props | horror dressing | |
| KayKit, Prototype Bits | CC0 (Bits bundle) | blocks and shapes | prototyping | |
| PSX Office Models, knightanni (knightanni.itch.io/psx-office-models) | **not shown in the listing** | 28 low-poly pieces and low-resolution textures; baseboard, column, walls, floor, ceiling light, doors, window; "made for horror office games" | the closest to the look | read the licence first; textures are not used |
| other office packs on itch.io (modern office props, PSX 90s office, low poly office essentials) | **not shown in the listings** | desks, cubicles, cabinets | candidates | read the licence first |
| Backrooms packs on itch.io: a CC0 pack of about 103 MB as one `.glb`; a "Backrooms-like" modular pack | CC0 by its title; the other not shown | modular rooms | reference only | 103 MB is far too heavy for the web build |
| Quaternius, Low Poly Guns Pack; Kenney, Blaster Kit | CC0 | guns; 40 files of blasters, targets, magazines, crates | not needed now (the weapons are built from parts) | |

Bits packs: the listing says each is textured from **one small gradient atlas** (1024 by 1024, usable down to 128 by 128), CC0, no credit needed. That converts directly to vertex colours.

URLs for the models that are taken are to be added to the ledger when they are taken (`08` section 11). Licences above are what listings showed during the research.

## 4. mid-engine (read)

### 4.1 What exists and how the game uses it

| In mid-engine | Where | Use in the generator |
|---|---|---|
| `Pcg32` with a `seq` argument that gives independent streams; 16 bytes; period 2^64 | `mid-math/src/ran_gen/pcg.rs` | **mirror** in C: one stream per stage, zone and socket |
| deterministic fixed point, pure integer, "bit-identical on x86_64, aarch64 and wasm32" | `mid-math/src/fixed/` | the discipline (integer-only layout, Q16.16 where a ramp is needed) |
| `BitMask8` to `BitMask64`, `BitMask128` (two words), `BitMask256` (four words), with `count_ones` and `iter_ones` | `mid-math/src/storage/storage_mask.rs` | **mirror** the 128-bit shape for a row of up to 128 tiles |
| wide integer types `i32x4`, `u32x4`, `i16x8`, `u16x8`, `i8x16`, `u8x16` and masks, with SSE2, NEON, AVX2 (additive: 8 and 16 wide), WASM simd128 and scalar backends | `mid-math/src/wide/int/`, `docs/platform-optimization.md` section 9 | a C header with the same names, on the compiler's vector extensions |
| a bump arena | `docs/mid-arena.md` | the per-floor arena for upper levels and scratch |
| the optimisation rules: measure first; unmeasured means nonexistent; release numbers only; publish a Step Summary; Tier 1 (compiler-guided) by default; Tier 2 (intrinsics) needs a scalar fallback and at least 10 percent at scale; assembly only for `rdtsc`, `prefetch`, `pause`; the benchmark protocol | `docs/platform-optimization.md` | the method of `08` section 13.1 |
| noise (value, Perlin, simplex, Worley, fbm) | `mid-math/src/noise/` | float based: for cosmetic things only (light mood), never for a layout |

Two platform notes from the same document that apply to the game: **NEON is mandatory on aarch64**, so no runtime check is needed on the phone; and **WASM SIMD is not automatic** (a target feature flag is needed; for `emcc` that is `-msimd128`, which `tools/mray.py` does not pass).

### 4.2 Mirror, do not link

The game is C99 and its release builds run under Termux, Emscripten and the NDK. Linking the Rust crate (its C FFI layer exists) would add a Rust toolchain to each of them for a few tiny operations, and each call would cost more than the operation. The algorithms are small. So the game takes them as C files and keeps shared test vectors, which is also what makes it possible for a Rust server to generate the same floors later.

### 4.3 Test vectors for the C port (computed in C and again in Python; they agree)

`Pcg32::new(seed, seq)` as read in `pcg.rs`: `inc = (seq << 1) | 1`; `state = seed`; one `next_u32` is discarded. The step is `state = state * 6364136223846793005 + inc`, the output is the xorshift-and-rotate of the old state. **This is not the seeding of the PCG reference code** (which takes two steps), so the vectors are not the reference ones.

| Call | Output |
|---|---|
| seed 42, seq 54: six `next_u32` | `0c855c84 0bde36a5 49dd4da9 92dc7b03 044ceb1d b7c9a0b0` |
| seed `0x123456789ABCDEF0`, seq 1: four `next_u32` | `93525db3 1f4d360e d1332d05 a3a75e1c` |
| seed 42, seq 54: eight `range(0, 100)` | 4 4 28 57 1 71 29 94 |
| seed 7, seq 9: eight `range(3, 19)` | 14 17 15 9 15 18 4 10 |
| `SplitMix64(0)`, `SplitMix64(1)`, `SplitMix64(0xDEADBEEF)` | `e220a8397b1dcdaf 910a2dec89025cc1 4adfb90f68c9eb9b` |

`range(lo, hi)` is Lemire's method with the **32-bit** rejection threshold `(2^32 - n) mod n`.

**A finding in mid-math.** `Pcg32::range_u32` (`pcg.rs:105`) declares `range` as a `u64` and takes the threshold as `range.wrapping_neg() % range`, which is `2^64 mod range`, not `2^32 mod range`. The two are equal for powers of two and for some other ranges and differ for others (range 7: 4 against 2; range 45: 31 against 16; range 81: 49 against 52; range 100: 96 against 16). The effect on the statistics is negligible (a few counts in hundreds of millions), but the outputs can differ from an exact Lemire in the rare rejection branch, so a Rust implementation and the C one would not agree bit for bit on every seed. Either the crate is corrected (compute the threshold in 32-bit) or the C port mirrors the crate; the plan uses the exact 32-bit form and records this here. Not touched in mid-engine.

## 5. Prototype measurements (measured)

**What.** Two small programs, kept in `tools/bench/` (`gen_prims_bench.c`, `light_scale_bench.c`), built for this research and to be replaced by the real harness in round R0. Each file is wrapped in a `-D` guard (`GEN_PRIMS_BENCH`, `LIGHT_SCALE_BENCH`) so mray's build, which compiles every `.c` file, ignores it, as with the headless tests; the build line is in the file's header. **To get the phone's numbers**, run in Termux from the project root: `clang -O2 -std=gnu99 -DGEN_PRIMS_BENCH tools/bench/gen_prims_bench.c -o /tmp/g && /tmp/g` and the same with `-DLIGHT_SCALE_BENCH ... -lm` for the second; the program prints the compiler, the architecture and whether the build is optimised. The first compares plain loops with bit mask rows on the operations the generator repeats most: a span scan (free tiles from a tile to the next wall, four directions), a band check (rows z-3 to z+3 over a column range all free), a flood fill from one tile (reach and farthest distance: a queue search on `uint16` arrays against a row-wise bit wavefront), a pattern search (every tile whose 3 by 3 block is free, with the count) and a per-tile hash fill (a scalar loop against a 4 wide vector loop written with the compiler's vector extensions). **Every pair is checked to give identical results before it is timed.** The second replicates the two whole-grid loops of `Lighting_Update` and times them against a 37 by 37 window.

**Where.** An Intel Xeon at 2.1 GHz, one shared core, gcc 13.3.0, Ubuntu 24.04, x86-64. Floors are synthetic: a border and random wall segments with a 3 tile doorway. Each figure is the best of several repetitions inside a run, and the tables show the **median of five runs**. A single run differed by up to about 1.7 times from the median (the first run of the lighting replica gave 332 microseconds at 128 by 128, the median is 217), so only the order and the order of magnitude should be trusted.

**Not measured:** the phone, the WASM build, ARM, a build with `-msimd128`.

Per-operation figures: span and band in nanoseconds per query, the others in microseconds for the whole floor. "Loop" is the plain code, "bits" the bit mask version.

### gcc -O2 (x86-64 baseline: no hardware population count)

| Size | span loop -> bits | band loop -> bits | flood queue -> bits | 3x3 loop -> bits | hash scalar -> wide |
|---|---|---|---|---|---|
| 45 | 16.1 -> 6.3 | 20.2 -> 3.4 | 7.8 -> 8.7 | 2.8 -> 0.9 | 1.3 -> 0.9 |
| 64 | 16.2 -> 6.4 | 16.5 -> 3.4 | 17.0 -> 17.4 | 6.4 -> 1.9 | 2.6 -> 1.9 |
| 81 | 16.0 -> 7.9 | 17.9 -> 4.7 | 34.5 -> 24.2 | 10.3 -> 2.8 | 4.1 -> 3.0 |
| 128 | 16.5 -> 9.5 | 18.7 -> 7.8 | 144.1 -> 60.0 | 32.4 -> 7.2 | 10.3 -> 7.6 |

### gcc -O3

| Size | span loop -> bits | band loop -> bits | flood queue -> bits | 3x3 loop -> bits | hash scalar -> wide |
|---|---|---|---|---|---|
| 45 | 16.6 -> 6.5 | 18.5 -> 3.4 | 6.0 -> 9.5 | 2.0 -> 0.9 | 1.8 -> 0.9 |
| 64 | 16.6 -> 6.4 | 14.8 -> 3.4 | 13.2 -> 19.1 | 4.1 -> 1.9 | 3.7 -> 1.9 |
| 81 | 16.4 -> 7.8 | 16.0 -> 4.2 | 25.4 -> 26.9 | 6.2 -> 2.9 | 5.9 -> 3.1 |
| 128 | 16.8 -> 9.2 | 16.9 -> 7.2 | 117.8 -> 65.4 | 22.5 -> 7.2 | 14.5 -> 7.4 |

### gcc -O2 -mpopcnt -mavx2 (hardware population count and AVX2)

| Size | span loop -> bits | band loop -> bits | flood queue -> bits | 3x3 loop -> bits | hash scalar -> wide |
|---|---|---|---|---|---|
| 45 | 16.5 -> 6.1 | 20.4 -> 3.5 | 8.1 -> 5.4 | 2.8 -> 0.9 | 1.3 -> 0.5 |
| 64 | 16.7 -> 6.3 | 16.6 -> 3.4 | 23.9 -> 10.7 | 6.9 -> 1.9 | 2.6 -> 1.0 |
| 81 | 16.4 -> 7.8 | 18.1 -> 4.8 | 58.8 -> 14.9 | 11.9 -> 2.9 | 4.2 -> 1.6 |
| 128 | 16.8 -> 8.9 | 18.6 -> 7.9 | 181.9 -> 36.0 | 40.6 -> 7.1 | 10.6 -> 4.0 |

### The lighting replica (gcc -O2, microseconds per frame)

| Floor size | whole grid | clearing the sums | 37 by 37 window |
|---|---|---|---|
| 45 | 19.2 | 0.7 | 9.2 |
| 64 | 45.7 | 0.7 | 9.6 |
| 128 | 216.7 | 0.7 | 9.5 |

### What it says

- **Span scans:** the bit rows were 1.7 to 2.7 times faster at every size and flag set. **Band checks:** 2.4 to 5.9 times. **Pattern searches:** 2.1 to 5.7 times, and the gain grows with the floor size, because the loop pays for the area and the rows only for their count. These pass the 10 percent gate by a wide margin and are adopted (`08` 13.3).
- **Flood fill is not a clear win.** Without hardware population count the bit wavefront tied or lost at 45 and 64 tiles, was mixed at 81 (level with `-O3`, 1.4 times faster with `-O2`) and won 1.8 to 2.4 times at 128; with hardware population count (the situation on ARM64 and in WASM, which has an instruction for it) it won at every size, 1.5 to 5 times. The queue search itself costs 6 to 8 microseconds at 45 and 120 to 180 at 128, which is small next to a floor's generation time unless it is called many times. So the plan keeps the queue up to 81 and uses the wavefront at 128 if the phone agrees. This is the case Rule 0 warns about: a technique that looks faster on paper and has to be measured on the target.
- **The wide hash fill** was 1.4 to 2.7 times faster (the best with AVX2), but the whole-floor cost is 0.5 to 15 microseconds. It passes the 10 percent gate and fails the "is it a hotspot" question, so it is left scalar for now.
- **Lighting** is the real finding: the per-frame loop costs 19 microseconds at 45 by 45 and 217 at 128 by 128 (11 times, for 8 times the tiles), and a window around the viewer costs about 9 to 10 at any size. At the phone's speed (not measured, and likely several times slower than this core) the whole-grid loop would be a visible part of a 16.6 ms frame at 128 by 128.
- **Compiler flags matter:** `-O3` made the queue search faster at 45 to 81 tiles (6.0 against 7.8 microseconds at 45), which flipped the flood fill comparison. Everything here must be repeated on the phone's compiler and in the WASM build.

## 6. Options considered and left out

| Option | Why not |
|---|---|
| Wave Function Collapse for the whole layout | no global structure; connectivity and pacing would have to be bolted on (2.2) |
| BSP rooms and corridors only | the tree of rooms is the dead-end-heavy shape the cyclic approach exists to avoid; the Backrooms feel is open space with partitions, which the zone types keep |
| Voronoi zones | diagonal borders, and a zone is no longer a rectangle in a small local buffer |
| Floats with `-ffp-contract=off` | it would fix fused multiply-add but not libm differences, and the layout is on a grid, where integers cost nothing |
| A full-size layer for every one of four levels | most floors have one level; the zone-sized arena follows the stacked area only |
| Streaming or hash-per-chunk "infinite" floors | floors are finite and carry meaning (stairs, sockets, a boss arena) |
| Re-running the whole floor on failure (today's 8 attempts) | a local failure should cost a local retry; zones retry alone |
| Linking mid-math's C FFI | see 4.2 |
| The 103 MB CC0 Backrooms `.glb` | far too heavy for the web build |
| Textured packs as they are | the renderer has no textures; shapes are used, colours come from palette slots |

## 7. Sources

- Boris the Brave: *Dungeon Generation in Unexplored* (https://www.boristhebrave.com/2021/04/10/dungeon-generation-in-unexplored/), *Graph Rewriting for Procedural Level Generation* (https://www.boristhebrave.com/2021/04/02/graph-rewriting/), *Wave Function Collapse tips and tricks* (https://boristhebrave.com/2020/02/08/wave-function-collapse-tips-and-tricks), *Model Synthesis and Modifying in Blocks* (https://www.boristhebrave.com/2021/10/26/model-synthesis-and-modifying-in-blocks/)
- Game Developer: *Unexplored's secret: cyclic dungeon generation* (https://gamedeveloper.com/design/unexplored-s-secret-cyclic-dungeon-generation-)
- Spelunky generator write-ups: https://tinysubversions.com/spelunkyGen and https://www.tinysubversions.com/spelunkyGen2/index.html; Smith et al., *Quality of Play* (https://grail.cs.washington.edu/wp-content/uploads/2015/08/smith2013qop.pdf)
- Building interiors: the *Subveillance* devlog, parts 1 and 3 (https://unusual-intersection.itch.io/subveillance/devlog/492733/procedural-generation-of-building-interiors-part-1, https://unusual-intersection.itch.io/subveillance/devlog/505410/procedural-generation-of-building-interiors-part-3); Rinde and Dahl, *Procedural Generation of Indoor Environments*, Chalmers (https://www.cse.chalmers.se/~uffe/xjobb/)
- Determinism: https://box2d.org/posts/2024/08/determinism/; https://bugnet.io/blog/how-to-debug-floating-point-determinism-across-platforms; https://gafferongames.com/post/floating_point_determinism
- Emscripten: pull request 18191 (https://github.com/emscripten-core/emscripten/pull/18191) and the 3.1.27 entry of its ChangeLog
- Assets: https://kenney.nl/assets/furniture-kit; https://quaternius.com/packs/modularscifimegakit.html; https://kaylousberg.itch.io/furniture-bits; https://kaylousberg.itch.io/kaykit-dungeon-remastered; https://kaylousberg.itch.io/halloween-bits; https://kaylousberg.itch.io/bits-bundle-1; https://knightanni.itch.io/psx-office-models
- In the repository: `docs/dungeon.md`, `docs/floor_run.md`, `docs/lighting.md`, `docs/phone-rendering.md`, `docs/design/01`, `03`, `05`, `06`, `07`; in mid-engine: `docs/platform-optimization.md`, `docs/mid-arena.md`, `crates/mid-math/src/ran_gen/pcg.rs`, `crates/mid-math/src/fixed/`, `crates/mid-math/src/storage/storage_mask.rs`
