// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lighting.md, section "lighting_headless.c"
// ============================================================================
// Checks the ceiling lights: where they hang, how many are dead, that light stops at walls, that dead
// zones are dark and lit ones are lit, and how the lights move. Inside a macro so mray's build, which
// compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DLIGHTING_HEADLESS_TEST -Isrc -Iraylib/src tests/lighting_headless.c src/lighting/lighting.c src/dungeon/dungeon_raycast.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o lighting_headless
// Run:
//   ./lighting_headless                      checks only
//   LIGHT_DUMP=out.txt ./lighting_headless   also writes the light of floor 1 as a grid
#ifdef LIGHTING_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "dungeon/dungeon_generator.h"
#include "dungeon/dungeon_raycast.h"
#include "lighting/lighting.h"

void rlBegin(int mode) { (void)mode; }
void rlEnd(void) {}
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { (void)r; (void)g; (void)b; (void)a; }
void rlVertex3f(float x, float y, float z) { (void)x; (void)y; (void)z; }
bool rlCheckRenderBatchLimit(int n) { (void)n; return false; }
void rlDrawRenderBatchActive(void) {}

static FloorMap g_floor;
static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static void Fill(FloorMap *f, int wallX)
{
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            f->grid[z][x].type = (x == 0 || z == 0 || x == MAP_WIDTH - 1 || z == MAP_HEIGHT - 1 || x == wallX) ? TILE_WALL : TILE_FLOOR;
            f->grid[z][x].variant = 0;
            f->grid[z][x].flags = 0;
        }
    f->seed = 5u; f->floorNumber = 1;
}

