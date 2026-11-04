# Testing

Nothing here needs a window, a GPU or a phone. `bash tools/run_tests.sh` (from the project root) builds and runs everything below and says which passed, failed or were skipped; it needs `gcc`, and optionally `node` 18+ and `python3` with `numpy`.

## The C tests (`tests/*_headless.c`)

Each file holds its own `main()` inside a macro (`-DNAME_HEADLESS_TEST`) so mray's build, which compiles every `.c` file, does not pick up a second `main`. The exact build line is in each file's header comment; `tools/run_tests.sh` uses those lines. Run from the project root.

| Test | What it covers | Doc |
|---|---|---|
| `platform_check.sh` (not a C test) | main.c and every source compiled as web, Android and desktop, and `platform.h` checked against the table in `platforms.md` | `platforms.md` |
| `input_headless.c` | the touch controls (floating stick, sprint at the rim, the fire pad that aims, roles, buttons, locked and walk-only) and the HUD layout (nothing overlaps, targets are thumb-sized) | `input.md` |
| `audio_headless.c` | synthesiser numbers, limiter, voices, pan and distance, footsteps, every shipped sound recipe, the bank and the config registry | `audio.md` |
| `dungeon_headless.c` | generator, floor window, probes | `dungeon.md` |
| `stairs_headless.c` | staircase geometry and collision | `dungeon.md` |
| `renderer_headless.c` | per-tile quads, culling, dark quads, the flashlight's reach | `dungeon.md`, `flashlight.md` |
| `lighting_headless.c` | ceiling lights, light map, line of sight | `lighting.md` |
| `highp_headless.c` | the shaders' text and the beam calls | `lighting.md`, `flashlight.md` |
| `health_headless.c` | health and damage | `player.md` |
| `movement_headless.c` | velocity, sprint, stamina, noise, sliding | `player.md` |
| `enemies_headless.c`, `spider_headless.c` | enemy AI and spiders | `enemies.md` |
| `weapons_headless.c` | arsenal, fire modes, pickups, models, shots at real enemies | `weapons.md` |
| `projectiles_headless.c` | patterns, falloff, swept hits, piercing, tracers, `Gunfire_Shoot` | `projectiles.md` |
| `inventory_headless.c` | items, stacks, the inventory and its touch screen | `inventory.md` |
| `config_headless.c` | the config layer, the baked tables, bad documents | `config.md` |
| `flashlight_headless.c` | charge, batteries, flicker, the beam's shape | `flashlight.md` |

## The Node tests

- `node tests/mdix_bridge_test.mjs`: the JavaScript in `mdix_web.c` against the real DixScript wasm (`config.md`).
- `node tools/mdix_bake.mjs --check`: the `.mdix` files are valid and `src/config/config_baked.c` is up to date. Run `node tools/mdix_bake.mjs` after editing any `.mdix` file.

## The shader test (Python)

`python3 tests/shader_gles_test.py` compiles the high precision shaders with a real GLSL ES 1.00 compiler and renders with them, using Mesa's software OpenGL ES through EGL (the `libEGL` and `libGLESv2` libraries Mesa installs; no GPU and no window). It skips, and says so, when those are not there. What it can and cannot tell you is in `flashlight.md`.

## What no test here can tell you

How anything looks and feels on the phone, how fast it runs there, how the Mali's shader compiler treats the shaders, and whether the WebAssembly build links. After each delivery the first run on the phone is the real test: read the diagnostics overlay (tap the FPS counter) for the `config:` and `flashlight:` lines.

## `tools/run_tests.sh`

See the top of the file. It runs from the project root by default (or from the folder given as the first argument); `.github/workflows/tests.yml` runs it on GitHub (see `docs/repo.md`). It prints one line per test, `PASS`, `FAIL` or `SKIP`, and exits non-zero if anything failed.
