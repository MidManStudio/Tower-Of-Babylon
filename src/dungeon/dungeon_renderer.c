// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_renderer.c"
// ============================================================================
#include "dungeon_renderer.h"

#include <math.h>
#include <stddef.h>

#include "rlgl.h"
#include "dungeon_raycast.h"
#include "floor_access.h"
#include "../lighting/lighting.h"

#define DRAW_RADIUS_TILES 18
#define VIEW_CONE_DEG     55.0f   // half angle of the cone of tiles drawn: the camera's diagonal half field of view is 53 degrees at full sprint
#define CULL_MIN_M        6.0f    // closer than this nothing is culled
#define DARK_BELOW        3       // a quad whose four corners are all under this (of 255) in every channel is not drawn: it is black

// Face brightness by direction, so neighbouring faces read as separate surfaces.
#define SHADE_PLUS_Z      1.00f
#define SHADE_MINUS_Z     0.88f
#define SHADE_PLUS_X      0.78f
#define SHADE_MINUS_X     0.66f
#define SHADE_FLOOR       0.92f
#define SHADE_CEILING     0.85f

// Surface colours before light. Light multiplies them by a warm tint, so these are what a surface looks
// like in full light. Tune the look of the whole tower here.
static const Color COL_WALL_SHELL     = { 176, 160, 112, 255 };
static const Color COL_WALL_PARTITION = { 170, 150, 108, 255 };
static const Color COL_WALL_PILLAR    = { 188, 168, 122, 255 };
static const Color FLOOR_CARPET   = { 122, 104, 66, 255 };
static const Color FLOOR_UP_ALCOVE   = { 96, 128, 98, 255 };
static const Color FLOOR_DOWN_ALCOVE = { 96, 104, 140, 255 };
static const Color CEILING_TILE   = { 150, 146, 128, 255 };

#define WALL_FOOT_LIGHT   0.72f   // light at the foot of a wall compared with the top, where the lamps are
#define AO_PER_SOLID      0.10f   // floor and ceiling corners darken by this for each wall tile touching them
#define AO_CONCAVE_WALL   0.82f   // the inside corner of a wall, where two walls meet

// Where surfaces fade to black. Set at the start of each draw. Colours are set per vertex from the
// distance to the viewer, so the fade is a smooth gradient across a surface instead of a step per tile.
static Vector3 g_fadeViewer;
static float   g_fadeEnd = 1.0f;

static float FadeAt(float x, float z)
{
    const float dx = x - g_fadeViewer.x, dz = z - g_fadeViewer.z;
    const float k = 1.0f - sqrtf(dx * dx + dz * dz) / g_fadeEnd;
    return k < 0.0f ? 0.0f : k;
}

static Color Surface(Color albedo, float shade, float light, float ao, float x, float z)
{
    float tr, tg, tb;
    Lighting_Tint(light, &tr, &tg, &tb);
    const float k = shade * ao * FadeAt(x, z);
    float r = (float)albedo.r * k * tr, g = (float)albedo.g * k * tg, b = (float)albedo.b * k * tb;
    if (r > 255.0f) r = 255.0f;
    if (g > 255.0f) g = 255.0f;
    if (b > 255.0f) b = 255.0f;
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
}

// One quad, wound so the front is the side `outward` points to.
static void EmitQuad(const Vector3 p[4], const Color c[4], Vector3 outward)
{
    const Vector3 e1 = { p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z };
    const Vector3 e2 = { p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z };
    const Vector3 n = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
    const bool keep = n.x * outward.x + n.y * outward.y + n.z * outward.z >= 0.0f;
    static const int fwd[4] = { 0, 1, 2, 3 }, rev[4] = { 0, 3, 2, 1 };
    const int *o = keep ? fwd : rev;
    if (rlCheckRenderBatchLimit(4)) rlDrawRenderBatchActive();
    rlBegin(RL_QUADS);
    for (int i = 0; i < 4; ++i) {
        rlColor4ub(c[o[i]].r, c[o[i]].g, c[o[i]].b, 255);
        rlVertex3f(p[o[i]].x, p[o[i]].y, p[o[i]].z);
    }
    rlEnd();
}

