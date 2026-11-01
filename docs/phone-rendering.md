# Rendering on the phone: what went wrong, why, and the rules that came out of it

This game runs as WebAssembly in Chrome on an Android phone (a Samsung Galaxy A13, Mali GPU), built by mray with Emscripten and raylib's web (OpenGL ES 2 / WebGL) backend. For several rounds "everything looked glitchy" on the phone and nowhere else. This page is the short version of what it was. The long version, with what was tried and what each attempt showed, is in `dungeon.md` ("Phone glitches") and `lighting.md` (`highp_shader`).

## The problem

Small things looked broken and the effect changed as the camera moved: thin black lines on walls, the first "ribbon" outlines looking like cracks, ceiling lights with black spikes running off their rims, hands that looked like crowns of spikes, a pipe with sliver faces. Large flat things (walls, floors) were fine, which is why the walls looked fixed first when the thin geometry on them was removed.

## The cause

**raylib's default vertex shader for the web declares `precision mediump float;`** (rlgl.h, `rlLoadShaderDefault`, with the comment "Precision required for OpenGL ES2 (WebGL) (on some browsers)"). On a phone GPU mediump is real 16-bit floating point, about three decimal digits. The vertex shader multiplies each vertex position by the model-view-projection matrix, and in this game positions are world coordinates up to about 90 m across. So:

| distance from the origin | a position is rounded to about |
|---|---|
| 10 to 20 m | 1 cm |
| 20 to 40 m | 3 cm |
| 40 to 80 m | 6 cm |

A 2 m wall tile survives that. A 10 cm rim, a 1.5 cm finger, a 3 cm ribbon do not: their corners jump by more than their own width and the shapes tear into spikes and slivers, differently each frame as the camera (and so the matrix) changes. On a desktop the qualifier is ignored, so desktop builds, headless tests and offline renders could never show it. That is why it survived three rounds of fixes aimed at the wrong things.

## What was ruled out, and how

- **A 16-bit depth buffer.** Suspected from TerminalViscosity's notes. The game now reports `GL_DEPTH_BITS` and `GL_SAMPLES` on screen: the phone says **24 bits and 4x MSAA**. The depth range was still tightened (near 0.25, far 60; hands 0.04 to 4 after a depth-only clear) and everything layered a few centimetres over something else was removed or rebuilt. Those changes are correct and stay; they were not the cause.
- **Two surfaces fighting in depth.** The ceiling lights drew a frame 2 cm and a panel 4 cm below the ceiling and showed dark slabs with slivers of white. They are now part of the ceiling (nine touching, non-overlapping quads). Also correct, also not the root cause: the slivers were the same rounding.

## The fix

The game draws with a copy of raylib's default shader with `precision highp float;` in the vertex shader (and in the fragment shader where `GL_FRAGMENT_PRECISION_HIGH` allows), loaded once and switched on for the whole frame: `src/ui/highp_shader.*`. Tapping the "depth .. shader ..." line on the diagnostics overlay switches between it and raylib's own shader so the two can be compared on the same view.

## Rules for anything new

1. **Anything drawn through raylib on the web inherits the shader.** Do not call `BeginShaderMode` with a shader of your own that omits `precision highp float;` in the vertex stage, and do not trust a headless render to show precision problems.
2. **Do not lay one surface over another within a few centimetres.** Build it into the surface (as the ceiling lights are) or stand it off by 1.5 cm or more. Surfaces that touch along an edge are fine; surfaces that overlap are not.
3. **Keep small parts away from far-from-the-origin extremes if you can.** The highp shader makes this safe, but a 32-bit float at x = 90 m is still only good to about 8 micrometres; do not build anything that needs better, and do not add a second shader without highp.
4. **Test on the phone with the diagnostics on.** The overlay shows depth bits, MSAA samples, which shader is active, light counts and the 3D timings. A screenshot of it is worth more than a description.

## Performance on the phone

The frame graph (top right, the diagnostics) showed 38 to 60 FPS with spikes. What was measured and what changed:

- **Enemy AI is not the cost.** A native profile of the enemy and spider update over 6000 frames: about 5 to 9 microseconds a frame on average, 0.2 to 0.5 ms worst, drawing calls negligible. On a phone running WebAssembly that is a few times slower, but nowhere near the 148 ms `ai` spike seen in one screenshot, which was taken just after the game started: the likeliest cause is WebAssembly warming up (the browser compiles quickly first and optimises in the background) and is not something the game controls.
- **The battery saver may be on.** The screenshots with low FPS show 13 to 14% battery with an orange battery icon, the sign of Android's power saving mode, which lowers CPU and GPU clocks. Compare at over 20% or on charge before concluding anything about frame rate.
- **Culling.** With the camera looking steeply up or down the old code turned culling off and drew every tile in range. It now culls by the real 3D look direction (a 55 degree cone, a little wider than the 53 degree diagonal of the screen at full sprint) at every pitch: looking level draws about a third of the quads in range, looking up about a third, straight up about 6%. A test checks that nothing on screen is dropped.
- **Black quads are not drawn.** A quad whose corners are all under 3 of 255 is invisible against the black background, so it is skipped. In dead patches that is a large part of the floor and ceiling.
- **Cheaper hands.** The fingers use a 36-triangle blob in place of the 64-triangle one, and each part is one `rlBegin` with its vertices instead of one per triangle. Both hands are about 1250 triangles, from about 1900.
- **Finer timers.** The diagnostics now show the 3D stage split into lighting, world, entities and hands (worst of recent frames, time spent submitting). If the next screenshot shows FPS low and these small, the time is going to the GPU, not the code.

## Tracers and other additive light

Projectile streaks and wall sparks (`src/projectiles/projectiles.c`) add light to what is behind them. Rules that keep them right on the phone: draw them after everything opaque, with depth testing on and depth writes off (otherwise a later enemy paints over a streak in front of it, and a streak writes a hole into the depth buffer for what follows); culling off, because a strip that turns about its own axis can show either face; one `rlBegin(RL_QUADS)` per strip, flushed before the blend mode changes; and everything stays inside the highp shader's frame so a 3 cm wide strip at 40 m is not torn up by the half precision position rounding described above.

## The flashlight's shader

The beam is a second program (`HIGHP_FLASH_VS_ES2`/`_FS_ES2` in `highp_shader.c`), highp like the plain one and used only when it compiles, so a failure there cannot bring the glitch back. It reads the world position from a varying, which is a world position because raylib's immediate mode batches vertices already moved by the model matrix. Setting its uniforms flushes the batch first (raylib sets a uniform at once and draws a batch later). It adds to the colour inside a cone and is turned off before the tracers, the first person hands and the 2D screen. Details, and the checks run on it under Mesa's GLSL ES compiler, are in `flashlight.md`; that is not the Mali's compiler.
