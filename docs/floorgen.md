# Floor generation: the foundation (round R0)

The module documentation for `src/floorgen/` and for what changed in the generator. The plan this belongs to is `design/08-floor-generation.md`; the research and measurements behind it are `design/09-floor-generation-research.md`. Status on 2026-11-21: **R0 is partly built.** The core (an integer-only generator on PCG32 streams, bit-row scans, a floor hash, a generation report with a time per stage, and five new test suites) is in and verified. Four things the plan lists for R0 are **not** built yet and are handed to the next chat: see "What is left of R0".

## Overview

A floor is built from its seed on the server and on every client, so every client and the server must make the same floor. Round R0 makes that true by construction instead of by luck, without changing what a floor looks like:

- **Integer only.** Nothing that decides a layout uses `float`, `double` or libm any more. The old generator called `hypotf`, `cosf` and `sinf`, and no build script turns off fused multiply-add, so a different libm or compiler could move a wall for one player. Distances are Q16.16 fixed point (`fg_fixed.h`); the 16 probe directions are a table of integer vectors written down once (`fg_probe.h`). `tests/floorgen_integer_headless.c` reads the source and fails on any float or libm use.
- **PCG32 streams.** One random stream per sub-stage and attempt (`fg_rng.h`), so a stage can be added or changed without moving what the others made.
- **Bit rows.** Free runs, 3 by 3 fits and side bands are shifts and bit scans on 128-bit rows instead of tile loops (`fg_bitrow.h`). A plain version of each stays as the oracle.
- **A floor hash** (`fg_hash.h`) and golden hashes for 40 floors, the same on every build and platform tried.
- **A generation report with a time per stage** (`FloorMap.stageMicros`, `FloorMap.genMicros`), so the next round starts from numbers.

