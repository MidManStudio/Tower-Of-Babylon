# Flashlight and batteries

## Overview

A flashlight lights the dark. The lighting system (`lighting.md`) leaves whole patches of every floor black, which is the point, and the flashlight is the player's answer to them: a warm cone of light that follows where the camera looks, on a charge that runs out. It is found on floor 1 (and on any floor, for a player who has none) and fed with batteries found on every floor. When the charge is low the light starts to fail: it cuts out and comes back, more and more often, and is dimmer between the cuts, until it dies and stays dead until a battery goes in.

It has four parts: the rules (`src/flashlight/`), the beam (a second shader in `src/ui/highp_shader.c`), the renderer change that keeps dark surfaces the beam can reach (`src/dungeon/dungeon_renderer.c`), and the game wiring (items, pickups, the LIGHT button, `main.c`). Its numbers are in `assets/config/flashlight.mdix` (see `config.md`).

## Using it

- Pick up a flashlight (USE next to it, like a weapon). A found one holds 80 s of charge, 40% of its 200 s capacity.
- A **LIGHT** button appears at the right of the screen once one is carried (450, 150), with a charge bar along its bottom edge. Tap it to switch the light on and off. `L` on a keyboard, or USE on the flashlight in the inventory, do the same.
- **Batteries** are picked up like medkits (USE) and stack to 6. Using one from the inventory loads it: it adds 100 s up to the 200 s capacity, and is refused ("FLASHLIGHT IS FULL") while the light holds more than 150 s, so a battery is never half wasted.
- When the light is flat, tapping LIGHT takes a spare battery from the inventory (loading it into a flat light also switches it on); with none it says "FLASHLIGHT IS FLAT. NO BATTERY".
- Below 20% of capacity (60 s) the light fails, the LIGHT button blinks, and a toast says so once. At zero it goes off ("FLASHLIGHT DIED") and the toast tells whether there is a battery to use.
- Dropping the flashlight keeps its charge: the dropped pickup carries the seconds left, and whoever picks it up gets that much. A player who dropped theirs and left finds a new one on a later floor (below).
- The diagnostics overlay (tap the FPS counter) has a `flashlight:` line: on or off, seconds left, this frame's level, and whether the beam is done by the shader or by the glow fallback.

## Modules

### `flashlight.h` and `flashlight.c`

The rules. No drawing and no inventory; the game owns the item.

- **Charge.** `charge` is seconds of light left. It drains one second per second while on, and only then. At zero the light switches itself off (`died` is set for that one frame so the game can say so once) and `Flashlight_Toggle` will not switch it on again.
- **Batteries.** `Flashlight_AddBattery` adds `battery_secs` (clamped to the capacity), refuses (returns 0) while the charge is above `capacity - battery_secs / 2`, and switches a flat light on.
- **Failing.** `FlickerLevel` gives this frame's brightness, 0 to 1. At or above `low_fraction` of capacity it is steady 1. Below it, `s` runs from 0 (at the low mark) to 1 (empty): the light is cut into cells of 1/(9 + 11 s) second; each cell is a dropout (brightness below 0.22, nearly dark) with probability 0.08 + 0.72 s, and otherwise lit at 1 - 0.45 s. So the dropouts get commoner and quicker and the lit stretches dimmer as the charge falls. The pattern is a hash of the cell number, so it is the same every time and needs no random state. Measured by the test over the 60 s of the failing zone in four windows: dark 16%, 34%, 51%, then 70% of the time.
- **The beam follows the camera, a little behind.** `dir` moves toward the camera's forward direction by `lag * dt` of the way each frame (14 a second: a sudden quarter turn moves it about a fifth of the way in one frame and it has caught up in about a third of a second), kept unit length.
- `Flashlight_BeamAt(f, eye, point)` is the beam's strength at a point, 0 to `strength`: `smoothstep((cos angle - cos outer) / (cos inner - cos outer)) * (1 - distance / range)^2 * strength * level`. It is the same arithmetic as the shader, in C, so a test can compare a real render against it.
- `Flashlight_GlowAt` is the fallback when the shader is not in use: up to 0.6 within 6 m, stronger where the player looks and never fully dark behind.

### The beam shader (`src/ui/highp_shader.c`)

