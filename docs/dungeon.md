# Dungeon module

Procedural floors for Tower of Babylon: a fixed shell and stair strip, probe-ray driven interior, a three-floor sliding window, and the climbable stairs and doors that move the player between floors.

## Overview

Every floor is a 45 by 45 tile grid (2 m tiles, 6 m walls). What never changes between floors is generated first and marked reserved: the border ring and two stair alcoves in the east corners. The interior is then built around that fixed part.

Generation order for one floor:

1. Stamp the shell (border, both alcoves, doors, pads) and flag those cells reserved.
2. Mark protected tiles: everything reserved plus a disc of radius 6 around the outside of each alcove door.
3. Pick two to four "pillar halls", 11 by 11 squares kept free of partitions.
4. Partition pass: a grid of probes shoots rays along the four axes. Where a probe sees more open span than its target, a wall segment is placed across that span. The target shrinks from 17 tiles next to the stair doors to 9 tiles far from them.
5. Pillar pass: full lattice in the halls, partial lattice in other wide rooms found by 16-ray probes.
6. Pocket pass: every open tile the arrival point cannot reach is either joined to the rest with a doorway or filled in.
7. Validate. If the floor fails the hard rules or misses the quality targets, rebuild from the next sub-seed, up to eight times. After that a pillar-hall-only layout is used.

Floors are deterministic: the same tower seed and floor number always give the same layout.

Each floor has a ceiling at wall height. It covers the hall and the east strip between the alcoves, and leaves the two stair alcoves open to the shaft. The ceiling and the walls are real geometry for the raycast module, which the spiders use to crawl on them.

Arrival is in the opposite corner from the exit on purpose. The player climbs the flight in the up alcove to a door at the top, and the door leads to the down alcove of the next floor, where they step out in front of that floor's down door. Going back down uses the down door, which leads to the top landing of the flight below. Every floor has to be crossed from one corner to the other. A short fade covers the jump. The old glowing pads are gone; the pad tiles (`TILE_STAIRS_UP`, `TILE_STAIRS_DOWN`) stay in the grid because the generator's reachability checks use them. The spawn-safety radii for enemies still measure from `ARRIVE_BELOW_*` and `ARRIVE_ABOVE_*`, and the real arrival points are within a few tiles of those, so they stay covered.

The ideas come from the sources reviewed for this module. The survey (van der Linden et al., 2014) and the occupancy-regulated extension it describes place designer-fixed content first and let the generator fill around it, which is the shell-first order used here. The Moonjump article recommends flood-fill connectivity checks, rejection of bad layouts and seed-based reproducibility, all of which are in step 6 and 7. The Medium tutorial's exit-socket rooms were not used, because the fixed alcoves already give each floor its two sockets.

## Modules

### `dungeon_types.h`

Constants, tile and cell types, and `FloorMap`. The fixed layout lives here as macros: `HALL_*`, `STAIR_WALL_X`, the alcove rows, door rows, pad tiles, arrival tiles and approach tiles. `MIN_CORRIDOR_TILES` sets the narrowest corridor the generator may make.

Walls are `TILE_WALL` with a `variant`: `WALL_SHELL`, `WALL_PARTITION` or `WALL_PILLAR`. Pillars stay `TILE_WALL`, so the player collision code, which only tests for `TILE_WALL`, blocks them without changes. `CELL_RESERVED` marks cells the generator must not touch. The grid is indexed `[z][x]`. `TILE_STAIRS_DOWN` was added at the end of the enum so existing values are unchanged.

### `dungeon_probes.h` and `dungeon_probes.c`

Sparse probe lattice (every 4 tiles from tile 2, 11 by 11 probes) and the ray caster. `Probe_CastRay` is a grid DDA that accepts any direction. `ProbeField_Analyze` always measures the four axis reaches and spans; with `fullRays` it also casts 16 evenly spaced rays and stores the mean and maximum reach. Tile units, rays start at tile centres.

