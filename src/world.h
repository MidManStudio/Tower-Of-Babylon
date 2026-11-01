#ifndef WORLD_H
#define WORLD_H

#include "raylib.h"

void InitWorld(void);
void DrawWorld(void);
bool CheckWorldCollisions(Vector3 *pos, float radius);
void UnloadWorld(void);

#endif

