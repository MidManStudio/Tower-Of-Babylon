// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lighting.md, section "lighting.h and lighting.c"
// ============================================================================
#include "lighting.h"
#include "../dungeon/floor_access.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#define TONE_GAIN      2.0f
#define INFLUENCE_POOL LIGHT_POOL_ENTRIES     // 40000 were enough for 45 by 45; a 128 by 128 floor uses about 48,000 (6 bytes each)

static Fixture  g_fix[LIGHT_MAX_FIXTURES];
static int      g_nFix = 0;
static uint16_t g_poolIdx[INFLUENCE_POOL];   // offset of the tile in g_tileI, z * MAP_MAX + x (row stride of the array, under 16,384)
static float    g_poolW[INFLUENCE_POOL];
static int      g_poolN = 0;

static float g_tileI[MAP_MAX][MAP_MAX];           // summed light before the tone curve
static float g_tile[MAP_MAX][MAP_MAX];            // 0 to 1
static float g_corner[MAP_MAX + 1][MAP_MAX + 1];
static bool  g_open[MAP_MAX][MAP_MAX];            // an open tile: not a wall
static int   g_panelAt[MAP_MAX][MAP_MAX];         // index of the fixture whose panel hangs over the tile, or -1
static int   g_w = 0, g_h = 0;                 // the size of the floor built for; nothing is lit before the first one
static uint32_t g_seed = 1;

// ---- hashing ---------------------------------------------------------------------------------
static uint32_t Hash3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
static float Hash01(uint32_t a, uint32_t b, uint32_t c) { return (float)(Hash3(a, b, c) & 0xFFFFFFu) / 16777216.0f; }

// Smooth patches: value noise over a coarse lattice, so neighbouring fixtures share a mood.
static float Patch(float px, float pz, uint32_t salt)
{
    const float gx = px / (float)LIGHT_ZONE_TILES, gz = pz / (float)LIGHT_ZONE_TILES;
    const int ix = (int)floorf(gx), iz = (int)floorf(gz);
    float fx = gx - (float)ix, fz = gz - (float)iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    const float a = Hash01(g_seed ^ salt, (uint32_t)ix, (uint32_t)iz);
    const float b = Hash01(g_seed ^ salt, (uint32_t)(ix + 1), (uint32_t)iz);
    const float c = Hash01(g_seed ^ salt, (uint32_t)ix, (uint32_t)(iz + 1));
    const float d = Hash01(g_seed ^ salt, (uint32_t)(ix + 1), (uint32_t)(iz + 1));
    return (a + (b - a) * fx) * (1.0f - fz) + (c + (d - c) * fx) * fz;
}

// ---- tile line of sight --------------------------------------------------------------------------
static bool Open(int x, int z) { return x >= 0 && x < g_w && z >= 0 && z < g_h && g_open[z][x]; }

static bool LineOfSight(int x0, int z0, int x1, int z1)
{
    int dx = abs(x1 - x0), dz = abs(z1 - z0);
    const int sx = x0 < x1 ? 1 : -1, sz = z0 < z1 ? 1 : -1;
    int err = dx - dz, x = x0, z = z0;
    while (x != x1 || z != z1) {
        const int e2 = 2 * err;
        int nx = x, nz = z;
        if (e2 > -dz) { err -= dz; nx += sx; }
        if (e2 < dx)  { err += dx; nz += sz; }
        if (nx != x && nz != z && !Open(nx, z) && !Open(x, nz)) return false;   // squeezes through a diagonal pinch
        x = nx; z = nz;
        if (!Open(x, z)) return false;
    }
    return true;
}