static bool Exposed(const FloorMap *f, int x, int z)
{
    return !Floor_Solid(f, 0, x + 1, z) || !Floor_Solid(f, 0, x - 1, z) || !Floor_Solid(f, 0, x, z + 1) || !Floor_Solid(f, 0, x, z - 1);
}

// Number of wall tiles among the four tiles that meet at corner (ci, cj).
static int SolidAround(const FloorMap *f, int ci, int cj)
{
    return (int)Floor_Solid(f, 0, ci - 1, cj - 1) + (int)Floor_Solid(f, 0, ci, cj - 1) + (int)Floor_Solid(f, 0, ci - 1, cj) + (int)Floor_Solid(f, 0, ci, cj);
}

static float CornerX(int ci) { return ((float)ci - 0.5f) * TILE_SIZE; }

// Is something worth drawing, given where the camera is and which way it looks? The thing is a sphere of `radius` metres
// around (cx, cy, cz). It is in view when the angle between the look direction and the line to it, less the angle the
// sphere itself spans, is inside the cone. (A cone is a little wider than the screen, so nothing visible is dropped.)
static float g_coneCos = 0.0f, g_coneSin = 0.0f;
static bool  g_cull = false;
static Vector3 g_eye, g_fwd;

static bool InView(float cx, float cy, float cz, float radius)
{
    if (!g_cull) return true;
    const float dx = cx - g_eye.x, dy = cy - g_eye.y, dz = cz - g_eye.z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < CULL_MIN_M * CULL_MIN_M) return true;
    const float d = sqrtf(d2);
    const float dot = (dx * g_fwd.x + dy * g_fwd.y + dz * g_fwd.z) / d;     // cosine of the angle to the thing
    // limit = cos(cone + spanned angle) = cos(cone) cos(span) - sin(cone) sin(span), with span = atan(radius / d)
    const float r = sqrtf(d2 + radius * radius);
    const float limit = g_coneCos * (d / r) - g_coneSin * (radius / r);
    return dot > limit;
}

static float g_collapseX = 0.0f, g_collapseZ = 0.0f, g_collapseR = -1.0f;
void DrawFloorMapCollapse(float cx, float cz, float radius) { g_collapseX = cx; g_collapseZ = cz; g_collapseR = radius; }

static bool g_keepDark = false;
void DrawFloorMapKeepDark(bool keep) { g_keepDark = keep; }

// The flashlight as the renderer sees it (set each frame by DrawFloorMapFlashlight).
static struct {
    const Flashlight *light;    // NULL or off: no flashlight
    Vector3 eye;
    float   range, outerRad;
    bool    glow;               // the glow fallback is in use: no shader beam
} g_flash;

void DrawFloorMapFlashlight(const Flashlight *light, Vector3 eye, bool glowFallback)
{
    g_flash.light = (light && light->level > 0.0f) ? light : NULL;
    g_flash.eye = eye;
    g_flash.glow = glowFallback;
    if (g_flash.light) {
        const FlashlightConfig *c = Flashlight_Config();
        g_flash.range = c->range;
        g_flash.outerRad = c->beamOuterDeg * (3.14159265f / 180.0f);
    }
}

// The soft glow that stands in for the beam when the shader is not in use.
static float Glow(float x, float y, float z)
{
    return (g_flash.light && g_flash.glow) ? Flashlight_GlowAt(g_flash.light, g_flash.eye, (Vector3){ x, y, z }) : 0.0f;
}

