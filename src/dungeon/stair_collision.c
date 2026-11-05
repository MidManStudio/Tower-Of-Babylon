// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "stair_collision.h and stair_collision.c"
// ============================================================================
#include "stair_collision.h"

#include <math.h>
#include <stddef.h>

#include "stair_layout.h"

#define MAX_BOXES 48
static StairBox g_boxes[MAX_BOXES];
static int  g_count = 0;
static bool g_built = false;

static void AddBox(float x0, float x1, float z0, float z1, float y0, float y1, unsigned char kind)
{
    if (g_count >= MAX_BOXES) return;
    g_boxes[g_count++] = (StairBox){ x0, x1, z0, z1, y0, y1, kind };
}

static void Build(void)
{
    if (g_built) return;
    g_built = true;
    g_count = 0;

    for (int j = 0; j < RUN_STEPS; ++j) {                      // run A
        const float x0 = RUN_X_LOW + (float)j * STEP_TREAD;
        AddBox(x0, x0 + STEP_TREAD, LANE_A_Z0, LANE_A_Z1, 0.0f, (float)(j + 1) * STEP_RISE, STAIR_BOX_SOLID);
    }
    AddBox(RUN_X_HIGH, ALC_X1, LANE_A_Z0, LANE_B_Z1, 0.0f, RUN_RISE, STAIR_BOX_SOLID);   // landing
    for (int j = 0; j < RUN_STEPS; ++j) {                      // run B
        const float x1 = RUN_X_HIGH - (float)j * STEP_TREAD;
        AddBox(x1 - STEP_TREAD, x1, LANE_B_Z0, LANE_B_Z1, 0.0f, RUN_RISE + (float)(j + 1) * STEP_RISE, STAIR_BOX_SOLID);
    }
    AddBox(ALC_X0, RUN_X_LOW, LANE_B_Z0, LANE_B_Z1, 0.0f, 2.0f * RUN_RISE, STAIR_BOX_SOLID);   // top landing

    // Railings. They block, they are never stood on.
    AddBox(RUN_X_LOW, RUN_X_HIGH, LANE_A_Z0 - RAIL_HALF, LANE_A_Z0 + RAIL_HALF, STEP_RISE, RUN_RISE + 1.0f, STAIR_BOX_RAIL);
    AddBox(RUN_X_LOW, RUN_X_HIGH, LANE_A_Z1 - RAIL_HALF, LANE_A_Z1 + RAIL_HALF, STEP_RISE, RUN_RISE + 1.0f, STAIR_BOX_RAIL);
    AddBox(RUN_X_HIGH, ALC_X1, LANE_A_Z0 - RAIL_HALF, LANE_A_Z0 + RAIL_HALF, RUN_RISE, RUN_RISE + 1.0f, STAIR_BOX_RAIL);
    AddBox(RUN_X_HIGH - RAIL_HALF, RUN_X_HIGH + RAIL_HALF, LANE_A_Z1, LANE_B_Z0, RUN_RISE, RUN_RISE + 1.0f, STAIR_BOX_RAIL);
    AddBox(ALC_X0, RUN_X_HIGH, LANE_B_Z0 - RAIL_HALF, LANE_B_Z0 + RAIL_HALF, RUN_RISE + STEP_RISE, 2.0f * RUN_RISE + 1.0f, STAIR_BOX_RAIL);
}

const StairBox *Stair_Box(int i)
{
    Build();
    return (i >= 0 && i < g_count) ? &g_boxes[i] : NULL;
}

// ---- The player's queries (declared in staircase.h) ------------------------
float Staircase_GroundHeight(float x, float z, float y)
{
    Build();
    float ground = 0.0f;
    const float reach = y + STEP_UP_MAX + 0.001f;
    for (int i = 0; i < g_count; ++i) {
        const StairBox *b = &g_boxes[i];
        if (b->kind != STAIR_BOX_SOLID) continue;
        if (x < b->x0 || x > b->x1 || z < b->z0 || z > b->z1) continue;
        if (b->y1 <= reach && b->y1 > ground) ground = b->y1;
    }
    return ground;
}

bool Staircase_Blocked(float x, float z, float radius, float y, float height)
{
    Build();
    const float reach = y + STEP_UP_MAX + 0.001f;
    for (int i = 0; i < g_count; ++i) {
        const StairBox *b = &g_boxes[i];
        if (x < b->x0 - radius || x > b->x1 + radius || z < b->z0 - radius || z > b->z1 + radius) continue;
        if (b->y1 > reach && b->y0 < y + height - 0.05f) return true;
    }
    return false;
}

