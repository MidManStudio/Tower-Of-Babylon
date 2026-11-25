# Testing

Nothing here needs a window, a GPU or a phone. `bash tools/run_tests.sh` (from the project root) builds and runs everything below and says which passed, failed or were skipped; it needs `gcc`, and optionally `node` 18+ and `python3` with `numpy`.

## The C tests (`tests/*_headless.c`)

Each file holds its own `main()` inside a macro (`-DNAME_HEADLESS_TEST`) so mray's build, which compiles every `.c` file, does not pick up a second `main`. The exact build line is in each file's header comment; `tools/run_tests.sh` uses those lines. Run from the project root.

| Test | What it covers | Doc |
|---|---|---|
| `server_smoke.sh` (not a C test) | the server and two bots built (through mid-net's driver: needs cargo and a mid-engine checkout, else SKIP) and run as separate programs over real UDP with 10% of every packet lost on the way out, plus a bot that lists, finds and joins a second, announcing server with no address given (about 15 s) | `net.md` |
| `android_script_check.sh` (not a C test) | the logic of `scripts/build_android.sh` (stages midnet, compile and link) run in a temporary project against a FAKE NDK clang, llvm-nm, llvm-readelf, cargo, rustc and rustup: the cargo command line and environment, where mid-engine comes from (a path, or cloned, with a branch), the pinned lock file put in and taken away, the Rust target added from inside the mid-engine folder, `-DTOWER_MIDNET` in every compile, the library in the link line, the fallback to an APK without QUIC for each way it can fail (cargo, a missing function, a link error, an extra library) and `TOWER_MIDNET_REQUIRED=1` stopping instead; stale libraries; `bash -n`, shellcheck if installed. 53 checks, about 2 s, including `llvm-nm` exiting 1 on a library from a newer rustc and a stage that dies of an unexpected error. It builds nothing for ARM | `android.md` |
| `platform_check.sh` (not a C test) | main.c and every source compiled as web, Android and desktop, and `platform.h` checked against the table in `platforms.md` | `platforms.md` |
| `input_headless.c` | the touch controls (the quick floating stick, sprint at the rim in any direction, the fixed fire pad as a drag (the default) and as a joystick, roles, buttons including RELOAD and DIVE, locked and walk-only) and the HUD layout (nothing overlaps, targets are thumb-sized) | `input.md` |
| `audio_headless.c` | synthesiser numbers, limiter, voices, pan and distance, footsteps, every shipped sound recipe, the bank and the config registry | `audio.md` |
| `dungeon_headless.c` | generator, floor window, probes | `dungeon.md` |
| `floor_access_headless.c` | the accessor layer (`floor_access.h`) against an oracle on 40 floors and tiles outside them, each rule broken on purpose; and the guard that keeps the moved source files off `grid[]`, `TILE_WALL`, `Tile_IsSolid` and `WALL_HEIGHT` (reads files; run from the project root) | `dungeon.md` |
| `stairs_headless.c` | staircase geometry and collision | `dungeon.md` |
| `renderer_headless.c` | per-tile quads, culling, dark quads, the flashlight's reach | `dungeon.md`, `flashlight.md` |
| `lighting_headless.c` | ceiling lights, light map, line of sight | `lighting.md` |
| `highp_headless.c` | the shaders' text and the beam calls | `lighting.md`, `flashlight.md` |
| `health_headless.c` | health and damage | `player.md` |
| `movement_headless.c` | velocity, omni sprint, forward-only slide, stamina, noise, aiming down sights, automatic corner leaning, sprinting beside a wall (the lean stays), the strafe amount, dive, roll and lying (stays down until the stick is moved; the camera never below the floor), the stim shot | `player.md` |
| `enemies_headless.c`, `spider_headless.c` | enemy AI and spiders | `enemies.md` |
| `weapons_headless.c` | arsenal, fire modes, magazines and reloading, melee durability, aiming-down-sights numbers and pose (and the gun's strafe tilt, and shots from a seed), pickups, models, shots at real enemies | `weapons.md` |
| `projectiles_headless.c` | patterns, falloff, swept hits, piercing, tracers, `Gunfire_Shoot` | `projectiles.md` |
| `inventory_headless.c` | items (the stim shot included), stacks, the inventory and its touch screen | `inventory.md` |
| `sim_headless.c` | the input latch (dive included), the same steps at any frame rate, look per frame against movement per step, the monsters' blend hooks | `sim.md` |
| `game_sim_headless.c` | the simulation with no window: the event queue, the rules reported as events (flashlight, shots, hits, medkit, pickups, the stair clock, doors, death), the same world at any frame rate and on every run, and a scan that it reaches for no window, clock, sound or screen | `sim.md` |
| `floor_run_headless.c` | the timers, the fixed step, the unlock console, the countdown, the collapse, the events and floor.mdix | `floor_run.md` |
| `floor_game_headless.c` | the stair clock in the game: where the console goes, the door lock, the monster calls, the kill, frame rate independence | `floor_run.md` |
| `net_rel_headless.c` | the reliable-message layer over a path that loses, doubles and reorders: order, once each, wraparound, window, round trip, giving up | `net.md` |
| `server_smoke_quic.sh` (not a C test) | the same server and bots over mid-net's QUIC, with a bot that lists and finds the server; skipped (exit 77) without cargo and a mid-engine checkout (about 17 s once built) | `net.md` |
| `net_discovery_headless.c` | finding a game on the network: strict packets, the table of lobbies, address helpers, a host and a searcher over real UDP on the loopback, strangers' packets and a flood (about a second) | `net.md` |
| `net_udp_headless.c` | the real UDP transport on this machine's loopback, in real time (about 15 s): handshake, play, 15% loss, a full server, strangers' packets | `net.md` |
| `net_headless.c` | the multiplayer netcode: wire, protocol (and fuzz), pretend network, handshake, prediction and reconciliation over delay and loss, commands not to be trusted, other players, the clock lead | `net.md` |
| `swarm_headless.c` | the worm swarm: its numbers, spawning, waking, crawling round walls, bodies (links, walls, floor, stairs, player), the health box, melee, the bite, determinism, cost | `swarm.md` |
| `characters_headless.c` | the roster file and its loader, the choice kept as an id, the selection screen's taps, keys, BACK, reopening and layout, the word wrap, and that every shipped text fits its room | `characters.md` |
| `lobby_headless.c` | the lobby: the logic and layout of both screens, the keyboard and taps, the search lifecycle and the list of games (with scrolling) against a scripted backend, and the real backend over the LAN discovery with a host answering on this machine | `lobby.md` |
| `net_session_headless.c` | real hosted and joined games over real UDP on this machine: hosting, joining, the lists of players, a full game, leaving, the host leaving, a join nobody answers, two hosts on one machine; built plain it checks that a build without the driver library says so (18 checks); `net_session_check.sh` runs it with the library (55 checks); about 5 seconds in real time | `net.md` |
| `net_session_check.sh` (not a C test) | `net_session_headless.c` built against the Rust library under the sanitizers (real games through the driver, a link that goes quiet, no leaks), and the discovery and lobby tests with the driver flag; SKIP without cargo and a mid-engine checkout | `net.md` |
| `net_play_headless.c` | walking in a networked game: the buttons and the command, the look turned at once, the camera between ticks, and two players on a loopback link walking, turning, sprinting, diving and seeing each other through the real input path | `net.md` |
| `net_shot_headless.c` | the foundations for shooting in a networked game: the shot seed, the server's pose history and its rewind, the view lag a client puts on a command, and on a loopback link that the rewound pose is the pose the shooter's screen showed | `net_shots.md` |
| `obstacles_headless.c` | the props' suspend switch (a networked game has no props): collision and drawing off and back on | `net.md` |
| `floorgen_rng_headless.c` | the floor generator's random numbers: PCG32 vectors, SplitMix64, the 32-bit range threshold, stream keys, and each rule broken on purpose | `floorgen.md` |
| `floorgen_bits_headless.c` | the bit rows and the integer probe rays against plain loops and exact arithmetic, and each rule broken on purpose | `floorgen.md` |
| `floorgen_hash_headless.c` | 40 golden floors (hash, attempts, counts, route), the same floor after others, what the hash sees, the generation report, and the envelope of the old generator over 500 floors | `floorgen.md` |
| `floorgen_plain_headless.c` | the same golden floors with the generator built with plain scans (the oracle for the bit scans) | `floorgen.md` |
| `floorgen_integer_headless.c` | the layout source files contain no float, double, libm or float literal (reads files; run from the project root) | `floorgen.md` |
| `floorgen_wasm_check.sh` (not a C test) | the 40 golden floors built natively and as WebAssembly (Emscripten with node, or Zig with wasmtime, the Zig build with the web build's 64 KB stack) and compared byte for byte; SKIP (exit 77) with no wasm toolchain | `floorgen.md` |
| `config_headless.c` | the config layer, the baked tables, bad documents | `config.md` |
| `flashlight_headless.c` | charge, batteries, flicker, the beam's shape | `flashlight.md` |

`tools/bench_floorgen.c` is not a test and `run_tests.sh` does not run it: it measures the floor generator and is built by hand, on the phone as well (`floorgen.md`).

## The Node tests

- `node tests/mdix_bridge_test.mjs`: the JavaScript in `mdix_web.c` against the real DixScript wasm (`config.md`).
- `node tools/mdix_bake.mjs --check`: the `.mdix` files are valid and `src/config/config_baked.c` is up to date. Run `node tools/mdix_bake.mjs` after editing any `.mdix` file.

## The shader test (Python)

`python3 tests/shader_gles_test.py` compiles the high precision shaders with a real GLSL ES 1.00 compiler and renders with them, using Mesa's software OpenGL ES through EGL (the `libEGL` and `libGLESv2` libraries Mesa installs; no GPU and no window). It skips, and says so, when those are not there. What it can and cannot tell you is in `flashlight.md`.

## Tests that follow a seed

Every floor is built from a seed, and since round R0 (`floorgen.md`) the same seed gives a different floor than it did before. A check that held on 25 or 40 seeds of the old generator can therefore fail on the new one without anything being wrong. When that happens, find out which of the two it is before touching either: sweep a few thousand seeds with the old generator (a `git worktree` at the commit before) and the new one, and compare how often the condition holds. If the rates match, the test was asking for something the code never promised, and the test is what changes.

The one case so far: `weapons_headless.c`, "pickups ... never within 10 steps of walking of where the player arrives". It used a straight-line distance; the rule is walking distance (`weapons.md`). It now floods the floor itself and checks the walking distance.

## What no test here can tell you

How anything looks and feels on the phone, how fast it runs there, how the Mali's shader compiler treats the shaders, and whether the WebAssembly build links. After each delivery the first run on the phone is the real test: read the diagnostics overlay (tap the FPS counter) for the `config:` and `flashlight:` lines.

## `tools/run_tests.sh`

See the top of the file. It runs from the project root by default (or from the folder given as the first argument); `.github/workflows/tests.yml` runs it on GitHub (see `docs/repo.md`). It prints one line per test, `PASS`, `FAIL` or `SKIP`, and exits non-zero if anything failed.