int main(void)
{
    printf("Placement\n");
    GenerateFloorMap(&g_floor, 1, 2026u);
    Lighting_BuildForFloor(&g_floor, 1);
    const int n = Lighting_FixtureCount();
    Check(n > 60 && n <= LIGHT_MAX_FIXTURES, "a floor has dozens of fixtures");
    int onOpen = 1, hasCeiling = 1, spacingOk = 1, panels = 0, lamps = 0;
    for (int i = 0; i < n; ++i) {
        const Fixture *f = Lighting_Fixture(i);
        if (!f->hasPanel) { ++lamps; continue; }
        ++panels;
        if (Tile_IsSolid(&g_floor, f->tx, f->tz)) onOpen = 0;
        if (!Dungeon_HasCeiling(f->tx, f->tz)) hasCeiling = 0;
        for (int j = 0; j < i; ++j) {
            const Fixture *g = Lighting_Fixture(j);
            if (!g->hasPanel) continue;
            if (abs(g->tx - f->tx) + abs(g->tz - f->tz) < 2) spacingOk = 0;
        }
    }
    Check(onOpen, "every panel hangs over an open tile");
    Check(hasCeiling, "and a tile that has a ceiling (none over the stair alcoves)");
    Check(spacingOk, "and no two panels sit on top of each other");
    Check(lamps == 2, "the two stair alcoves have one steady lamp each, with no panel");
    int lookup = 1, perTile = 0;
    for (int i = 0; i < n; ++i) { const Fixture *f = Lighting_Fixture(i); if (f->hasPanel) { if (Lighting_PanelOnTile(f->tx, f->tz) != f) lookup = 0; ++perTile; } }
    Check(lookup && perTile == panels, "each panel is found by the tile it hangs over, one per tile");
    Check(Lighting_PanelOnTile(-1, 3) == NULL && Lighting_PanelOnTile(3, 999) == NULL && Lighting_PanelOnTile(UP_PAD_X, UP_PAD_Z) == NULL, "and tiles without one, or off the map, have none");

    printf("Patches of light and dark\n");
    int total = 0, counts[LIGHT_MODE_COUNT] = { 0 };
    int patchRuns = 0, patchSame = 0;
    for (int floorNo = 1; floorNo <= 20; ++floorNo) {
        GenerateFloorMap(&g_floor, floorNo, 4000u + (unsigned)floorNo * 31u);
        Lighting_BuildForFloor(&g_floor, floorNo);
        for (int i = 0; i < Lighting_FixtureCount(); ++i) {
            const Fixture *f = Lighting_Fixture(i);
            if (!f->hasPanel) continue;
            ++counts[f->mode]; ++total;
            // Neighbouring fixtures (6 m apart) tend to share a mood: that is what makes dark patches.
            for (int j = 0; j < i; ++j) {
                const Fixture *g = Lighting_Fixture(j);
                if (!g->hasPanel) continue;
                if (abs(g->tx - f->tx) + abs(g->tz - f->tz) == LIGHT_LATTICE_TILES) {
                    ++patchRuns;
                    const int fa = f->mode == LIGHT_DEAD, ga = g->mode == LIGHT_DEAD;
                    if (fa == ga) ++patchSame;
                }
            }
        }
    }
    const float fOn = (float)counts[LIGHT_ON] / (float)total, fDim = (float)counts[LIGHT_DIM] / (float)total;
    const float fFl = (float)counts[LIGHT_FLICKER] / (float)total, fDead = (float)counts[LIGHT_DEAD] / (float)total;
    printf("    over 20 floors: on %.0f%%  dim %.0f%%  flicker %.0f%%  dead %.0f%%\n", fOn * 100, fDim * 100, fFl * 100, fDead * 100);
    Check(fOn > 0.25f && fOn < 0.65f, "about a third to a half of the lights work");
    Check(fDead > 0.15f && fDead < 0.50f, "a fifth to a half are dead");
    Check(fDim > 0.05f && fFl > 0.03f, "and there are dim ones and flickering ones too");
    Check(patchRuns > 0 && (float)patchSame / (float)patchRuns > 0.75f, "neighbours share a mood more often than not, so dead lights come in patches");

    printf("Light on the floor\n");
    GenerateFloorMap(&g_floor, 1, 2026u);
    Lighting_BuildForFloor(&g_floor, 1);
    Lighting_Update(10.0f);
    float maxL = 0.0f, minL = 1.0f; int dark = 0, lit = 0, open = 0;
    int wallsDark = 1, inRange = 1;
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            const float l = Lighting_Tile(x, z);
            if (l < 0.0f || l > 1.0f || !isfinite(l)) inRange = 0;
            if (Tile_IsSolid(&g_floor, x, z)) { if (l != 0.0f) wallsDark = 0; continue; }
            ++open;
            if (l > maxL) maxL = l;
            if (l < minL) minL = l;
            if (l < 0.05f) ++dark;
            if (l > 0.5f) ++lit;
        }
    Check(inRange, "light is between 0 and 1 everywhere");
    Check(wallsDark, "a wall tile holds no light of its own");
    Check(maxL > 0.8f, "under a working light the floor is nearly fully lit");
    Check((float)dark / (float)open > 0.10f, "at least a tenth of the open floor is in the dark");
    Check((float)lit / (float)open > 0.15f, "and at least a seventh is properly lit");
    int cornersOk = 1;
    for (int j = 0; j <= MAP_HEIGHT; ++j) for (int i = 0; i <= MAP_WIDTH; ++i) { const float c = Lighting_Corner(i, j); if (c < 0.0f || c > 1.0f || !isfinite(c)) cornersOk = 0; }
    Check(cornersOk, "and so is the light at every corner");
    Check(Lighting_Corner(-3, 4) == 0.0f && Lighting_Corner(4, 999) == 0.0f, "outside the map there is none");

    // The arrival points are always lit.
    const float up = Lighting_Tile(UP_APPROACH_X, UP_APPROACH_Z), dn = Lighting_Tile(DOWN_APPROACH_X, DOWN_APPROACH_Z);
    int arrivalsLit = 1;
    for (int floorNo = 1; floorNo <= 20; ++floorNo) {
        GenerateFloorMap(&g_floor, floorNo, 4000u + (unsigned)floorNo * 31u);
        Lighting_BuildForFloor(&g_floor, floorNo);
        for (int k = 0; k < 8; ++k) {
            Lighting_Update(0.37f * (float)k + 1.0f);
            if (Lighting_Tile(UP_PAD_X - 2, UP_PAD_Z) < 0.4f || Lighting_Tile(DOWN_PAD_X - 2, DOWN_PAD_Z) < 0.4f) arrivalsLit = 0;
        }
    }
    printf("    floor 1 up approach %.2f  down approach %.2f\n", up, dn);
    Check(arrivalsLit, "the ground by both stair alcoves is lit on all 20 floors, at all times tried");

    printf("Light does not go through walls\n");
    Fill(&g_floor, 12);
    Lighting_BuildForFloor(&g_floor, 1);
    Lighting_SetAllModes(LIGHT_DEAD);
    // Fixture on tile (11, 11): next to the wall at x = 12.
    int idx = -1;
    for (int i = 0; i < Lighting_FixtureCount(); ++i) if (Lighting_Fixture(i)->tx == 11 && Lighting_Fixture(i)->tz == 11) idx = i;
    Check(idx >= 0, "there is a fixture on tile (11, 11)");
    if (idx >= 0) {
        Lighting_SetMode(idx, LIGHT_ON);
        Lighting_Update(10.0f);
        Check(Lighting_Tile(11, 11) > 0.8f, "beneath it the floor is bright");
        Check(Lighting_Tile(9, 11) > Lighting_Tile(7, 11), "and it fades with distance");
        Check(Lighting_Tile(10, 13) > 0.1f, "and it lights the floor around it");
        Check(Lighting_Tile(13, 11) == 0.0f && Lighting_Tile(13, 10) == 0.0f && Lighting_Tile(14, 12) == 0.0f, "but the floor on the other side of the wall gets none");
        Check(Lighting_Tile(1, 1) == 0.0f, "and the dead rest of the floor is dark");
        Check(Lighting_Corner(12, 11) > 0.0f && Lighting_Corner(13, 11) == 0.0f, "corners at the wall: lit on its near face, dark on its far face");
        const float ap = Lighting_AtPoint((Vector3){ 22.0f, 0.0f, 22.0f });
        const float far = Lighting_AtPoint((Vector3){ 100.0f, 0.0f, 22.0f });
        Check(ap > 0.7f && far == 0.0f, "Lighting_AtPoint reads the light under the fixture and in the dark");
        const float mid = Lighting_AtPoint((Vector3){ 18.0f, 0.0f, 22.0f });
        Check(mid > far && mid < ap, "and in between it interpolates");
    }

    printf("Light moves\n");
    Fill(&g_floor, 12);
    Lighting_BuildForFloor(&g_floor, 1);
    int found[LIGHT_MODE_COUNT]; for (int m = 0; m < LIGHT_MODE_COUNT; ++m) found[m] = -1;
    for (int i = 0; i < Lighting_FixtureCount(); ++i) { const Fixture *f = Lighting_Fixture(i); if (f->hasPanel && found[f->mode] < 0) found[f->mode] = i; }
    // Make sure each mode is present by forcing three fixtures.
    Lighting_SetMode(0, LIGHT_ON); Lighting_SetMode(1, LIGHT_DIM); Lighting_SetMode(2, LIGHT_FLICKER); Lighting_SetMode(3, LIGHT_DEAD);
    float minV[4] = { 9, 9, 9, 9 }, maxV[4] = { -9, -9, -9, -9 }; int zero[4] = { 0 };
    const int samples = 4000;
    for (int k = 0; k < samples; ++k) {
        Lighting_Update(0.0173f * (float)k);
        for (int m = 0; m < 4; ++m) {
            const float v = Lighting_Fixture(m)->level;
            if (v < minV[m]) minV[m] = v;
            if (v > maxV[m]) maxV[m] = v;
            if (v < 0.15f) ++zero[m];
        }
    }
    printf("    on %.2f..%.2f   dim %.2f..%.2f   flicker %.2f..%.2f (off %d of %d)   dead %.2f..%.2f (off %d of %d)\n", minV[0], maxV[0], minV[1], maxV[1], minV[2], maxV[2], zero[2], samples, minV[3], maxV[3], zero[3], samples);
    Check(minV[0] > 0.9f && maxV[0] <= 1.0f, "a working light hums a little and never goes out");
    Check(minV[1] > 0.15f && maxV[1] < 0.45f, "a dim light stays weak");
    Check(minV[2] < 0.15f && maxV[2] > 0.85f, "a flickering light goes nearly out and comes back to nearly full");
    Check(zero[2] > samples / 50 && zero[2] < samples / 3, "but is out only a small part of the time");
    Check(zero[3] > samples * 9 / 10 && maxV[3] > 0.3f, "a dead light is dark nearly all the time, but twitches back now and then");

    printf("Tint and cost\n");
    float r, g, b;
    Lighting_Tint(0.0f, &r, &g, &b);
    Check(b > r && r < 0.1f, "with no light a surface is faintly blue and very dark");
    Lighting_Tint(1.0f, &r, &g, &b);
    Check(r > g && g > b && r > 0.9f, "in full light it is warm and bright");
    Fixture litF = { 0 }, deadF = { 0 }, weakF = { 0 };
    litF.level = 1.0f; deadF.level = 0.0f; weakF.level = 0.3f;
    float lr, lg, lb, dr, dg, db, wr, wg, wb;
    Lighting_PanelColour(&litF, &lr, &lg, &lb); Lighting_PanelColour(&deadF, &dr, &dg, &db); Lighting_PanelColour(&weakF, &wr, &wg, &wb);
    Check(lr > 240 && lg > 230 && lb > 200 && lr >= lg && lg >= lb, "a lit panel glows warm white");
    Check(dr < 40 && dg < 40 && db < 40, "a dead panel is dark grey");
    Check(wr > dr && wr < lr && wr > wg - 1 && wg > wb, "a dim panel is a weak amber between the two");
    GenerateFloorMap(&g_floor, 1, 2026u);
    Lighting_BuildForFloor(&g_floor, 1);
    const clock_t t0 = clock();
    for (int k = 0; k < 2000; ++k) Lighting_Update(0.016f * (float)k);
    const double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / 2000.0;
    printf("    Lighting_Update: %.3f ms per call on this machine\n", ms);
    Check(ms < 1.0, "updating the whole floor's light takes under a millisecond here");

    if (getenv("LIGHT_DUMP")) {
        Lighting_Update(10.0f);
        FILE *fp = fopen(getenv("LIGHT_DUMP"), "w");
        if (fp) {
            for (int z = 0; z < MAP_HEIGHT; ++z) {
                for (int x = 0; x < MAP_WIDTH; ++x) fprintf(fp, "%d ", Tile_IsSolid(&g_floor, x, z) ? -1 : (int)(Lighting_Tile(x, z) * 100.0f));
                fprintf(fp, "\n");
            }
            fclose(fp);
        }
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
