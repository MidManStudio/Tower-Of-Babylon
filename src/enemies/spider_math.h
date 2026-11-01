// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "spider_math.h"
// ============================================================================
#ifndef SPIDER_MATH_H
#define SPIDER_MATH_H

#include <math.h>

#include "raylib.h"

// Small vector helpers kept local to the spider code so it does not depend on raymath.
static inline Vector3 V3(float x, float y, float z) { return (Vector3){ x, y, z }; }
static inline Vector3 V3Add(Vector3 a, Vector3 b) { return (Vector3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline Vector3 V3Sub(Vector3 a, Vector3 b) { return (Vector3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline Vector3 V3Scale(Vector3 a, float s) { return (Vector3){ a.x * s, a.y * s, a.z * s }; }
static inline Vector3 V3Mad(Vector3 a, Vector3 b, float s) { return (Vector3){ a.x + b.x * s, a.y + b.y * s, a.z + b.z * s }; }
static inline float   V3Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float   V3Len(Vector3 a) { return sqrtf(V3Dot(a, a)); }
static inline Vector3 V3Lerp(Vector3 a, Vector3 b, float t) { return V3Add(V3Scale(a, 1.0f - t), V3Scale(b, t)); }

static inline Vector3 V3Cross(Vector3 a, Vector3 b)
{
    return (Vector3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

static inline Vector3 V3NormOr(Vector3 v, Vector3 fallback)
{
    const float l = V3Len(v);
    return l > 1e-6f ? V3Scale(v, 1.0f / l) : fallback;
}

// Component of v lying in the plane with unit normal n.
static inline Vector3 V3OnPlane(Vector3 v, Vector3 n) { return V3Mad(v, n, -V3Dot(v, n)); }

// Rodrigues rotation of v about a unit axis.
static inline Vector3 V3RotateAxis(Vector3 v, Vector3 axis, float angle)
{
    const float c = cosf(angle), s = sinf(angle);
    return V3Add(V3Add(V3Scale(v, c), V3Scale(V3Cross(axis, v), s)), V3Scale(axis, V3Dot(axis, v) * (1.0f - c)));
}

// Carries v along with the rotation that turns unit normal n0 into n1. This is
// parallel transport across a surface switch: it keeps the direction of travel
// from flipping when the surface changes, ceilings included.
static inline Vector3 V3Transport(Vector3 v, Vector3 n0, Vector3 n1)
{
    const Vector3 axis = V3Cross(n0, n1);
    const float s = V3Len(axis), c = V3Dot(n0, n1);
    if (s < 1e-5f) {
        if (c > 0.0f) return v;
        return V3RotateAxis(v, V3NormOr(V3Cross(n0, v), V3(1.0f, 0.0f, 0.0f)), 3.14159265f);
    }
    return V3RotateAxis(v, V3Scale(axis, 1.0f / s), atan2f(s, c));
}

static inline float V3SignedAngle(Vector3 a, Vector3 b, Vector3 n)
{
    return atan2f(V3Dot(V3Cross(a, b), n), V3Dot(a, b));
}

static inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float FracF(float x) { return x - floorf(x); }

#endif
