// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_renderer.h"
// ============================================================================
#ifndef DUNGEON_RENDERER_H
#define DUNGEON_RENDERER_H

#include <stdbool.h>

#include "dungeon_types.h"
#include "raylib.h"
#include "../flashlight/flashlight.h"

// Draws the floor, ceiling, walls and ceiling lights of one floor around the viewer. Every surface is
// built from per-tile quads whose corners are lit from the light map (src/lighting), so pools of light
// and dark patches fall across floor, walls and ceiling alike. Only wall tiles that touch open space
// and sit close to the viewer are drawn, and everything fades to black with distance.
// yOffset is the floor's height relative to the player's.
// Call Lighting_BuildForFloor when the floor changes and Lighting_Update once a frame before drawing.
void DrawFloorMap(const FloorMap *floor, Vector3 viewer, float yOffset);

// Draws only what can be in view: tiles outside a cone around the look direction are skipped, whichever way the
// camera points (up at the ceiling lights, down at the floor, along a corridor). eye is the camera position, forward
// its unit look direction (3D), yOffset as above.
void DrawFloorMapView(const FloorMap *floor, Vector3 eye, Vector3 forward, float yOffset);

// By default a quad that would be black (all four corners under 3 of 255 in every channel) is not drawn. Pass true to draw
// them anyway: for tests that count every surface, and for seeing the whole map when hunting a rendering fault.
void DrawFloorMapKeepDark(bool keep);

// The collapse: floor tiles farther than radius from (cx, cz) are not drawn (they have fallen away; the ceiling stays). A negative
// radius turns it off. Set each frame by src/floor.
void DrawFloorMapCollapse(float cx, float cz, float radius);

// The flashlight, for the renderer. Pass the flashlight and where the light is before drawing each frame (NULL, or a light
// that is off, clears it). Two things change:
//   * A quad the lighting left black is normally not drawn at all. One the beam can reach is drawn anyway (black), so the
//     shader has a surface to put the light on. Quads the beam cannot reach are skipped as before.
//   * With glowFallback (the beam shader is not in use) the corners within 6 m of the player are made lighter instead, a
//     soft glow without a beam, so the flashlight still does something where there is no shader for it.
void DrawFloorMapFlashlight(const Flashlight *light, Vector3 eye, bool glowFallback);

// Same, but skips tiles that are behind the viewer or far off to the side. (fwdX, fwdZ) is the unit
// look direction on the floor plane; pass (0, 0) to draw everything, for example while
// looking steeply up or down.
void DrawFloorMapFacing(const FloorMap *floor, Vector3 viewer, float fwdX, float fwdZ, float yOffset);

#endif
