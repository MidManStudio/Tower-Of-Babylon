// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectile_types.h"
// ============================================================================
#ifndef PROJECTILE_TYPES_H
#define PROJECTILE_TYPES_H

#include "raylib.h"
#include <stdint.h>
#include <stdbool.h>

// Movement and piercing numbers match Unity's ProjectileMovementType and ProjectilePiercingType, and the enums in
// assets/config/projectiles.mdix.
#define MOVE_STRAIGHT 0
#define MOVE_GUIDED   2
#define MOVE_TELEPORT 3
#define MOVE_WAVE     4
#define MOVE_CIRCULAR 5

#define PIERCE_NONE   0
#define PIERCE_PIERCER 1
#define PIERCE_RANDOM 2

#define MAX_PROJECTILES        64
#define MAX_PROJECTILE_CONFIGS 16

// One projectile config: how a shot flies, fades and looks. Read from assets/config/projectiles.mdix; a weapon refers to one by index.
typedef struct {
    char     id[24];
    int      motion;                     // MOVE_*
    float    speedMin, speedMax;         // m/s; a pellet picks its own between them
    float    lifetime, maxRange;         // seconds and metres, whichever ends first
    float    gravity;                    // 1 falls like a stone (9.8 m/s per second)
    float    radius;                     // added to a target's own radius when testing a hit
    float    falloffStart, falloffEnd;   // metres: full damage up to start, damage * falloffMin from end on
    float    falloffMin;
    int      piercing;                   // PIERCE_*
    int      maxHits;                    // targets one shot may hit when piercing (1 otherwise)
    float    trailLength, trailWidth;    // the glowing streak, metres
    unsigned colorCore, colorGlow;       // 0xRRGGBB
} ProjectileConfig;

// One live projectile.
typedef struct {
    float x, y, z;                       // now
    float px, py, pz;                    // at the start of this frame's move: the swept hit test runs from here to there
    float vx, vy, vz;
    float ax, ay, az;
    float curveT;
    float scaleX, scaleY, scaleZ;
    float scaleTarget, scaleSpeed;
    float lifetime, maxLifetime;
    float travelDist;
    float maxRange;
    float damage;                        // before the distance falloff
    float noHitUntil;                    // travelDist below which this projectile skips hit tests (just after it went through a target)
    uint16_t configId;                   // index into the projectile configs
    uint16_t ownerId;
    uint32_t projId;
    uint8_t collisionCount;
    uint8_t movementType;
    uint8_t piercingType;
    uint8_t alive;
} Projectile;

#endif