A second shader program, `HIGHP_FLASH_VS_ES2` and `HIGHP_FLASH_FS_ES2`, kept apart from the plain high precision pair so that if it ever fails to compile on some device the plain one carries on and the glitch fix (`phone-rendering.md`) stays. The vertex shader is the same highp one plus a varying that passes the vertex position on (raylib's batches hold positions already in the world, so `vertexPosition` is a world position). The fragment shader takes `flashPos`, `flashDir`, `flashParams` (cos inner, cos outer, 1 / range, strength) and `flashTint`, computes the beam as above, and adds it to the colour: `base + beam * (0.85 * tint * (1 - 0.55 * peak) + 0.35 * base)` where `peak` is the brightest channel already there. On a surface the lighting left black that is just `beam * 0.85 * tint`; on a lit one it adds less, so a lit wall is lifted, not bleached to a flat disc (the first formula did bleach, which the picture of the real dungeon showed). A strength of 0 skips the beam entirely.

`Highp_SetFlashlight(pos, dir, cosInner, cosOuter, range, strength, tint)` turns the beam on for everything drawn after it and `Highp_FlashlightOff()` ends it. Both **flush the batch first**: raylib sets a uniform at once but draws a queued batch later, so without the flush the geometry queued before the call would be drawn with the new beam. `Highp_Begin` switches to the beam shader if it compiled and starts every frame with the beam off. The world, enemies, pickups and stairs are drawn with the beam on; it is turned off before the tracers, the first person hands and the 2D screen. If the beam shader is not there (or the whole highp shader is switched off by tapping the diagnostics line) `Highp_FlashlightAvailable()` is false and the game uses the glow fallback.

### Renderer (`dungeon_renderer.c`)

`DrawFloorMapFlashlight(light, eye, glowFallback)` is called before drawing each frame. A quad the lighting left black (all corners under 3/255) used to be skipped, which is most of the dark patches, and a surface that is not drawn cannot be lit. With the flashlight on, a black quad the beam can reach is drawn anyway: it is tested as a sphere around the quad against the cone, widened by the angle the sphere covers from the light, and the range. Quads the beam cannot reach are still skipped. With `glowFallback`, the corners within 6 m of the player get the glow added to their light value instead, so a phone without the beam shader still gets something.

### Items, pickups and wiring

- Items: `ITEM_FLASHLIGHT` (a new kind, `KIND_TOOL`: one to a slot, USE button, no equip) and `ITEM_BATTERY` (a consumable, stack 6). Icons in `inventory_ui.c`.
- Pickups: `PICKUP_FLASHLIGHT` (its count is the seconds of charge it holds) and `PICKUP_BATTERY`, with models. Floor 1 holds a flashlight and two batteries. Later floors hold one battery, and four times in ten a second, plus a flashlight if the player has none (so it is never lost for good).
- `main.c`: the LIGHT button (`lightButtonRec`) and the `L` key call `ToggleTorch`; USE on the item does too; `LoadBattery` is shared by the battery's USE and a flat light's button. `Flashlight_Update` runs once a frame after the camera. The beam starts a little to the right of and below the eye, along the lagged direction; the LIGHT button, its charge bar and the failing blink are drawn with the other buttons. Respawning switches the light off but keeps its charge.

## Tests

### `tests/flashlight_headless.c`

Build line in the file. The rules are tested against a fixed set of numbers set in the test (capacity 300, a found one 120, a battery 150, low mark 20%, beam 14 to 30 degrees over 20 m), not the ones in `flashlight.mdix`, so tuning the file never breaks them; `tests/config_headless.c` checks what the file says. It covers switching and charge (no charge, no light; a found one holds 120 s; clamped to capacity; ten seconds on costs ten; off costs nothing); batteries (adds 150, refused at 250 s, clamped to 300, brings a flat light back on); dying (goes off at zero and reports it on exactly one frame); failing (steady above the mark, dropouts in each 14 s window of the failing zone increasing 133, 283, 430, 588 frames of 841, mostly lit just below the mark and dark more than half the time at the end, dimmer between cuts, the same pattern for the same clock, many switches off and on in the last 10 s); the beam following the camera (starts aligned, a quarter turn moves about 21% in a frame, caught up in half a second, looking straight up works, a zero direction changes nothing); the beam's shape (0.45 at 5 m straight ahead, fading with distance, gone at 20 m, full inside 14 degrees and nothing past 30, nothing behind, up equals sideways, half brightness gives half the beam, off gives none); the glow.

### `tests/shader_gles_test.py`

See `testing.md`. It compiles all four shaders with Mesa's GLSL ES compiler, checks the attribute and uniform names raylib and `highp_shader.c` look for, renders a floor and a wall, and compares the pixels with the beam maths written again in Python: the pool of light matches to within 0.5/255 on black surfaces and on lit ones, nothing outside the cone is lit, the light is warm, strength scales it, and with strength 0 the beam shader draws exactly the same pixels as the plain one. It also proves it can fail (a deliberately broken shader is refused).

### The other tests that cover it

- `tests/highp_headless.c`: the beam shaders' text (highp vertex stage, the fallbacks, the varying, the four uniforms, everything raylib's batches need, that it is a second program) and that the beam calls do nothing at all without a shader.
- `tests/renderer_headless.c`, "The flashlight": over 60 view points and directions on a real generated floor, every black quad whose centre is inside the beam is drawn, no extra quads are drawn beyond those within reach of the cone, a light that is off or in a dropout draws exactly what no flashlight draws, and the glow fallback makes nearby surfaces lighter and none darker. With `FLASH_DUMP=/tmp/fl` it also writes the quads of the view where the flashlight adds the most.
- `tests/weapons_headless.c` and `tests/inventory_headless.c`: the new pickups (floor 1's eight, a flashlight on every floor for a player without one and none for a player with one, two batteries on floor 1, a flashlight holds `start_secs`, models draw, labels) and items (kinds, stacks, usable and droppable, not equippable).
- `tests/config_headless.c`: the flashlight table reads back as written, and four bad documents and a missing value are refused and named.

### `tools/render_dump_gles.py`

The picture of the real thing. It takes the quads `renderer_headless` wrote (the dungeon as the renderer draws it, with and without the flashlight), and draws them through the actual shaders of `highp_shader.c` with Mesa's software OpenGL ES at the phone's size (800 by 450, 60 degree field of view, near 0.25, far 60, depth test and culling on): the plain shader without the flashlight, the beam shader with it. `python3 tools/render_dump_gles.py /tmp/fl /tmp/flashlight.png`. It shows what the beam does to a hallway the lighting left dark: the floor ahead gets a warm pool and the wall beside it, which was black and not even drawn, is lit.

## Fixes and Problems

- **The first beam bleached lit walls.** `base + beam * (0.85 tint + 0.5 base)` at full strength turned a lit wall into a flat pale disc; found by drawing the real dungeon, not by a number. Now it adds less to what is already bright and strength is 0.8.
- **A surface that is not drawn cannot be lit.** The renderer's dark-quad skip would have left the beam nothing to land on in exactly the places it is wanted. Found while designing it; fixed as above and covered by the 60-view-point test.
- **The beam is a second shader** so that a compile failure on some phone cannot take away the glitch fix.
- The picture tool first showed a close-up of a lit wall; the dump now picks a view with at least 9 m of room ahead.

## Tuning

`flashlight.mdix`: `capacity_secs` 200, `start_secs` 80, `battery_secs` 100, `low_fraction` 0.20, `beam_inner_deg` 14, `beam_outer_deg` 30, `range` 20, `strength` 0.8, `lag` 14, `tint` 0xFFE9B0. The failing pattern (cell rate 9 + 11 s, dropout chance 0.08 + 0.72 s, dimming 0.45 s) and the glow (6 m, 0.6) are constants in `flashlight.c`. The beam's origin offset (0.10 ahead, 0.12 right, 0.08 below the eye) is in `main.c`. The shader's mix (0.85, 0.55, 0.35) is in `highp_shader.c` and must be changed with the same numbers in `tests/shader_gles_test.py`. Battery supply (how many per floor) is in `Pickups_SpawnForFloor`.

**Tuning history.** The first values (300 / 120 / 150) made a battery last too long in play (the user's report after the first phone test). Now 200 s capacity, 80 s for a found flashlight, 100 s per battery: the same shape (a battery is half the capacity, a found one is 40%), one third shorter. The failing zone stays 20% of the capacity, so it is now 40 s instead of 60 s. To tune further, change the three numbers in `assets/config/flashlight.mdix` and run `node tools/mdix_bake.mjs`; the Android build bakes by itself.

## Known limits

- **Not seen on a phone.** The shader compiles and matches the maths under Mesa's GLSL ES compiler, which is not the Mali's compiler and is more forgiving, and the picture of the dungeon is Mesa's too. If the beam shader fails on the phone the plain shader is used and the diagnostics line says "glow (no beam shader)". Whether it costs frames on the phone is not measured: it is a handful of operations per pixel on an 800 by 450 canvas, and a few dozen extra quads where the beam reaches.
- There are no normals, so the beam lights a surface the same however it is turned to the light, and there are no shadows: a surface in the cone behind a corner is lit if it is drawn at all.
- Enemies and pickups are lit by the beam (they are drawn with the shader) but not by the light map. The first person hands and the tracers are not lit by it.
- Enemies do not react to the flashlight yet.
- The flashlight has no hand model; the light just starts a little below and to the right of the eye.
