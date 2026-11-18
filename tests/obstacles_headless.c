// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/testing.md, section "obstacles_headless.c"
// ============================================================================
// Headless test of the props' suspend switch (src/obstacles/obstacles.c): a networked game has no props, so while a session is on nothing may
// collide or be drawn; the list itself must come back untouched for solo play.
// Build and run from the project root (the raylib sources are only read for their headers):
//   cc -O2 -std=c99 -DOBSTACLES_HEADLESS_TEST -Isrc -Iraylib/src tests/obstacles_headless.c src/obstacles/obstacles.c -o obstacles_headless
#ifdef OBSTACLES_HEADLESS_TEST
#include <stdio.h>

#include "obstacles/obstacles.h"

static int g_draws = 0;
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; ++g_draws; }
void DrawCubeWires(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; ++g_draws; }

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

int main(void)
{
    InitObstaclesSystem();
    const Vector3 atCrate = { 6.0f, 0.0f, 6.0f }, far = { 40.0f, 0.0f, 40.0f };
    printf("Props and a networked game\n");
    Check(CheckObstacleCollision(atCrate, 0.35f) && !CheckObstacleCollision(far, 0.35f), "solo: the crate is there, and only there");
    DrawObstaclesSystem();
    Check(g_draws == 2, "solo: it is drawn (a box and its edges)");

    Obstacles_Suspend(true);
    g_draws = 0;
    Check(!CheckObstacleCollision(atCrate, 0.35f), "suspended: the player walks through where the crate is (the server has none)");
    DrawObstaclesSystem();
    Check(g_draws == 0, "suspended: nothing is drawn");

    Obstacles_Suspend(false);
    Check(CheckObstacleCollision(atCrate, 0.35f) && !CheckObstacleCollision(far, 0.35f), "resumed: the crate is back, untouched");
    DrawObstaclesSystem();
    Check(g_draws == 2, "resumed: and drawn");

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
