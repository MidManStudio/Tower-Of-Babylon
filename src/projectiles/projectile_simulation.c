// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectile_simulation.h and projectile_simulation.c"
// ============================================================================
#include "projectile_simulation.h"
#include <math.h>

static void TickStraight(Projectile *p, float dt) {
    p->vx += p->ax * dt;
    p->vy += p->ay * dt;
    p->vz += p->az * dt;
    p->x += p->vx * dt;
    p->y += p->vy * dt;
    p->z += p->vz * dt;
}

static void TickWave(Projectile *p, float dt) {
    p->x += p->vx * dt;
    p->y += p->vy * dt;
    p->z += p->vz * dt;
    p->curveT += dt;
    float freq = 4.0f;
    float amp = 0.4f;
    float phase = p->curveT * freq * 2.0f * PI;
    p->y += amp * sinf(phase) * dt * 5.0f;
}

static void TickCircular(Projectile *p, float dt) {
    p->curveT += dt;
    float omega = 3.14159f;
    float angle = p->curveT * omega;
    p->x += (p->vx * dt) + cosf(angle) * 0.04f;
    p->y += p->vy * dt;
    p->z += (p->vz * dt) + sinf(angle) * 0.04f;
}

static void TickScale(Projectile *p, float dt) {
    if (p->scaleSpeed == 0.0f) return;
    float diff = p->scaleTarget - p->scaleX;
    if (fabsf(diff) > 0.001f) {
        float delta = diff * p->scaleSpeed * dt;
        p->scaleX += delta;
        p->scaleY += delta;
        p->scaleZ += delta;
    }
}

int TickProjectiles(Projectile *projs, int count, float dt) {
    int died = 0;
    for (int i = 0; i < count; i++) {
        Projectile *p = &projs[i];
        if (!p->alive) continue;

        p->lifetime += dt;
        if (p->lifetime >= p->maxLifetime || p->y < 0.0f) {
            p->alive = 0;
            died++;
            continue;
        }

        p->px = p->x; p->py = p->y; p->pz = p->z;

        switch (p->movementType) {
            case MOVE_WAVE:
                TickWave(p, dt);
                break;
            case MOVE_CIRCULAR:
                TickCircular(p, dt);
                break;
            case MOVE_STRAIGHT:
            default:
                TickStraight(p, dt);
                break;
        }

        TickScale(p, dt);

        {
            // The distance actually flown this frame, whatever the movement type did to the position.
            const float dx = p->x - p->px, dy = p->y - p->py, dz = p->z - p->pz;
            p->travelDist += sqrtf(dx*dx + dy*dy + dz*dz);
        }
        if (p->maxRange > 0.0f && p->travelDist >= p->maxRange) { p->alive = 0; died++; }
    }
    return died;
}
