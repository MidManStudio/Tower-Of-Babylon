// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "enemy_types.h"
// ============================================================================
#ifndef ENEMY_TYPES_H
#define ENEMY_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"
#include "../shared/health.h"

typedef enum {
    ENEMY_STATE_IDLE = 0,
    ENEMY_STATE_CHASE,
    ENEMY_STATE_ATTACK,
    ENEMY_STATE_DEAD
} EnemyState;

typedef struct {
    uint32_t        id;
    Vector3         position;     // centre of the cylinder base
    Vector3         prevPosition; // where it was at the start of the last step: drawing blends from here (Enemies_BeginTick)
    float           radius;
    float           height;
    Color           tint;
    HealthComponent health;
    EnemyState      state;
    float           aggroRange;   // sight distance in world units
    float           attackRange;
    float           moveSpeed;
    float           alertTimer;   // seconds left to keep chasing after losing sight
    float           attackTimer;  // counts down while in reach of the player; a blow lands at 0
    float           hurtTimer;    // seconds left of the flash and stagger after being hit
    float           rear;         // 0..1: crouching and swelling before a blow (animation only)
    float           lunge;        // 1 at the moment of a blow, fades to 0 (animation only)
    float           faceX, faceZ; // unit direction toward the player while attacking (animation only)
    bool            active;
} Enemy;

// How fast things chase. The player walks at 4.5 m/s and sprints at 7.6 for about six seconds; an exhausted player moves at
// well under 2.5. The enemies are tuned so that a sprint buys a lead and then costs it back: the fastest cylinder matches
// a walking player, a spider is almost as quick, and nothing is slower than a tired player (see docs/player.md).
#define ENEMY_CHASE_SPEED_BASE  3.6f     // tier 0; each tier (mod 4) adds ENEMY_CHASE_SPEED_STEP
#define ENEMY_CHASE_SPEED_STEP  0.3f
#define SPIDER_CHASE_SPEED      4.4f

#endif
