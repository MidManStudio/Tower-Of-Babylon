// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "floor_game.h and floor_game.c"
// ============================================================================
#include "floor_game.h"

#include <math.h>
#include <stdint.h>

#include "../config/game_config.h"
#include "../core/fixed_step.h"
#include "../dungeon/dungeon_probes.h"
#include "../dungeon/staircase.h"
#include "../enemies/enemy_manager.h"
#include "../sim/sim_events.h"
#include "floor_config.h"
#include "floor_run.h"

extern const MdixBaked g_bakedFloor;                 // config_baked.c, written by tools/mdix_bake.mjs

#define RUN_TICK_SECS (1.0f / 30.0f)                 // the clock's rules run 30 times a second, whatever the frame rate
#define RUN_MAX_STEPS_PER_FRAME 5

static FloorRun  g_run;
static FixedStep g_step;
static bool      g_cleared = true;
static uint32_t  g_calls = 1;
static float     g_doorX = 73.0f, g_doorZ = 13.5f;
static float     g_waveX = 73.0f, g_waveZ = 13.5f, g_waveR = -1.0f;      // the collapse wave, for whoever draws the floor (FloorGame_CollapseWave)

void FloorGame_Register(void) { GameConfig_RegisterExtra("assets/config/floor.mdix", &g_bakedFloor, FloorConfig_LoadFrom); }

// A cylinder of radius r at (x, z) fits on open ground when none of the four corners of its bounding square is in a solid tile
// (the same test the enemies walk by).
static int TileOf(float world) { return (int)floorf((world + TILE_SIZE * 0.5f) / TILE_SIZE); }
static bool Blocked(const FloorMap *map, float x, float z, float r)
{
    return Tile_IsSolid(map, TileOf(x - r), TileOf(z - r)) || Tile_IsSolid(map, TileOf(x + r), TileOf(z - r)) ||
           Tile_IsSolid(map, TileOf(x - r), TileOf(z + r)) || Tile_IsSolid(map, TileOf(x + r), TileOf(z + r));
}

static uint32_t Mix(uint32_t h) { h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16; return h; }

// The console goes on open ground well away from the stairs (the player has to leave the stairs, work it, and come back), not in
// either alcove, with a metre of room round it. Chosen from the floor's own seed, so the same floor always has it in the same place.
static void PlaceConsole(const FloorMap *map, int floorNumber, float *outX, float *outZ)
{
    static int cand[MAP_WIDTH * MAP_HEIGHT];
    for (int pass = 0; pass < 3; ++pass) {
        const float minFromStairs = pass == 0 ? 22.0f : (pass == 1 ? 12.0f : 0.0f);
        int n = 0;
        for (int z = 1; z < MAP_HEIGHT - 1; ++z)
            for (int x = 1; x < MAP_WIDTH - 1; ++x) {
                if (map->grid[z][x].type == TILE_WALL) continue;
                if (x >= 36 && x <= 44 && (z <= 8 || z >= 36)) continue;                       // the alcoves
                if (hypotf((float)(x - 35), (float)(z - 4)) < minFromStairs) continue;         // tiles from the foot of the up stairs
                if (hypotf((float)(x - 38), (float)(z - 40)) < 8.0f) continue;                 // and from where the player arrives
                if (pass < 2 && Blocked(map, (float)x * TILE_SIZE, (float)z * TILE_SIZE, 1.0f)) continue;
                cand[n++] = z * MAP_WIDTH + x;
            }
        if (n > 0) {
            const int pick = cand[Mix(map->seed ^ (uint32_t)(floorNumber * 7919)) % (uint32_t)n];
            *outX = (float)(pick % MAP_WIDTH) * TILE_SIZE;
            *outZ = (float)(pick / MAP_WIDTH) * TILE_SIZE;
            return;
        }
    }
    *outX = 44.0f; *outZ = 44.0f;
}