// Can the beam (or the glow) reach this quad? Tested on a sphere around it: out of range, or outside the cone widened by the
// angle the sphere spans from the light.
static bool QuadInLight(const Vector3 p[4])
{
    if (!g_flash.light) return false;
    Vector3 c = { (p[0].x + p[1].x + p[2].x + p[3].x) * 0.25f, (p[0].y + p[1].y + p[2].y + p[3].y) * 0.25f, (p[0].z + p[1].z + p[2].z + p[3].z) * 0.25f };
    float r = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float dx = p[i].x - c.x, dy = p[i].y - c.y, dz = p[i].z - c.z;
        const float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > r) r = d;
    }
    const float vx = c.x - g_flash.eye.x, vy = c.y - g_flash.eye.y, vz = c.z - g_flash.eye.z;
    const float d = sqrtf(vx * vx + vy * vy + vz * vz);
    if (g_flash.glow && d - r < 6.0f) return true;              // within reach of the glow
    if (d - r > g_flash.range) return false;
    if (d <= r) return true;                                    // the light is inside the sphere
    const Vector3 dir = g_flash.light->dir;
    const float cosA = (vx * dir.x + vy * dir.y + vz * dir.z) / d;
    const float spread = asinf(r / d > 1.0f ? 1.0f : r / d);
    const float limit = g_flash.outerRad + spread;
    return limit >= 3.14159265f || cosA >= cosf(limit);
}

static bool Dark(const Color c[4], const Vector3 p[4])
{
    if (g_keepDark) return false;
    for (int i = 0; i < 4; ++i) if (c[i].r >= DARK_BELOW || c[i].g >= DARK_BELOW || c[i].b >= DARK_BELOW) return false;
    return !QuadInLight(p);          // black, and the flashlight cannot reach it: nothing to draw
}

// A flat tile surface at height y: floor faces up, ceiling faces down.
static void DrawSurfaceTile(const FloorMap *f, int x, int z, float y, Color albedo, float shade, bool up)
{
    const int ci[4] = { x, x + 1, x + 1, x };
    const int cj[4] = { z, z, z + 1, z + 1 };
    Vector3 p[4];
    Color c[4];
    for (int i = 0; i < 4; ++i) {
        const float wx = CornerX(ci[i]), wz = CornerX(cj[i]);
        const float ao = 1.0f - AO_PER_SOLID * (float)SolidAround(f, ci[i], cj[i]);
        p[i] = (Vector3){ wx, y, wz };
        c[i] = Surface(albedo, shade, Lighting_Corner(ci[i], cj[i]) + Glow(wx, y, wz), ao, wx, wz);
    }
    if (Dark(c, p)) return;
    EmitQuad(p, c, (Vector3){ 0.0f, up ? 1.0f : -1.0f, 0.0f });
}

// ---- ceiling tiles with a light in them -----------------------------------------------------------
// A light is part of the ceiling. The tile is cut into nine rectangles that touch along their edges and never overlap:
// four of ceiling around the frame, four of dark frame around the panel, and the glowing panel. Nothing is laid over
// anything else, so the picture does not depend on how finely the phone's depth buffer can tell two nearby surfaces
// apart (an earlier version laid a frame and a panel 2 cm and 4 cm below the ceiling, and on the phone the dark
// frame won most of the panel back, leaving slivers of white).
static const Color LIGHT_FRAME = { 46, 43, 34, 255 };

static Color Lerp4(const Color c[4], float u, float v)
{
    // c[0] at (u 0, v 0), c[1] at (1, 0), c[2] at (1, 1), c[3] at (0, 1)
    const float w0 = (1.0f - u) * (1.0f - v), w1 = u * (1.0f - v), w2 = u * v, w3 = (1.0f - u) * v;
    return (Color){ (unsigned char)((float)c[0].r * w0 + (float)c[1].r * w1 + (float)c[2].r * w2 + (float)c[3].r * w3),
                    (unsigned char)((float)c[0].g * w0 + (float)c[1].g * w1 + (float)c[2].g * w2 + (float)c[3].g * w3),
                    (unsigned char)((float)c[0].b * w0 + (float)c[1].b * w1 + (float)c[2].b * w2 + (float)c[3].b * w3) , 255 };
}

