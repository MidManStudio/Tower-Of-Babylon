#include "world.h"
#include <math.h>

static Shader grassShader;
static Mesh floorMesh;
static Model floorModel;

// Stair configuration
#define STAIR_COUNT 5
static Rectangle stairBoxes[STAIR_COUNT];
static float stairHeights[STAIR_COUNT];

void InitWorld(void)
{
    // Load external GLSL shaders bundled via mray's asset preloader
    grassShader = LoadShader("assets/shaders/grass.vs", "assets/shaders/grass.fs");

    // Floor Plane Mesh
    floorMesh = GenMeshPlane(30.0f, 30.0f, 10, 10);
    floorModel = LoadModelFromMesh(floorMesh);
    floorModel.materials[0].shader = grassShader;

    // Define Stairs Geometry & Bounds
    for (int i = 0; i < STAIR_COUNT; i++) {
        stairHeights[i] = (i + 1) * 0.25f;
        stairBoxes[i] = (Rectangle){ -1.5f, -3.0f - (i * 0.6f), 3.0f, 0.6f };
    }
}

void DrawWorld(void)
{
    // Draw Grass Floor
    DrawModel(floorModel, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);

    // Draw Stairs
    for (int i = 0; i < STAIR_COUNT; i++) {
        Vector3 stairPos = { 0.0f, stairHeights[i] / 2.0f, stairBoxes[i].y + stairBoxes[i].height / 2.0f };
        Vector3 stairSize = { stairBoxes[i].width, stairHeights[i], stairBoxes[i].height };
        DrawCube(stairPos, stairSize.x, stairSize.y, stairSize.z, DARKGREEN);
        DrawCubeWires(stairPos, stairSize.x, stairSize.y, stairSize.z, LIME);
    }
}

bool CheckWorldCollisions(Vector3 *pos, float radius)
{
    bool onStair = false;

    // Check Stair Collisions & Step Heights
    for (int i = 0; i < STAIR_COUNT; i++) {
        if (pos->x >= stairBoxes[i].x && pos->x <= (stairBoxes[i].x + stairBoxes[i].width) &&
            pos->z >= stairBoxes[i].y && pos->z <= (stairBoxes[i].y + stairBoxes[i].height)) {
            
            float targetHeight = stairHeights[i];
            if (pos->y < targetHeight + 0.1f) {
                pos->y = targetHeight;
                onStair = true;
            }
        }
    }
    return onStair;
}

void UnloadWorld(void)
{
    UnloadShader(grassShader);
    UnloadModel(floorModel);
}