void FloorGame_NewFloor(const FloorMap *map, int floorNumber, bool cleared)
{
    g_cleared = cleared;
    FixedStep_Init(&g_step, RUN_TICK_SECS, RUN_MAX_STEPS_PER_FRAME);
    Staircase_DoorPosition(DOOR_UP, &g_doorX, &g_doorZ);
    Enemies_SetRage(1.0f);
    g_waveX = g_waveZ = 0.0f; g_waveR = -1.0f;
    if (cleared) { Staircase_SetUpDoorLocked(false); return; }

    float cx, cz;
    PlaceConsole(map, floorNumber, &cx, &cz);
    float far = 0.0f;
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            if (map->grid[z][x].type == TILE_WALL) continue;
            const float d = hypotf((float)x * TILE_SIZE - g_doorX, (float)z * TILE_SIZE - g_doorZ);
            if (d > far) far = d;
        }
    FloorRun_Begin(&g_run, FloorConfig_Get(), cx, cz, g_doorX, g_doorZ, far + 2.0f);
    Staircase_SetUpDoorLocked(true);
    SimOut_Toast("FIND THE CONSOLE. HOLD USE THERE TO UNLOCK THE STAIRS");
}

bool FloorGame_NearConsole(Vector3 p)
{
    if (g_cleared || g_run.phase != RUN_EXPLORING) return false;
    return p.y < 1.5f && hypotf(p.x - g_run.consoleX, p.z - g_run.consoleZ) <= g_run.cfg.reach;
}

bool FloorGame_ConsolePosition(float *x, float *z)
{
    if (g_cleared) return false;
    *x = g_run.consoleX; *z = g_run.consoleZ;
    return true;
}

// One monster, called by the console: on open ground 7 to 11 m from it, already hunting.
static void CallMonster(const FloorMap *map, int floorNumber)
{
    for (int tries = 0; tries < 12; ++tries) {
        const uint32_t h = Mix(g_calls++ * 2654435761u + map->seed);
        const float ang = (float)(h % 628) * 0.01f, dist = 7.0f + (float)((h >> 10) % 40) * 0.1f;
        const float x = g_run.consoleX + cosf(ang) * dist, z = g_run.consoleZ + sinf(ang) * dist;
        if (x < 2.0f || z < 2.0f || x > (MAP_WIDTH - 2) * TILE_SIZE || z > (MAP_HEIGHT - 2) * TILE_SIZE) continue;
        if (Blocked(map, x, z, 0.8f)) continue;
        Enemies_SpawnAt((Vector3){ x, 0.0f, z }, floorNumber, true);
        return;
    }
}

// What follows a step (or a frame with no step): do what the rules asked for, lock or open the door, let the floor fall, see who is on it.
static void Settle(const FloorGameIn *in, FloorGameOut *out)
{
    RunEvent ev;
    while (FloorRun_PopEvent(&g_run, &ev)) {
        switch (ev.kind) {
            case RUN_EV_CONSOLE_START: SimOut_Sound(SIMSND_CONSOLE_START, 0); break;
            case RUN_EV_MONSTER:       CallMonster(in->map, in->floorNumber); break;
            case RUN_EV_UNLOCKED:      SimOut_Sound(SIMSND_UNLOCKED, 0); SimOut_Toast("STAIRS UNLOCKED. THE TOP DOOR OPENS WHEN THE CLOCK ENDS"); break;
            case RUN_EV_COUNT:         SimOut_Sound(SIMSND_COUNT_TICK, (int)ev.value); break;
            case RUN_EV_DOOR_OPEN:     Enemies_SetRage(g_run.cfg.rageSpeed); SimOut_Toast("THE DOOR IS OPEN. GET OUT"); break;
            case RUN_EV_COLLAPSE_BEGIN: SimOut_Sound(SIMSND_COLLAPSE, 0); break;
            default: break;
        }
    }

    Staircase_SetUpDoorLocked(!FloorRun_DoorOpen(&g_run));
    g_waveX = g_doorX; g_waveZ = g_doorZ; g_waveR = g_run.phase == RUN_COLLAPSING ? g_run.waveRadius : -1.0f;
    if (in->alive && FloorRun_Gone(&g_run, in->player.x, in->player.z)) out->killPlayer = true;
}

