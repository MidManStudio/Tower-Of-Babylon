# Lighting: ceiling lights, dark patches and the depth range

## Overview

Every floor has a lattice of ceiling lights, one every 6 m. Each is working, dim, flickering or dead, and the moods come in patches, so a stretch of corridor can be dead and black while the next is lit and glaring. This is the look of the Backrooms reference screenshots: rows of fluorescent panels in a flat ceiling, some lit, some out, warm light on yellowed walls, black where the lights have died.

The light is worked out per tile, not per pixel, and read at tile corners by the renderer (see `dungeon.md`). It costs about 0.03 ms a frame on a desktop CPU, so it is a small part of the frame on the phone too.

## Modules (all in `src/lighting/`, plus `src/ui/view_clip.*`)

### `lighting.h` and `lighting.c`

**Placement.** `Lighting_BuildForFloor(floor, number)` puts a fixture on the lattice points (tile 2 + 3k in each direction). A lattice point that is in a wall moves to an open neighbour or is left out. There are none over the stair alcoves, which have no ceiling. Instead each alcove has one steady lamp (no panel) so the alcove and its doors are always lit. All fixtures within 8 steps of walking (Manhattan distance) of either stair approach are forced ON, so arriving on a floor is never blind.

**Mood.** Each fixture gets a darkness value from smooth value noise over a coarse lattice (patches about 12 tiles, 24 m, across), stretched to use the full range, plus a little of its own random roll. Dark values map to a mode: lit patches are mostly ON with the odd flicker or dim; middling patches are a mix of dim, flickering and some dead; dark patches are nearly all dead. Over 20 generated floors the split comes out at about 46% on, 11% dim, 5% flickering and 38% dead, and neighbouring fixtures agree about being dead more than 75% of the time (both are checked by `lighting_headless.c`). The seed is the floor's seed combined with its number, so a floor always has the same lights. The thresholds are in `PickMode` and the patch size is `LIGHT_ZONE_TILES`.

**Animation.** `Lighting_Update(time)` sets each fixture's `level` and recomputes the light:

- ON: 1.0 with a faint fast hum (2.5%).
- DIM: about 0.3, wandering a little (0.2 to 0.4).
- FLICKER: about 0.92, but every 4 to 7 seconds it stutters for 0.5 to 1.3 s, on and off in 40 ms steps (0.04 or 0.95), then settles.
- DEAD: 0, except that every 23 to 40 s it twitches back to 0.55 for about a tenth of a second.

**Spreading the light.** Each fixture shines on the open tiles within 4 tiles (8 m) of it that it has a tile-grid line of sight to (Bresenham, and a diagonal squeeze between two wall tiles blocks it), so light never goes through a wall. The weight of a tile is a smooth falloff of its 3D distance to the lamp, taken to be `LIGHT_HEIGHT` (5 m) above the floor: `(1 - d^2/R^2)^2`, scaled so the tile under the lamp is 1. This is done once when the floor is built and stored as a list of (tile, weight) pairs per fixture, about 60 pairs each. Each frame `Lighting_Update` adds `weight * level` for every lit fixture into the tile sums, then maps each sum through `1 - exp(-2 * sum)`, so overlapping lights brighten smoothly and never blow out. A corner's light is the mean over the open tiles that touch it (a wall tile has none), so a wall's face is lit by the tiles on its own side only and light on one side of a wall is not on the other (tested).

**Reading.** `Lighting_Corner(i, j)` is for the renderer. `Lighting_Tile(x, z)` is per tile. `Lighting_AtPoint(p)` interpolates between corners; `main.c` uses it to dim the first person hands in dark places. `Lighting_Tint(light, &r, &g, &b)` gives the colour multiplier: faint blue-grey with no light (0.045, 0.05, 0.07), warm in full light (about 1.0, 0.95, 0.71). The ambient is low on purpose: in a dead patch a wall is a barely visible dark shape.