// ---- building ------------------------------------------------------------------------------------
static void AddInfluence(Fixture *fx)
{
    fx->first = g_poolN;
    fx->count = 0;
    const float rm2 = (float)(fx->reach * 2) * (float)(fx->reach * 2) + LIGHT_HEIGHT * LIGHT_HEIGHT;
    const float w0 = (1.0f - LIGHT_HEIGHT * LIGHT_HEIGHT / rm2) * (1.0f - LIGHT_HEIGHT * LIGHT_HEIGHT / rm2);
    for (int dz = -fx->reach; dz <= fx->reach; ++dz) {
        for (int dx = -fx->reach; dx <= fx->reach; ++dx) {
            const int x = fx->tx + dx, z = fx->tz + dz;
            if (!Open(x, z)) continue;
            const float h2 = (float)(dx * dx + dz * dz) * TILE_SIZE * TILE_SIZE;
            const float d2 = h2 + LIGHT_HEIGHT * LIGHT_HEIGHT;
            if (d2 >= rm2) continue;
            if (!LineOfSight(fx->tx, fx->tz, x, z)) continue;
            const float k = 1.0f - d2 / rm2;
            if (g_poolN >= INFLUENCE_POOL) return;
            g_poolIdx[g_poolN] = (uint16_t)(z * MAP_MAX + x);
            g_poolW[g_poolN] = k * k / w0;
            ++g_poolN;
            ++fx->count;
        }
    }
}

static LightMode PickMode(float dark, float own)
{
    // dark: the patch's mood, 0 bright to 1 dead. own: this fixture's own roll, 0 to 1.
    if (dark < 0.36f) return own < 0.05f ? LIGHT_FLICKER : (own < 0.12f ? LIGHT_DIM : LIGHT_ON);
    if (dark < 0.50f) return own < 0.30f ? LIGHT_ON : (own < 0.60f ? LIGHT_DIM : (own < 0.80f ? LIGHT_FLICKER : LIGHT_DEAD));
    if (dark < 0.68f) return own < 0.58f ? LIGHT_DEAD : (own < 0.80f ? LIGHT_DIM : (own < 0.91f ? LIGHT_FLICKER : LIGHT_ON));
    return own < 0.92f ? LIGHT_DEAD : LIGHT_DIM;
}

static void AddFixture(int tx, int tz, bool panel, int reach, LightMode mode, uint32_t id)
{
    if (g_nFix >= LIGHT_MAX_FIXTURES) return;
    Fixture *fx = &g_fix[g_nFix++];
    fx->tx = tx; fx->tz = tz;
    fx->x = (float)tx * TILE_SIZE; fx->z = (float)tz * TILE_SIZE;
    fx->mode = mode;
    fx->alongX = (Hash3(g_seed, id, 7u) & 1u) != 0u;
    fx->hasPanel = panel;
    fx->phase = Hash01(g_seed, id, 11u) * 100.0f;
    fx->level = 0.0f;
    fx->reach = reach;
    AddInfluence(fx);
    if (panel) g_panelAt[tz][tx] = g_nFix - 1;
}

