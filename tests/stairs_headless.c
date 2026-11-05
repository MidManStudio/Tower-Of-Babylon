// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "stairs_headless.c"
// ============================================================================
// Everything is inside the macro so mray's build, which compiles every .c file,
// does not pick up a second main(). It links the real player controller and
// staircase code and stubs the draw calls and the obstacle test.
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DSTAIRS_HEADLESS_TEST -Isrc -Iraylib/src tests/stairs_headless.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o stairs_headless
// Run:
//   ./stairs_headless
#ifdef STAIRS_HEADLESS_TEST

#include <math.h>
#include <stdio.h>

#include "dungeon/dungeon_generator.h"
#include "dungeon/stair_collision.h"
#include "dungeon/staircase.h"
#include "player/player_controller.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static FloorMap g_floor;
static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

#define DT (1.0f / 60.0f)

static PlayerController Place(float x, float y, float z, float yaw)
{
    PlayerController p;
    InitPlayerController(&p, x, z);
    TeleportPlayer(&p, (Vector3){ x, y, z }, yaw);
    return p;
}

// Holds "forward" for up to maxFrames, stopping early when stop() says so.
// Returns the largest height change in one frame and whether the player ever left the ground.
typedef struct { float maxStep; bool airborne; int frames; } Walk;

static Walk WalkForward(PlayerController *p, int maxFrames, bool (*stop)(const PlayerController *))
{
    Walk w = { 0.0f, false, 0 };
    for (int i = 0; i < maxFrames; ++i) {
        const float y0 = p->position.y;
        UpdatePlayerController(p, (Vector2){ 0.0f, -1.0f }, (Vector2){ 0.0f, 0.0f }, DT, &g_floor);
        const float dy = fabsf(p->position.y - y0);
        if (dy > w.maxStep) w.maxStep = dy;
        if (!p->isGrounded) w.airborne = true;
        ++w.frames;
        if (stop && stop(p)) break;
    }
    return w;
}

static bool StopX_ge84(const PlayerController *p) { return p->position.x >= 85.0f; }
static bool StopZ_ge13(const PlayerController *p) { return p->position.z >= 13.5f; }
static bool StopX_le75(const PlayerController *p) { return p->position.x <= 75.6f; }
static bool StopX_ge84_5(const PlayerController *p) { return p->position.x >= 84.5f; }
static bool StopZ_le8(const PlayerController *p) { return p->position.z <= 8.0f; }
static bool StopX_le74(const PlayerController *p) { return p->position.x <= 74.5f; }