### `dungeon_raycast.h` and `dungeon_raycast.c`

Three dimensional queries against a floor treated as solid geometry: the floor plane at y = 0, a ceiling plane at y = `WALL_HEIGHT` where `Dungeon_HasCeiling` allows it, and every wall or pillar tile as a box from floor to ceiling. `Dungeon_Raycast` returns the nearest surface along a unit-direction ray with its point and normal. `Dungeon_PushOutSphere` moves a sphere out of walls, floor and ceiling, repeating a few passes so a sphere in a corner settles. `Dungeon_SurfaceDistance` gives the distance to the nearest surface and is used by tests to check that feet and bodies sit where they should.

Rays only test tiles inside the ray's own bounding box, so a query costs a handful of box tests. A ray that starts inside a solid tile reports no hit for that tile. All normals are axis aligned.

### `dungeon_generator.h` and `dungeon_generator.c`

`GenerateFloorMap(out, floor, towerSeed)` runs the steps above. `DungeonFloorSeed` mixes tower seed and floor number with an integer hash. `ValidateFloorMap` checks the hard rules: shell and alcoves unchanged, up pad and upper arrival reachable, no cut-off pockets, route no longer than 260 steps.

Quality targets are separate and only apply to generated layouts: route to the stairs at least 52 steps, open space between 55% and 90% of the hall. A segment may only be placed where the band of `MIN_CORRIDOR_TILES` tiles on both sides is free of walls, which is what keeps corridors at least 3 tiles wide. Partitions longer than 8 tiles get a 3 tile doorway.

The generator keeps its working buffers in static storage to stay clear of the small WASM stack, so it is not re-entrant. Tunables are the `#define`s at the top of the file.

### `floor_window.h` and `floor_window.c`

`FloorWindow` holds three `FloorMap`s. Floor n is stored in slot `n % 3`. `FloorWindow_Advance` changes the current floor and calls a refresh that generates only floors in range that are not already loaded, so a climb or descent builds exactly one floor and keeps the other two untouched. Floors below 1 are never built. `FloorWindow_RelativeY` gives a floor's height relative to the current one, so everything is drawn around the player's origin and heights stay small however high the tower goes.

State kept on a floor, such as `discovered`, is lost when that floor falls out of the window and is rebuilt. Save it elsewhere if it must persist.

### `staircase.h` and `staircase.c`

The up alcove holds a real flight: two runs of 16 steps (0.25 m rise, 0.5 m tread) joined by a 4 m landing, 8 m of climb in all, in a switchback. Run A climbs east along z 6.5 to 9.5, the landing sits at x 84 to 87, run B climbs back west along z 12 to 15, and a top landing at x 73 to 76 holds the door in the west wall. Between the runs there is a 2.5 m slot open to the floor.

Every step is a solid block from the floor up, kept in one table of boxes. Drawing, ground height and blocking all read that same table, so what is seen is what is walked on. Railings are boxes that block but are never stood on, and are drawn as handrails with posts.

- `Staircase_GroundHeight(x, z, y)`: height of the highest standable surface under the point that is at most `STEP_UP_MAX` (0.30 m) above `y`. A player on the floor under the top landing is not lifted onto it.
- `Staircase_Blocked(x, z, radius, y, height)`: true when the body would overlap a block or railing it cannot step onto. The player controller uses a 0.30 m clearance, which keeps the camera off the step faces.
- `Staircase_DoorInRange`, `Staircase_DoorLocked`: a door is in reach within 2.4 m of its wall and about 1.7 m either side of its centre, at the right height. Only the down door of floor 1 is locked.
- `Staircase_SpawnBelow` / `Staircase_SpawnAbove`: where the player stands after a door. Below is 2.8 m in front of the down door at floor level, facing west. Above is on the top landing 2.6 m from the up door, facing east. Both are just outside door reach, so arriving does not offer the door again.
- `DrawStairs(floor, doorOpen)`: draws the shaft, flight, rails and both doors. The alcove walls are only 6 m tall, so the shaft carries them up to 11.5 m and caps the top, which makes the climb end in a small dark room. Only the current floor's flight is drawn.

