// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lighting.md, section "highp_shader.h and highp_shader.c"
// ============================================================================
#ifndef HIGHP_SHADER_H
#define HIGHP_SHADER_H

#include <stdbool.h>

#include "raylib.h"

// raylib's default shader for the web starts its VERTEX shader with "precision mediump float". On a phone GPU
// (Mali, Adreno, ...) that makes all the vertex math 16 bit: a position at x = 80 m is rounded to about 6 cm, one
// at 40 m to 3 cm. Walls, which are metres across, survive; anything small does not: the corner lines, the rim of a
// ceiling light, a finger. They stretch into spikes and slivers and swim as the camera moves. This is the same
// shader as raylib's default with the precision raised to highp (float, 32 bit) in the vertex shader and, where the
// device allows, in the fragment shader. Same attribute and uniform names, so raylib's batches use it unchanged.
//
// Use: Highp_Init once after InitWindow; Highp_Begin right after ClearBackground; Highp_End right before EndDrawing.
// Off the web it does nothing (desktop OpenGL has no such default).

extern const char *const HIGHP_VS_ES2;
extern const char *const HIGHP_FS_ES2;

// The same two shaders with a flashlight beam added (a second program, so that if it ever fails to compile on some device
// the plain high precision shader above is still used, and the glitch fix stays). The vertex shader also passes the world
// position on; the fragment shader adds light inside a cone from `flashPos` along `flashDir`:
//   uniform vec3 flashPos, flashDir;   the light, and the direction it points (unit)
//   uniform vec4 flashParams;          x cos(inner angle), y cos(outer angle), z 1 / range, w strength (0 = no beam at all)
//   uniform vec3 flashTint;            the colour of the light
// Where the beam is it adds  beam * (0.85 * tint * (1 - 0.55 * the brightest channel already there) + 0.35 * the colour there)
// to the colour: full strength on a surface the lighting left black, less on one that was already lit (so a lit wall is
// lifted, not bleached to a flat disc). See flashlight.c (Flashlight_BeamAt) for the same maths in C.
extern const char *const HIGHP_FLASH_VS_ES2;
extern const char *const HIGHP_FLASH_FS_ES2;

void Highp_Init(void);
bool Highp_Available(void);        // the shader compiled and can be used
void Highp_SetEnabled(bool on);    // for comparing: off uses raylib's own default; takes effect at the next Highp_Begin
bool Highp_Enabled(void);
void Highp_Begin(void);
void Highp_End(void);

// The beam. Only has an effect between Highp_Begin and Highp_End, and when Highp_FlashlightAvailable().
// Highp_SetFlashlight turns the beam on for everything drawn after it; Highp_FlashlightOff ends it (call it before the
// first person hands, the tracers and the 2D screen, which should not be lit by it). Both flush what has been queued so
// far first: raylib sets a uniform at once but draws a batch later, and without the flush the earlier geometry would be
// drawn with the new beam.
bool Highp_FlashlightAvailable(void);
void Highp_SetFlashlight(Vector3 pos, Vector3 dir, float cosInner, float cosOuter, float range, float strength, unsigned tintRGB);
void Highp_FlashlightOff(void);

#endif