The six new files are **header-only** (`static inline`). That is deliberate: no new `.c` file means no build line had to change anywhere (the tests, `scripts/build_server.sh`, the Android script and mray's build, which compiles every `.c` file in the project, are all untouched).

The layout the player sees is statistically the same as before: over 40,000 floors the old and the new generator have the same route length, open space, partition and pillar distributions (`design/09`, section 5b). The floors themselves are different for every seed, as the plan said they would be.

## Rules

1. Layout code is integer only: no `float`, `double`, libm, `math.h` or float literal in `src/floorgen/*.h` or `src/dungeon/dungeon_generator.c`. The one float in the generator, the display fraction `walkableFraction`, carries the allow marker and is never read by the layout (the layout compares the integer `openTiles`).
2. Never shift a negative number right (implementation defined). Values passed to `FX_TO_INT` are not negative.
3. Never call a random function twice inside one argument list, or any list: C does not say in which order the arguments are evaluated (gcc on x86-64 evaluates them right to left). Found while writing the first test vectors.
4. A new random choice gets its own stream (a new stage number in `fg_rng.h`), so it cannot move an existing one. Changing any output for any seed means raising `FG_GEN_VERSION`.
5. A floor made twice, or after other floors, must be identical: the generator keeps working state in statics, and the golden test makes a floor, makes two others, and makes it again.

## Modules

### `fg_fixed.h`

`Fx` is signed Q16.16 (`FX_ONE` 65536, `FX_HALF` 32768, `FX_FROM_INT`, `FX_TO_INT` for non-negative values). `FgIsqrt64` is an exact integer square root (floor), bit by bit. `FgDistFx2(d2)` is the distance for a squared tile distance, as Q16.16, rounded down: it replaces `hypotf`.

### `fg_rng.h`

PCG32 as in mid-engine (`mid-math/src/ran_gen/pcg.rs`): `FgPcg32`, `FgPcg32_Seed(seed, seq)` (`inc = (seq << 1) | 1`, `state = seed`, one step discarded, as in mid-math and **not** as in the PCG reference code), `FgPcg32_Next`. `FgPcg32_Range(lo, hi)` is Lemire's method with the **32-bit** threshold (`FgPcg32_Threshold(n) = 2^32 mod n`); `FgRng_Int(lo, hi)` includes both ends; `FgRng_Chance(perMille)`. `FgSplitMix64` is the usual SplitMix64.

`FG_GEN_VERSION` (1) is the generator version. `FgFloorSeed64(floorSeed32)` widens the 32-bit floor seed that travels on the wire, mixed with the version. `FgStream(r, seed64, stage, index, attempt)` makes one independent stream: the key is `(stage << 32) | (index << 8) | attempt` (index 24 bits, attempt 8 bits), and the **state and the increment are both derived from it with SplitMix64**. Stage numbers (`FG_STREAM_*`) are listed there; only `FG_STREAM_ZONE` is used so far, with `FG_ZONE_HALLS`, `FG_ZONE_PARTITIONS` and `FG_ZONE_PILLARS` as the sub-stages (index = `(zone << 4) | sub-stage`; the one zone is 0).

Why the key is mixed and not used as the stream number directly: with mid-math's seeding, two stream numbers of **one seed start with the same first output** (measured: 20,000 of 20,000 seeds; the second outputs differ). The first output comes from a state that differs between the streams only in its low bits, and the output function reads the high ones. `FgStream` gives each stream its own state and increment, so neighbouring keys start differently (checked on 2,000 floors). The test keeps the raw property as a documented check. The mid-math documentation says different `seq` values give "completely independent streams"; for the first output that is not so (see the findings below).

Test vectors (computed in C and again in Python; they agree). Seed 42, stream 54: `0c855c84 0bde36a5 49dd4da9 92dc7b03 044ceb1d b7c9a0b0`. Seed `0x123456789ABCDEF0`, stream 1: `93525db3 1f4d360e d1332d05 a3a75e1c`. `range(0,100)` from seed 42, stream 54: 4 4 28 57 1 71 29 94. `range(3,19)` from seed 7, stream 9: 14 17 15 9 15 18 4 10. `SplitMix64(0)` = `e220a8397b1dcdaf`, `(1)` = `910a2dec89025cc1`, `(0xDEADBEEF)` = `4adfb90f68c9eb9b`. `FgFloorSeed64(0x12345678)` = `5657179ed0274a01`; the stream (zone, index 1, attempt 3) of it starts `07e71de9 3796ab1f 51e4de36`. These are in `tests/floorgen_rng_headless.c`.

### `fg_clock.h`

`FgClockMicros()`: microseconds from C99 `clock()`, so it builds for the tests, the server, Android and the web alike. It wraps after about 71 minutes, so only differences mean anything. In the browser the resolution is the browser's timer. Used only for the generation report, never for a decision. (WASI, used here only for a check, lacks process clocks; build that with `-D_WASI_EMULATED_PROCESS_CLOCKS -lwasi-emulated-process-clocks`. Emscripten has `clock()`.)

### `fg_bitrow.h`

`FgRow` is a row of up to 128 tiles as two 64-bit words (the shape of mid-math's `BitMask128`): `Or`, `And`, `AndNot`, `Shl`, `Shr` (across the word boundary), `Ctz`, `Clz`, `Pop`, `MaskBelow`, `MaskRange`. `FgBitGrid` keeps the solid tiles of a floor twice, by row (bit x) and by column (bit z), so a free run in any of the four directions is one shift and one bit scan. Out of bounds is solid, like `Tile_IsSolid`.

Queries: `FgBitGrid_RunE/W/S/N`, `FgBitGrid_Open3x3`, `FgBitGrid_SidesFree`. Each has a `_Fast` version and a `_Plain` version (one tile at a time); the name without a suffix is the fast one, or the plain one when the whole program is built with `-DFG_PLAIN_SCANS`. A floor made either way must be identical, which is how the golden hashes were taken (plain) and are checked (fast).

### `fg_probe.h`

The probe field of the generator, integer only: a lattice of probes (every 4 tiles from tile 2) that measure the open space around them. The four axis reaches are bit scans (`n` free tiles give `n + 0.5`, capped at 64); with all rays, 16 evenly spaced rays are cast by a grid walk (Amanatides and Woo) in Q16.16: which tile boundary comes first is decided by multiplying across, not dividing, a tie goes to the z boundary, and one division at the end gives the distance. `FG_RAY_DIR` holds the 16 directions as Q16.16 (cos, sin), made once offline, so nothing at run time needs trigonometry. `FgProbeField` is sized for 128 by 128; at 45 by 45 it uses 11 by 11 probes.

The old float code is gone from layout code. `Probe_CastRay` (float) stays in `dungeon_probes.c` only because the monsters use it for line of sight.

### `fg_hash.h`

`FgFloorHash(floor)`: a 32-bit FNV-1a over the generator version, the size, the floor number and seed, every tile's type, variant and flags, and the result counts of the report (partitions, pillars, route length, attempts, fallback). **Not** in it: the discovered bit, the timings, the display fraction, the open count. Two builds that make the same floor give the same hash.

## What changed in the generator

- `src/dungeon/dungeon_generator.c` is rewritten with the same structure: shell, protect mask, pillar halls, the probe-driven partition pass, the pillar pass, the pocket pass, measure, up to 8 attempts, the pillar-hall fallback. Random choices come from three streams per attempt (halls, partitions, pillars). The float decisions became whole numbers: pick chances in per mille, the pillar chance 700 per mille, the span target and the probe thresholds in Q16.16, the hall distance test as a squared distance, the walkable targets as integer percentages of the hall area (55 to 90 of 1,849 tiles).
- It keeps a bit grid in step with the floor (`SetWall`, `SetOpen`, `SyncBits`) and uses it for the axis rays, the free runs, the 3 by 3 pillar fit and the side bands.
- A small saving that changes no result: `BuildAttempt` returns its measurements, so `GenerateFloorMap` no longer measures the finished floor a second time (it used to call `ValidateFloorMap`, which measured again).
- `src/dungeon/dungeon_probes.h` and `.c` keep only the float `Probe_CastRay`; the old probe lattice types and `ProbeField_Analyze` moved, as integer code, to `fg_probe.h`.
- `src/dungeon/dungeon_types.h`: `FloorMap` gained `openTiles`, `stageMicros[GEN_STAGE_COUNT]` and `genMicros`; `walkableFraction` stays for display. `GenStage` names the stages (`shell`, `protect`, `halls`, `partitions`, `pillars`, `pockets`, `measure`); `GenStageName(stage)` is in `dungeon_generator.h`.
- Speed, on the sandbox core (an x86-64 Xeon at 2.1 GHz, gcc `-O2`): 40,000 floors in 35.5 s against 110 s for the old generator, about 0.9 ms per floor against 2.75 ms (including printing). Not measured on the phone or in the WASM build.

## Tests

All five are inside a `-D` guard so mray's build ignores them, are picked up by `tools/run_tests.sh` from `tests/*_headless.c`, and break each rule on purpose in a copy to prove the check would notice.

### `floorgen_rng_headless.c`

The vectors above; the threshold equals 2^32 mod n for every n tried, and the 64-bit form differs for 7, 45, 81, 100 and 129; ranges stay inside, are uniform within 2 percent (including a number whose range uses the rejection branch); streams are distinct over 48 keys and the first-output property of raw PCG32. Broken on purpose: no rotation, a wrong multiplier, the 64-bit threshold, streams that ignore the attempt or the stage.

### `floorgen_bits_headless.c`

Every `FgRow` and `FgBitGrid` operation against a bit-by-bit or tile-by-tile reference: shifts by 0 to 200 across the word boundary, all masks, runs, 3 by 3 fits and side bands (fast and plain) on nine sizes and four densities, 20,000 random sets and clears, and the probe rays: every one of the 16 rays is the exact walk along its integer direction (0 of 23,232 differ), and the 12 non-diagonal ones agree with the old float directions to 0.01 tile (17,424 of 17,424). The four diagonal rays pass exactly through tile corners, where which neighbour is tested first is a tie: the integer walk always tests z first, the old float code settled it by rounding, which differed at 225 degrees (668 of 1,444 rays there were more than a tile off against the old code, none against exact arithmetic). Broken on purpose: a shift without the carry between words, a mask one bit too long, a west run off by one, a 3 by 3 fit with no edge test, a side band that keeps the centre, a set that only updates the rows.

### `floorgen_hash_headless.c`

40 golden floors (4 tower seeds by floors 1 to 10) with their hash, attempts, partitions, pillars and route; the same floor after other floors; the hash sees a changed type, variant, flag, floor number, seed or count and does not see the discovered bit, timings or display fraction; the report adds up (the stage times never exceed the total, `openTiles` is the open count); and 500 floors inside the envelope of the old generator (route within 2.5 of 59.5, open space within 0.006 of 0.838, partitions within 1.0 of 31.3, pillars within 1.8 of 29.2, 58 to 72 percent on the first attempt, at most 1 floor in 100 on the fallback). Broken on purpose: a hash that forgets the flags no longer matches. With `dump` as the first argument it prints the 40 golden lines, for comparing builds.

### `floorgen_plain_headless.c`

The same file built with `-DFG_PLAIN_SCANS`: the generator with every scan done tile by tile must give the same 40 goldens. The goldens were taken that way first, so this is the oracle for the bit scans.

### `floorgen_integer_headless.c`

Reads the seven layout source files and fails on a `float` or `double`, a libm function, `math.h` or a float literal in code; comments are not code, and one marker is allowed per file in a line's comment. Exactly one such marked line may exist (the display fraction). The detector is tried on ten lines that must and must not be caught.

## Verified so far

- The existing `dungeon_headless` test passes on the new generator, and its statistics match: route to the stairs 59.7 (old 60.1), open space 84%, partitions 31.1 (31.2), pillars 29.4 (29.3).
- 5,000 floors per generator: the means of route length, open space, partitions and pillars are equal to within a few percent, and a two-sample Kolmogorov-Smirnov distance of 0.007 to 0.023 on each of five measures, against the 0.027 that separates two samples of 5,000 at the 5 percent level, so the distributions are not told apart. 40,000 floors per generator: fallback layouts 7 (new) against 4 (old), first attempt 65.5 against 65.9 percent.
- The 40 golden lines are identical in eight native builds (`-O0`, `-O2`, `-O3`, plain scans at `-O0` and `-O2`, `-Os -ffast-math`, `-march=native -ffp-contract=fast`, and `-O1` under the address and undefined behaviour sanitizers with no report) and in a **WebAssembly build** (wasm32-wasi, made with Zig's C compiler and run under wasmtime): byte for byte the same output as native x86-64. The generator also compiles for aarch64, 32-bit ARM, riscv64 and Windows (Zig cross compile; compile only, not run).
- The whole of `tools/run_tests.sh` was started on the final code but **not waited for**: when it was packed, 41 lines had passed and **one existing test failed, `tests/weapons_headless.c`**. It was not investigated (see the handover file for the recorded lines). Everything else printed up to that point passed.

## Not verified

- Nothing was run on the phone, in the browser build, or on a 32-bit or ARM machine (the 32-bit x86 build could not run in the sandbox). The WASM check used WASI, not Emscripten.
- The per-stage timings (`stageMicros`) have not been looked at yet: the benchmark harness is not written. The pocket pass still runs an O(n squared) search per carve, which will not scale to 128 by 128; the timings will say how much it costs now.
- Mid-math's `Pcg32` itself was not run; its algorithm was read and re-implemented twice.

## Findings about mid-engine (not changed there)

1. `Pcg32::range_u32` takes its rejection threshold from a 64-bit negation (`2^64 mod range`) where Lemire's method needs `2^32 mod range`. They are equal for powers of two and differ for 7, 45, 81, 100, 129 and others. The bias is negligible, but outputs can differ in the rare rejection branch from an exact implementation.
2. Streams of one seed with different `seq` start with the **same first output** with mid-math's seeding (one step after adding the seed); the documentation says they are independent. Seeding as the PCG reference code does (two steps), or discarding another output, avoids it.

## Fixes and problems found on the way

- The integer-only test caught two things in the new generator at once: a `0.0f` literal (now an integer zero assigned to the float fields) and a comment that contained the allow marker (it counted as a second marked line).
- A first version of the stream check expected two stream numbers of one seed to start differently; they do not (finding 2). The check now documents the property and tests `FgStream` instead.
- The first comparison of the 16 rays against the old float walk reported 2.9 percent of rays a tile or more off. All of it was the 225 degree diagonal tie described under `floorgen_bits_headless.c`; the integer walk is exact. The test now compares against exact arithmetic and, separately, the non-diagonal rays against the old directions.
- `printf` with several `Next()` calls in one argument list printed the values in reverse order (argument evaluation order): rule 3 above.

## What is left of R0

0. **`tests/weapons_headless.c` fails with the new generator.** Cause not found. Every floor is different now, so a check that depends on a particular seed's layout is the first suspect; confirm before changing the test or the generator.
1. **The accessor layer and the call-site migration** (`design/08`, section 6.3): `Floor_Solid`, `Floor_WallClass`, `Floor_WallTopY`, `Floor_CeilY` (and the dimensions) as `static inline` wrappers, then the 11 source files outside the generator that read `grid[]`, `Tile_IsSolid` or `WALL_HEIGHT` moved onto them (renderer, spider, swarm, pickups, floor_game, enemy_manager, raycast, controller, lighting, and the 22 `WALL_HEIGHT` uses in 7 files). The counts are in `design/09`, section 1.2.
2. **The benchmark harness** `tools/bench_floorgen.c` (guarded by a `-D` define, like `tools/bench/`): generate N floors and print the per-stage microseconds and the attempts, with the compiler, architecture and optimisation banner, so the phone can be measured in Termux.
3. **The generation line in the HUD** (`main.c`): append the total microseconds to the existing generation report line. `main.c` is edited by several chats, so rebuild it from the latest main and change that one line only.
4. **A WebAssembly check in the repository** (`tests/floorgen_wasm_check.sh`): build `tests/floorgen_hash_headless.c` for wasm and compare its `dump` output with the native one; exit 77 (SKIP) when there is no toolchain. The way it was done in the sandbox is in the handover file (Zig and wasmtime from PyPI); with Emscripten it is `emcc ... -o x.js` and `node x.js dump`.

Then R1 (bigger floors) can start. The fixes it needs first are listed in `design/08`, section 13.4.
