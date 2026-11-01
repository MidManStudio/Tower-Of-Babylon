// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/player.md, section "health_headless.c"
// ============================================================================
// Player health, damage, knockback, the grace period and the screen effect curve. Links the
// real player controller, staircase and screen_fx code and stubs the draw calls. Inside a
// macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DHEALTH_HEADLESS_TEST -Isrc -Iraylib/src tests/health_headless.c src/player/player_controller.c src/dungeon/staircase.c src/ui/screen_fx.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o health_headless
// Run:
//   ./health_headless
#ifdef HEALTH_HEADLESS_TEST

#include <math.h>
#include <stdio.h>

#include "dungeon/dungeon_generator.h"
#include "player/player_controller.h"
#include "ui/screen_fx.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }
Image GenImageGradientRadial(int w, int h, float d, Color i, Color o) { (void)w; (void)h; (void)d; (void)i; (void)o; return (Image){ 0 }; }
Texture2D LoadTextureFromImage(Image img) { (void)img; return (Texture2D){ 0 }; }
void UnloadImage(Image img) { (void)img; }
void UnloadTexture(Texture2D t) { (void)t; }
void SetTextureFilter(Texture2D t, int f) { (void)t; (void)f; }
void SetTextureWrap(Texture2D t, int w) { (void)t; (void)w; }
void DrawTexturePro(Texture2D t, Rectangle s, Rectangle d, Vector2 o, float r, Color c) { (void)t; (void)s; (void)d; (void)o; (void)r; (void)c; }
void DrawLineEx(Vector2 a, Vector2 b, float t, Color c) { (void)a; (void)b; (void)t; (void)c; }
void DrawRectangle(int x, int y, int w, int h, Color c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void DrawRectangleLines(int x, int y, int w, int h, Color c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void DrawText(const char *t, int x, int y, int s, Color c) { (void)t; (void)x; (void)y; (void)s; (void)c; }
Color Fade(Color c, float a) { c.a = (unsigned char)(255.0f * a); return c; }
const char *TextFormat(const char *t, ...) { return t; }

static FloorMap g_floor;
static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

#define DT (1.0f / 60.0f)

static PlayerController Place(float x, float z, float yaw)
{
    PlayerController p;
    InitPlayerController(&p, x, z);
    TeleportPlayer(&p, (Vector3){ x, 0.0f, z }, yaw);
    return p;
}

static void Run(PlayerController *p, float secs)
{
    for (float t = 0.0f; t < secs; t += DT)
        UpdatePlayerController(p, (Vector2){ 0, 0 }, (Vector2){ 0, 0 }, DT, &g_floor);
}

int main(void)
{
    GenerateFloorMap(&g_floor, 2, 12345u);

    printf("Damage\n");
    PlayerController p = Place(70.0f, 10.0f, 0.0f);
    Check(p.health.isAlive && p.health.hp == PLAYER_MAX_HP, "starts alive at full health");
    Check(DamagePlayer(&p, 25.0f, (Vector3){ 66.0f, 0.0f, 10.0f }), "a blow lands");
    Check(p.health.hp == 75.0f, "and takes 25 off");
    Check(p.hitFlash == 1.0f && p.shake == 1.0f, "the flash and the camera shake start at full");
    Check(!DamagePlayer(&p, 25.0f, (Vector3){ 66.0f, 0.0f, 10.0f }) && p.health.hp == 75.0f, "a second blow right away does nothing (grace period)");
    Run(&p, PLAYER_HIT_GRACE_SECS + 0.1f);
    Check(DamagePlayer(&p, 25.0f, (Vector3){ 66.0f, 0.0f, 10.0f }) && p.health.hp == 50.0f, "after the grace period the next blow lands");
    Run(&p, 1.5f);
    Check(p.hitFlash == 0.0f && p.shake == 0.0f, "flash and shake have faded out");

    printf("Knockback\n");
    p = Place(70.0f, 10.0f, 0.0f);
    DamagePlayer(&p, 10.0f, (Vector3){ 66.0f, 0.0f, 10.0f });     // blow from the west
    Run(&p, 1.0f);
    Check(p.position.x - 70.0f > 0.8f && p.position.x - 70.0f < 2.0f, "knocked 1 to 2 m, away from the source");
    Check(fabsf(p.position.z - 10.0f) < 0.01f, "straight along the line from the source");
    Check(fabsf(p.knock.x) < 0.06f && fabsf(p.knock.z) < 0.06f, "and the push has died away");
    p = Place(70.0f, 10.0f, 1.0f);
    DamagePlayer(&p, 10.0f, (Vector3){ 70.0f, 0.0f, 10.0f });     // from exactly the same spot
    Run(&p, 0.5f);
    Check(hypotf(p.position.x - 70.0f, p.position.z - 10.0f) > 0.5f, "a blow from the player's own position still pushes, backwards");
    p = Place(74.0f, 76.0f, 0.0f);                                // down alcove, 1 m from its west wall
    DamagePlayer(&p, 10.0f, (Vector3){ 80.0f, 0.0f, 76.0f });     // blow from the east: pushed into the wall
    Run(&p, 1.0f);
    Check(p.position.x >= 73.44f, "a knockback into a wall stops 0.45 m short of it");

    printf("Death and reset\n");
    p = Place(70.0f, 10.0f, 0.0f);
    for (int i = 0; i < 5; ++i) { DamagePlayer(&p, 30.0f, (Vector3){ 66.0f, 0.0f, 10.0f }); Run(&p, PLAYER_HIT_GRACE_SECS + 0.05f); }
    Check(!p.health.isAlive && p.health.hp == 0.0f, "health stops at zero and the player is dead");
    Check(!DamagePlayer(&p, 10.0f, (Vector3){ 66.0f, 0.0f, 10.0f }), "the dead cannot be hurt further");
    ResetPlayerHealth(&p);
    Check(p.health.isAlive && p.health.hp == PLAYER_MAX_HP && p.hitFlash == 0.0f && p.knock.x == 0.0f, "reset gives full health and clears the effects");

    printf("Vignette curve\n");
    Check(ScreenFx_VignetteStrength(1.0f) == 0.0f && ScreenFx_VignetteStrength(0.75f) == 0.0f, "none at 75% health or above");
    Check(ScreenFx_VignetteStrength(0.0f) == 1.0f, "full at no health");
    float prev = 0.0f;
    int rising = 1;
    for (float h = 0.75f; h >= 0.0f; h -= 0.05f) {
        const float v = ScreenFx_VignetteStrength(h);
        if (v < prev - 1e-6f) rising = 0;
        prev = v;
    }
    Check(rising, "it only ever grows as health falls");
    Check(ScreenFx_VignetteStrength(0.5f) > 0.1f && ScreenFx_VignetteStrength(0.5f) < 0.9f, "and is partway at half of the ramp");

    printf("Frame graph stats\n");
    for (int i = 0; i < 10; ++i) FrameGraph_Push(16.0f, 4.0f);
    FrameGraph_Push(50.0f, 70.0f);
    float avg, mx, ml;
    FrameGraph_Stats(&avg, &mx, &ml);
    Check(mx == 50.0f && ml == 70.0f && avg > 16.0f && avg < 20.0f, "average, worst frame and worst look input are reported");

    printf("Hit marker and stage timers\n");
    ScreenFx_DrawHitMarker(800, 450, 1.0f, 1.0f);        // draws without a window, nothing to check but that it runs
    ScreenFx_DrawCrosshair(800, 450);
    Check(1, "the crosshair and hit marker draw");
    {
        const float st[FRAME_STAGES] = { 0.5f, 1.0f, 38.0f, 4.0f, 0.5f, 0.2f };
        FrameGraph_Push(80.0f, 2.0f);
        FrameGraph_PushStages(st);
        const float quiet[FRAME_STAGES] = { 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f };
        FrameGraph_Push(16.0f, 2.0f);
        FrameGraph_PushStages(quiet);
        float cpu, got[FRAME_STAGES];
        FrameGraph_WorstCpu(&cpu, got);
        Check(fabsf(cpu - 44.2f) < 0.01f && got[2] == 38.0f, "the worst frame of our own code is reported with its stages");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
