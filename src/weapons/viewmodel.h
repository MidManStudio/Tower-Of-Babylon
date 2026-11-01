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

// Where the tip and the middle of the pipe are in the last pose built, in world space, and whether the
// pipe is in hand at all. The slash trail is drawn between these.
bool Viewmodel_PipePoints(Vector3 *tip, Vector3 *mid);

// Builds and draws it over the world. The depth buffer is cleared first and the hands get their own
// near depth range, so the parts sort against each other by depth (no more painter's order), a wall the
// player stands against never hides them, and a pipe swing leaves a short fading trail.
// brightness 0 to 1 darkens the hands in dark places; a muzzle flash lights them.
// Call inside the 3D pass, after everything else has been drawn.
void Viewmodel_Draw(const Arsenal *arsenal, const AimFrame *cam, float walk01, float timeSecs, float brightness);

#endif
