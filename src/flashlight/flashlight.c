// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/flashlight.md, section "flashlight.h and flashlight.c"
// ============================================================================
#include "flashlight.h"

#include <math.h>
#include <stdint.h>

static FlashlightConfig g_cfg;

void Flashlight_SetConfig(const FlashlightConfig *config) { g_cfg = *config; }
const FlashlightConfig *Flashlight_Config(void) { return &g_cfg; }

void Flashlight_Init(Flashlight *f)
{
    *f = (Flashlight){ 0 };
    f->dir = (Vector3){ 0.0f, 0.0f, 1.0f };
}

void Flashlight_Give(Flashlight *f, float chargeSecs)
{
    if (chargeSecs < 0.0f) chargeSecs = 0.0f;
    f->charge = chargeSecs > g_cfg.capacitySecs ? g_cfg.capacitySecs : chargeSecs;
}

bool Flashlight_Toggle(Flashlight *f)
{
    if (f->on) { f->on = false; f->level = 0.0f; return false; }
    if (f->charge <= 0.0f) return false;
    f->on = true;
    return true;
}

float Flashlight_AddBattery(Flashlight *f)
{
    if (f->charge > g_cfg.capacitySecs - g_cfg.batterySecs * 0.5f) return 0.0f;
    const float before = f->charge;
    f->charge += g_cfg.batterySecs;
    if (f->charge > g_cfg.capacitySecs) f->charge = g_cfg.capacitySecs;
    if (before <= 0.0f) f->on = true;                   // the light had gone flat: the player wants it back
    return f->charge - before;
}

float Flashlight_Fraction(const Flashlight *f) { return g_cfg.capacitySecs > 0.0f ? f->charge / g_cfg.capacitySecs : 0.0f; }
bool  Flashlight_IsLow(const Flashlight *f) { return Flashlight_Fraction(f) < g_cfg.lowFraction; }

static float Hash01(uint32_t n)
{
    n *= 0x9E3779B1u; n ^= n >> 15; n *= 0x85EBCA6Bu; n ^= n >> 13; n *= 0xC2B2AE35u; n ^= n >> 16;
    return (float)(n >> 8) / 16777216.0f;
}

// How bright the light is at this moment. Above the low mark it is steady. Below it the light fails: it is cut into short
// cells (faster the emptier it gets); each cell is either lit or, with a chance that rises as the charge falls, a
// dropout that is nearly dark; and the lit cells get dimmer too. The pattern comes from the clock, so it is the same
// every time and does not need a random state.
static float FlickerLevel(const Flashlight *f)
{
    const float frac = Flashlight_Fraction(f);
    if (frac >= g_cfg.lowFraction || g_cfg.lowFraction <= 0.0f) return 1.0f;
    const float s = 1.0f - frac / g_cfg.lowFraction;           // 0 at the low mark, 1 when empty
    const float rate = 9.0f + 11.0f * s;                       // cells a second
    const uint32_t cell = (uint32_t)(f->clock * rate);
    const float h1 = Hash01(cell), h2 = Hash01(cell * 7u + 3u);
    const float dropout = 0.08f + 0.72f * s;
    if (h1 < dropout) return h2 * 0.22f;
    return 1.0f - 0.45f * s;
}

void Flashlight_Update(Flashlight *f, float dt, Vector3 forward)
{
    f->clock += dt;
    f->died = false;

    // The beam follows the camera, a little behind it.
    const float fl = sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (fl > 1e-6f) {
        forward = (Vector3){ forward.x / fl, forward.y / fl, forward.z / fl };
        if (!f->dirValid) { f->dir = forward; f->dirValid = true; }
        else {
            float k = g_cfg.lag * dt;
            if (k > 1.0f) k = 1.0f;
            Vector3 d = { f->dir.x + (forward.x - f->dir.x) * k, f->dir.y + (forward.y - f->dir.y) * k, f->dir.z + (forward.z - f->dir.z) * k };
            const float dl = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
            f->dir = dl > 1e-6f ? (Vector3){ d.x / dl, d.y / dl, d.z / dl } : forward;
        }
    }

    if (f->on) {
        f->charge -= dt;
        if (f->charge <= 0.0f) { f->charge = 0.0f; f->on = false; f->died = true; }
    }
    f->level = f->on ? FlickerLevel(f) : 0.0f;
}

static float Smooth(float x) { return x * x * (3.0f - 2.0f * x); }

float Flashlight_BeamAt(const Flashlight *f, Vector3 eye, Vector3 point)
{
    if (f->level <= 0.0f) return 0.0f;
    const Vector3 v = { point.x - eye.x, point.y - eye.y, point.z - eye.z };
    const float d = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    const float cosIn = cosf(g_cfg.beamInnerDeg * (3.14159265f / 180.0f)), cosOut = cosf(g_cfg.beamOuterDeg * (3.14159265f / 180.0f));
    const float cosA = d > 0.001f ? (v.x * f->dir.x + v.y * f->dir.y + v.z * f->dir.z) / d : 1.0f;
    float spot = (cosA - cosOut) / (cosIn - cosOut);
    spot = spot < 0.0f ? 0.0f : (spot > 1.0f ? 1.0f : spot);
    spot = Smooth(spot);
    float fall = 1.0f - d / g_cfg.range;
    fall = fall < 0.0f ? 0.0f : (fall > 1.0f ? 1.0f : fall);
    return spot * fall * fall * g_cfg.strength * f->level;
}

float Flashlight_GlowAt(const Flashlight *f, Vector3 eye, Vector3 point)
{
    if (f->level <= 0.0f) return 0.0f;
    const Vector3 v = { point.x - eye.x, point.y - eye.y, point.z - eye.z };
    const float d = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    const float R = 6.0f;
    if (d >= R) return 0.0f;
    const float cosA = d > 0.001f ? (v.x * f->dir.x + v.y * f->dir.y + v.z * f->dir.z) / d : 1.0f;
    const float facing = 0.35f + 0.65f * (cosA > 0.0f ? cosA : 0.0f);    // brighter where the player looks, never dark beside
    const float fall = 1.0f - d / R;
    return 0.6f * facing * fall * fall * f->level;
}
