// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "viewmodel.h and viewmodel.c"
// ============================================================================
#ifndef VIEWMODEL_H
#define VIEWMODEL_H

#include "parts.h"
#include "weapons.h"

// The first person hands and whatever they hold, placed relative to the camera and animated
// from the arsenal's state: idle sway and walking bob, raising a weapon on equip, alternating
// punches, a slanting overhead chop with the pipe, a pistol's recoil and muzzle flash.
// walk01 is how fast the player is moving, 0 to 1. timeSecs drives the sway.
void Viewmodel_Build(PartList *out, const Arsenal *arsenal, const AimFrame *cam, float walk01, float timeSecs);
// The same with the gun brought up to the eye: ads01 is 0 at the hip and 1 aimed down the sights (the player's eased aim amount). The
// gun and both hands slide to the middle of the view so the sights sit on its centre, and the sway calms. Only guns aim: for the
// hands and the pipe it changes nothing.
void Viewmodel_BuildAimed(PartList *out, const Arsenal *arsenal, const AimFrame *cam, float walk01, float timeSecs, float ads01);

// The same with the aimed gun's sway to the side: strafe01 is -1 to 1, the player's sideways speed (PlayerStrafe01, then eased with
// Viewmodel_StrafeSwayStep). Aimed and moving left or right, the gun and both hands tilt a little toward the way of the move, turning about the
// view's centre line, which is where the sights are, so the sights stay put. At the hip, with strafe01 0, or with no sights it changes nothing.
void Viewmodel_BuildAimedSway(PartList *out, const Arsenal *arsenal, const AimFrame *cam, float walk01, float timeSecs, float ads01, float strafe01);
// The tilt in radians for a strafe01 and an ads01 (the view's roll: negative is the top of the gun leaning to the right).
float Viewmodel_StrafeTilt(float strafe01, float ads01);
// Eases the strafe value toward its target over about VIEWMODEL_STRAFE_EASE_SECS (frame time dt), so the tilt comes and goes smoothly.
float Viewmodel_StrafeSwayStep(float current, float target, float dt);

// Where the tip and the middle of the pipe are in the last pose built, in world space, and whether the
// pipe is in hand at all. The slash trail is drawn between these.
bool Viewmodel_PipePoints(Vector3 *tip, Vector3 *mid);

// Builds and draws it over the world. The depth buffer is cleared first and the hands get their own
// near depth range, so the parts sort against each other by depth (no more painter's order), a wall the
// player stands against never hides them, and a pipe swing leaves a short fading trail.
// brightness 0 to 1 darkens the hands in dark places; a muzzle flash lights them.
// Call inside the 3D pass, after everything else has been drawn.
void Viewmodel_Draw(const Arsenal *arsenal, const AimFrame *cam, float walk01, float timeSecs, float brightness);
void Viewmodel_DrawAimed(const Arsenal *arsenal, const AimFrame *cam, float walk01, float timeSecs, float brightness, float ads01);
void Viewmodel_DrawAimedSway(const Arsenal *arsenal, const AimFrame *cam, float walk01, float timeSecs, float brightness, float ads01, float strafe01);

#endif