// One step of the clock, for a game that already runs its rules on fixed steps (src/sim): in->dt is not used, the step is
// always RUN_TICK_SECS, which is the game's own step.
void FloorGame_Tick(const FloorGameIn *in, FloorGameOut *out)
{
    out->killPlayer = false;
    if (g_cleared) return;
    const bool reach = in->alive && FloorGame_NearConsole(in->player);
    FloorRun_Step(&g_run, RUN_TICK_SECS, reach, in->useHeld && reach);
    Settle(in, out);
}

// The same from frame time: the clock keeps its own accumulator and takes as many steps as the time is worth.
void FloorGame_Frame(const FloorGameIn *in, FloorGameOut *out)
{
    out->killPlayer = false;
    if (g_cleared) return;
    const bool reach = in->alive && FloorGame_NearConsole(in->player);
    const int steps = FixedStep_Advance(&g_step, in->dt);
    for (int i = 0; i < steps; ++i) FloorRun_Step(&g_run, g_step.step, reach, in->useHeld && reach);
    Settle(in, out);
}

// The collapse wave as the last step left it: centre and radius (a negative radius is no wave). The caller hands it to the renderer.
void FloorGame_CollapseWave(float *cx, float *cz, float *radius) { *cx = g_waveX; *cz = g_waveZ; *radius = g_waveR; }

// ---- drawing the console ------------------------------------------------------------------------------------
void FloorGame_Draw(Vector3 viewer, float time)
{
    if (g_cleared) return;
    const float cx = g_run.consoleX, cz = g_run.consoleZ;
    if (hypotf(viewer.x - cx, viewer.z - cz) > 45.0f) return;

    const bool done = g_run.phase != RUN_EXPLORING;
    const float pulse = 0.5f + 0.5f * sinf(time * (g_run.holding ? 9.0f : 3.0f));
    const Color amber = { 230, 150, 30, 255 }, green = { 60, 230, 110, 255 };
    const float k = done ? 1.0f : g_run.progress;
    const Color screen = { (unsigned char)(amber.r + (green.r - amber.r) * k), (unsigned char)(amber.g + (green.g - amber.g) * k),
                           (unsigned char)(amber.b + (green.b - amber.b) * k), 255 };

    DrawCube((Vector3){ cx, 0.55f, cz }, 1.0f, 1.1f, 0.7f, (Color){ 38, 40, 50, 255 });                 // the cabinet
    DrawCube((Vector3){ cx, 1.0f, cz }, 0.8f, 0.4f, 0.72f, Fade(screen, done ? 1.0f : 0.55f + 0.45f * pulse));   // the screen, a hair deeper than the cabinet
    DrawCube((Vector3){ cx, 0.25f + 0.2f * k, cz }, 0.12f, 0.1f + 0.4f * k, 0.72f, green);               // a bar that fills as it is worked
    // A pillar of light so it can be found in the dark.
    DrawCube((Vector3){ cx, 3.0f, cz }, 0.14f, 3.6f, 0.14f, Fade(done ? green : amber, done ? 0.9f : 0.45f + 0.4f * pulse));
}

int FloorGame_Phase(void)
{
    if (g_cleared) return 0;
    return g_run.phase == RUN_EXPLORING ? 1 : (g_run.phase == RUN_COUNTDOWN ? 2 : 3);
}
float FloorGame_ConsoleProgress(void) { return g_cleared ? 0.0f : g_run.progress; }
bool  FloorGame_ConsoleHeld(void) { return !g_cleared && g_run.holding; }
float FloorGame_CountdownLeft(void) { return g_cleared ? 0.0f : FloorRun_CountdownLeft(&g_run); }
float FloorGame_CollapseLeft01(void)
{
    if (g_cleared || g_run.phase != RUN_COLLAPSING || g_run.maxRadius <= 0.0f) return 1.0f;
    return g_run.waveRadius / g_run.maxRadius;
}
