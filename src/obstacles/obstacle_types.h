#ifndef OBSTACLE_TYPES_H
#define OBSTACLE_TYPES_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
    OBS_BARRIER_LASER,
    OBS_CORPO_CRATE,
    OBS_TURRET_BASE
} ObstacleType;

typedef struct {
    Vector3 position;
    Vector3 size;
    ObstacleType type;
    bool active;
} Obstacle;

#endif