// ---- Shots and spiders ------------------------------------------------------
bool Stair_Raycast(Vector3 o, Vector3 d, float maxDist, float *tOut, Vector3 *nOut)
{
    Build();
    const float org[3] = { o.x, o.y, o.z }, dir[3] = { d.x, d.y, d.z };
    float best = maxDist;
    bool hit = false;
    int bestAxis = 0;
    float bestSign = 1.0f;

    for (int i = 0; i < g_count; ++i) {
        const StairBox *b = &g_boxes[i];
        if (b->kind != STAIR_BOX_SOLID) continue;
        const float lo[3] = { b->x0, b->y0, b->z0 }, hi[3] = { b->x1, b->y1, b->z1 };
        if (org[0] > lo[0] && org[0] < hi[0] && org[1] > lo[1] && org[1] < hi[1] && org[2] > lo[2] && org[2] < hi[2]) continue;   // starts inside

        float tmin = 0.0f, tmax = best;
        int axis = -1;
        float sign = 1.0f;
        bool miss = false;
        for (int a = 0; a < 3; ++a) {
            if (fabsf(dir[a]) < 1e-8f) {
                if (org[a] < lo[a] || org[a] > hi[a]) { miss = true; break; }
                continue;
            }
            const float inv = 1.0f / dir[a];
            float t0 = (lo[a] - org[a]) * inv, t1 = (hi[a] - org[a]) * inv, sg = -1.0f;    // through the low face: the normal points the other way
            if (t0 > t1) { const float t = t0; t0 = t1; t1 = t; sg = 1.0f; }
            if (t0 > tmin) { tmin = t0; axis = a; sign = sg; }
            if (t1 < tmax) tmax = t1;
            if (tmin > tmax) { miss = true; break; }
        }
        if (miss || axis < 0) continue;
        if (tmin < best) { best = tmin; hit = true; bestAxis = axis; bestSign = sign; }
    }
    if (!hit) return false;
    if (tOut) *tOut = best;
    if (nOut) {
        *nOut = (Vector3){ 0.0f, 0.0f, 0.0f };
        if (bestAxis == 0) nOut->x = bestSign; else if (bestAxis == 1) nOut->y = bestSign; else nOut->z = bestSign;
    }
    return true;
}

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool Stair_PushOutSphere(Vector3 *c, float r)
{
    Build();
    bool moved = false;
    for (int i = 0; i < g_count; ++i) {
        const StairBox *b = &g_boxes[i];
        if (b->kind != STAIR_BOX_SOLID) continue;
        if (c->x < b->x0 - r || c->x > b->x1 + r || c->y < b->y0 - r || c->y > b->y1 + r || c->z < b->z0 - r || c->z > b->z1 + r) continue;
        const float cx = Clampf(c->x, b->x0, b->x1), cy = Clampf(c->y, b->y0, b->y1), cz = Clampf(c->z, b->z0, b->z1);
        const float dx = c->x - cx, dy = c->y - cy, dz = c->z - cz;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= r * r) continue;
        if (d2 > 1e-10f) {
            const float dl = sqrtf(d2), k = (r - dl) / dl;
            c->x += dx * k; c->y += dy * k; c->z += dz * k;
        } else {                                                   // the centre is inside: leave by the nearest face
            const float gap[6] = { c->x - b->x0, b->x1 - c->x, c->y - b->y0, b->y1 - c->y, c->z - b->z0, b->z1 - c->z };
            int face = 0;
            for (int k = 1; k < 6; ++k) if (gap[k] < gap[face]) face = k;
            switch (face) {
                case 0: c->x = b->x0 - r; break;
                case 1: c->x = b->x1 + r; break;
                case 2: c->y = b->y0 - r; break;
                case 3: c->y = b->y1 + r; break;
                case 4: c->z = b->z0 - r; break;
                default: c->z = b->z1 + r; break;
            }
        }
        moved = true;
    }
    return moved;
}

float Stair_SurfaceDistance(Vector3 p)
{
    Build();
    float best = 1e9f;
    for (int i = 0; i < g_count; ++i) {
        const StairBox *b = &g_boxes[i];
        if (b->kind != STAIR_BOX_SOLID) continue;
        const float dx = p.x - Clampf(p.x, b->x0, b->x1), dy = p.y - Clampf(p.y, b->y0, b->y1), dz = p.z - Clampf(p.z, b->z0, b->z1);
        const float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < best) best = d;
    }
    return best;
}

