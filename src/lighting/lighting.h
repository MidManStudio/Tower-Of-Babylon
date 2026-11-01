// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lighting.md, section "lighting.h and lighting.c"
// ============================================================================
#ifndef LIGHTING_H
#define LIGHTING_H

#include <stdbool.h>

#include "raylib.h"
#include "../dungeon/dungeon_types.h"

// Ceiling lights for one floor. A fixture hangs in the ceiling on a regular lattice, and each one is
// working, dim, flickering or dead. Moods come in patches, so a whole stretch of corridor can be dead
// while the next one is lit, which is what makes some places very dark and others dreadful.
//
// The light is worked out once per tile, not per pixel: each fixture shines on the open tiles it can
// see (a tile-grid line of sight, so light never goes through a wall), and the renderer reads the
// result at tile corners so floors, ceilings and walls fade smoothly between tiles.

typedef enum { LIGHT_ON = 0, LIGHT_DIM, LIGHT_FLICKER, LIGHT_DEAD, LIGHT_MODE_COUNT } LightMode;

typedef struct {
    float     x, z;          // world position of the centre of the panel
    int       tx, tz;        // the tile it hangs over
    LightMode mode;
    bool      alongX;        // the long side of the panel runs along x
    bool      hasPanel;      // false for the always-on lamps of the stair alcoves, which have no ceiling
    float     phase;         // makes each fixture's flicker its own
    float     level;         // right now, 0 (dark) to about 1 (full)
    int       reach;         // tiles
    int       first, count;  // slice of the influence pool
} Fixture;

#define LIGHT_LATTICE_TILES 3     // one fixture every 3 tiles, 6 m
#define LIGHT_MAX_FIXTURES  224
#define LIGHT_HEIGHT        5.0f  // how far above the floor the light is taken to be, for the falloff
#define LIGHT_REACH_TILES   4
#define LIGHT_ZONE_TILES    12    // moods come in patches about this big

// Builds the fixtures and the per-tile influence of the floor. Call whenever the current floor changes.
void Lighting_BuildForFloor(const FloorMap *f, int floorNumber);

// Moves every fixture to its level for this time and recomputes the light of every tile and corner.
void Lighting_Update(float timeSecs);

// Light 0 to 1 at the corner (ci, cj) between tiles (ci-1..ci, cj-1..cj): world x = (ci - 0.5) * TILE_SIZE.
// Valid for 0 <= ci <= MAP_WIDTH, 0 <= cj <= MAP_HEIGHT. Ambient light is not included.
float Lighting_Corner(int ci, int cj);

// Light 0 to 1 over open tile (tx, tz); 0 for a wall or outside the map.
float Lighting_Tile(int tx, int tz);

// Light 0 to 1 at a world point on the floor plane (bilinear between corners). For things that should
// look dark in dark places, such as the player's hands.
float Lighting_AtPoint(Vector3 p);

// Colours the renderer multiplies a surface colour by: dark ambient is faintly blue, light is warm.
// brightness = the corner light with any ambient occlusion already applied.
void Lighting_Tint(float light, float *r, float *g, float *b);

int  Lighting_FixtureCount(void);
const Fixture *Lighting_Fixture(int i);
int  Lighting_CountMode(LightMode m);   // only fixtures with a panel

// Forces a fixture (or every fixture with a panel) into a mode. For tests and for trying looks out
// by hand; the game itself never calls it.
void Lighting_SetMode(int fixture, LightMode m);
void Lighting_SetAllModes(LightMode m);

// The panel hanging over tile (tx, tz), or NULL when there is none. The renderer builds the ceiling of that
// tile around it, so a light is part of the ceiling and not a second surface laid over it.
const Fixture *Lighting_PanelOnTile(int tx, int tz);

// Panel size and its frame, in metres. The frame is the dark rim around the glowing panel.
#define LIGHT_PANEL_LEN   1.50f
#define LIGHT_PANEL_WID   0.60f
#define LIGHT_FRAME_RIM   0.10f

// How the panel glows right now, as a colour of 0 to 255 per channel: dead grey, a weak amber when dim,
// warm white when lit. Follows the animated level, so a flickering panel visibly flickers.
void Lighting_PanelColour(const Fixture *f, float *r, float *g, float *b);

#endif
