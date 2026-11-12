// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/player.md, section "screen_fx.h and screen_fx.c"
// ============================================================================
#ifndef SCREEN_FX_H
#define SCREEN_FX_H

#include <stdbool.h>

#include "raylib.h"

// Call once after InitWindow (it creates a texture) and once at shutdown.
void ScreenFx_Init(void);
void ScreenFx_Unload(void);

// 0 while health is above 75%, rising smoothly to 1 at no health.
float ScreenFx_VignetteStrength(float hpFraction);

// The darkening ring and the red hit flash, drawn over the 3D view and under the HUD.
// The ring closes in as health falls and pulses slowly when health is low.
void ScreenFx_DrawHealthEffects(int screenW, int screenH, float hpFraction, float hitFlash, float timeSecs);

// A small aiming dot at the centre of the screen.
void ScreenFx_DrawCrosshair(int screenW, int screenH);
// The same, fading out as the player aims down the sights (ads01 0 at the hip, 1 aimed): the gun's own sights take over.
void ScreenFx_DrawCrosshairAimed(int screenW, int screenH, float ads01);

// Feedback for shots that land. hit and kill run from 1 (just happened) down to 0.
// A hit is four short white ticks around the centre; a kill is larger, red ticks.
void ScreenFx_DrawHitMarker(int screenW, int screenH, float hit, float kill);

void ScreenFx_DrawHealthBar(int x, int y, int w, int h, float hp, float maxHp);

// A thin stamina bar. Blue when well, amber when low, and a pulsing red while exhausted (with a tick at the level
// where the player recovers). fraction is 0 to 1.
void ScreenFx_DrawStaminaBar(int x, int y, int w, int h, float fraction, bool exhausted, float recoverFraction, float timeSecs);

// ---- Frame graph: a diagnostic overlay ------------------------------------
// Two strips: time per frame in ms (green up to 18.5, yellow up to 34, red above) and how far
// the camera look input moved in pixels that frame. Even bars mean smooth pacing; tall bars
// mean hitches. A look bar much taller than its neighbours is an input spike.
#define FRAME_GRAPH_SAMPLES 120
#define FRAME_STAGES 6   // input, simulation, enemies, 3D drawing, 2D drawing, EndDrawing
void FrameGraph_Push(float frameMs, float lookPx);
// How long our own code took in each stage of the frame just pushed, in ms. If a late frame
// shows little time here, the delay was outside the game: the browser, the GPU or the system.
void FrameGraph_PushStages(const float stagesMs[FRAME_STAGES]);
// The slowest frame of our own code in the window, with its stages.
void FrameGraph_WorstCpu(float *cpuMs, float stagesMs[FRAME_STAGES]);
void FrameGraph_Stats(float *avgMs, float *maxMs, float *maxLookPx);
void FrameGraph_Draw(int x, int y, int w);

#endif