// A glowing panel stays bright until well into the dark, so a lit corridor can be seen from its far end,
// then fades out with the rest of the world.
static float GlowFade(float x, float z)
{
    const float dx = x - g_fadeViewer.x, dz = z - g_fadeViewer.z;
    const float d = sqrtf(dx * dx + dz * dz);
    const float start = g_fadeEnd * 0.65f;
    if (d <= start) return 1.0f;
    const float k = 1.0f - (d - start) / (g_fadeEnd - start);
    return k < 0.0f ? 0.0f : k;
}

typedef struct { float x0, z0, x1, z1; } Rect;

static void CeilingRect(const Color tile[4], Rect t, Rect r, int kind, Color flat, float y)
{
    // kind 0: ceiling, interpolated from the tile's corner colours; 1: a flat colour that is faded per corner
    const float xs[4] = { r.x0, r.x1, r.x1, r.x0 }, zs[4] = { r.z0, r.z0, r.z1, r.z1 };
    Vector3 p[4];
    Color c[4];
    for (int i = 0; i < 4; ++i) {
        p[i] = (Vector3){ xs[i], y, zs[i] };
        if (kind == 0) {
            c[i] = Lerp4(tile, (xs[i] - t.x0) / (t.x1 - t.x0), (zs[i] - t.z0) / (t.z1 - t.z0));
        } else {
            const float k = kind == 1 ? FadeAt(xs[i], zs[i]) : GlowFade(xs[i], zs[i]);
            c[i] = (Color){ (unsigned char)((float)flat.r * k), (unsigned char)((float)flat.g * k), (unsigned char)((float)flat.b * k), 255 };
        }
    }
    EmitQuad(p, c, (Vector3){ 0.0f, -1.0f, 0.0f });
}

static void DrawCeilingWithPanel(const FloorMap *f, int x, int z, float y, const Fixture *fx)
{
    // The tile's own corner colours, as for any ceiling tile.
    const int ci[4] = { x, x + 1, x + 1, x };
    const int cj[4] = { z, z, z + 1, z + 1 };
    Color tile[4];
    for (int i = 0; i < 4; ++i) {
        const float ao = 1.0f - AO_PER_SOLID * (float)SolidAround(f, ci[i], cj[i]);
        tile[i] = Surface(CEILING_TILE, SHADE_CEILING, Lighting_Corner(ci[i], cj[i]) + Glow(CornerX(ci[i]), y, CornerX(cj[i])), ao, CornerX(ci[i]), CornerX(cj[i]));
    }
    const Rect T = { CornerX(x), CornerX(z), CornerX(x + 1), CornerX(z + 1) };
    const float hl = (fx->alongX ? LIGHT_PANEL_LEN : LIGHT_PANEL_WID) * 0.5f, hw = (fx->alongX ? LIGHT_PANEL_WID : LIGHT_PANEL_LEN) * 0.5f;
    const Rect P = { fx->x - hl, fx->z - hw, fx->x + hl, fx->z + hw };
    const Rect F = { P.x0 - LIGHT_FRAME_RIM, P.z0 - LIGHT_FRAME_RIM, P.x1 + LIGHT_FRAME_RIM, P.z1 + LIGHT_FRAME_RIM };
    const Color none = { 0, 0, 0, 255 };

    // Ceiling around the frame
    CeilingRect(tile, T, (Rect){ T.x0, T.z0, T.x1, F.z0 }, 0, none, y);
    CeilingRect(tile, T, (Rect){ T.x0, F.z1, T.x1, T.z1 }, 0, none, y);
    CeilingRect(tile, T, (Rect){ T.x0, F.z0, F.x0, F.z1 }, 0, none, y);
    CeilingRect(tile, T, (Rect){ F.x1, F.z0, T.x1, F.z1 }, 0, none, y);
    // Frame around the panel
    CeilingRect(tile, T, (Rect){ F.x0, F.z0, F.x1, P.z0 }, 1, LIGHT_FRAME, y);
    CeilingRect(tile, T, (Rect){ F.x0, P.z1, F.x1, F.z1 }, 1, LIGHT_FRAME, y);
    CeilingRect(tile, T, (Rect){ F.x0, P.z0, P.x0, P.z1 }, 1, LIGHT_FRAME, y);
    CeilingRect(tile, T, (Rect){ P.x1, P.z0, F.x1, P.z1 }, 1, LIGHT_FRAME, y);
    // The panel itself
    float r, g, b;
    Lighting_PanelColour(fx, &r, &g, &b);
    CeilingRect(tile, T, P, 2, (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 }, y);
}

