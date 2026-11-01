#ifndef OBSTACLES_H
#define OBSTACLES_H

#include "obstacle_types.h"

void InitObstaclesSystem(void);
void DrawObstaclesSystem(void);
bool CheckObstacleCollision(Vector3 pos, float radius);

#endif