void Lighting_BuildForFloor(const FloorMap *f, int floorNumber)
{
    g_nFix = 0;
    g_poolN = 0;
    g_w = f->w;
    g_h = f->h;
    const FloorShell sh = Floor_Shell(f);
    for (int z = 0; z < g_h; ++z) for (int x = 0; x < g_w; ++x) g_panelAt[z][x] = -1;
    g_seed = (f->seed ^ ((uint32_t)floorNumber * 2654435761u)) | 1u;
    for (int z = 0; z < g_h; ++z)
        for (int x = 0; x < g_w; ++x) g_open[z][x] = !Floor_Solid(f, 0, x, z);

    // Panels hang over open tiles that have a ceiling, one lattice cell each. A lattice point that falls
    // in a wall moves to a neighbouring open tile, or is left out.
    for (int lz = 0; lz * LIGHT_LATTICE_TILES + 2 < g_h - 1; ++lz) {
        for (int lx = 0; lx * LIGHT_LATTICE_TILES + 2 < g_w - 1; ++lx) {
            int tx = lx * LIGHT_LATTICE_TILES + 2, tz = lz * LIGHT_LATTICE_TILES + 2;
            if (!Open(tx, tz)) {
                static const int ox[4] = { 1, -1, 0, 0 }, oz[4] = { 0, 0, 1, -1 };
                bool moved = false;
                for (int k = 0; k < 4 && !moved; ++k)
                    if (Open(tx + ox[k], tz + oz[k])) { tx += ox[k]; tz += oz[k]; moved = true; }
                if (!moved) continue;
            }
            if (tx >= sh.wallX && (tz <= sh.upAlcoveZ1 || tz >= sh.downAlcoveZ0)) continue;     // alcoves have no ceiling
            const uint32_t id = (uint32_t)(lz * 64 + lx);
            // Value noise sits near the middle, so stretch it: the extremes are what make a patch clearly dark or clearly lit.
            float patch = 0.5f + (Patch((float)tx, (float)tz, 0x51u) - 0.5f) * 2.1f;
            patch = patch < 0.0f ? 0.0f : (patch > 1.0f ? 1.0f : patch);
            const float dark = 0.88f * patch + 0.12f * Hash01(g_seed, id, 3u);
            LightMode m = PickMode(dark, Hash01(g_seed, id, 5u));
            // The ground around both stair doors is always lit, so arriving on a floor is never blind.
            const int d1 = abs(tx - sh.upApproachX) + abs(tz - sh.upApproachZ);
            const int d2 = abs(tx - sh.downApproachX) + abs(tz - sh.downApproachZ);
            if (d1 <= 8 || d2 <= 8) m = LIGHT_ON;
            if (g_panelAt[tz][tx] >= 0) continue;   // two lattice points shifted onto the same tile: one panel per tile
            AddFixture(tx, tz, true, LIGHT_REACH_TILES, m, id);
        }
    }

    // One steady lamp over each stair alcove, with no panel because the alcove is open to the shaft.
    AddFixture(sh.upPadX, sh.upPadZ, false, 6, LIGHT_ON, 9001u);
    AddFixture(sh.downPadX, sh.downPadZ, false, 6, LIGHT_ON, 9002u);
    memset(g_tile, 0, sizeof g_tile);
    memset(g_corner, 0, sizeof g_corner);
}

// ---- animation -----------------------------------------------------------------------------------
static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static float Animate(const Fixture *f, float t)
{
    const float p = f->phase;
    switch (f->mode) {
        case LIGHT_ON:
            return 1.0f + 0.025f * sinf(t * 53.0f + p) * sinf(t * 7.1f + p * 0.3f);          // a faint hum
        case LIGHT_DIM:
            return 0.30f + 0.06f * sinf(t * 0.8f + p) + 0.03f * sinf(t * 9.0f + p * 2.0f);   // weak and unsteady
        case LIGHT_FLICKER: {
            // Mostly on. Every few seconds it stutters for about a second: on and off in 40 ms steps.
            const float period = 4.0f + 3.0f * Hash01((uint32_t)(p * 7.0f), 1u, 2u);
            const float ph = fmodf(t + p, period);
            const float burst = 0.55f + 0.7f * Hash01((uint32_t)(p * 7.0f), 3u, 4u);
            if (ph < burst) {
                const uint32_t step = (uint32_t)(t * 25.0f);
                return (Hash3(step, (uint32_t)(p * 13.0f), 9u) & 3u) == 0u ? 0.10f : 0.95f;
            }
            return 0.92f + 0.03f * sinf(t * 41.0f + p);
        }
        case LIGHT_DEAD:
        default: {
            // Dead, except that now and then it twitches back for a moment.
            const float period = 23.0f + 17.0f * Hash01((uint32_t)(p * 5.0f), 5u, 6u);
            const float ph = fmodf(t + p * 3.0f, period);
            if (ph < 0.12f) return (Hash3((uint32_t)(t * 30.0f), (uint32_t)(p * 3.0f), 1u) & 1u) ? 0.55f : 0.0f;
            return 0.0f;
        }
    }
}