// One face of a wall: corners a and b at the foot, c and d at the top, over the corners (i0, j0) and (i1, j1).
static void DrawWallFace(const FloorMap *f, int i0, int j0, int i1, int j1, float y0, float y1, Vector3 outward, Color albedo, float shade)
{
    const float x0 = CornerX(i0), z0 = CornerX(j0), x1 = CornerX(i1), z1 = CornerX(j1);
    const float l0 = Lighting_Corner(i0, j0), l1 = Lighting_Corner(i1, j1);
    const float a0 = SolidAround(f, i0, j0) == 3 ? AO_CONCAVE_WALL : 1.0f;
    const float a1 = SolidAround(f, i1, j1) == 3 ? AO_CONCAVE_WALL : 1.0f;
    const Vector3 p[4] = { { x0, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z0 } };
    const Color c[4] = {
        Surface(albedo, shade, l0 * WALL_FOOT_LIGHT + Glow(x0, y0, z0), a0, x0, z0), Surface(albedo, shade, l1 * WALL_FOOT_LIGHT + Glow(x1, y0, z1), a1, x1, z1),
        Surface(albedo, shade, l1 + Glow(x1, y1, z1), a1, x1, z1),                   Surface(albedo, shade, l0 + Glow(x0, y1, z0), a0, x0, z0)
    };
    if (Dark(c, p)) return;
    EmitQuad(p, c, outward);
}

// Draws the faces of wall tile (x, z) that touch open space. Each face is drawn once and is its own
// quad, so there are no hidden faces and no two surfaces share a plane.
static void DrawWallFaces(const FloorMap *f, int x, int z, float y0, float y1, Color base)
{
    if (!Floor_Solid(f, 0, x + 1, z)) DrawWallFace(f, x + 1, z, x + 1, z + 1, y0, y1, (Vector3){ 1, 0, 0 }, base, SHADE_PLUS_X);
    if (!Floor_Solid(f, 0, x - 1, z)) DrawWallFace(f, x, z, x, z + 1, y0, y1, (Vector3){ -1, 0, 0 }, base, SHADE_MINUS_X);
    if (!Floor_Solid(f, 0, x, z + 1)) DrawWallFace(f, x, z + 1, x + 1, z + 1, y0, y1, (Vector3){ 0, 0, 1 }, base, SHADE_PLUS_Z);
    if (!Floor_Solid(f, 0, x, z - 1)) DrawWallFace(f, x, z, x + 1, z, y0, y1, (Vector3){ 0, 0, -1 }, base, SHADE_MINUS_Z);
}

static Color FloorColour(int x, int z)
{
    if (x >= 37 && x <= 43 && z >= 1 && z <= 7)   return FLOOR_UP_ALCOVE;
    if (x >= 37 && x <= 43 && z >= 37 && z <= 43) return FLOOR_DOWN_ALCOVE;
    return FLOOR_CARPET;
}