**Panels.** `Lighting_PanelOnTile(x, z)` returns the fixture whose panel hangs over a tile (or NULL), and `Lighting_PanelColour(fixture, &r, &g, &b)` the colour it glows right now: dead grey (24, 24, 22), a weak amber when dim, warm white (255, 246, 214) when lit, following the animated level so a flickering panel visibly flickers. The panel is 1.5 by 0.6 m with a 10 cm dark rim (`LIGHT_PANEL_LEN`, `LIGHT_PANEL_WID`, `LIGHT_FRAME_RIM`). The renderer draws it (see `dungeon.md`): **a light is part of the ceiling, not a second surface laid over it.** The ceiling tile that holds a panel is cut into nine rectangles that touch along their edges and never overlap (four of ceiling, four of frame, one panel), all at the ceiling's own height. The first version drew a dark frame 2 cm below the ceiling and a panel 4 cm below that, relying on the depth test to put the panel on top; on the phone the frame won most of the panel back, leaving a dark slab with slivers of white along an edge (see "Fixes and Problems"). A glowing panel stays at full brightness until 65% of the fade distance and then fades out, so a lit corridor can be seen from its far end in the dark.

**Test hooks.** `Lighting_SetMode(i, mode)` and `Lighting_SetAllModes(mode)` force fixtures into a mode. The game does not call them, but they are the quickest way to try a look by hand (for example, everything ON to see what the maps look like without the dark).

