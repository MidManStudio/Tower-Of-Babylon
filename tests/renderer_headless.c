// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "renderer_headless.c"
// ============================================================================
// Records what DrawFloorMapFacing asks raylib to draw and checks it. Inside a macro so
// mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DRENDERER_HEADLESS_TEST -Isrc -Iraylib/src tests/renderer_headless.c src/dungeon/dungeon_renderer.c src/dungeon/dungeon_raycast.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c src/lighting/lighting.c src/flashlight/flashlight.c src/config/game_config.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c src/weapons/weapons.c src/projectiles/projectile_config.c src/projectiles/projectile_patterns.c -lm -o renderer_headless
// Run:
//   ./renderer_headless                       checks only
//   RENDER_DUMP=out.txt ./renderer_headless   also writes the triangles of one view
#ifdef RENDERER_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "dungeon/dungeon_generator.h"
#include "dungeon/dungeon_raycast.h"
#include "dungeon/floor_access.h"
#include "dungeon/dungeon_renderer.h"
#include "config/game_config.h"
#include "flashlight/flashlight.h"
#include "lighting/lighting.h"

#define MAX_REC 400000
typedef struct { Vector3 p[4]; Color col[4]; } Quad;
static Quad g_quads[MAX_REC];
static int  g_nQuads = 0;

static Color g_curCol; static Vector3 g_vtx[4]; static Color g_vcol[4]; static int g_nv = 0;
void rlBegin(int mode) { (void)mode; g_nv = 0; }
void rlEnd(void) {}
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { g_curCol = (Color){ r, g, b, a }; }
void rlVertex3f(float x, float y, float z)
{
    g_vtx[g_nv] = (Vector3){ x, y, z }; g_vcol[g_nv] = g_curCol;
    if (++g_nv == 4) {
        if (g_nQuads < MAX_REC) { for (int i = 0; i < 4; ++i) { g_quads[g_nQuads].p[i] = g_vtx[i]; g_quads[g_nQuads].col[i] = g_vcol[i]; } ++g_nQuads; }
        g_nv = 0;
    }
}
bool rlCheckRenderBatchLimit(int n) { (void)n; return false; }
void rlDrawRenderBatchActive(void) {}