static void DrawView(const FloorMap *floor, Vector3 viewer, float yOffset)
{
    const int vx = (int)(viewer.x / TILE_SIZE + 0.5f);
    const int vz = (int)(viewer.z / TILE_SIZE + 0.5f);
    const float maxDist = (float)DRAW_RADIUS_TILES * TILE_SIZE;
    g_fadeViewer = viewer;
    g_fadeEnd = maxDist - 1.5f;   // a surface dropped at maxDist is already fully black at its nearest vertex
    const float ceilY = yOffset + Floor_RoofY(floor);
    const float midY = yOffset + Floor_RoofY(floor) * 0.5f;

    for (int z = vz - DRAW_RADIUS_TILES; z <= vz + DRAW_RADIUS_TILES; ++z) {
        if (z < 0 || z >= Floor_Depth(floor)) continue;
        for (int x = vx - DRAW_RADIUS_TILES; x <= vx + DRAW_RADIUS_TILES; ++x) {
            if (x < 0 || x >= Floor_Width(floor)) continue;
            const float tx = (float)x * TILE_SIZE, tz = (float)z * TILE_SIZE;
            const float dx = tx - viewer.x, dz = tz - viewer.z;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d > maxDist) continue;

            if (Floor_Solid(floor, 0, x, z)) {
                if (!Exposed(floor, x, z)) continue;
                if (!InView(tx, midY, tz, 3.4f)) continue;      // a wall tile reaches 3 m up and down from its middle
                Color base = COL_WALL_SHELL;
                const uint8_t variant = Floor_Variant(floor, 0, x, z);
                if (variant == WALL_PARTITION) base = COL_WALL_PARTITION;
                else if (variant == WALL_PILLAR) base = COL_WALL_PILLAR;
                DrawWallFaces(floor, x, z, yOffset, yOffset + Floor_WallTopY(floor, 0, x, z), base);
            } else {
                // Open ground: a floor tile, and a ceiling tile above it wherever there is a ceiling.
                // Beyond the fade distance both are black, so they are not drawn at all.
                if (d - 1.5f > g_fadeEnd) continue;
                const bool fallen = g_collapseR >= 0.0f && hypotf(tx - g_collapseX, tz - g_collapseZ) > g_collapseR;
                if (!fallen && InView(tx, yOffset, tz, 1.5f)) DrawSurfaceTile(floor, x, z, yOffset, FloorColour(x, z), SHADE_FLOOR, true);
                if (Floor_HasCeiling(floor, x, z) && InView(tx, ceilY, tz, 1.5f)) {
                    const Fixture *panel = Lighting_PanelOnTile(x, z);
                    if (panel) DrawCeilingWithPanel(floor, x, z, ceilY, panel);
                    else       DrawSurfaceTile(floor, x, z, ceilY, CEILING_TILE, SHADE_CEILING, false);
                }
            }
        }
    }
}

void DrawFloorMapView(const FloorMap *floor, Vector3 eye, Vector3 forward, float yOffset)
{
    const float l = sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    g_cull = l > 0.5f;
    if (g_cull) {
        g_eye = eye;
        g_fwd = (Vector3){ forward.x / l, forward.y / l, forward.z / l };
        g_coneCos = cosf(VIEW_CONE_DEG * (3.14159265f / 180.0f));
        g_coneSin = sinf(VIEW_CONE_DEG * (3.14159265f / 180.0f));
    }
    DrawView(floor, eye, yOffset);
}

void DrawFloorMapFacing(const FloorMap *floor, Vector3 viewer, float fwdX, float fwdZ, float yOffset)
{
    // The old flat version: the camera is taken to be level, at head height over the viewer.
    DrawFloorMapView(floor, (Vector3){ viewer.x, yOffset + 1.8f, viewer.z }, (Vector3){ fwdX, 0.0f, fwdZ }, yOffset);
}

void DrawFloorMap(const FloorMap *floor, Vector3 viewer, float yOffset)
{
    DrawFloorMapView(floor, viewer, (Vector3){ 0.0f, 0.0f, 0.0f }, yOffset);
}
