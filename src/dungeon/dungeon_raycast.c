// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_raycast.c"
// ============================================================================
#include "dungeon_raycast.h"
#include "floor_access.h"
#include "stair_collision.h"

#include <math.h>

#define HALF (TILE_SIZE * 0.5f)

static int TileOfWorld(float w) { return Floor_TileOfWorld(w); }

bool Dungeon_HasCeiling(int tx, int tz)
{
    if (tx < 0 || tx >= MAP_WIDTH || tz < 0 || tz >= MAP_HEIGHT) return false;
    return !(tx >= STAIR_WALL_X && (tz <= UP_ALCOVE_Z1 || tz >= DOWN_ALCOVE_Z0));
}

static bool InsideMapXZ(const FloorMap *f, float x, float z)
{
    return x >= -HALF && x <= ((float)Floor_Width(f) - 0.5f) * TILE_SIZE && z >= -HALF && z <= ((float)Floor_Depth(f) - 0.5f) * TILE_SIZE;
}

// Slab test. Reports the entry distance and the face normal. A ray that starts
// inside the box is treated as a miss.
static bool RayBox(Vector3 o, Vector3 d, Vector3 bmin, Vector3 bmax, float *tOut, Vector3 *nOut)
{
    const float om[3] = { o.x, o.y, o.z }, dm[3] = { d.x, d.y, d.z };
    const float lo[3] = { bmin.x, bmin.y, bmin.z }, hi[3] = { bmax.x, bmax.y, bmax.z };
    float tmin = -1e30f, tmax = 1e30f;
    int axis = -1;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(dm[i]) < 1e-8f) {
            if (om[i] < lo[i] || om[i] > hi[i]) return false;
            continue;
        }
        float t1 = (lo[i] - om[i]) / dm[i], t2 = (hi[i] - om[i]) / dm[i];
        if (t1 > t2) { const float s = t1; t1 = t2; t2 = s; }
        if (t1 > tmin) { tmin = t1; axis = i; }
        if (t2 < tmax) tmax = t2;
        if (tmax < tmin) return false;
    }
    if (tmin < 0.0f || axis < 0) return false;
    float n[3] = { 0.0f, 0.0f, 0.0f };
    n[axis] = dm[axis] > 0.0f ? -1.0f : 1.0f;
    *tOut = tmin;
    *nOut = (Vector3){ n[0], n[1], n[2] };
    return true;
}

bool Dungeon_Raycast(const FloorMap *f, Vector3 o, Vector3 d, float maxDist, DungeonHit *hit)
{
    float bestT = maxDist;
    Vector3 bestN = { 0.0f, 0.0f, 0.0f };
    bool found = false;

    if (d.y < -1e-6f && o.y >= -1e-4f) {
        const float t = -o.y / d.y;
        if (t >= 0.0f && t < bestT && InsideMapXZ(f, o.x + d.x * t, o.z + d.z * t)) {
            bestT = t; bestN = (Vector3){ 0.0f, 1.0f, 0.0f }; found = true;
        }
    }
    const float roofY = Floor_RoofY(f);
    if (d.y > 1e-6f && o.y <= roofY + 1e-4f) {
        const float t = (roofY - o.y) / d.y;
        if (t >= 0.0f && t < bestT && Floor_HasCeiling(f, TileOfWorld(o.x + d.x * t), TileOfWorld(o.z + d.z * t))) {
            bestT = t; bestN = (Vector3){ 0.0f, -1.0f, 0.0f }; found = true;
        }
    }

    const float ex = o.x + d.x * maxDist, ez = o.z + d.z * maxDist;
    int x0 = TileOfWorld(fminf(o.x, ex)), x1 = TileOfWorld(fmaxf(o.x, ex));
    int z0 = TileOfWorld(fminf(o.z, ez)), z1 = TileOfWorld(fmaxf(o.z, ez));
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= Floor_Width(f)) x1 = Floor_Width(f) - 1;
    if (z1 >= Floor_Depth(f)) z1 = Floor_Depth(f) - 1;

    for (int tz = z0; tz <= z1; ++tz)
        for (int tx = x0; tx <= x1; ++tx) {
            if (!Floor_Solid(f, 0, tx, tz)) continue;
            const Vector3 bmin = { (float)tx * TILE_SIZE - HALF, 0.0f, (float)tz * TILE_SIZE - HALF };
            const Vector3 bmax = { (float)tx * TILE_SIZE + HALF, Floor_WallTopY(f, 0, tx, tz), (float)tz * TILE_SIZE + HALF };
            float t; Vector3 n;
            if (RayBox(o, d, bmin, bmax, &t, &n) && t < bestT) { bestT = t; bestN = n; found = true; }
        }

    {   // the flight of stairs in the up alcove is solid too
        float ts; Vector3 ns;
        if (Stair_Raycast(o, d, bestT, &ts, &ns)) { bestT = ts; bestN = ns; found = true; }
    }

    if (!found) { if (hit) hit->hit = false; return false; }
    if (hit) {
        hit->hit = true;
        hit->t = bestT;
        hit->normal = bestN;
        hit->point = (Vector3){ o.x + d.x * bestT, o.y + d.y * bestT, o.z + d.z * bestT };
    }
    return true;
}