void Lighting_Update(float t)
{
    memset(g_tileI, 0, sizeof g_tileI);
    float *flat = &g_tileI[0][0];
    for (int i = 0; i < g_nFix; ++i) {
        Fixture *f = &g_fix[i];
        f->level = Clamp01(Animate(f, t) * 1.05f) ;
        if (f->level < 0.002f) continue;
        for (int k = f->first; k < f->first + f->count; ++k) flat[g_poolIdx[k]] += g_poolW[k] * f->level;
    }
    for (int z = 0; z < g_h; ++z)
        for (int x = 0; x < g_w; ++x) {
            const float I = g_tileI[z][x];
            g_tile[z][x] = I > 0.0f ? 1.0f - expf(-TONE_GAIN * I) : 0.0f;
        }
    // Corner light is the mean of the open tiles around the corner.
    for (int cj = 0; cj <= g_h; ++cj)
        for (int ci = 0; ci <= g_w; ++ci) {
            float sum = 0.0f;
            int n = 0;
            for (int dz = -1; dz <= 0; ++dz)
                for (int dx = -1; dx <= 0; ++dx) {
                    if (!Open(ci + dx, cj + dz)) continue;
                    sum += g_tile[cj + dz][ci + dx];
                    ++n;
                }
            g_corner[cj][ci] = n ? sum / (float)n : 0.0f;
        }
}

// The same as Lighting_Update, for the window round the viewer. A fixture that lights a window tile stands within its reach of the
// window, and everything such a fixture lights is within twice its reach of it, so clearing the window padded by twice the longest
// reach (the stair lamps, 6 tiles) means every sum a fixture adds to lands on a cleared tile. Only the tiles of the window and the
// one-tile margin are ever read, so the padding is not needed for correctness; it keeps the sums beyond them from growing without
// bound. Fixtures are added in the same order as in the full update, so the sums are the same floats.
#define LIGHT_MAX_REACH 6
void Lighting_UpdateAround(float t, float viewerX, float viewerZ)
{
    if (g_w <= 2 * LIGHT_WINDOW_TILES + 1 && g_h <= 2 * LIGHT_WINDOW_TILES + 1) { Lighting_Update(t); return; }   // the window would be the whole floor
    const int cx = (int)floorf(viewerX / TILE_SIZE + 0.5f), cz = (int)floorf(viewerZ / TILE_SIZE + 0.5f);
    int x0 = cx - LIGHT_WINDOW_TILES, x1 = cx + LIGHT_WINDOW_TILES, z0 = cz - LIGHT_WINDOW_TILES, z1 = cz + LIGHT_WINDOW_TILES;
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 > g_w - 1) x1 = g_w - 1;
    if (z1 > g_h - 1) z1 = g_h - 1;
    if (x0 > x1 || z0 > z1) return;                                   // the viewer is far outside the floor

    // A corner on the edge of the window reads the tile just outside it (a corner lies between four tiles), so the tiles, and the
    // fixtures that light them, run one further on every side.
    const int xa = x0 > 0 ? x0 - 1 : 0, za = z0 > 0 ? z0 - 1 : 0;
    const int xb = x1 < g_w - 1 ? x1 + 1 : g_w - 1, zb = z1 < g_h - 1 ? z1 + 1 : g_h - 1;
    const int pad = 2 * LIGHT_MAX_REACH;
    const int cx0 = xa - pad < 0 ? 0 : xa - pad, cx1 = xb + pad > g_w - 1 ? g_w - 1 : xb + pad;
    const int cz0 = za - pad < 0 ? 0 : za - pad, cz1 = zb + pad > g_h - 1 ? g_h - 1 : zb + pad;
    for (int z = cz0; z <= cz1; ++z) memset(&g_tileI[z][cx0], 0, (size_t)(cx1 - cx0 + 1) * sizeof(float));

    float *flat = &g_tileI[0][0];
    for (int i = 0; i < g_nFix; ++i) {
        Fixture *f = &g_fix[i];
        if (f->tx + f->reach < xa || f->tx - f->reach > xb || f->tz + f->reach < za || f->tz - f->reach > zb) continue;
        f->level = Clamp01(Animate(f, t) * 1.05f);
        if (f->level < 0.002f) continue;
        for (int k = f->first; k < f->first + f->count; ++k) flat[g_poolIdx[k]] += g_poolW[k] * f->level;
    }
    for (int z = za; z <= zb; ++z)
        for (int x = xa; x <= xb; ++x) {
            const float I = g_tileI[z][x];
            g_tile[z][x] = I > 0.0f ? 1.0f - expf(-TONE_GAIN * I) : 0.0f;
        }
    for (int cj = z0; cj <= z1 + 1; ++cj)
        for (int ci = x0; ci <= x1 + 1; ++ci) {
            float sum = 0.0f;
            int n = 0;
            for (int dz = -1; dz <= 0; ++dz)
                for (int dx = -1; dx <= 0; ++dx) {
                    if (!Open(ci + dx, cj + dz)) continue;
                    sum += g_tile[cj + dz][ci + dx];
                    ++n;
                }
            g_corner[cj][ci] = n ? sum / (float)n : 0.0f;
        }
}

