#ifndef OBSTACLES_H
#define OBSTACLES_H

#include "obstacle_types.h"

void InitObstaclesSystem(void);
void DrawObstaclesSystem(void);
bool CheckObstacleCollision(Vector3 pos, float radius);
// A networked game has no props yet (the server has none: they are this process's list, made for the single-player floor), so the player's
// movement must not meet them there or the client's guess and the server's answer would differ at a crate. While suspended nothing collides
// and nothing is drawn; the list itself is untouched, so solo play comes back as it was.
void Obstacles_Suspend(bool suspended);

#endif

