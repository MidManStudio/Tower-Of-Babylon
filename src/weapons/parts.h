// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "parts.h and parts.c"
// ============================================================================
#ifndef PARTS_H
#define PARTS_H

#include "raylib.h"

// A part is a tapered prism between two points: a limb, a finger, a box, a barrel. With 4 sides
// and a 45 degree twist it is a box; with 6 or 8 sides it is a cylinder. Everything the hands,
// weapons and pickups are made of is built from parts, so there are no model files to load.
typedef struct {
    Vector3 a, b;       // end centres
    float   ra, rb;     // radius at each end (for a box: half the diagonal)
    int     sides;      // 3..8
    float   flat;       // 1 = round or square; less squashes the cross-section along its "up" side
    float   twist;      // radians, turns the cross-section about the axis
    Vector3 upHint;     // the cross-section's flattened side faces roughly this way
    Color   col;
} Part;

#define PARTS_MAX 96

typedef struct {
    Part parts[PARTS_MAX];
    int  count;
} PartList;

// Builders. All return the part, add it with PartList_Add.
Part Part_Cyl(Vector3 a, Vector3 b, float ra, float rb, int sides, Color col);
Part Part_Box(Vector3 a, Vector3 b, float halfW, float halfH, Vector3 upHint, Color col);

void PartList_Clear(PartList *l);
void PartList_Add(PartList *l, Part p);

// How bright parts are drawn, 0 to 1 (default 1). The first person hands use it to go dark in dark
// places. Set it before drawing and put it back to 1 afterwards.
void Parts_SetBrightness(float b);

// A rounded blob: an ellipsoid centred at `centre`, long in the direction of `axisEnd` (half its length is the
// distance from centre to axisEnd), `radius` across and `radius * flat` in the direction of upHint. It is smooth
// shaded and closed, so fists, knuckles and the ends of the pipe have no corners to catch the eye.
Part Part_Blob(Vector3 centre, Vector3 axisEnd, float radius, float flat, Vector3 upHint, Color col);

// The same with fewer triangles (36 instead of 64): for fingers and other things too small on screen to show the difference.
Part Part_BlobLow(Vector3 centre, Vector3 axisEnd, float radius, float flat, Vector3 upHint, Color col);

// Draws one part now. A part with 6 or more sides is smooth shaded (each corner of the ring takes its
// light from its own outward direction, so a finger or a pipe reads as round); a box (4 sides) and
// a prism of 3 or 5 sides is shaded flat, face by face. lightDir is a unit vector, world space.
void Part_Draw(const Part *p, Vector3 lightDir);

// Draws every part in the list. Needs depth testing on: the parts overlap each other and the depth
// buffer sorts them.
void PartList_Draw(const PartList *l, Vector3 lightDir);

// Draws all parts far to near as seen from eye looking along forward (painter's order), for use with
// depth testing off.
void PartList_DrawSorted(const PartList *l, Vector3 eye, Vector3 forward, Vector3 lightDir);

#endif
