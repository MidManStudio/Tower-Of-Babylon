// Headless test of the stair clock inside the game (src/floor/floor_game.c): where the console is put, the lock on the top door through
// the whole unlock, countdown and collapse, the monsters the console calls, and who the collapse kills. The sound and message sinks, the enemies and
// the staircase are stand-ins that record what they were asked (the renderer is not touched at all: the wave is read back). Run from the project root:
//   cc -O2 -std=c99 -DFLOOR_GAME_HEADLESS_TEST -Isrc -Iraylib/src tests/floor_game_headless.c src/floor/floor_game.c src/floor/floor_run.c src/floor/floor_config.c src/core/timers.c src/core/fixed_step.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c -lm -o floor_game_headless
#ifdef FLOOR_GAME_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config/game_config.h"
#include "dungeon/dungeon_generator.h"
#include "dungeon/staircase.h"
#include "enemies/enemy_manager.h"
#include "floor/floor_game.h"
#include "sim/sim_events.h"

// ---- stand-ins ------------------------------------------------------------------------------------------------
static int g_sndStart, g_sndUnlocked, g_sndCount, g_sndCollapse, g_toasts;
void SimOut_Sound(SimSound s, int arg)      // the clock's sounds and messages go out through these (sim/sim_events.h)
{
    (void)arg;
    if (s == SIMSND_CONSOLE_START) ++g_sndStart; else if (s == SIMSND_UNLOCKED) ++g_sndUnlocked; else if (s == SIMSND_COUNT_TICK) ++g_sndCount; else if (s == SIMSND_COLLAPSE) ++g_sndCollapse;
}
void SimOut_Toast(const char *t) { (void)t; ++g_toasts; }
bool GameConfig_RegisterExtra(const char *path, const MdixBaked *baked, ConfigBuildFn build) { (void)path; (void)baked; (void)build; return true; }
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
Color Fade(Color c, float a) { (void)a; return c; }

static bool g_upLocked; static float g_rage = 1.0f;
void Staircase_SetUpDoorLocked(bool l) { g_upLocked = l; }
void Staircase_DoorPosition(DoorId id, float *x, float *z) { (void)id; *x = 73.0f; *z = 13.5f; }
void Enemies_SetRage(float m) { g_rage = m; }
static int g_spawned, g_spawnedAlerted; static Vector3 g_spawnAt[16]; static int g_spawnTier;
int Enemies_SpawnAt(Vector3 p, int tier, bool alerted) { if (g_spawned < 16) g_spawnAt[g_spawned] = p; ++g_spawned; g_spawnedAlerted += alerted; g_spawnTier = tier; return 0; }

static float WaveR(void) { float x, z, r; FloorGame_CollapseWave(&x, &z, &r); return r; }      // the collapse wave as the renderer would be given it

static FloorMap g_floor;
static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

static bool FindConsole(Vector3 *out)
{
    float x, z;
    if (!FloorGame_ConsolePosition(&x, &z)) return false;
    *out = (Vector3){ x, 0.0f, z };
    return true;
}

static void Frames(float secs, float fps, Vector3 player, bool held, int floorNumber, FloorGameOut *last)
{
    const float dt = 1.0f / fps;
    for (float t = 0.0f; t < secs - 0.00001f; t += dt) {
        FloorGameIn in = { dt, &g_floor, player, true, held, floorNumber };
        FloorGame_Frame(&in, last);
    }
}