// ---- The route of a walking monster -----------------------------------------
// The flight is a switchback, and a monster that only steers straight at its target runs into a railing. These four points
// are the middle of the way up: the foot of run A, the far end of run A and the landing, the start of run B, the top landing.
#define ROUTE_N 4
#define LANE_A_MID ((LANE_A_Z0 + LANE_A_Z1) * 0.5f)
#define LANE_B_MID ((LANE_B_Z0 + LANE_B_Z1) * 0.5f)
#define LANDING_MID ((RUN_X_HIGH + ALC_X1) * 0.5f)
#define GATE_X (ALC_X0 - 3.0f)                      // in the hall, in front of the doorway into the alcove
static const float k_rx[ROUTE_N] = { RUN_X_LOW - 1.0f, LANDING_MID, LANDING_MID, (ALC_X0 + RUN_X_LOW) * 0.5f };
static const float k_rz[ROUTE_N] = { LANE_A_MID, LANE_A_MID, LANE_B_MID, LANE_B_MID };

static float RouteS(int vertex)                     // distance along the route of a vertex
{
    float s = 0.0f;
    for (int i = 0; i < vertex; ++i) s += hypotf(k_rx[i + 1] - k_rx[i], k_rz[i + 1] - k_rz[i]);
    return s;
}

static float RouteProject(float px, float pz)       // distance along the route of the nearest point on it
{
    float bestD2 = 1e30f, bestS = 0.0f, acc = 0.0f;
    for (int i = 0; i < ROUTE_N - 1; ++i) {
        const float sx = k_rx[i + 1] - k_rx[i], sz = k_rz[i + 1] - k_rz[i], len = hypotf(sx, sz);
        const float t = Clampf(((px - k_rx[i]) * sx + (pz - k_rz[i]) * sz) / (len * len), 0.0f, 1.0f);
        const float qx = k_rx[i] + sx * t - px, qz = k_rz[i] + sz * t - pz, d2 = qx * qx + qz * qz;
        if (d2 < bestD2) { bestD2 = d2; bestS = acc + len * t; }
        acc += len;
    }
    return bestS;
}

bool Stair_OnFlight(float x, float z, float y)
{
    return x >= ALC_X0 && x <= ALC_X1 && z >= LANE_A_Z0 && z <= LANE_B_Z1 && y > 0.2f;
}

bool Stair_ChaseWaypoint(Vector3 from, Vector3 to, float *wx, float *wz)
{
    const bool onE = Stair_OnFlight(from.x, from.z, from.y), onT = Stair_OnFlight(to.x, to.z, to.y);
    if (!onE && !onT) return false;

    if (!onE) {                                     // on the floor, the target is up on the flight
        // Outside the alcove: to the doorway first (until in front of it and in line with it), then in through it to the foot.
        if (from.x < ALC_X0 - 0.5f && (from.x < GATE_X - 0.5f || fabsf(from.z - LANE_A_MID) > 2.5f)) { *wx = GATE_X; *wz = LANE_A_MID; return true; }
        if (hypotf(from.x - k_rx[0], from.z - k_rz[0]) > 1.0f) { *wx = k_rx[0]; *wz = k_rz[0]; return true; }    // inside: to the foot of run A
        *wx = k_rx[1]; *wz = k_rz[1];               // at the foot: walk onto it
        return true;
    }

    const float sE = RouteProject(from.x, from.z);
    const float sT = onT ? RouteProject(to.x, to.z) : -1.0f;
    if (sT < sE - 0.6f) {                           // the target is below: back down the way it came
        int v = -1;
        for (int i = 0; i < ROUTE_N; ++i) if (RouteS(i) < sE - 0.05f) v = i;
        if (v < 0) return false;                    // at the very foot: walk off at it
        *wx = k_rx[v]; *wz = k_rz[v];
        return true;
    }
    if (sT > sE + 0.6f) {                           // the target is above: on to the next corner, unless it is before it
        int v = -1;
        for (int i = 0; i < ROUTE_N; ++i) if (RouteS(i) > sE + 0.05f) { v = i; break; }
        if (v < 0 || sT <= RouteS(v)) return false;
        *wx = k_rx[v]; *wz = k_rz[v];
        return true;
    }
    return false;                                   // close enough: straight at it
}
