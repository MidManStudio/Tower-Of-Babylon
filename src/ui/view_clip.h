// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lighting.md, section "view_clip.h and view_clip.c"
// ============================================================================
#ifndef VIEW_CLIP_H
#define VIEW_CLIP_H

#include "raylib.h"

// The depth range of the 3D view. The phone's browser may give the page a 16 bit depth buffer, which
// resolves about z * z / (near * 65536) metres at distance z: with a near plane of 0.1 that is 6 cm at 20 m,
// so anything layered closer than that flickers. The world therefore uses a near plane of 0.25 and a far
// plane of 60 (everything is black by 36 m), and the first person hands use their own tiny range after
// the depth buffer has been cleared.
#define WORLD_NEAR 0.25
#define WORLD_FAR  60.0
#define HANDS_NEAR 0.04
#define HANDS_FAR  4.0

// Call inside BeginMode3D. Sets the projection for this camera with the given range and remembers the
// camera's field of view.
void ViewClip_Apply(const Camera3D *cam, double nearPlane, double farPlane);

// Same field of view and aspect as the last Apply, new range. Flush the render batch first.
void ViewClip_Reapply(double nearPlane, double farPlane);

// Clears only the depth buffer, so what is drawn next is tested against itself and never against
// the world. Flushes the render batch first. Does nothing off the web.
void ViewClip_ClearDepth(void);

// What the browser actually gave the page: depth bits and MSAA samples (-1 when unknown, off the web).
// Read once after the window exists.
void ViewClip_QueryBuffers(int *depthBits, int *samples);

#endif