static FloorMap g_floor;
static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static Vector3 Normal(const Quad *q)
{
    const Vector3 e1 = { q->p[1].x - q->p[0].x, q->p[1].y - q->p[0].y, q->p[1].z - q->p[0].z };
    const Vector3 e2 = { q->p[2].x - q->p[0].x, q->p[2].y - q->p[0].y, q->p[2].z - q->p[0].z };
    return (Vector3){ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
}
static Vector3 Centre(const Quad *q)
{
    return (Vector3){ (q->p[0].x + q->p[1].x + q->p[2].x + q->p[3].x) * 0.25f, (q->p[0].y + q->p[1].y + q->p[2].y + q->p[3].y) * 0.25f,
                      (q->p[0].z + q->p[1].z + q->p[2].z + q->p[3].z) * 0.25f };
}
static bool Horizontal(const Quad *q) { return fabsf(q->p[0].y - q->p[1].y) < 1e-4f && fabsf(q->p[0].y - q->p[2].y) < 1e-4f && fabsf(q->p[0].y - q->p[3].y) < 1e-4f; }
static int Lum(Color c) { return c.r + c.g + c.b; }

static int TileAt(float v) { return (int)floorf((v + TILE_SIZE * 0.5f) / TILE_SIZE); }

static void Draw(Vector3 viewer, float fx, float fz) { g_nQuads = 0; DrawFloorMapFacing(&g_floor, viewer, fx, fz, 0.0f); }

int main(void)
{
    GenerateFloorMap(&g_floor, 1, 2026u);
    Lighting_BuildForFloor(&g_floor, 1);
    Lighting_Update(10.0f);
    const Vector3 viewer = { g_floor.startX, 1.8f, g_floor.startZ };
    const float maxDist = 18.0f * TILE_SIZE;

    printf("Counts\n");
    DrawFloorMapKeepDark(true);          // count every surface, including the black ones
    Draw(viewer, 0.0f, 0.0f);
    int wallQuads = 0, floorQuads = 0, ceilQuads = 0, otherQuads = 0;
    double ceilArea = 0.0;
    for (int i = 0; i < g_nQuads; ++i) {
        const Quad *q = &g_quads[i];
        if (!Horizontal(q)) { ++wallQuads; continue; }
        const float y = q->p[0].y;
        if (fabsf(y) < 1e-6f) ++floorQuads;
        else if (fabsf(y - WALL_HEIGHT) < 1e-6f) {
            ++ceilQuads;
            ceilArea += (double)fabsf((q->p[1].x - q->p[0].x) * (q->p[2].z - q->p[1].z));
        } else ++otherQuads;
    }
    int expWalls = 0, expFloor = 0, expCeil = 0, ceilTiles = 0, panelTiles = 0;
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            const float dx = (float)x * TILE_SIZE - viewer.x, dz = (float)z * TILE_SIZE - viewer.z;
            if (sqrtf(dx * dx + dz * dz) > maxDist) continue;
            if (g_floor.grid[z][x].type == TILE_WALL) {
                expWalls += !Tile_IsSolid(&g_floor, x + 1, z) + !Tile_IsSolid(&g_floor, x - 1, z) + !Tile_IsSolid(&g_floor, x, z + 1) + !Tile_IsSolid(&g_floor, x, z - 1);
            } else {
                ++expFloor;
                if (Floor_HasCeiling(&g_floor, x, z)) {
                    ++ceilTiles;
                    if (Lighting_PanelOnTile(x, z)) { expCeil += 9; ++panelTiles; } else ++expCeil;
                }
            }
        }
    Check(wallQuads == expWalls, "every wall face that touches open space is drawn once, as one quad");
    Check(floorQuads == expFloor, "every open tile in range has a floor quad");
    Check(ceilQuads == expCeil, "and a ceiling quad wherever there is a ceiling (not over the stair alcoves), nine where a light hangs");
    Check(panelTiles > 5, "there are lights in view");
    Check(otherQuads == 0, "nothing else is drawn: no surface is laid over another");
    Check(fabs(ceilArea - 4.0 * (double)ceilTiles) < 0.05, "the ceiling pieces add up to exactly the ceiling: no overlap and no gap, lights included");

    // With dark quads dropped (the default), fewer are drawn, and what is missing is black.
    {
        static Quad withDark[MAX_REC];
        const int nWith = g_nQuads;
        for (int i = 0; i < nWith; ++i) withDark[i] = g_quads[i];
        DrawFloorMapKeepDark(false);
        Draw(viewer, 0.0f, 0.0f);
        const int nWithout = g_nQuads;
        int blackMissing = 1;
        // every quad in the full list that has no twin in the short list must be black at all four corners
        for (int i = 0; i < nWith; ++i) {
            const Vector3 c = Centre(&withDark[i]);
            int found = 0;
            for (int j = 0; j < nWithout && !found; ++j) {
                const Vector3 d = Centre(&g_quads[j]);
                if (fabsf(d.x - c.x) < 1e-3f && fabsf(d.y - c.y) < 1e-3f && fabsf(d.z - c.z) < 1e-3f) found = 1;
            }
            if (!found) for (int k = 0; k < 4; ++k) if (withDark[i].col[k].r >= 3 || withDark[i].col[k].g >= 3 || withDark[i].col[k].b >= 3) blackMissing = 0;
        }
        printf("    %d quads in range, %d once the black ones are dropped\n", nWith, nWithout);
        Check(nWithout < nWith && nWithout > nWith / 3, "the black quads of the dead patches are dropped, and a good part of the floor still draws");
        Check(blackMissing, "and every dropped quad was black at all four corners");
        DrawFloorMapKeepDark(true);
        Draw(viewer, 0.0f, 0.0f);
    }

    printf("Winding and surfaces\n");
    int upOk = 1, downOk = 1, wallOk = 1, wallCoplanar = 1;
    for (int i = 0; i < g_nQuads; ++i) {
        const Quad *q = &g_quads[i];
        const Vector3 n = Normal(q), c = Centre(q);
        if (Horizontal(q)) {
            const float y = q->p[0].y;
            if (fabsf(y) < 1e-6f && n.y <= 0.0f) upOk = 0;
            if (fabsf(y - WALL_HEIGHT) < 1e-6f && n.y >= 0.0f) downOk = 0;
        } else {
            // A wall face must face the open tile next to it: step half a tile out along the normal.
            const float l = sqrtf(n.x * n.x + n.z * n.z);
            if (l < 1e-6f) { wallOk = 0; continue; }
            const float px = c.x + n.x / l * 1.0f, pz = c.z + n.z / l * 1.0f;
            if (Tile_IsSolid(&g_floor, TileAt(px), TileAt(pz))) wallOk = 0;
            if (fabsf(n.y) > 1e-4f * l) wallCoplanar = 0;
        }
    }
    Check(upOk, "floor quads face up");
    Check(downOk, "ceiling quads, frames and panels face down");
    Check(wallOk, "every wall face is wound so its front is toward the open tile it borders");
    Check(wallCoplanar, "wall faces are vertical");

    // No two quads of the same height may cover the same ground: compare each ceiling and floor quad with the others
    // that share its tile.
    int overlaps = 0;
    for (int i = 0; i < g_nQuads && i < 3000; ++i) {
        if (!Horizontal(&g_quads[i])) continue;
        const Quad *a = &g_quads[i];
        for (int j = i + 1; j < g_nQuads; ++j) {
            const Quad *b = &g_quads[j];
            if (!Horizontal(b) || fabsf(b->p[0].y - a->p[0].y) > 1e-6f) continue;
            const float ox = fminf(fmaxf(a->p[0].x, a->p[1].x), fmaxf(b->p[0].x, b->p[1].x)) - fmaxf(fminf(a->p[0].x, a->p[1].x), fminf(b->p[0].x, b->p[1].x));
            const float oz = fminf(fmaxf(a->p[0].z, a->p[2].z), fmaxf(b->p[0].z, b->p[2].z)) - fmaxf(fminf(a->p[0].z, a->p[2].z), fminf(b->p[0].z, b->p[2].z));
            if (ox > 1e-3f && oz > 1e-3f) ++overlaps;
        }
    }
    Check(overlaps == 0, "no two floor quads, and no two ceiling quads, cover the same ground");

    // A glowing panel is drawn, in its colour, and a dead one is dark.
    int brightPanel = 0, deadPanel = 0;
    for (int i = 0; i < g_nQuads; ++i) {
        const Quad *q = &g_quads[i];
        if (!Horizontal(q) || fabsf(q->p[0].y - WALL_HEIGHT) > 1e-6f) continue;
        const float w = fabsf(q->p[1].x - q->p[0].x), d = fabsf(q->p[2].z - q->p[1].z);
        const bool panelSized = (fabsf(w - LIGHT_PANEL_LEN) < 1e-3f && fabsf(d - LIGHT_PANEL_WID) < 1e-3f) || (fabsf(w - LIGHT_PANEL_WID) < 1e-3f && fabsf(d - LIGHT_PANEL_LEN) < 1e-3f);
        if (!panelSized) continue;
        if (Lum(q->col[0]) > 500) ++brightPanel;
        if (Lum(q->col[0]) < 120) ++deadPanel;
    }
    Check(brightPanel > 0, "lit panels glow (a bright quad of panel size)");
    Check(deadPanel > 0, "and dead ones are dark");

    printf("Fade\n");
    int farLit = 0, nearLit = 0;
    for (int i = 0; i < g_nQuads; ++i) {
        for (int k = 0; k < 4; ++k) {
            const float dx = g_quads[i].p[k].x - viewer.x, dz = g_quads[i].p[k].z - viewer.z;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d > maxDist - 1.5f && Lum(g_quads[i].col[k]) > 0 && g_quads[i].p[k].y < WALL_HEIGHT - 0.01f) ++farLit;
            if (d < 4.0f && Lum(g_quads[i].col[k]) > 0) ++nearLit;
        }
    }
    Check(farLit == 0, "surfaces at the edge of the draw distance are black (no pop at the edge)");
    Check(nearLit > 0, "and surfaces near the viewer are not");

    printf("Light\n");
    // Brightest and dimmest floor quads.
    int bright = -1, brightL = -1, dimL = 1 << 30;
    for (int i = 0; i < g_nQuads; ++i) {
        const Quad *q = &g_quads[i];
        if (!Horizontal(q) || fabsf(q->p[0].y) > 1e-4f) continue;
        const Vector3 c = Centre(q);
        const float dx = c.x - viewer.x, dz = c.z - viewer.z;
        if (sqrtf(dx * dx + dz * dz) > 20.0f || c.x > (float)STAIR_WALL_X * TILE_SIZE - 2.0f) continue;   // the stair alcoves have their own floor tints
        const int l = Lum(q->col[0]) + Lum(q->col[1]) + Lum(q->col[2]) + Lum(q->col[3]);
        if (l > brightL) { brightL = l; bright = i; }
        if (l < dimL) dimL = l;
    }
    Check(bright >= 0 && brightL > 4 * 100 && brightL > dimL, "near the arrival the floor is clearly lit (the arrival is always lit) and not all one brightness");
    if (bright >= 0) {
        const Color c = g_quads[bright].col[0];
        Check(c.r > c.g && c.g > c.b, "lit surfaces are warm: red over green over blue");
    }
    // Walls are lighter at the top, where the lamps are: compare the two vertices over each foot point.
    int topBrighter = 0, topDarker = 0;
    for (int i = 0; i < g_nQuads; ++i) {
        const Quad *q = &g_quads[i];
        if (Horizontal(q)) continue;
        for (int a = 0; a < 4; ++a) {
            if (q->p[a].y > 0.01f) continue;
            for (int b = 0; b < 4; ++b) {
                if (q->p[b].y < WALL_HEIGHT - 0.01f || fabsf(q->p[b].x - q->p[a].x) > 1e-4f || fabsf(q->p[b].z - q->p[a].z) > 1e-4f) continue;
                if (Lum(q->col[b]) > Lum(q->col[a])) ++topBrighter;
                if (Lum(q->col[b]) < Lum(q->col[a])) ++topDarker;
            }
        }
    }
    Check(topBrighter > 0 && topDarker == 0, "a wall is never lighter at its foot than at its top");

    printf("Culling\n");
    Draw(viewer, 1.0f, 0.0f);
    const int facing = g_nQuads;
    int behind = 0;
    for (int i = 0; i < g_nQuads; ++i) if (Centre(&g_quads[i]).x < viewer.x - TILE_SIZE * 3.0f - 1.5f) ++behind;
    Draw(viewer, 0.0f, 0.0f);
    const int all = g_nQuads;
    Check(facing < all * 7 / 10, "looking one way draws well under 70% of what looking all round would");
    Check(behind == 0, "and nothing that is wholly behind the viewer");

    // The same with the real 3D look direction, at several pitches. The eye is at head height.
    {
        const Vector3 eye = { viewer.x - 30.0f, 1.8f, viewer.z - 22.0f };
        g_nQuads = 0; DrawFloorMapView(&g_floor, eye, (Vector3){ 0, 0, 0 }, 0.0f);
        static Quad everything[MAX_REC];
        const int nAll = g_nQuads;
        for (int i = 0; i < nAll; ++i) everything[i] = g_quads[i];
        const float pitches[4] = { 0.0f, 0.8f, -0.8f, 1.5707963f };
        const char *names[4] = { "level", "up 46 degrees", "down 46 degrees", "straight up" };
        for (int k = 0; k < 4; ++k) {
            const float cp = cosf(pitches[k]), sp = sinf(pitches[k]);
            const Vector3 fwd = { 0.7071f * cp, sp, 0.7071f * cp };           // looking diagonally
            g_nQuads = 0; DrawFloorMapView(&g_floor, eye, fwd, 0.0f);
            const int nCulled = g_nQuads;
            // Every quad whose centre is inside the real camera's view (90 degrees across, 66 high, sprint field of view) must still be drawn.
            const Vector3 right = { fwd.z, 0.0f, -fwd.x };
            const float rl = sqrtf(right.x * right.x + right.z * right.z);
            Vector3 rr = rl > 1e-4f ? (Vector3){ right.x / rl, 0.0f, right.z / rl } : (Vector3){ 1.0f, 0.0f, 0.0f };
            const Vector3 up = { rr.y * fwd.z - rr.z * fwd.y, rr.z * fwd.x - rr.x * fwd.z, rr.x * fwd.y - rr.y * fwd.x };
            int missing = 0, visibleNeeded = 0;
            for (int i = 0; i < nAll; ++i) {
                const Vector3 c = Centre(&everything[i]);
                const Vector3 v = { c.x - eye.x, c.y - eye.y, c.z - eye.z };
                const float depth = v.x * fwd.x + v.y * fwd.y + v.z * fwd.z;
                if (depth < 0.25f) continue;
                const float sx = (v.x * rr.x + v.y * rr.y + v.z * rr.z) / depth, sy = (v.x * up.x + v.y * up.y + v.z * up.z) / depth;
                if (fabsf(sx) > 1.0f || fabsf(sy) > 0.65f) continue;      // outside 90 degrees across, 66 up
                ++visibleNeeded;
                int found = 0;
                for (int j = 0; j < nCulled && !found; ++j) {
                    const Vector3 d = Centre(&g_quads[j]);
                    if (fabsf(d.x - c.x) < 1e-3f && fabsf(d.y - c.y) < 1e-3f && fabsf(d.z - c.z) < 1e-3f) found = 1;
                }
                if (!found) ++missing;
            }
            printf("    looking %s: %d of %d quads drawn, %d in view\n", names[k], nCulled, nAll, visibleNeeded);
            char what[160];
            snprintf(what, sizeof what, "looking %s: nothing that is on screen is dropped", names[k]);
            Check(missing == 0, what);
            snprintf(what, sizeof what, "looking %s: well under 75%% of everything is drawn", names[k]);
            Check(nCulled < nAll * 3 / 4, what);
        }
    }

    DrawFloorMapKeepDark(false);
    printf("Another floor, another place\n");
    GenerateFloorMap(&g_floor, 7, 99u);
    Lighting_BuildForFloor(&g_floor, 7);
    Lighting_Update(3.0f);
    Draw((Vector3){ g_floor.startX, 1.8f, g_floor.startZ }, -1.0f, 0.0f);
    printf("    (%d quads, start %.1f %.1f)\n", g_nQuads, g_floor.startX, g_floor.startZ);
    Check(g_nQuads > 100, "a second floor draws too");

    if (getenv("RENDER_DUMP")) {
        // RENDER_VIEW="floor x z fx fz time" picks the view (default: floor 1 from the arrival, looking south)
        int floorNo = 1; float vx = viewer.x, vz = viewer.z, vfx = 0.0f, vfz = 1.0f, vt = 10.0f;
        if (getenv("RENDER_VIEW")) sscanf(getenv("RENDER_VIEW"), "%d %f %f %f %f %f", &floorNo, &vx, &vz, &vfx, &vfz, &vt);
        GenerateFloorMap(&g_floor, floorNo, floorNo == 1 ? 2026u : 99u);
        Lighting_BuildForFloor(&g_floor, floorNo);
        Lighting_Update(vt);
        const Vector3 dumpViewer = { vx, 1.8f, vz };
        Draw(dumpViewer, vfx, vfz);
        FILE *f = fopen(getenv("RENDER_DUMP"), "w");
        if (f) {
            fprintf(f, "VIEWER %f %f %f %f %f\n", dumpViewer.x, dumpViewer.y, dumpViewer.z, vfx, vfz);
            for (int i = 0; i < g_nQuads; ++i) {
                const Quad *q = &g_quads[i];
                fprintf(f, "Q");
                for (int k = 0; k < 4; ++k) fprintf(f, " %f %f %f %d %d %d", q->p[k].x, q->p[k].y, q->p[k].z, q->col[k].r, q->col[k].g, q->col[k].b);
                fprintf(f, "\n");
            }
            fclose(f);
        }
    }

    printf("The flashlight\n");
    {
        GameConfig_LoadBaked();                                   // the beam's range and angle
        DrawFloorMapKeepDark(false);
        Flashlight fl;
        Flashlight_Init(&fl); Flashlight_Give(&fl, 120.0f); Flashlight_Toggle(&fl);
        const FlashlightConfig *fc = Flashlight_Config();
        // Try the beam from several places and directions on this floor.
        int bestExtra = -1; Vector3 bestEye = { 0 }; Vector3 bestDir = { 1, 0, 0 };
        int placesTested = 0, extraTotal = 0, darkInBeamMissed = 0, darkOutsideDrawn = 0, glowDarker = 0, glowBrighter = 0, glowCompared = 0, offDiffers = 0;
        const float dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int z = 3; z < MAP_HEIGHT - 3 && placesTested < 60; z += 4) {
            for (int x = 3; x < MAP_WIDTH - 3 && placesTested < 60; x += 4) {
                if (g_floor.grid[z][x].type == TILE_WALL) continue;
                const Vector3 eye = { (float)x * TILE_SIZE, 1.6f, (float)z * TILE_SIZE };
                for (int k = 0; k < 4; ++k) {
                    ++placesTested;
                    fl.dir = (Vector3){ dirs[k][0], 0.0f, dirs[k][1] };
                    fl.dirValid = true; fl.level = 1.0f;
                    // Everything, with the black quads (the reference), then what is drawn with no flashlight, then with it.
                    DrawFloorMapKeepDark(true);  DrawFloorMapFlashlight(NULL, eye, false);
                    g_nQuads = 0; DrawFloorMapFacing(&g_floor, eye, 0.0f, 0.0f, 0.0f);
                    static Quad all[MAX_REC / 4]; const int nAll = g_nQuads < MAX_REC / 4 ? g_nQuads : MAX_REC / 4;
                    for (int i = 0; i < nAll; ++i) all[i] = g_quads[i];
                    DrawFloorMapKeepDark(false);
                    g_nQuads = 0; DrawFloorMapFacing(&g_floor, eye, 0.0f, 0.0f, 0.0f);
                    const int nOff = g_nQuads;
                    DrawFloorMapFlashlight(&fl, eye, false);
                    g_nQuads = 0; DrawFloorMapFacing(&g_floor, eye, 0.0f, 0.0f, 0.0f);
                    const int nOn = g_nQuads;
                    extraTotal += nOn - nOff;
                    {
                        DungeonHit ahead = { 0 };   // for the picture only: a view with room to look into, not a wall in the face
                        const int room = !(Dungeon_Raycast(&g_floor, eye, (Vector3){ fl.dir.x, 0.0f, fl.dir.z }, 9.0f, &ahead) && ahead.hit);
                        if (room && nOn - nOff > bestExtra) { bestExtra = nOn - nOff; bestEye = eye; bestDir = fl.dir; }
                    }
                    // Every quad that is black and truly inside the beam (its centre within range and the outer angle) must be drawn.
                    for (int i = 0; i < nAll; ++i) {
                        const Quad *q = &all[i];
                        int black = 1;
                        for (int c = 0; c < 4; ++c) if (q->col[c].r >= 3 || q->col[c].g >= 3 || q->col[c].b >= 3) black = 0;
                        if (!black) continue;
                        const Vector3 cc = Centre(q);
                        const float vx = cc.x - eye.x, vy = cc.y - eye.y, vz = cc.z - eye.z, d = sqrtf(vx * vx + vy * vy + vz * vz);
                        const float cosA = d > 1e-3f ? (vx * fl.dir.x + vy * fl.dir.y + vz * fl.dir.z) / d : 1.0f;
                        const int inBeam = d < fc->range && cosA > cosf(fc->beamOuterDeg * 0.0174533f);
                        int drawn = 0;
                        for (int j = 0; j < nOn && !drawn; ++j) drawn = fabsf(g_quads[j].p[0].x - q->p[0].x) < 1e-4f && fabsf(g_quads[j].p[0].y - q->p[0].y) < 1e-4f
                                                                   && fabsf(g_quads[j].p[0].z - q->p[0].z) < 1e-4f && fabsf(g_quads[j].p[2].x - q->p[2].x) < 1e-4f && fabsf(g_quads[j].p[2].z - q->p[2].z) < 1e-4f;
                        if (inBeam && !drawn) ++darkInBeamMissed;
                    }
                    // A black quad that is nowhere near the beam must still be skipped: the count may not exceed
                    // (quads drawn with no light) + (black quads within reach of the beam's bounding cone).
                    int reach = 0;
                    for (int i = 0; i < nAll; ++i) {
                        const Quad *q = &all[i];
                        int black = 1;
                        for (int c = 0; c < 4; ++c) if (q->col[c].r >= 3 || q->col[c].g >= 3 || q->col[c].b >= 3) black = 0;
                        if (!black) continue;
                        const Vector3 cc = Centre(q);
                        const float vx = cc.x - eye.x, vy = cc.y - eye.y, vz = cc.z - eye.z, d = sqrtf(vx * vx + vy * vy + vz * vz);
                        const float cosA = d > 1e-3f ? (vx * fl.dir.x + vy * fl.dir.y + vz * fl.dir.z) / d : 1.0f;
                        // a quad is at most 6.5 m across (a wall tile's diagonal): generous margins
                        if (d < fc->range + 4.0f && (d < 5.0f || cosA > cosf(fc->beamOuterDeg * 0.0174533f + 1.0f))) ++reach;
                    }
                    if (nOn - nOff > reach) ++darkOutsideDrawn;
                    // With the light off, nothing changes.
                    Flashlight off = fl; off.level = 0.0f; off.on = false;
                    DrawFloorMapFlashlight(&off, eye, false);
                    g_nQuads = 0; DrawFloorMapFacing(&g_floor, eye, 0.0f, 0.0f, 0.0f);
                    if (g_nQuads != nOff) ++offDiffers;
                    // The glow fallback: quads near the player are lighter than without it.
                    DrawFloorMapFlashlight(NULL, eye, false);
                    g_nQuads = 0; DrawFloorMapFacing(&g_floor, eye, 0.0f, 0.0f, 0.0f);
                    static Quad plain[MAX_REC / 4]; const int nPlain = g_nQuads < MAX_REC / 4 ? g_nQuads : MAX_REC / 4;
                    for (int i = 0; i < nPlain; ++i) plain[i] = g_quads[i];
                    DrawFloorMapFlashlight(&fl, eye, true);
                    g_nQuads = 0; DrawFloorMapFacing(&g_floor, eye, 0.0f, 0.0f, 0.0f);
                    for (int i = 0; i < g_nQuads; ++i) {
                        for (int j = 0; j < nPlain; ++j) {
                            if (fabsf(plain[j].p[0].x - g_quads[i].p[0].x) > 1e-4f || fabsf(plain[j].p[0].y - g_quads[i].p[0].y) > 1e-4f || fabsf(plain[j].p[0].z - g_quads[i].p[0].z) > 1e-4f
                                || fabsf(plain[j].p[2].x - g_quads[i].p[2].x) > 1e-4f || fabsf(plain[j].p[2].z - g_quads[i].p[2].z) > 1e-4f) continue;
                            const Vector3 cc = Centre(&g_quads[i]);
                            const float dd = sqrtf((cc.x - eye.x) * (cc.x - eye.x) + (cc.y - eye.y) * (cc.y - eye.y) + (cc.z - eye.z) * (cc.z - eye.z));
                            if (dd < 4.0f) {
                                int a = 0, b = 0;
                                for (int c = 0; c < 4; ++c) { a += Lum(plain[j].col[c]); b += Lum(g_quads[i].col[c]); }
                                ++glowCompared;
                                if (b < a) ++glowDarker; else if (b > a) ++glowBrighter;
                            }
                            break;
                        }
                    }
                    DrawFloorMapFlashlight(NULL, eye, false);
                }
            }
        }
        printf("    %d view points tried, %d extra quads drawn in all\n", placesTested, extraTotal);
        // FLASH_DUMP=/tmp/fl writes /tmp/fl_off.txt and /tmp/fl_on.txt (every quad the renderer drew, with its corner colours) and
        // /tmp/fl_meta.txt (the eye and the look direction) for the view where the flashlight draws the most extra surfaces; the
        // picture is made offline by tools/render_dump_gles.py, through the real beam shader.
        if (getenv("FLASH_DUMP") && bestExtra > 0) {
            const char *base = getenv("FLASH_DUMP");
            Flashlight lit = fl; lit.dir = bestDir; lit.level = 1.0f; lit.on = true;
            Vector3 look3 = { bestDir.x, -0.12f, bestDir.z };
            const float ll = sqrtf(look3.x * look3.x + look3.y * look3.y + look3.z * look3.z);
            look3 = (Vector3){ look3.x / ll, look3.y / ll, look3.z / ll };
            lit.dir = look3;
            for (int pass = 0; pass < 2; ++pass) {
                DrawFloorMapFlashlight(pass ? &lit : NULL, bestEye, false);
                g_nQuads = 0; DrawFloorMapFacing(&g_floor, bestEye, 0.0f, 0.0f, 0.0f);
                char name[256]; snprintf(name, sizeof name, "%s_%s.txt", base, pass ? "on" : "off");
                FILE *o = fopen(name, "w");
                if (o) {
                    for (int i = 0; i < g_nQuads; ++i) {
                        fprintf(o, "Q");
                        for (int c = 0; c < 4; ++c) fprintf(o, " %.4f %.4f %.4f", (double)g_quads[i].p[c].x, (double)g_quads[i].p[c].y, (double)g_quads[i].p[c].z);
                        for (int c = 0; c < 4; ++c) fprintf(o, " %d %d %d", g_quads[i].col[c].r, g_quads[i].col[c].g, g_quads[i].col[c].b);
                        fprintf(o, "\n");
                    }
                    fclose(o);
                }
            }
            char name[256]; snprintf(name, sizeof name, "%s_meta.txt", base);
            FILE *o = fopen(name, "w");
            if (o) { fprintf(o, "%.4f %.4f %.4f %.5f %.5f %.5f\n", (double)bestEye.x, (double)bestEye.y, (double)bestEye.z, (double)look3.x, (double)look3.y, (double)look3.z); fclose(o); }
            printf("    wrote %s_off.txt, _on.txt and _meta.txt (%d extra quads in that view)\n", base, bestExtra);
            DrawFloorMapFlashlight(NULL, bestEye, false);
        }
        Check(extraTotal > 0, "with the flashlight on, surfaces the lighting left black are drawn where the beam reaches");
        Check(darkInBeamMissed == 0, "every black quad whose centre is inside the beam is drawn (none is left for the shader to miss)");
        Check(darkOutsideDrawn == 0, "and the extra quads are only ones within reach of the beam: the black quads elsewhere are still skipped");
        Check(offDiffers == 0, "a light that is off, or a dropout, draws exactly what no flashlight draws");
        Check(glowCompared > 100 && glowDarker == 0 && glowBrighter > glowCompared / 2, "the glow fallback makes the surfaces near the player lighter and none darker");
        DrawFloorMapFlashlight(NULL, viewer, false);
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
