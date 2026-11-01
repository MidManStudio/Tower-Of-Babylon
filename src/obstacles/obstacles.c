#include "obstacles.h"

#define MAX_OBSTACLES 16
static Obstacle obstaclePool[MAX_OBSTACLES];

void InitObstaclesSystem(void) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        obstaclePool[i].active = false;
    }
    // Setup a test obstacle block
    obstaclePool[0] = (Obstacle){
        .position = { 6.0f, 0.5f, 6.0f },
        .size = { 1.5f, 1.0f, 1.5f },
        .type = OBS_CORPO_CRATE,
        .active = true
    };
}

void DrawObstaclesSystem(void) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstaclePool[i].active) {
            DrawCube(obstaclePool[i].position, obstaclePool[i].size.x, obstaclePool[i].size.y, obstaclePool[i].size.z, DARKPURPLE);
            DrawCubeWires(obstaclePool[i].position, obstaclePool[i].size.x, obstaclePool[i].size.y, obstaclePool[i].size.z, PURPLE);
        }
    }
}

bool CheckObstacleCollision(Vector3 pos, float radius) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstaclePool[i].active) continue;
        // Simple bounding box collision check
        float minX = obstaclePool[i].position.x - obstaclePool[i].size.x/2 - radius;
        float maxX = obstaclePool[i].position.x + obstaclePool[i].size.x/2 + radius;
        float minZ = obstaclePool[i].position.z - obstaclePool[i].size.z/2 - radius;
        float maxZ = obstaclePool[i].position.z + obstaclePool[i].size.z/2 + radius;

        if (pos.x >= minX && pos.x <= maxX && pos.z >= minZ && pos.z <= maxZ) {
            return true;
        }
    }
    return false;
}

