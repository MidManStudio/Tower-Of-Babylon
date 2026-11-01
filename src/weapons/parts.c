// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "parts.h and parts.c"
// ============================================================================
#include "parts.h"

#include <math.h>
#include <stdlib.h>

#include "rlgl.h"

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }
static Vector3 Add(Vector3 a, Vector3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static Vector3 Sub(Vector3 a, Vector3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static Vector3 Mul(Vector3 a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static float   Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vector3 Cross(Vector3 a, Vector3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static Vector3 Norm(Vector3 a, Vector3 fallback)
{
    const float l = sqrtf(Dot(a, a));
    return l > 1e-6f ? Mul(a, 1.0f / l) : fallback;
}

Part Part_Cyl(Vector3 a, Vector3 b, float ra, float rb, int sides, Color col)
{
    return (Part){ a, b, ra, rb, sides, 1.0f, 0.0f, V(0, 1, 0), col };
}

Part Part_Box(Vector3 a, Vector3 b, float halfW, float halfH, Vector3 upHint, Color col)
{
    // A square cross-section has circumradius = half side * sqrt(2); flat squashes the up side.
    const float r = halfW * 1.41421356f;
    return (Part){ a, b, r, r, 4, halfW > 0.0f ? halfH / halfW : 1.0f, 0.78539816f, upHint, col };
}

void PartList_Clear(PartList *l) { l->count = 0; }

void PartList_Add(PartList *l, Part p)
{
    if (l->count < PARTS_MAX) l->parts[l->count++] = p;
}

Part Part_Blob(Vector3 centre, Vector3 axisEnd, float radius, float flat, Vector3 upHint, Color col)
{
    // sides == 0 marks a blob. a is the centre and b the end of the long axis.
    return (Part){ centre, axisEnd, radius, radius, 0, flat, 0.0f, upHint, col };
}

Part Part_BlobLow(Vector3 centre, Vector3 axisEnd, float radius, float flat, Vector3 upHint, Color col)
{
    Part p = Part_Blob(centre, axisEnd, radius, flat, upHint, col);
    p.twist = 1.0f;       // the detail switch DrawBlob reads
    return p;
}

static float g_brightness = 1.0f;
void Parts_SetBrightness(float b) { g_brightness = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b); }

static Color Shade(Color c, float k)
{
    k *= g_brightness;
    float r = (float)c.r * k, g = (float)c.g * k, b = (float)c.b * k;
    if (r > 255.0f) r = 255.0f;
    if (g > 255.0f) g = 255.0f;
    if (b > 255.0f) b = 255.0f;
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, c.a };
}

// One triangle with a colour at each corner, wound so its front faces outward. It only adds vertices: the caller
// has an rlBegin(RL_TRIANGLES) open (one per part, not one per triangle, which cut the draw-call overhead by a quarter).
static void Tri(Vector3 a, Color ca, Vector3 b, Color cb, Vector3 c, Color cc, Vector3 outward)
{
    const Vector3 n = Cross(Sub(b, a), Sub(c, a));
    if (Dot(n, outward) < 0.0f) { const Vector3 tp = b; b = c; c = tp; const Color tc = cb; cb = cc; cc = tc; }
    rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex3f(a.x, a.y, a.z);
    rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlVertex3f(b.x, b.y, b.z);
    rlColor4ub(cc.r, cc.g, cc.b, cc.a); rlVertex3f(c.x, c.y, c.z);
}

static float Light(Vector3 n, Vector3 lightDir)
{
    const float d = Dot(n, lightDir);
    return 0.50f + 0.50f * (d > 0.0f ? d : 0.0f) + 0.10f;
}

#define BLOB_SEG_MAX   8
#define BLOB_RINGS_MAX 5

static void DrawBlob(const Part *p, Vector3 lightDir)
{
    // twist is the level of detail here: 0 the full 8 by 5 (64 triangles), anything else the cheap 6 by 4 (36), used for
    // fingers, which are too small on screen to show the difference.
    const int SEG = p->twist != 0.0f ? 6 : BLOB_SEG_MAX;
    const int RINGS = p->twist != 0.0f ? 4 : BLOB_RINGS_MAX;
    const Vector3 axisV = Sub(p->b, p->a);
    const float L = sqrtf(Dot(axisV, axisV)) > 1e-5f ? sqrtf(Dot(axisV, axisV)) : 1e-5f;
    const Vector3 axis = Mul(axisV, 1.0f / L);
    const Vector3 u = Norm(Cross(axis, p->upHint), Norm(Cross(axis, V(1, 0, 0)), V(0, 1, 0)));
    const Vector3 vUnit = Norm(Cross(u, axis), V(0, 1, 0));
    const float r = p->ra, rv = p->ra * (p->flat > 0.05f ? p->flat : 0.05f);

    Vector3 pos[BLOB_RINGS_MAX + 1][BLOB_SEG_MAX], nrm[BLOB_RINGS_MAX + 1][BLOB_SEG_MAX];
    Color col[BLOB_RINGS_MAX + 1][BLOB_SEG_MAX];
    for (int i = 0; i <= RINGS; ++i) {
        const float th = 3.14159265f * (float)i / (float)RINGS;
        const float ct = cosf(th), st = sinf(th);
        for (int j = 0; j < SEG; ++j) {
            const float ph = 6.28318531f * (float)j / (float)SEG;
            const float cp = cosf(ph), sp = sinf(ph);
            pos[i][j] = Add(p->a, Add(Mul(axis, L * ct), Add(Mul(u, r * cp * st), Mul(vUnit, rv * sp * st))));
            nrm[i][j] = Norm(Add(Mul(axis, ct / L), Add(Mul(u, cp * st / r), Mul(vUnit, sp * st / rv))), axis);
            col[i][j] = Shade(p->col, Light(nrm[i][j], lightDir));
        }
    }
    for (int i = 0; i < RINGS; ++i) {
        for (int j = 0; j < SEG; ++j) {
            const int k = (j + 1) % SEG;
            const Vector3 out = Norm(Add(nrm[i][j], nrm[i + 1][k]), axis);
            if (i == 0) {
                Tri(pos[0][0], col[0][0], pos[1][j], col[1][j], pos[1][k], col[1][k], out);
            } else if (i == RINGS - 1) {
                Tri(pos[i][j], col[i][j], pos[RINGS][0], col[RINGS][0], pos[i][k], col[i][k], out);
            } else {
                Tri(pos[i][j], col[i][j], pos[i + 1][j], col[i + 1][j], pos[i + 1][k], col[i + 1][k], out);
                Tri(pos[i][j], col[i][j], pos[i + 1][k], col[i + 1][k], pos[i][k], col[i][k], out);
            }
        }
    }
}

static void DrawPrism(const Part *p, Vector3 lightDir)
{
    const int N = p->sides < 3 ? 3 : (p->sides > 8 ? 8 : p->sides);
    const bool smooth = N >= 6;
    const Vector3 axis = Norm(Sub(p->b, p->a), V(0, 0, 1));
    Vector3 u = Norm(Cross(axis, p->upHint), Norm(Cross(axis, V(1, 0, 0)), V(0, 1, 0)));   // across
    const Vector3 vUnit = Norm(Cross(u, axis), V(0, 1, 0));                                  // the "up" side
    const float flat = p->flat > 0.05f ? p->flat : 0.05f;

    Vector3 ringA[8], ringB[8], nrm[8];
    for (int i = 0; i < N; ++i) {
        const float th = p->twist + 6.28318531f * (float)i / (float)N;
        const float cs = cosf(th), sn = sinf(th);
        const Vector3 off = Add(Mul(u, cs), Mul(vUnit, sn * flat));
        ringA[i] = Add(p->a, Mul(off, p->ra));
        ringB[i] = Add(p->b, Mul(off, p->rb));
        nrm[i] = Norm(Add(Mul(u, cs), Mul(vUnit, sn / flat)), u);   // outward normal of a squashed ring
    }

    const Vector3 centre = Mul(Add(p->a, p->b), 0.5f);
    for (int i = 0; i < N; ++i) {
        const int j = (i + 1) % N;
        const Vector3 mid = Mul(Add(Add(ringA[i], ringA[j]), Add(ringB[i], ringB[j])), 0.25f);
        const Vector3 out = Norm(Sub(mid, centre), u);
        Color ci, cj;
        if (smooth) { ci = Shade(p->col, Light(nrm[i], lightDir)); cj = Shade(p->col, Light(nrm[j], lightDir)); }
        else        { ci = cj = Shade(p->col, Light(out, lightDir)); }
        Tri(ringA[i], ci, ringA[j], cj, ringB[j], cj, out);
        Tri(ringA[i], ci, ringB[j], cj, ringB[i], ci, out);
    }
    // Caps
    const Color ca = Shade(p->col, Light(Mul(axis, -1.0f), lightDir) * 0.9f);
    const Color cb = Shade(p->col, Light(axis, lightDir) * 0.9f);
    for (int i = 0; i < N; ++i) {
        const int j = (i + 1) % N;
        Tri(p->a, ca, ringA[i], ca, ringA[j], ca, Mul(axis, -1.0f));
        Tri(p->b, cb, ringB[i], cb, ringB[j], cb, axis);
    }
}

void Part_Draw(const Part *p, Vector3 lightDir)
{
    // The most one part adds is 64 triangles (a blob), 192 vertices; 320 leaves room.
    if (rlCheckRenderBatchLimit(320)) rlDrawRenderBatchActive();
    rlBegin(RL_TRIANGLES);
    if (p->sides == 0) DrawBlob(p, lightDir);
    else DrawPrism(p, lightDir);
    rlEnd();
}

void PartList_Draw(const PartList *l, Vector3 lightDir)
{
    for (int i = 0; i < l->count; ++i) Part_Draw(&l->parts[i], lightDir);
}

typedef struct { float depth; int index; } Order;

static int FarFirst(const void *x, const void *y)
{
    const float a = ((const Order *)x)->depth, b = ((const Order *)y)->depth;
    return a < b ? 1 : (a > b ? -1 : 0);
}

void PartList_DrawSorted(const PartList *l, Vector3 eye, Vector3 forward, Vector3 lightDir)
{
    Order order[PARTS_MAX];
    for (int i = 0; i < l->count; ++i) {
        const Vector3 mid = Mul(Add(l->parts[i].a, l->parts[i].b), 0.5f);
        order[i].depth = Dot(Sub(mid, eye), forward);
        order[i].index = i;
    }
    qsort(order, (size_t)l->count, sizeof(Order), FarFirst);
    for (int i = 0; i < l->count; ++i) Part_Draw(&l->parts[order[i].index], lightDir);
}
