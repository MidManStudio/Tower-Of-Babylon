// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectile_patterns.h and projectile_patterns.c"
// ============================================================================
#include "projectile_patterns.h"

#include <math.h>

float Rng01(uint32_t *state)
{
    uint32_t x = *state ? *state : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return (float)(x >> 8) / 16777216.0f;
}

static Vector3 Dir(Vector3 f, Vector3 r, Vector3 u, float cosA, float sinA, float cosP, float sinP)
{
    // forward * cos(a) + (right * cos(p) + up * sin(p)) * sin(a)
    const float s = sinA;
    Vector3 v = { f.x * cosA + (r.x * cosP + u.x * sinP) * s,
                  f.y * cosA + (r.y * cosP + u.y * sinP) * s,
                  f.z * cosA + (r.z * cosP + u.z * sinP) * s };
    const float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l > 1e-6f) { v.x /= l; v.y /= l; v.z /= l; }
    return v;
}

void Pattern_Directions(ShotPattern pattern, int count, float coneDeg, Vector3 forward, Vector3 right, Vector3 up,
                        uint32_t *rng, Vector3 *out)
{
    const float cone = coneDeg * (3.14159265f / 180.0f);
    for (int i = 0; i < count; ++i) {
        if (pattern == SHOT_FAN) {
            const float a = count > 1 ? -cone + 2.0f * cone * (float)i / (float)(count - 1) : 0.0f;
            out[i] = Dir(forward, right, up, cosf(a), sinf(a), 1.0f, 0.0f);   // turned sideways by a
        } else {
            // sqrt so the pellets are spread evenly over the disc, not bunched at the middle
            const float a = cone * sqrtf(Rng01(rng));
            const float p = 6.28318531f * Rng01(rng);
            out[i] = Dir(forward, right, up, cosf(a), sinf(a), cosf(p), sinf(p));
        }
    }
}