// One pass over the boxes near the sphere. Returns true if the sphere moved.
static bool PushOutPass(const FloorMap *f, Vector3 *c, float r)
{
    bool moved = false;
    int x0 = TileOfWorld(c->x - r), z0 = TileOfWorld(c->z - r);
    int x1 = TileOfWorld(c->x + r), z1 = TileOfWorld(c->z + r);
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= Floor_Width(f)) x1 = Floor_Width(f) - 1;
    if (z1 >= Floor_Depth(f)) z1 = Floor_Depth(f) - 1;

    for (int tz = z0; tz <= z1; ++tz)
        for (int tx = x0; tx <= x1; ++tx) {
            if (!Floor_Solid(f, 0, tx, tz)) continue;
            const float topY = Floor_WallTopY(f, 0, tx, tz);
            const float mnx = (float)tx * TILE_SIZE - HALF, mxx = (float)tx * TILE_SIZE + HALF;
            const float mnz = (float)tz * TILE_SIZE - HALF, mxz = (float)tz * TILE_SIZE + HALF;
            const float px = fminf(fmaxf(c->x, mnx), mxx);
            const float py = fminf(fmaxf(c->y, 0.0f), topY);
            const float pz = fminf(fmaxf(c->z, mnz), mxz);
            const float dx = c->x - px, dy = c->y - py, dz = c->z - pz;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 >= r * r) continue;
            if (d2 > 1e-10f) {
                const float d = sqrtf(d2), k = (r - d) / d;
                c->x += dx * k; c->y += dy * k; c->z += dz * k;
            } else {
                // Centre is inside the box: leave through the nearest face.
                const float exits[5] = { c->x - mnx, mxx - c->x, c->z - mnz, mxz - c->z, topY - c->y };
                int best = 0;
                for (int i = 1; i < 5; ++i) if (exits[i] < exits[best]) best = i;
                switch (best) {
                    case 0: c->x = mnx - r; break;
                    case 1: c->x = mxx + r; break;
                    case 2: c->z = mnz - r; break;
                    case 3: c->z = mxz + r; break;
                    default: c->y = topY + r; break;
                }
            }
            moved = true;
        }
    return moved;
}

bool Dungeon_PushOutSphere(const FloorMap *f, Vector3 *c, float r)
{
    bool moved = false;
    // A sphere in a corner can touch two boxes, so repeat until it settles.
    for (int pass = 0; pass < 4; ++pass) {
        bool m = PushOutPass(f, c, r);
        if (Stair_PushOutSphere(c, r)) m = true;
        if (c->y < r) { c->y = r; m = true; }
        const float ceilY = Floor_CeilY(f, c->x, c->z, c->y);        // FLOOR_NO_CEILING (huge) over the alcoves, so nothing is clamped there
        if (c->y > ceilY - r) { c->y = ceilY - r; m = true; }
        if (!m) break;
        moved = true;
    }
    return moved;
}

float Dungeon_SurfaceDistance(const FloorMap *f, Vector3 p)
{
    float best = fabsf(p.y);
    if (Floor_HasCeiling(f, TileOfWorld(p.x), TileOfWorld(p.z))) best = fminf(best, fabsf(Floor_RoofY(f) - p.y));

    const int x0 = TileOfWorld(p.x) - 2, x1 = TileOfWorld(p.x) + 2, z0 = TileOfWorld(p.z) - 2, z1 = TileOfWorld(p.z) + 2;
    for (int tz = z0 < 0 ? 0 : z0; tz <= z1 && tz < Floor_Depth(f); ++tz)
        for (int tx = x0 < 0 ? 0 : x0; tx <= x1 && tx < Floor_Width(f); ++tx) {
            if (!Floor_Solid(f, 0, tx, tz)) continue;
            const float topY = Floor_WallTopY(f, 0, tx, tz);
            const float px = fminf(fmaxf(p.x, (float)tx * TILE_SIZE - HALF), (float)tx * TILE_SIZE + HALF);
            const float py = fminf(fmaxf(p.y, 0.0f), topY);
            const float pz = fminf(fmaxf(p.z, (float)tz * TILE_SIZE - HALF), (float)tz * TILE_SIZE + HALF);
            const float d = sqrtf((p.x - px) * (p.x - px) + (p.y - py) * (p.y - py) + (p.z - pz) * (p.z - pz));
            if (d < best) best = d;
        }
    const float sd = Stair_SurfaceDistance(p);
    return sd < best ? sd : best;
}
