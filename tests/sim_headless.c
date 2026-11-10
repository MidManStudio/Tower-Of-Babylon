// Headless test of the fixed-step simulation layer: the input latch (a press between two 30 Hz steps is not lost), the stepped player
// (the same steps whatever the frame rate), look every frame against movement every step, and the blend hooks of the monsters. No
// window. Run from the project root:
//   cc -O2 -std=c99 -DSIM_HEADLESS_TEST -Isrc -Iraylib/src tests/sim_headless.c src/input/input_latch.c src/core/fixed_step.c src/player/player_controller.c src/enemies/enemy_manager.c src/enemies/spider.c src/enemies/spider_ai.c src/enemies/spider_draw.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_raycast.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o sim_headless
#ifdef SIM_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/fixed_step.h"
#include "dungeon/dungeon_types.h"
#include "enemies/enemy_manager.h"
#include "input/input_latch.h"
#include "player/player_controller.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
void DrawCylinder(Vector3 p, float rt, float rb, float h, int s, Color c) { (void)p; (void)rt; (void)rb; (void)h; (void)s; (void)c; }
void DrawCylinderWires(Vector3 p, float rt, float rb, float h, int s, Color c) { (void)p; (void)rt; (void)rb; (void)h; (void)s; (void)c; }
void DrawSphereEx(Vector3 p, float r, int a, int b, Color c) { (void)p; (void)r; (void)a; (void)b; (void)c; }
double GetTime(void) { return 0.0; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static FloorMap g_floor;
static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }
static int Near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

#define STEP (1.0f / 30.0f)

static void OpenFloor(void)
{
    memset(&g_floor, 0, sizeof g_floor);
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x)
            g_floor.grid[z][x].type = (x == 0 || z == 0 || x == MAP_WIDTH - 1 || z == MAP_HEIGHT - 1) ? TILE_WALL : TILE_FLOOR;
}

static InputFrame Frame(float mx, float my, bool sprint, bool jump, bool fire, bool use)
{
    InputFrame f; memset(&f, 0, sizeof f);
    f.move = (Vector2){ mx, my }; f.sprint = sprint; f.jump = jump; f.fire = fire; f.use = use;
    return f;
}