int main(void)
{
    printf("Where the console goes\n");
    {
        int placed = 0, farEnough = 0, open = 0, notAlcove = 0, same = 0, differs = 0;
        float firstX = 0, firstZ = 0;
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            GenerateFloorMap(&g_floor, 1 + (int)(seed % 7), seed * 7919u);
            FloorGame_NewFloor(&g_floor, 1 + (int)(seed % 7), false);
            Vector3 c;
            if (!FindConsole(&c)) continue;
            ++placed;
            const int tx = (int)(c.x / TILE_SIZE + 0.5f), tz = (int)(c.z / TILE_SIZE + 0.5f);
            if (g_floor.grid[tz][tx].type != TILE_WALL && FloorGame_NearConsole(c)) ++open;
            if (!(tx >= 36 && tx <= 44 && (tz <= 8 || tz >= 36))) ++notAlcove;
            if (hypotf((float)(tx - 35), (float)(tz - 4)) >= 12.0f) ++farEnough;
            if (seed == 1) { firstX = c.x; firstZ = c.z; }
        }
        GenerateFloorMap(&g_floor, 2, 7919u); FloorGame_NewFloor(&g_floor, 2, false);
        Vector3 again; if (FindConsole(&again)) { (void)again; }
        GenerateFloorMap(&g_floor, 2, 7919u); FloorGame_NewFloor(&g_floor, 2, false);
        Vector3 third; FindConsole(&third);
        same = (again.x == third.x && again.z == third.z);
        GenerateFloorMap(&g_floor, 2, 99991u); FloorGame_NewFloor(&g_floor, 2, false);
        Vector3 other; FindConsole(&other); differs = (other.x != third.x || other.z != third.z);
        Check(placed == 60, "every one of 60 floors has a console, and USE works standing on it");
        Check(open == placed && notAlcove == placed, "each on open ground and outside both alcoves");
        Check(farEnough == placed, "each at least 12 tiles from the foot of the stairs, so the player has to go out and come back");
        Check(same && differs, "the same floor always has it in the same place; another floor has it somewhere else");
        (void)firstX; (void)firstZ;
    }

    printf("The whole sequence, through the glue\n");
    const Vector3 far = { 10.0f, 0.0f, 80.0f }, atDoor = { 74.5f, 8.0f, 13.5f };
    {
        GenerateFloorMap(&g_floor, 3, 424242u);
        FloorGame_NewFloor(&g_floor, 3, false);
        Vector3 c; FindConsole(&c);
        Check(g_upLocked && FloorGame_Phase() == 1 && g_rage == 1.0f && WaveR() < 0.0f, "a new floor: the top door is locked, the clock is waiting for the console, nothing has fallen");
        Check(g_toasts >= 1, "and the player is told to find it");
        FloorGameOut out = { 0 };
        Frames(3.0f, 60.0f, c, false, 3, &out);
        Check(FloorGame_Phase() == 1 && FloorGame_ConsoleProgress() == 0.0f, "standing at it without USE does nothing");
        Frames(5.0f, 60.0f, c, true, 3, &out);
        Check(FloorGame_ConsoleHeld() && FloorGame_ConsoleProgress() > 0.45f && FloorGame_ConsoleProgress() < 0.55f && g_sndStart == 1, "5 s of USE: about half, the start sound played once");
        Check(g_spawned == 3 && g_spawnedAlerted == 3 && g_spawnTier == 3, "the console has called three monsters, hunting, at this floor's tier");
        int okSpots = 1;
        for (int i = 0; i < g_spawned && i < 16; ++i) {
            const float d = hypotf(g_spawnAt[i].x - c.x, g_spawnAt[i].z - c.z);
            const int tx = (int)(g_spawnAt[i].x / TILE_SIZE + 0.5f), tz = (int)(g_spawnAt[i].z / TILE_SIZE + 0.5f);
            if (d < 5.0f || d > 12.5f || g_floor.grid[tz][tx].type == TILE_WALL) okSpots = 0;
        }
        Check(okSpots, "each 5 to 12 m from the console, on open ground");
        Frames(5.2f, 60.0f, c, true, 3, &out);
        Check(FloorGame_Phase() == 2 && g_sndUnlocked == 1 && g_upLocked, "10 s in: unlocked, the countdown runs, and the top door is still shut");
        Check(!FloorGame_NearConsole(c), "the console cannot be worked again");
        const float left0 = FloorGame_CountdownLeft();
        Check(left0 > 28.5f && left0 <= 30.0f, "the countdown shows its 30 s");
        Frames(10.0f, 60.0f, far, false, 3, &out);
        Check(FloorGame_Phase() == 2 && g_upLocked && !out.killPlayer && g_sndCount == 0, "10 s on: still counting, the door still shut, the player fine anywhere, and no ticks yet (they are the last ten seconds)");
        Frames(11.0f, 60.0f, far, false, 3, &out);
        Check(g_sndCount >= 1 && g_sndCount <= 3 && FloorGame_CountdownLeft() < 9.5f, "21 s on: the ticks have started");
        Frames(8.0f, 60.0f, far, false, 3, &out);
        Check(FloorGame_Phase() == 2 && FloorGame_CountdownLeft() < 1.5f && g_sndCount >= 8 && g_sndCount <= 11, "29 s on: about a second left, and the last ten seconds have ticked about ten times");
        Frames(2.5f, 60.0f, far, false, 3, &out);
        Check(FloorGame_Phase() == 3 && !g_upLocked && g_rage > 1.0f && g_sndCollapse == 1, "the countdown ends: the door unlocks, the monsters speed up, the collapse sound plays once");
        Check(WaveR() > 0.0f && FloorGame_CollapseLeft01() < 1.0f && FloorGame_CollapseLeft01() > 0.9f, "the floor has started to go: the front is moving in from the far edge");
        Check(!out.killPlayer, "the player at the far edge is not killed on the first frames: the front starts beyond the farthest floor");
        int killedAt = -1;
        for (float t = 0.0f; t < 45.0f; t += 1.0f / 60.0f) { FloorGameIn in = { 1.0f / 60.0f, &g_floor, far, true, false, 3 }; FloorGame_Frame(&in, &out); if (out.killPlayer) { killedAt = (int)t; break; } }
        Check(killedAt >= 0 && killedAt < 30, "a player who stays at the far side of the floor is killed when the front reaches them, well before it has all gone");
        FloorGameIn dead = { 1.0f / 60.0f, &g_floor, far, false, false, 3 }; FloorGameOut o3 = { 0 };
        FloorGame_Frame(&dead, &o3);
        Check(!o3.killPlayer, "someone already dead is not killed again");
    }
    {
        // From the start of a collapse: how long the player at the door has.
        GenerateFloorMap(&g_floor, 3, 424242u);
        FloorGame_NewFloor(&g_floor, 3, false);
        Vector3 c; FindConsole(&c); FloorGameOut out = { 0 };
        Frames(10.3f, 60.0f, c, true, 3, &out);
        Frames(30.3f, 60.0f, far, false, 3, &out);
        Check(FloorGame_Phase() == 3, "(a second floor, the same sequence, 10.3 + 30.3 s in: the collapse has just begun)");
        Frames(30.0f, 60.0f, atDoor, false, 3, &out);
        Check(!out.killPlayer && FloorGame_CollapseLeft01() > 0.2f, "a player at the top door is safe 30 s into the collapse, which has 40 s");
        Frames(10.0f, 60.0f, atDoor, false, 3, &out);
        Check(FloorGame_CollapseLeft01() == 0.0f && out.killPlayer, "and when all of it has gone, the door's floor goes too");
    }

    printf("Frame rate does not matter\n");
    {
        double secs[3] = { 0 }; const float fps[3] = { 30.0f, 60.0f, 144.0f };
        for (int k = 0; k < 3; ++k) {
            GenerateFloorMap(&g_floor, 2, 31337u);
            FloorGame_NewFloor(&g_floor, 2, false);
            Vector3 c; FindConsole(&c);
            FloorGameOut out;
            float t = 0.0f; const float dt = 1.0f / fps[k];
            while (FloorGame_Phase() == 1 && t < 30.0f) { FloorGameIn in = { dt, &g_floor, c, true, true, 2 }; FloorGame_Frame(&in, &out); t += dt; }
            secs[k] = t;
        }
        Check(fabs(secs[0] - 10.0) < 0.12 && fabs(secs[1] - 10.0) < 0.12 && fabs(secs[2] - 10.0) < 0.12, "the unlock takes 10 s at 30, 60 and 144 frames a second");
        GenerateFloorMap(&g_floor, 2, 31337u); FloorGame_NewFloor(&g_floor, 2, false);
        Vector3 c; FindConsole(&c); FloorGameOut out;
        FloorGameIn stall = { 2.0f, &g_floor, c, true, true, 2 };            // a 2 s stall with USE held
        FloorGame_Frame(&stall, &out);
        Check(FloorGame_ConsoleProgress() < 0.2f, "a 2 s stall counts for at most a handful of ticks, not 2 s of the unlock (the extra is dropped)");
    }

    printf("A floor with no clock, and a new life\n");
    {
        GenerateFloorMap(&g_floor, 2, 5u);
        FloorGame_NewFloor(&g_floor, 2, true);
        FloorGameOut out = { 0 }; FloorGameIn in = { 1.0f, &g_floor, (Vector3){ 5, 0, 5 }, true, true, 2 };
        for (int i = 0; i < 100; ++i) FloorGame_Frame(&in, &out);
        Vector3 c;
        Check(FloorGame_Phase() == 0 && !g_upLocked && !FindConsole(&c) && !out.killPlayer && WaveR() < 0.0f, "a floor the player came down into: no clock, no console, the top door open, nothing falls");
        g_spawned = 0;
        FloorGame_NewFloor(&g_floor, 2, false);
        Check(FloorGame_Phase() == 1 && g_upLocked && FloorGame_ConsoleProgress() == 0.0f && g_rage == 1.0f, "a new life on a floor that had a clock starts it afresh: locked, no progress, monsters at their normal speed");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