Doors are sliding slabs in front of a frame, with a doorway panel that goes from dark to bright as the door opens and a lamp (red locked, amber ready, green open). The up door is at the top landing, 8 m above the floor, centred on z 13.5. The down door is on the east wall of the down alcove at floor level, centred on z 80. No step, rail or door face is coplanar with another surface, and the flight is drawn without wire edges, to keep clear of depth fighting.

The flight is identical on every floor, so it is constants, not part of `FloorMap`.

### `dungeon_renderer.h` and `dungeon_renderer.c`

`DrawFloorMap(floor, viewer, yOffset)` draws everything in view of one floor: a floor quad for every open tile, a ceiling quad over every open tile that has a ceiling (`Dungeon_HasCeiling`, so not over the stair alcoves) and the faces of every wall tile that touches open space. Where a ceiling light hangs, the ceiling tile is built around it: nine quads (four of ceiling, four of dark frame, the glowing panel) that touch along their edges and never overlap, so the light is part of the ceiling and not a second surface laid over it. Ceiling colour inside such a tile is interpolated from the tile's four corner colours, so the light does not break the gradient. All of it is within 18 tiles of the viewer. Every surface is a quad whose four corners are coloured separately, and the colour of a corner is its surface colour times a shade for the direction it faces, times a fade with distance, times the light at that corner (`Lighting_Corner`, `Lighting_Tint`, see `lighting.md`). So pools of light, dark patches and the fade to black are smooth gradients over each surface, not one flat colour per tile.