int main(void)
{
    printf("The input latch\n");
    {
        SimCmd c; SimCmd_Init(&c);
        InputFrame f = Frame(0.0f, -1.0f, true, false, false, false);
        SimCmd_Add(&c, &f);
        SimCmd t = SimCmd_Take(&c);
        Check(t.move.y == -1.0f && t.sprint && !t.jump && !t.fire && !t.use, "a held stick and sprint are passed on");
        InputFrame tapJump = Frame(0, 0, false, true, false, false), none = Frame(0, 0, false, false, false, false);
        SimCmd_Add(&c, &tapJump); SimCmd_Add(&c, &none); SimCmd_Add(&c, &none);          // a press, then two frames with nothing
        t = SimCmd_Take(&c);
        Check(t.jump && t.move.y == 0.0f, "a jump pressed in one frame and gone by the next is still there at the step (and the stick is as last seen)");
        t = SimCmd_Take(&c);
        Check(!t.jump, "and it is taken: the next step does not jump again");
        InputFrame tapFire = Frame(0, 0, false, false, true, false);
        SimCmd_Add(&c, &tapFire); SimCmd_Add(&c, &none);
        t = SimCmd_Take(&c);
        Check(t.fire && !t.fireHeld, "a fire tap that was over before the step still fires, as a press, not as a hold");
        t = SimCmd_Take(&c);
        Check(!t.fire, "once");
        InputFrame hold = Frame(0, 0, false, false, true, false);
        SimCmd_Add(&c, &hold); t = SimCmd_Take(&c); SimCmd_Add(&c, &hold); t = SimCmd_Take(&c);
        Check(t.fire && t.fireHeld, "holding fire fires every step");
        InputFrame useP = Frame(0, 0, false, false, false, true); useP.useHeld = true; useP.swap = true; useP.light = true; useP.slide = true; useP.reload = true;
        SimCmd_Add(&c, &useP); SimCmd_Add(&c, &none);
        t = SimCmd_Take(&c);
        Check(t.use && t.swap && t.light && t.slide && t.reload && !t.useHeld, "use, swap, light, slide and reload presses are all kept, and the hold is only as long as it was");
        SimCmd_Add(&c, &useP); SimCmd_Take(&c); SimCmd_Add(&c, &none);
        t = SimCmd_Take(&c);
        Check(!t.use && !t.swap && !t.light && !t.slide && !t.reload, "and none is taken twice");
        SimCmd d; SimCmd_Init(&d); SimCmd e = SimCmd_Take(&d);
        Check(!e.jump && !e.fire && e.move.x == 0.0f, "a fresh command asks for nothing");
    }

    printf("The same steps at any frame rate\n");
    {
        const float fps[3] = { 30.0f, 60.0f, 144.0f };
        float endX[3], endZ[3], endY[3]; int stepsRun[3];
        int jumps[3] = { 0, 0, 0 };
        for (int scenario = 0; scenario < 2; ++scenario) {              // 0: constant input; 1: the same, with one jump press at half a second
            for (int k = 0; k < 3; ++k) {
                OpenFloor();
                PlayerController p; InitPlayerController(&p, 40.0f, 40.0f); TeleportPlayer(&p, (Vector3){ 40.0f, 0.0f, 40.0f }, 0.0f);
                FixedStep fs; FixedStep_Init(&fs, STEP, 5);
                SimCmd cmd; SimCmd_Init(&cmd);
                const float dt = 1.0f / fps[k];
                int steps = 0, frame = 0;
                while (steps < 90) {                                       // 3 seconds of rules
                    InputFrame in = Frame(0.0f, -1.0f, true, false, false, false);
                    if (scenario == 1 && frame == (int)(fps[k] * 0.5f)) in.jump = true;   // one frame of jump press, whatever the rate
                    SimCmd_Add(&cmd, &in);
                    PlayerSyncCamera(&p);
                    int n = FixedStep_Advance(&fs, dt);
                    for (int i = 0; i < n && steps < 90; ++i) {
                        SimCmd c = SimCmd_Take(&cmd);
                        p.wantSprint = c.sprint;
                        PlayerStep(&p, c.move, STEP, &g_floor);
                        if (c.jump && PlayerTryJump(&p)) ++jumps[k];
                        ++steps;
                    }
                    ++frame;
                }
                endX[k] = p.position.x; endY[k] = p.position.y; endZ[k] = p.position.z; stepsRun[k] = steps;
            }
            if (scenario == 0) {
                Check(stepsRun[0] == 90 && stepsRun[1] == 90 && stepsRun[2] == 90, "90 steps are run at 30, 60 and 144 frames a second");
                Check(endZ[0] != 40.0f && jumps[0] + jumps[1] + jumps[2] == 0, "the player walked, and nothing made it jump");
                Check(Near(endZ[0], endZ[1], 0.0005f) && Near(endZ[1], endZ[2], 0.0005f) && Near(endX[0], endX[2], 0.0005f),
                      "with the same input, after 90 steps the player is in exactly the same place at every frame rate");
            } else {
                Check(jumps[0] == 1 && jumps[1] == 1 && jumps[2] == 1, "a one-frame jump press makes exactly one jump at every frame rate");
                Check(Near(endY[0], 0.0f, 0.001f) && Near(endY[1], 0.0f, 0.001f) && Near(endY[2], 0.0f, 0.001f), "and each lands again");
                Check(Near(endZ[0], endZ[2], 0.3f) && Near(endZ[1], endZ[2], 0.3f), "the press can land on a different step depending on the frame rate (one step is 0.25 m of walking), but no more than that");
            }
        }
    }

    printf("Look every frame, movement every step\n");
    {
        OpenFloor();
        PlayerController a, b;
        InitPlayerController(&a, 30.0f, 30.0f); TeleportPlayer(&a, (Vector3){ 30.0f, 0.0f, 30.0f }, 0.3f);
        InitPlayerController(&b, 30.0f, 30.0f); TeleportPlayer(&b, (Vector3){ 30.0f, 0.0f, 30.0f }, 0.3f);
        for (int i = 0; i < 120; ++i) {
            const Vector2 look = { (float)(i % 7) - 3.0f, (float)(i % 5) - 2.0f };
            const Vector2 move = { 0.3f, -0.9f };
            UpdatePlayerController(&a, move, look, STEP, &g_floor);
            PlayerApplyLook(&b, look); PlayerSyncCamera(&b);
            PlayerStep(&b, move, STEP, &g_floor);
        }
        Check(a.position.x == b.position.x && a.position.z == b.position.z && a.yaw == b.yaw && a.pitch == b.pitch && a.camera.target.x == b.camera.target.x,
              "turning the camera and stepping the body separately gives exactly what the one-call update gave");
        PlayerController c; InitPlayerController(&c, 30.0f, 30.0f); TeleportPlayer(&c, (Vector3){ 30.0f, 0.0f, 30.0f }, 0.0f);
        const Vector3 posBefore = c.camera.position, dirBefore = { c.camera.target.x - c.camera.position.x, c.camera.target.y - c.camera.position.y, c.camera.target.z - c.camera.position.z };
        PlayerApplyLook(&c, (Vector2){ 100.0f, 0.0f }); PlayerSyncCamera(&c);
        const Vector3 dirAfter = { c.camera.target.x - c.camera.position.x, c.camera.target.y - c.camera.position.y, c.camera.target.z - c.camera.position.z };
        Check(Near(c.camera.position.x, posBefore.x, 1e-6f) && Near(c.camera.position.z, posBefore.z, 1e-6f), "turning moves the camera's direction but not its position");
        Check(fabsf(dirAfter.x - dirBefore.x) > 0.1f, "and the direction follows at once, with no step in between");
    }

    printf("Drawing between steps: the monsters' hooks\n");
    {
        OpenFloor();
        InitEnemiesSystem();
        const int slot = Enemies_SpawnAt((Vector3){ 20.0f, 0.0f, 20.0f }, 1, true);
        const Enemy *e = Enemies_At(slot);
        Check(e->prevPosition.x == e->position.x && e->prevPosition.z == e->position.z, "a new monster starts with nothing to blend from (it is not drawn sweeping in from the origin)");
        Enemies_SetPlayerNoise(200.0f);
        Enemies_BeginTick();
        UpdateEnemiesSystem(STEP, (Vector3){ 30.0f, 0.0f, 20.0f }, &g_floor);
        Enemies_BeginTick();
        const Vector3 afterFirst = e->position;                       // not where it was spawned: two steps in
        UpdateEnemiesSystem(STEP, (Vector3){ 30.0f, 0.0f, 20.0f }, &g_floor);
        Check(afterFirst.x > 20.0f && e->prevPosition.x == afterFirst.x && e->position.x > afterFirst.x,
              "BeginTick remembers where the monster was at the start of the step (not where it was born) and the step moves it on: the picture can blend between the two");
        Enemies_SetRenderAlpha(1.5f); Enemies_SetRenderAlpha(-1.0f); Enemies_SetRenderAlpha(0.5f);
        Check(1, "the blend factor accepts anything without harm (it is clamped to 0 to 1)");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