**Floors of any size (round R1a).** The arrays are `MAP_MAX` squared and the module keeps the size of the floor it was built for (`g_w`, `g_h`; nothing is lit before the first build). `LIGHT_MAX_FIXTURES` is 2,000 (a 128 by 128 floor has about 1,750 lattice points) and the influence pool `LIGHT_POOL_ENTRIES` 60,000 entries (a 128 floor uses about 48,000; 6 bytes each); the pool stores a tile as its offset in the arrays (row stride `MAP_MAX`, not the floor's width). The alcove rule, the approach points and the stair-pad lamps come from the floor's shell. `Lighting_Update(t)` still lights the whole floor (44 us at 45 by 45, 330 us at 128 by 128 on x86); `Lighting_UpdateAround(t, x, z)`, which the game calls, only does the tiles within `LIGHT_WINDOW_TILES` (24) of the viewer plus a one-tile margin (a corner reads the four tiles round it), about 50 us on every size, and gives exactly the whole-floor update's tile and corner light inside the window; outside it values are left as they were, which nothing reads, since every reader is drawing (the renderer, the swarm and player colour, the view model) within 18 tiles of the camera. On a floor that fits the window (45 by 45) it simply calls `Lighting_Update`. `lighting_headless.c` checks capacity, equality with the whole-floor update (five viewer places on 12 floors of 64, 100 and 128) and that far tiles are untouched.

### `view_clip.h` and `view_clip.c`

- `ViewClip_Apply(camera, near, far)` replaces the projection after `BeginMode3D` (the same `rlFrustum` method `main.c` used before), and remembers the field of view. `ViewClip_Reapply(near, far)` changes the range for the same camera.
- The world uses `WORLD_NEAR` 0.25 and `WORLD_FAR` 60. The first person hands use `HANDS_NEAR` 0.04 and `HANDS_FAR` 4. See "Phone glitches" in `dungeon.md` for why.
- `ViewClip_ClearDepth()` flushes the batch and clears only the depth buffer (`glClear(GL_DEPTH_BUFFER_BIT)`), web and Android only (both are OpenGL ES 2.0; a desktop build skips it). This is the one place the game calls OpenGL directly. It is what lets the hands be depth tested against themselves and not against the world.
- `ViewClip_QueryBuffers(&depthBits, &samples)` reads `GL_DEPTH_BITS` and `GL_SAMPLES`, web and Android only (-1 elsewhere). `main.c` logs them and shows them in the diagnostics.

The web-only branches could not be compiled here with Emscripten (it is not installed in the sandbox); they were syntax checked against stand-in headers. They are four short calls to standard GLES2 functions that raylib itself uses.

### `highp_shader.h` and `highp_shader.c`: the cause of the small-thing glitches

raylib's default shader for the web begins its **vertex** shader with `precision mediump float;` (`rlgl.h`, `rlLoadShaderDefault`: "Precision required for OpenGL ES2 (WebGL) (on some browsers)"). On a phone GPU that is real: mediump is 16 bit floating point, about three decimal digits. The vertex shader computes `gl_Position = mvp * vec4(vertexPosition, 1.0)` and the world is up to 90 m across, so a vertex at x = 80 m is stored to about 6 cm (at 40 m, 3 cm). A wall, 2 m wide and 6 m tall, survives that. Anything small does not: the black corner ribbons and the alcove tints of the first versions, the dark rim of a ceiling light (its black "lines" were rim quads 10 cm wide with corners rounded by 6 cm), and a finger or the end of a pipe in the first person hands (a shard-like crown of spikes). Walls were also the first to look wrong when anything thin was drawn on them. It never showed on desktop, which ignores the qualifier, and no desktop test can show it. TerminalViscosity's notes say the same of its fragment shaders ("a phone defaults to mediump ... the lighting math needs more").

The depth readout on the phone said 24 bits and 4x MSAA, so the buffer was never the problem; the vertex math was. Two earlier explanations (a 16-bit depth buffer, then two surfaces 2 cm apart fighting) were wrong or incomplete: both were symptoms of positions being rounded by centimetres.

The fix is raylib's own default shader text with the precision raised: `precision highp float;` in the vertex shader (32 bit, guaranteed on WebGL) and, where the device allows (`GL_FRAGMENT_PRECISION_HIGH`), in the fragment shader. Attribute and uniform names are unchanged, so raylib's batches use it as they use the default: `Highp_Init()` loads it once after the window exists, `Highp_Begin()` (right after `ClearBackground`) starts shader mode with it and `Highp_End()` (right before `EndDrawing`) ends it, so everything drawn in the frame, 3D and 2D, uses it. If it fails to compile, raylib's default stays in use and the console says so. Off the web nothing is loaded.

**Comparing.** The diagnostics line shows `shader highp` or `shader raylib default`. Tapping that line (while the diagnostics are on) switches between them at the next frame, so the same view can be photographed both ways. The default is highp.

**Cost.** None that could be measured on the desktop. Vertex math at 32 bit is slower than at 16 on a GPU in principle, but the frame has about 5000 vertices.

**Not changed.** The earlier rules stay: nothing is laid over another surface (the lights are part of the ceiling, the pickup details stand clear of what they sit on). They cost nothing and keep the picture steady on a coarse depth buffer, which a different phone might still have.

**On Android.** `Highp_Init` builds both programs when the platform has `TOWER_GLES2` (web and Android; see `platforms.md`). The phone's own driver (a Mali on the Galaxy A13) gets raylib's mediump vertex default as well, so the same fix applies; the beam shader is the same one the browser uses. If a program fails to compile on the device the log says so (`SHADER: ... FAILED` / `not available, the glow fallback is used`, read with `adb logcat -s raylib`) and the game carries on without it. This branch is only syntax-checked in the sandbox (see `docs/android.md`).

### `lighting_headless.c`

Checks placement (every panel over an open tile that has a ceiling, none stacked, two alcove lamps, one panel per tile and `Lighting_PanelOnTile` finding each), the mix of modes over 20 floors and that dead lights come in patches, that light is 0 to 1 everywhere and zero in walls, that at least a tenth of the open floor is dark and at least a seventh properly lit, that both stair approaches are lit on 20 floors at 8 times each, that light stops at a wall (a lamp beside a wall lights its own side and none of the other, corners included), the interpolation of `Lighting_AtPoint`, the range and shape of each mode over 4000 samples (a flickering light is out about 2% to 33% of the time; a dead light is dark more than 90% of the time and twitches back), the tint colours, the colour of a lit, dim and dead panel, and that one `Lighting_Update` is under a millisecond. `LIGHT_DUMP=file` writes the light of floor 1 as a grid of 0 to 100 (walls -1). Build line in the file header. Whole file sits inside `LIGHTING_HEADLESS_TEST`.

### `highp_headless.c`

Guards the shader's text, since a shader cannot be compiled without a GPU: both are GLSL ES 1.00 with the version line first; the vertex shader declares highp and says nothing of mediump or lowp; the fragment shader asks for highp under `GL_FRAGMENT_PRECISION_HIGH` and falls back to mediump; every attribute, uniform and varying the batches use is present; every one of those names still appears in `raylib/src/rlgl.h`'s default shader (so a raylib update that renames one is noticed), and it reports whether raylib's default web vertex shader still declares mediump (it does in the raylib in this project). Off the web `Highp_Begin` and `Highp_End` do nothing. Build line in the file header. Whole file sits inside `HIGHP_HEADLESS_TEST`.

## Fixes and Problems

- Light first reached 10 m, which with a lamp every 6 m lit everything, with no dark left even where whole patches were dead. Reach is now 4 tiles (8 m) and the patch noise is stretched, which gives clear dark patches. A first version also had too few bright tiles under a lamp (0.798 against a test of 0.8): the tone curve gain went from 1.6 to 2.0.
- The panel and its frame were two quads 2 cm and 4 cm below the ceiling, drawn over it. The phone showed dark slabs with slivers and wedges of white, then, after the lights were made part of the ceiling, thin black lines running off some rims. The depth buffer is 24 bits (the readout), so the cause was the mediump vertex shader rounding every vertex to 3 to 6 cm; see `highp_shader` above. The lights stay part of the ceiling anyway: it is the more robust way to draw them.
- Flicker no longer drops fully to black: its low level is 0.10, so a stutter reads as the light struggling, not as the screen blinking.
- The patch noise looked fine on paper and was too middling: value noise averages out toward the middle, so most fixtures landed in the mixed band. It is stretched by 2.1 around 0.5 before use.

## Tuning

- How much of the tower is dark: the thresholds in `PickMode` and the stretch in `Lighting_BuildForFloor`. Patch size: `LIGHT_ZONE_TILES`. Spacing: `LIGHT_LATTICE_TILES` (changing it changes which tiles the lamps are on, and the "forced on near arrival" distance of 8 steps).
- How far a lamp reaches and how bright its pool is: `LIGHT_REACH_TILES`, `LIGHT_HEIGHT`, `TONE_GAIN`.
- How dark the dark is: the ambient numbers in `Lighting_Tint`. Warm or cold: the same numbers.
- Surface colours: top of `dungeon_renderer.c`.
- Flicker timing: `Animate` in `lighting.c`.

## Known limits

- Light is by tile, so a lamp's pool has the shape of the tiles it can see; where walls are 1 tile thick its edge is a little stepped, which the per-corner interpolation hides at normal distances.
- Enemies, pickups, projectiles and the stairs are not lit by the light map, so an enemy in a dark patch is as visible as in a lit one. The pickup glow is meant to show across a dark room. The hands are dimmed. Dimming enemies would make dead patches properly dangerous and is the obvious next step.
- No flashlight. The dark patches are exactly where one would matter.
- No sound for the hum or the flicker.

## The flashlight and dark quads

A quad whose corners are all under 3/255 is not drawn. With the flashlight on, a quad of that kind that the beam can reach is drawn anyway so there is a surface for the beam to light (`DrawFloorMapFlashlight` in `dungeon_renderer.c`; `flashlight.md`). Everything else about the light map is unchanged.
