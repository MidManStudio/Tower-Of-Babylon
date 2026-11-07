// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/swarm.md, section "swarm_draw.c"
// ============================================================================
#include <math.h>

#include "raylib.h"

#include "swarm.h"
#include "../lighting/lighting.h"

static unsigned char Mul(int base, float k) { const int v = (int)((float)base * k); return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Pale worms that thin toward the tail, the head of each darker and redder, all dimmed by how dark the place is. Near dots are small
// low-poly spheres; far ones are cubes (a swarm is many dots, and the phone draws them all). The health box is a thin red frame with a
// small glowing cube in it, so there is something to aim at; it flashes white when hit. No test can run this (it needs a window).
void Swarm_Draw(const Swarm *s, Vector3 viewer, float alpha)
{
    if (s->state == SWARM_NONE) return;
    float light = Lighting_AtPoint(s->core);
    light = 0.22f + 0.78f * (light < 0.0f ? 0.0f : (light > 1.0f ? 1.0f : light));
    const float fade = s->state == SWARM_DEAD ? 1.0f - s->deathT / s->cfg.deathSecs : 1.0f;     // the dead sink out of sight as they go
    const float rad = s->cfg.dotRadius;
    for (int i = 0; i < s->count; ++i) {
        const Vector3 a = s->r[i], b = s->p[i];
        const Vector3 pos = { a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha, a.z + (b.z - a.z) * alpha };
        const float dx = pos.x - viewer.x, dy = pos.y - viewer.y, dz = pos.z - viewer.z, d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 45.0f * 45.0f) continue;
        const int k = i % s->dotsPer;
        const float t = (float)k / (float)(s->dotsPer - 1);
        const float rr = rad * (1.15f - 0.55f * t) * (fade > 0.2f ? 1.0f : fade * 5.0f);
        const Color col = k == 0 ? (Color){ Mul(150, light), Mul(45, light), Mul(45, light), 255 }
                                 : (Color){ Mul(215 - (int)(40.0f * t), light), Mul(180 - (int)(40.0f * t), light), Mul(160 - (int)(30.0f * t), light), 255 };
        if (d2 < 9.0f * 9.0f) DrawSphereEx(pos, rr, 3, 5, col);
        else DrawCube(pos, rr * 2.0f, rr * 2.0f, rr * 2.0f, col);
    }
    if (s->cfg.showCore && Swarm_Alive(s)) {
        const float pulse = 0.5f + 0.5f * sinf(s->time * 6.0f);
        const bool hurt = s->hurtT > 0.0f;
        const Color frame = hurt ? (Color){ 255, 255, 255, 255 } : (Color){ 220, (unsigned char)(40 + 30 * pulse), 40, 255 };
        const float h = s->coreHalf * 2.0f;
        DrawCubeWires(s->core, h, h, h, frame);
        const float g = 0.12f + 0.05f * pulse;
        DrawCube(s->core, g, g, g, hurt ? (Color){ 255, 255, 255, 255 } : (Color){ 255, 70, 50, 255 });
    }
}