int main(void)
{
    GenerateFloorMap(&g_floor, 2, 12345u);
    const float EAST = PI / 2.0f, WEST = -PI / 2.0f, SOUTH = 0.0f, NORTH = PI;

    printf("Ground height\n");
    Check(Staircase_GroundHeight(74.0f, 8.0f, 0.0f) == 0.0f, "open floor in the lobby is at 0");
    Check(fabsf(Staircase_GroundHeight(76.25f, 8.0f, 0.0f) - 0.25f) < 1e-4f, "first step is 0.25 high");
    Check(Staircase_GroundHeight(80.25f, 8.0f, 0.0f) == 0.0f, "a step 2 m up is not reachable from the floor");
    Check(fabsf(Staircase_GroundHeight(74.5f, 13.5f, 8.0f) - 8.0f) < 1e-4f, "top landing is at 8");
    Check(Staircase_GroundHeight(74.5f, 13.5f, 0.0f) == 0.0f, "walking under the top landing is not lifted onto it");

    printf("Climb\n");
    PlayerController p = Place(74.5f, 0.0f, 8.0f, EAST);
    Walk w = WalkForward(&p, 600, StopX_ge84);
    Check(p.position.x >= 85.0f, "run A walked to the landing");
    Check(fabsf(p.position.y - 4.0f) < 1e-3f, "landing is 4 m up");
    Check(!w.airborne, "stayed on the steps the whole way up run A");
    Check(w.maxStep <= STEP_RISE + 1e-3f, "no jump bigger than one step per frame");
    Check(fabsf(p.position.z - 8.0f) < 0.6f, "stayed in the lane");

    p.yaw = SOUTH;
    w = WalkForward(&p, 600, StopZ_ge13);
    Check(p.position.z >= 13.5f && fabsf(p.position.y - 4.0f) < 1e-3f, "crossed the landing to the foot of run B");

    p.yaw = WEST;
    w = WalkForward(&p, 900, StopX_le75);
    Check(p.position.x <= 75.6f, "run B walked to the top");
    Check(fabsf(p.position.y - 8.0f) < 1e-3f, "top landing is 8 m up");
    Check(!w.airborne && w.maxStep <= STEP_RISE + 1e-3f, "smooth climb on run B");
    Check(!Staircase_DoorInRange(DOOR_DOWN, p.position), "the down door is nowhere near");
    WalkForward(&p, 600, NULL);
    Check(p.position.x >= 73.40f && p.position.x < 73.55f, "stopped 0.45 m short of the wall in front of the door");
    Check(Staircase_DoorInRange(DOOR_UP, p.position), "still in reach at the wall");

    printf("Descend\n");
    Vector3 sp;
    float syaw;
    Staircase_SpawnAbove(&sp, &syaw);
    p = Place(sp.x, sp.y, sp.z, syaw);
    Check(!Staircase_DoorInRange(DOOR_UP, p.position), "arriving from above is outside the door's reach");
    Check(Staircase_GroundHeight(p.position.x, p.position.z, p.position.y) == 8.0f, "arrival stands on the top landing");
    w = WalkForward(&p, 500, StopX_ge84_5);
    Check(!w.airborne, "walking down run B stays on the steps");
    Check(fabsf(p.position.y - 4.0f) < 1e-3f, "reached the landing going down");
    p.yaw = NORTH;
    WalkForward(&p, 600, StopZ_le8);
    p.yaw = WEST;
    w = WalkForward(&p, 900, StopX_le74);
    Check(!w.airborne, "walking down run A stays on the steps");
    Check(p.position.y == 0.0f, "back on the floor");

    printf("Railings and mass\n");
    p = Place(80.25f, 2.0f, 8.0f, SOUTH);    // standing on run A, facing across the lane
    WalkForward(&p, 300, NULL);
    Check(p.position.z <= 9.5f - 0.3f + 0.01f && p.position.y > 1.9f, "railing stops the player leaving run A sideways");
    p = Place(80.25f, 2.0f, 8.0f, NORTH);
    WalkForward(&p, 300, NULL);
    Check(p.position.z >= 6.5f + 0.3f - 0.01f && p.position.y > 1.9f, "railing on the other side too");
    p = Place(80.0f, 0.0f, 10.75f, SOUTH);   // floor level, in the slot between the two runs, facing run B
    WalkForward(&p, 300, NULL);
    Check(p.position.z <= 12.0f - 0.3f + 0.01f && p.position.y == 0.0f, "the mass of run B stops a player on the floor");

    printf("Wall clearance\n");
    p = Place(70.0f, 0.0f, 2.5f, NORTH);                    // hall, facing the north border wall
    WalkForward(&p, 300, NULL);
    Check(p.position.z >= 1.40f && p.position.z < 1.50f, "stops 0.45 m from a wall face, not at it");
    p = Place(76.0f, 0.0f, 76.0f, NORTH);                   // down alcove, run diagonally into its north-west corner
    for (int i = 0; i < 400; ++i)
        UpdatePlayerController(&p, (Vector2){ -1.0f, -1.0f }, (Vector2){ 0.0f, 0.0f }, DT, &g_floor);
    Check(fabsf(p.position.x - 73.45f) < 0.06f && fabsf(p.position.z - 73.45f) < 0.06f, "slides along both walls into the corner and stops 0.45 m from each");
    p = Place(73.2f, 0.0f, 76.0f, WEST);                    // placed inside the clearance: may walk away, not deeper
    UpdatePlayerController(&p, (Vector2){ 0.0f, -1.0f }, (Vector2){ 0.0f, 0.0f }, DT, &g_floor);
    Check(p.position.x >= 73.2f - 0.0001f, "a player already inside the clearance is not pushed deeper into the wall");
    p.yaw = EAST;
    WalkForward(&p, 60, NULL);
    Check(p.position.x > 74.0f, "and can walk away from it");

    printf("Falling and jumping\n");
    p = Place(80.0f, 3.0f, 10.75f, SOUTH);
    p.isGrounded = false;
    for (int i = 0; i < 120; ++i) UpdatePlayerController(&p, (Vector2){ 0, 0 }, (Vector2){ 0, 0 }, DT, &g_floor);
    Check(p.position.y == 0.0f && p.isGrounded, "dropped from 3 m lands on the floor");
    p = Place(80.25f, 2.0f, 8.0f, EAST);
    UpdatePlayerController(&p, (Vector2){ 0, 0 }, (Vector2){ 0, 0 }, DT, &g_floor);
    p.velocityY = 6.0f;
    p.isGrounded = false;
    float apex = p.position.y;
    for (int i = 0; i < 120; ++i) {
        UpdatePlayerController(&p, (Vector2){ 0, 0 }, (Vector2){ 0, 0 }, DT, &g_floor);
        if (p.position.y > apex) apex = p.position.y;
    }
    Check(apex > 2.9f && apex < 3.6f, "a jump on the stairs rises about 1.3 m");
    Check(p.isGrounded && fabsf(p.position.y - 2.0f) < 0.3f, "and lands back on the flight");

    printf("Doors\n");
    Staircase_SpawnBelow(&sp, &syaw);
    Check(!Staircase_DoorInRange(DOOR_DOWN, sp), "arriving from below is outside the down door's reach");
    p = Place(sp.x, sp.y, sp.z, EAST);
    WalkForward(&p, 40, NULL);
    Check(Staircase_DoorInRange(DOOR_DOWN, p.position), "walking up to the down door puts it in reach");
    Check(Staircase_DoorLocked(DOOR_DOWN, &g_floor) == false, "down door is open on floor 2");
    GenerateFloorMap(&g_floor, 1, 12345u);
    Check(Staircase_DoorLocked(DOOR_DOWN, &g_floor), "down door is locked on floor 1");
    Check(!Staircase_DoorLocked(DOOR_UP, &g_floor), "up door is never locked");

    printf("\nThe flight as solid geometry: rays, push-out, distance\n");
    {
        float tt; Vector3 n;
        const Vector3 east = { 1, 0, 0 };
        Check(Stair_Raycast((Vector3){ 74.0f, 1.1f, 8.0f }, east, 50.0f, &tt, &n) && fabsf(tt - 4.0f) < 1e-3f && n.x == -1.0f && n.y == 0.0f && n.z == 0.0f,
              "a ray along run A at 1.1 m flies over the first four steps and meets the face of the fifth at 4 m, normal pointing back at the shooter");
        Check(Stair_Raycast((Vector3){ 76.25f, 2.0f, 8.0f }, (Vector3){ 0, -1, 0 }, 10.0f, &tt, &n) && fabsf(tt - 1.75f) < 1e-3f && n.y == 1.0f,
              "a ray straight down onto the first step lands on its top, 0.25 m up, normal up");
        Check(!Stair_Raycast((Vector3){ 74.0f, 9.0f, 8.0f }, east, 50.0f, &tt, &n), "a ray above the whole flight hits nothing");
        Check(!Stair_Raycast((Vector3){ 74.0f, 1.1f, 8.0f }, east, 3.9f, &tt, &n), "and one that is too short to reach the face does not hit it");
        Check(Stair_Raycast((Vector3){ 76.25f, 0.1f, 8.0f }, east, 5.0f, &tt, &n) && fabsf(tt - 0.25f) < 1e-3f,
              "a ray that starts inside a step ignores that step (like a ray starting in a wall) and meets the next");
        Check(Stair_Raycast((Vector3){ 80.25f, 3.0f, 3.0f }, (Vector3){ 0, 0, 1 }, 30.0f, &tt, &n) && fabsf(tt - 9.0f) < 1e-3f && n.z == -1.0f,
              "railings are bars: a ray at rail height goes between them and meets run B's face, not the rail");

        Vector3 c = { 76.1f, 0.1f, 8.0f };                         // the middle of the first step
        Check(Stair_PushOutSphere(&c, 0.3f) && Stair_SurfaceDistance(c) >= 0.3f - 1e-3f, "a sphere whose centre is inside a step is pushed out of it");
        Vector3 b = { 75.9f, 0.1f, 8.0f };                         // 10 cm from the face of the first step
        Check(Stair_PushOutSphere(&b, 0.3f) && fabsf(b.x - 75.7f) < 1e-3f && b.y == 0.1f, "a sphere touching a face is pushed straight back along the normal, to its own radius");
        Vector3 far = { 60.0f, 1.0f, 8.0f };
        Check(!Stair_PushOutSphere(&far, 0.3f) && far.x == 60.0f, "a sphere far from the flight is left alone");
        Check(Stair_SurfaceDistance((Vector3){ 60.0f, 1.0f, 8.0f }) > 10.0f && Stair_SurfaceDistance((Vector3){ 76.25f, 0.1f, 8.0f }) == 0.0f, "distance to the flight: far away is far, inside is 0");
        Check(Stair_Box(0) != NULL && Stair_Box(-1) == NULL && Stair_Box(1000) == NULL, "the box list answers NULL outside itself");
    }

    printf("\nThe route a monster walks\n");
    {
        float wx = -1.0f, wz = -1.0f;
        const Vector3 top = { 74.5f, 8.0f, 13.5f };
        Check(!Stair_OnFlight(60.0f, 8.0f, 0.0f) && !Stair_OnFlight(80.0f, 10.5f, 0.0f) && Stair_OnFlight(76.25f, 8.0f, 0.25f) && Stair_OnFlight(74.5f, 13.5f, 8.0f),
              "the flight is where the steps are, above the floor: the slot between the runs at floor level is not on it");
        Check(!Stair_ChaseWaypoint((Vector3){ 60, 0, 8 }, (Vector3){ 50, 0, 8 }, &wx, &wz), "with neither on the flight a monster just walks at its target");
        Check(Stair_ChaseWaypoint((Vector3){ 60, 0, 8 }, (Vector3){ 76.25f, 0.25f, 8 }, &wx, &wz) && fabsf(wx - 70.0f) < 0.01f && fabsf(wz - 8.0f) < 0.01f,
              "from the hall to a target on the flight: first to the doorway");
        Check(Stair_ChaseWaypoint((Vector3){ 70, 0, 8 }, top, &wx, &wz) && fabsf(wx - 75.0f) < 0.01f && fabsf(wz - 8.0f) < 0.01f,
              "in front of the doorway and in line with it: in through it to the foot of run A (not back to the doorway: it must not wait there)");
        Check(Stair_ChaseWaypoint((Vector3){ 70, 0, 20 }, top, &wx, &wz) && fabsf(wx - 70.0f) < 0.01f, "in front of the wall but off to the side: to the doorway first");
        Check(Stair_ChaseWaypoint((Vector3){ 80, 0, 3 }, top, &wx, &wz) && fabsf(wx - 75.0f) < 0.01f && fabsf(wz - 8.0f) < 0.01f,
              "inside the alcove but north of the railing: round to the foot of run A");
        Check(Stair_ChaseWaypoint((Vector3){ 75.2f, 0, 8 }, top, &wx, &wz) && fabsf(wx - 85.5f) < 0.01f && fabsf(wz - 8.0f) < 0.01f, "at the foot: along run A");
        Check(Stair_ChaseWaypoint((Vector3){ 80.25f, 2.25f, 8 }, top, &wx, &wz) && fabsf(wx - 85.5f) < 0.01f && fabsf(wz - 8.0f) < 0.01f, "on run A, the player above: on to the landing");
        Check(Stair_ChaseWaypoint((Vector3){ 85.5f, 4.0f, 10 }, top, &wx, &wz) && fabsf(wx - 85.5f) < 0.01f && fabsf(wz - 13.5f) < 0.01f, "on the landing: across to the start of run B");
        Check(!Stair_ChaseWaypoint((Vector3){ 80, 6.0f, 13.5f }, top, &wx, &wz), "on run B with the player ahead on it: straight at them");
        Check(Stair_ChaseWaypoint((Vector3){ 80, 6.0f, 13.5f }, (Vector3){ 60, 0, 8 }, &wx, &wz) && fabsf(wx - 85.5f) < 0.01f && fabsf(wz - 13.5f) < 0.01f,
              "on run B with the player down on the floor: back to the landing");
        Check(Stair_ChaseWaypoint((Vector3){ 80.25f, 2.25f, 8 }, (Vector3){ 60, 0, 8 }, &wx, &wz) && fabsf(wx - 75.0f) < 0.01f, "on run A with the player below: back down to the foot");
        Check(Stair_ChaseWaypoint((Vector3){ 76.25f, 0.25f, 8 }, (Vector3){ 60, 0, 8 }, &wx, &wz) && fabsf(wx - 75.0f) < 0.01f, "on the first step with the player below: off it, to the foot");
        Check(!Stair_ChaseWaypoint((Vector3){ 75.0f, 0.3f, 8 }, (Vector3){ 60, 0, 8 }, &wx, &wz), "and from the very foot it is an ordinary walk at them");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