- **Corners.** The map has (45 + 1) by (45 + 1) corners; corner (i, j) is at world x = (i - 0.5) * 2. A tile's four corners are shared with its neighbours, so neighbouring quads always agree on a colour along their shared edge and there are no seams.
- **Floors and ceilings** darken a little where they meet a wall: each corner loses 10% for every wall tile that touches it (a straight wall: 20%, an inside corner: 30%). This is the cheap ambient occlusion that makes the room read as a room. Floor quads are 92% and ceiling quads 85% of their colour, so the two read as different surfaces.
- **Walls** are one quad per exposed face, shaded by direction (+z 100%, -z 88%, +x 78%, -x 66%), lit at the top as at the lamps and at 72% of that at the foot (`WALL_FOOT_LIGHT`). The inside corner of two walls takes 18% off (`AO_CONCAVE_WALL`). Wall faces share their edges with the floor and the ceiling and never overlap them, so no two surfaces share a plane.
- **Fade.** Colour falls linearly to black 1.5 m inside the draw radius, per corner, so a tile is already fully dark when it is dropped. Floor and ceiling tiles whose nearest corner is past that are not drawn at all.
- **Culling.** `DrawFloorMapView(floor, eye, forward, yOffset)` draws only what is inside a cone around the 3D look direction: a tile (a sphere of 1.5 m round a floor or ceiling tile, 3.4 m round a wall) is dropped when the angle to it, less the angle it spans, is more than 55 degrees (the screen's diagonal half angle is 53 degrees at full sprint). Anything within 6 m of the eye is always kept. This works at every pitch: looking level, up at the ceiling lights, or down, about a third of the quads in range are drawn (it was all of them whenever the player looked steeply up or down), and straight up about 6%. `DrawFloorMapFacing` (flat look direction) and `DrawFloorMap` (no culling) remain for tests. `renderer_headless.c` checks that no quad whose centre is inside the real camera view (90 degrees across, 66 high) is dropped, at four pitches.
- **Black quads are skipped.** A quad whose four corners are all under 3 of 255 in every channel is not drawn: against the black background it cannot be seen. `DrawFloorMapKeepDark(true)` turns that off (for tests that count every surface).
- **Colours.** The surface colours before light are the `COL_WALL_*`, `FLOOR_*` and `CEILING_TILE` constants at the top of `dungeon_renderer.c`. They are what a surface looks like in full light; the light's warm tint multiplies them, so they are chosen paler than the result. Change them to recolour the tower.
- **Nothing is laid over anything else.** Every horizontal quad is at height 0 (floor) or exactly the wall height (ceiling, frames, panels), and the ceiling pieces of a view add up to exactly the area of the ceiling tiles. `renderer_headless.c` checks both.
- **Cost.** About 1000 quads in a view (4000 vertices), in `RL_QUADS` so each is four vertices, not six. Dark tiles are not skipped: they are drawn nearly black so that silhouettes show faintly in the dark.

There are no outline ribbons any more (see "Phone glitches" below): outlines were what looked like cracks in the walls, and the lighting now does their job of separating surfaces.

### `main.c`

Owns the `FloorWindow` in static storage. Each frame it finds the door in reach, if any, and shows a USE button for it (key E on a keyboard). Using a door freezes the player, slides the door open over 0.7 s, fades to black over 0.4 s, then advances the window, spawns that floor's enemies, teleports the player to the spawn point of the door they came through and fades back in. The locked down door on floor 1 shows a LOCKED button that does nothing. The per-frame work is in `Frame()` and the game state is at file scope. On the web the browser calls `Frame()` through `emscripten_set_main_loop(Frame, 0, 1)`; elsewhere `main` runs the usual `while (!WindowShouldClose())` loop at 60 FPS. The step is capped at 50 ms (`MAX_FRAME_DT`) so a hitch cannot throw the player across the map. Each frame it updates and draws enemies, moves projectiles and hands them to the enemy system for hit tests (see `enemies.md`). The HUD shows the generation report and the enemy count.

### `player_controller.c`

Walks the player with the existing joystick and look controls. The vertical part now asks `Staircase_GroundHeight` for the surface under the feet. A grounded player is glued to it, which climbs a step or follows a flight down without hopping. A player who steps off an edge falls with gravity (14 m/s squared) and lands on whatever is below. Horizontal moves are refused when they enter a wall tile, an obstacle, or a stair block or railing the player cannot step onto. The camera follows a smoothed eye height (`eyeY`), so each 0.25 m step does not jolt the view; `TeleportPlayer` snaps it. Walls are tested as a circle, not a point: a move that brings the body within 0.45 m of any wall face (tiles outside the map count as walls) is refused, which keeps the camera clear of the 0.1 m near plane. A body already inside that distance, such as one just placed near a wall, may still move so long as it does not get closer. Movement is tested one axis at a time, so the player slides along walls and into corners.

### `dungeon_headless.c`

Test for everything above that does not need raylib. The whole file sits inside `DUNGEON_HEADLESS_TEST` so mray's build, which compiles every `.c` file, does not see a second `main()`. Build line and options are in the file header.

### `renderer_headless.c`

Replaces the raylib draw calls with recorders, runs `DrawFloorMapFacing` on generated floors with the real lighting, and checks: every exposed wall face is drawn once, every open tile has a floor quad and every tile with a ceiling has a ceiling quad (nine where a light hangs); every horizontal quad is at exactly floor or ceiling height, so nothing is laid over anything; the ceiling pieces add up to exactly the ceiling area and no two floor quads or two ceiling quads cover the same ground; floors, ceilings, frames and panels face the right way, every wall face is wound toward the open tile it borders and is vertical; lit panels glow and dead ones are dark; surfaces at the edge of the draw distance are black and near ones are not; lit surfaces are warm (red over green over blue); a wall is never lighter at its foot than its top; looking one way draws well under 70% of what looking all round does and nothing wholly behind the viewer. Setting `RENDER_DUMP=file` also writes one view's quads and colours to a text file. Build line in the file header. Whole file sits inside `RENDERER_HEADLESS_TEST`.

### `stairs_headless.c`

Links the real player controller and staircase code with stubbed draw calls. It walks the whole flight up and down with scripted input, and checks the rail, the solid mass, falling, jumping on the stairs and the door queries. Build line in the file header. Whole file sits inside `STAIRS_HEADLESS_TEST`.

## CI and Workflows

None. Build and run through `mray.py` (option 2 and 3). Run the headless test locally with the command in `tests/dungeon_headless.c`.

## Fixes and Problems

### The earlier "fix" changed nothing

The new `dungeon_types.h` and `dungeon_generator.h` were saved into a folder named `src` with an invisible U+200B in front of it, created by pasting a path containing that character into mray's manual path option. The real `src/dungeon/dungeon_types.h` kept its old 25 by 25 contents, so the build never saw the change. Python's `str.strip()` does not remove U+200B, which is why the existing `.strip()` calls did not help. mray now removes zero-width characters from typed names and paths, and offers to delete such folders after applying an archive.

### Why the old layout was a box

The old generator made three to five rooms of 4 to 6 tiles inside solid rock on a 25 by 25 grid, joined by one-tile corridors. `main.c` regenerated all three floors on every stair step instead of sliding the window, and drew each staircase at an absolute height while drawing the floor at height zero. Per-floor seeds were `base + floor * 7919` fed to a linear congruential generator, so neighbouring floors had linearly related random streams.

### Problems met while building this version

- Filling cut-off pockets with wall produced large solid blocks. Pockets of 20 tiles or more, or any holding a reserved tile, are now joined with a doorway instead.
- Partitioning ran before any pillars were placed and cut every large space down, leaving about one pillar per floor. Pillar halls are now reserved before partitioning.
- The fallback layout failed its own validation because the quality targets (minimum route length, open-space range) were mixed into the hard rules. They are separate now.

### Verified so far

On the build machine (x86, not a phone): 360 floors from six tower seeds, 3.0 ms average and about 14 ms worst per floor, 244 of 360 accepted on the first attempt, no fallback layouts, no validation failures. Route to the stairs 52 to 114 steps, open space 81 to 86 percent. Same seed gives the same floor, neighbouring floors and different tower seeds differ, each climb builds exactly one floor, a floor left behind is kept, and coming back down rebuilds floor 1 identically. All sources were syntax-checked against the raylib headers inside the project (6.1-dev). The game was not linked or run, because the bundled `libraylib.a` is a WASM build. Generation time on the phone is not measured.

### Ceiling added with the spiders

The first version had no ceiling, which left nothing for a spider to walk on overhead. The ceiling is drawn and raycast over the same tiles, and the tile-for-tile match was checked by script.

### Stairs and doors added

The old stairs were scenery with a 4 m rise in six steps, partly inside the border wall, and a glowing pad that teleported. The player's vertical model assumed the ground was always at 0, so nothing could be climbed. The new flight fits inside the alcove, and the player controller got a ground-height query. Headless result: scripted walk up run A, across the landing, up run B to the top, back down; no frame ever moved the player by more than one step, the player never left the ground on the flight, rails and the run B mass stopped sideways walking, a 3 m drop landed on the floor, and a jump on the stairs rose about 1.3 m and landed back on the flight.

### Depth fighting and seeing through walls

Reported on the phone: dashed vertical lines on walls, walls going transparent when close, and uneven frame rate (35 to 43 FPS in the screenshots). Causes and fixes:

- **Dashed lines.** `DrawCubeWires` drew each wall's edges exactly on its own faces, so lines and faces fought for the same depth. A first fix enlarged every wire box slightly, which made things worse: neighbouring tiles each drew their own offset copy of the seam between them, so every seam showed as two lines and the lines hung off the corners, which looked like walls generated twice. The wires are gone. Walls are now face quads and the outline is one line per real corner (see Rendering above), which fixes both the dashing and the doubling.
- **Seeing through walls.** The player was tested as a single point, so the camera could stop a few millimetres from a wall, inside the 0.05 near plane. The player is now a circle with 0.45 m of wall clearance, and the near plane is 0.1 m.
- **Depth precision.** The raylib default view range is 0.05 to 4000, a ratio of 80000 to 1, which gives centimetre-sized depth steps at 20 m on a 16-bit depth buffer. `main.c` replaces the projection after `BeginMode3D` with 0.1 to 200 (2000 to 1), about 40 times finer. This is done with `rlFrustum` so it does not depend on the raylib build honouring `rlSetClipPlanes`.
- **Pop-in.** Walls used to stop at 14% brightness and then vanish at the draw radius. They now fade to black first.
- **Frame rate.** A wall tile used to cost 12 triangles for the cube plus 12 wire lines, all through `DrawCube`, which pushes and pops a matrix per call. A tile now costs only its exposed faces, two triangles each, with no matrix work. In a recorded view that is 862 triangles and 63 lines. Tiles behind the player are also skipped. The mray build already uses `-O2` (and ASYNCIFY, which is needed for the blocking main loop), so build flags are not the cause.

None of this has been seen on the phone. `renderer_headless.c` checks that every exposed face is drawn once, that every triangle is wound to face open space, and that lines are vertical, unique and in open space; geometry was also rasterised offline to check it by eye. The wall clearance and doors are covered by `stairs_headless.c`. Depth precision on the phone's GPU is the one thing that cannot be checked here.

### Judder while moving (web loop)

Still screenshots looked clean but the game stuttered as soon as the player or camera moved, with 36 to 46 FPS on a 60 Hz phone. The game used the blocking pattern, `SetTargetFPS(60)` and a `while` loop, which on the web needs Asyncify and makes raylib sleep to hold the frame rate. Frames then run from timers instead of the display's refresh, so they arrive unevenly and are presented unevenly. Emscripten's own documentation says Asyncify has a size and speed overhead that `emscripten_set_main_loop` does not. `main.c` now registers `Frame` with `emscripten_set_main_loop(Frame, 0, 1)`, where fps 0 means requestAnimationFrame, and does not call `SetTargetFPS` on the web. The 50 ms cap on the frame step is new in the same change.

Keep `-s ASYNCIFY=1` in mray's emcc command. The loop no longer needs it, but raylib's audio start-up waits with `emscripten_sleep`, which needs Asyncify, so it has to stay once sound is added. (An earlier version of this note said it could be removed. That was wrong for any game that will have sound.)

The loop change did not settle it: rotating the camera still glitched. A web search turned up one more known cause of crawling edges while turning, aliasing (raylib's answer is `FLAG_MSAA_4X_HINT`), and the touch code had a real bug of its own, described in `docs/player.md` ("Look input"). The game now asks for 4x MSAA, tracks fingers by id, caps the look step, and draws a frame graph in the top-right corner so the next report can be measured instead of guessed.

### After the loop fix: what the frame graph showed, and the next round

With the frame graph on screen the phone reported 62 to 63 FPS and an average frame of 16.7 ms, so frame pacing is fixed (it was 36 to 46 FPS). What remains is occasional single late frames: worst frames of 24, 25, 32 and 78 ms in four screenshots, and 103 ms in a first one taken right after loading. Look input was small in all of them (1 to 13 px per frame), so the input is not the cause. Changes made to reduce the chance of a late frame:

- **Fewer vertices per frame.** raylib sends everything through one 8192 vertex batch, and every time it fills it uploads and draws, which on a phone GPU is where a stall can hide. The stairs, shaft and rails (several thousand vertices) were drawn on every frame wherever the player was; they are now drawn only within 34 m. Enemies and spiders (a spider is about 2000 vertices) were drawn at any distance; they are now drawn only within 40 m. The frame graph itself was trimmed in the same spirit.
- **Hairline outlines replaced by ribbons** (see Rendering): one pixel lines crawl while the view turns, which reads as glitching without showing in any timing.
- **Stage timers.** The graph now also reports how long the game's own code took in each stage of the worst frame in the window: input, simulation, enemies, 3D drawing, 2D drawing, and EndDrawing. A late frame with small numbers there was held up outside the game (the browser, the GPU or Android); a late frame with a large number in one stage points at that stage.

### Walls still glitchy after the ribbons (stepped shading)

After the combat update the player reported smooth play but walls that still looked glitchy, without saying how. The one remaining cause found in the code was the brightness steps described under Rendering: with one colour per tile, a long wall showed a band of different brightness every 2 m, and the bands changed as the player walked. Walls are now shaded per vertex. `renderer_headless.c` checks that a nearer corner of a face is never darker than a farther one and that no face has a colour jump between its corners. If walls still look wrong, the next things to check are the tint colours of the three wall kinds (border, partition, pillar), which differ and meet at junctions, and the 4x MSAA request, which the phone's browser may or may not grant.

### Phone glitches: what the readout said, and the real cause

(The short version, with the rules that came out of it, is `phone-rendering.md`.)

After the stepped-shading change the player said everything still looked glitchy, including the hands and weapons, and asked whether the WASM playground itself could be to blame. It was, in a way nobody had looked at: not the depth buffer, but the shader raylib uses on the web.

What was tried, in order:

1. A 16-bit depth buffer was suspected from TerminalViscosity's notes (`client/docs/client.md`: tight clip planes and nothing within 4 cm of another surface). The second build removed everything layered (the black outline ribbons, the alcove tints, the tiled-cube floor), set the world's depth range to near 0.25 and far 60 (`WORLD_NEAR`, `WORLD_FAR` in `src/ui/view_clip.h`), gave the hands a range of their own (0.04 to 4) after a depth-only clear, and began reporting `GL_DEPTH_BITS` and `GL_SAMPLES` on screen. The walls stopped glitching. The readout said **24 depth bits and 4x MSAA**, so the buffer was fine and the wall fix was the removal of thin layered geometry, not depth precision.
2. The ceiling lights then showed dark slabs with slivers of white. They were drawn as a frame 2 cm and a panel 4 cm below the ceiling; they were rebuilt as nine touching, non-overlapping quads that are part of the ceiling. Their rims still showed thin black lines running off, and the fists still looked like crowns of spikes, even though the fists had been rebuilt from smooth blobs.
3. **The cause.** raylib's default WebGL vertex shader declares `precision mediump float;`. On a phone GPU every vertex position is then rounded to about 3 cm at 40 m and 6 cm at 80 m from the origin, which mangles anything centimetres across and leaves metres-wide walls alone. The game now draws with a copy of the default shader at highp (`src/ui/highp_shader.*`, documented in `lighting.md`).

The depth range changes stay: they cost nothing, give the hands real depth sorting, and the 0.25 near plane is under the 0.45 m wall clearance.

Nothing here could be run on the phone. The renderer test checks the geometry and offline renders were looked at by eye; neither can show a shader precision problem, which is why this one survived three rounds.

### Known limits

- The test obstacle at tile (3, 3) may land inside a generated wall.
- The down door is a plain door at floor level, not the top of a descending flight. It leads to the top landing of the flight on the floor below, so going down means walking down the flight there.
- Spiders and the cylinder enemies do not see the stair blocks. They can walk or crawl through the flight if they wander into the up alcove, and projectiles pass through it.
- The stairs were checked headless on x86 with scripted input only. How they look and feel on screen, and the frame rate on the phone, are not checked.
- Open space is steady around 84 percent. Making floors more maze-like needs a different partition rule, not smaller span targets, because the 3 tile corridor rule is what limits density.
- `src/world.c` and `src/world.h` are not used by `main.c`.
