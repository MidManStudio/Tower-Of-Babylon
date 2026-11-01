// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/player.md, section "player_types.h and player_controller.c"
// ============================================================================
#ifndef PLAYER_TYPES_H
#define PLAYER_TYPES_H

#include "raylib.h"
#include <stdbool.h>
#include "../shared/health.h"

typedef struct {
    Vector3 position;
    float velocityY;
    float radius;
    float height;
    bool isGrounded;
    float eyeY;          // smoothed height the camera follows, so stair steps do not jolt the view
    float fireCooldown;
    float fireRate;

    // Health and the effects of being hit
    HealthComponent health;   // invulnerabilityTimer is the short grace period after a hit
    float hitFlash;           // 1 right after a hit, fades to 0: drives the red flash
    float shake;              // 1 right after a hit, fades to 0: drives the camera shake
    float shakeClock;
    Vector3 knock;            // knockback velocity on the floor plane, m/s
    
    // Movement. The player has a velocity on the floor plane that speeds up and slows down; sprinting costs stamina.
    Vector3 moveVel;          // m/s on the floor plane (y unused)
    float stamina;            // 0 to PLAYER_STAMINA_MAX
    float staminaDelay;       // seconds before stamina starts to come back after it was spent
    bool exhausted;           // stamina ran out: slow and unable to sprint until it recovers
    bool wantSprint;          // set by the caller each frame; sprinting also needs stamina and forward movement
    bool sprinting;           // true while it is actually happening
    float sprintBlend;        // 0 to 1, eased: drives the field of view kick
    float bobPhase;           // advances with distance walked: the head bob
    float breathClock;

    // Sliding: started from a sprint, a short fast skid along the floor that runs down, with the camera dropped low.
    bool  sliding;
    float slideT;             // seconds into the slide
    float slideSpeed0;        // m/s at the start; the speed runs down from it to PLAYER_SLIDE_END_SPEED
    float slideCooldown;      // seconds until another slide may start
    float slideBlend;         // 0 to 1, eased: drives the dropped camera
    Vector3 slideDir;         // the way it is going (unit, on the floor plane); the stick steers it a little

    // Camera state
    float yaw;
    float pitch;
    Camera3D camera;
} PlayerController;

#endif