// ---- reading -------------------------------------------------------------------------------------
float Lighting_Corner(int ci, int cj)
{
    if (ci < 0 || ci > g_w || cj < 0 || cj > g_h) return 0.0f;
    return g_corner[cj][ci];
}

float Lighting_Tile(int tx, int tz)
{
    return Open(tx, tz) ? g_tile[tz][tx] : 0.0f;
}

float Lighting_AtPoint(Vector3 p)
{
    const float gx = p.x / TILE_SIZE + 0.5f, gz = p.z / TILE_SIZE + 0.5f;
    const int ci = (int)floorf(gx), cj = (int)floorf(gz);
    const float fx = gx - (float)ci, fz = gz - (float)cj;
    const float a = Lighting_Corner(ci, cj), b = Lighting_Corner(ci + 1, cj);
    const float c = Lighting_Corner(ci, cj + 1), d = Lighting_Corner(ci + 1, cj + 1);
    return (a + (b - a) * fx) * (1.0f - fz) + (c + (d - c) * fx) * fz;
}

void Lighting_Tint(float light, float *r, float *g, float *b)
{
    // Ambient is a very dim blue grey; lit surfaces take a warm fluorescent colour.
    *r = 0.045f + light * 1.00f;
    *g = 0.050f + light * 0.90f;
    *b = 0.070f + light * 0.64f;
}

int Lighting_FixtureCount(void) { return g_nFix; }
const Fixture *Lighting_Fixture(int i) { return (i >= 0 && i < g_nFix) ? &g_fix[i] : NULL; }

int Lighting_CountMode(LightMode m)
{
    int n = 0;
    for (int i = 0; i < g_nFix; ++i) n += (g_fix[i].hasPanel && g_fix[i].mode == m) ? 1 : 0;
    return n;
}

void Lighting_SetMode(int i, LightMode m)
{
    if (i >= 0 && i < g_nFix) g_fix[i].mode = m;
}

void Lighting_SetAllModes(LightMode m)
{
    for (int i = 0; i < g_nFix; ++i) if (g_fix[i].hasPanel) g_fix[i].mode = m;
}

// ---- panels ----------------------------------------------------------------------------------------
const Fixture *Lighting_PanelOnTile(int tx, int tz)
{
    if (tx < 0 || tx >= g_w || tz < 0 || tz >= g_h) return NULL;
    const int i = g_panelAt[tz][tx];
    return i >= 0 ? &g_fix[i] : NULL;
}

void Lighting_PanelColour(const Fixture *f, float *r, float *g, float *b)
{
    const float lv = f->level > 1.0f ? 1.0f : f->level;
    *r = 24.0f + lv * (255.0f - 24.0f);
    *g = 24.0f + lv * (190.0f - 24.0f + 56.0f * lv);
    *b = 22.0f + lv * (120.0f - 22.0f + 94.0f * lv);
}
