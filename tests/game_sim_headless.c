// Headless test of the simulation with no window (src/sim): the event queue, the rules as they report themselves (sounds, messages, effects
// as events), the stair clock and the doors through the whole simulation, death and coming back, the same result at any frame rate, the same
// result on every run, and a scan that the simulation never reaches for a window, a clock, a sound card or the screen. Run from the
// project root:
//   cc -O2 -std=c99 -DGAME_SIM_HEADLESS_TEST -Isrc -Iraylib/src tests/game_sim_headless.c src/sim/game_sim.c src/sim/sim_events.c src/floor/floor_game.c src/floor/floor_run.c src/floor/floor_config.c src/weapons/weapons.c src/weapons/parts.c src/weapons/pickups.c src/weapons/gunfire.c src/projectiles/projectiles.c src/projectiles/projectile_config.c src/projectiles/projectile_patterns.c src/projectiles/projectile_simulation.c src/flashlight/flashlight.c src/config/game_config.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c src/enemies/enemy_manager.c src/enemies/spider.c src/enemies/spider_ai.c src/enemies/spider_draw.c src/dungeon/dungeon_raycast.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c src/dungeon/staircase.c src/dungeon/floor_window.c src/player/player_controller.c src/inventory/item_types.c src/inventory/inventory.c src/core/timers.c src/core/fixed_step.c src/swarm/swarm.c src/swarm/swarm_config.c src/swarm/swarm_register.c src/obstacles/obstacles.c -lm -o game_sim_headless
#ifdef GAME_SIM_HEADLESS_TEST

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/game_config.h"
#include "core/fixed_step.h"
#include "dungeon/staircase.h"
#include "enemies/enemy_manager.h"
#include "floor/floor_game.h"
#include "inventory/inventory.h"
#include "obstacles/obstacles.h"
#include "projectiles/projectiles.h"
#include "rlgl.h"
#include "sim/game_sim.h"
#include "swarm/swarm_config.h"
#include "weapons/pickups.h"
#include "weapons/weapons.h"

// ---- stand-ins for what a window would be: drawing does nothing, and the clock COUNTS how often the simulation asks the time ----
static int g_clockCalls = 0;
double GetTime(void) { ++g_clockCalls; return 0.0; }
float GetFrameTime(void) { ++g_clockCalls; return 0.0f; }
void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCubeWires(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
void DrawCylinder(Vector3 p, float rt, float rb, float h, int s, Color c) { (void)p; (void)rt; (void)rb; (void)h; (void)s; (void)c; }
void DrawCylinderWires(Vector3 p, float rt, float rb, float h, int s, Color c) { (void)p; (void)rt; (void)rb; (void)h; (void)s; (void)c; }
void DrawSphereEx(Vector3 p, float r, int a, int b, Color c) { (void)p; (void)r; (void)a; (void)b; (void)c; }
Color Fade(Color c, float a) { (void)a; return c; }
const char *TextFormat(const char *fmt, ...) { static char b[8][512]; static int i = 0; char *o = b[i++ & 7]; va_list ap; va_start(ap, fmt); vsnprintf(o, 512, fmt, ap); va_end(ap); return o; }
void BeginBlendMode(int m) { (void)m; }
void EndBlendMode(void) {}
void rlBegin(int m) { (void)m; }
void rlEnd(void) {}
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { (void)r; (void)g; (void)b; (void)a; }
void rlVertex3f(float x, float y, float z) { (void)x; (void)y; (void)z; }
bool rlCheckRenderBatchLimit(int n) { (void)n; return false; }
void rlDrawRenderBatchActive(void) {}
void rlDisableBackfaceCulling(void) {}
void rlEnableBackfaceCulling(void) {}
void rlDisableDepthMask(void) {}
void rlEnableDepthMask(void) {}

static int g_failures = 0, g_checks = 0;
static void Check(int ok, const char *what) { ++g_checks; printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }
static int Near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

// ---- running the simulation and keeping what it said ---------------------------------------------------------------
static GameSim sim;
typedef struct {
    int kind[8];
    int snd[16];
    unsigned sndArgs[16];              // bit n set: a sound came with the number n (0..31)
    int toasts;
    char last[SIM_EVENT_TEXT];
    char all[16384];
    int floorArg;
} Tally;
static Tally T;

static void TallyClear(void) { memset(&T, 0, sizeof T); }
static void Drain(void)
{
    SimEvent ev;
    while (GameSim_PopEvent(&sim, &ev)) {
        ++T.kind[ev.kind];
        if (ev.kind == SIMEV_SOUND) { ++T.snd[ev.sound]; T.sndArgs[ev.sound] |= 1u << (ev.arg & 31); }
        if (ev.kind == SIMEV_TOAST) {
            ++T.toasts;
            strcpy(T.last, ev.text);
            if (strlen(T.all) + strlen(ev.text) + 2 < sizeof T.all) { strcat(T.all, ev.text); strcat(T.all, "|"); }
        }
        if (ev.kind == SIMEV_FLOOR) T.floorArg = ev.arg;
    }
}
static int Said(const char *part) { return strstr(T.all, part) != NULL; }

static void Boot(unsigned seed)
{
    GameSim_Init(&sim, seed);
    InitObstaclesSystem();
    InitProjectilesSystem();
    InitEnemiesSystem();
    GameSim_Begin(&sim);
    TallyClear();
    Drain();
}
static SimCmd None(void) { SimCmd c; memset(&c, 0, sizeof c); return c; }
static void Stp(SimCmd c) { GameSim_Step(&sim, c); Drain(); }
static void Idle(int n) { for (int i = 0; i < n; ++i) Stp(None()); }
static void God(void) { if (sim.player.health.isAlive) sim.player.health.hp = sim.player.health.maxHp; }
static void GoTo(Vector3 p) { TeleportPlayer(&sim.player, p, 0.0f); sim.prevEye = sim.player.camera.position; }
static Vector3 ConsolePos(void) { float x = 0, z = 0; FloorGame_ConsolePosition(&x, &z); return (Vector3){ x + 0.5f, 0.0f, z }; }

static unsigned Fnv(unsigned h, const void *p, size_t n) { const unsigned char *b = (const unsigned char *)p; while (n--) { h ^= *b++; h *= 16777619u; } return h; }
// Everything that decides what happens next, folded to one number.
static unsigned WorldHash(void)
{
    unsigned h = 2166136261u;
    h = Fnv(h, &sim.player.position, sizeof sim.player.position);
    h = Fnv(h, &sim.player.health.hp, sizeof sim.player.health.hp);
    h = Fnv(h, &sim.player.stamina, sizeof sim.player.stamina);
    h = Fnv(h, &sim.window.currentFloor, sizeof sim.window.currentFloor);
    h = Fnv(h, &sim.arsenal.ammo, sizeof sim.arsenal.ammo);
    h = Fnv(h, &sim.swarm.core, sizeof sim.swarm.core);
    h = Fnv(h, &sim.swarm.health, sizeof sim.swarm.health);
    for (int i = 0; i < sim.swarm.count; ++i) h = Fnv(h, &sim.swarm.p[i], sizeof sim.swarm.p[i]);
    for (int i = 0; i < MAX_FLOOR_ENEMIES; ++i) { const Enemy *e = Enemies_At(i); if (e && e->active) { h = Fnv(h, &e->position, sizeof e->position); h = Fnv(h, &e->health.hp, sizeof e->health.hp); } }
    for (int i = 0; i < INV_SLOTS; ++i) { h = Fnv(h, &sim.inv.slots[i].item, sizeof sim.inv.slots[i].item); h = Fnv(h, &sim.inv.slots[i].count, sizeof sim.inv.slots[i].count); }
    return h;
}

// A scripted player: the command for a step depends on the step's NUMBER and a seed, nothing else.
static SimCmd ScriptCmd(unsigned seed, int step)
{
    unsigned r = (unsigned)step * 2654435761u ^ seed * 40503u;
    r ^= r >> 13; r *= 1274126177u; r ^= r >> 16;
    SimCmd c = None();
    c.move = (Vector2){ ((float)(r & 255) / 255.0f - 0.5f) * 2.0f, ((float)((r >> 8) & 255) / 255.0f - 0.5f) * 2.0f };
    c.sprint = ((r >> 16) & 7) == 0; c.jump = ((r >> 19) & 31) == 0; c.fire = ((r >> 3) & 7) == 0; c.use = ((r >> 24) & 15) == 0; c.slide = ((r >> 20) & 63) == 0;
    return c;
}
static void LoadoutGuns(void)
{
    int given = 0;
    for (int w = WEAPON_HANDS + 1; w < WEAPON_COUNT; ++w) {
        Inventory_Add(&sim.inv, Item_ForWeapon((WeaponId)w), 1);
        const WeaponDef *d = Weapon_Def((WeaponId)w);
        if (d->kind == WEAPON_KIND_GUN) Inventory_Add(&sim.inv, Item_ForAmmo(d->ammo), 40);
        if (given < 2) { Arsenal_Give(&sim.arsenal, (WeaponId)w); ++given; }
    }
}
// Walks the scripted player for n steps, however the frames are paced: a frame takes as many steps as its time is worth.
static unsigned RunScript(unsigned seed, int steps, float fps)
{
    Boot(seed);
    LoadoutGuns();
    FixedStep fs;
    FixedStep_Init(&fs, SIM_STEP_SECS, SIM_MAX_STEPS_PER_FRAME);
    int step = 0;
    while (step < steps) {
        const int n = FixedStep_Advance(&fs, 1.0f / fps);
        for (int i = 0; i < n && step < steps; ++i) Stp(ScriptCmd(seed, step++));
    }
    return WorldHash();
}

// ---- the source scan: the simulation reaches for nothing outside itself -----------------------------------------------
static int FileHas(const char *path, const char *const *words, int n, char *found, size_t foundSize)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(found, foundSize, "(cannot open %s)", path); return 1; }
    char line[2048];
    int bad = 0;
    while (fgets(line, sizeof line, f)) {
        char *cut = strstr(line, "//");           // a comment may name them: only the code counts
        if (cut) *cut = '\0';
        for (int i = 0; i < n; ++i)
            if (strstr(line, words[i])) { snprintf(found, foundSize, "%s: %s", path, words[i]); bad = 1; }
    }
    fclose(f);
    return bad;
}

int main(void)
{
    printf("game_sim_headless: the simulation with no window\n");
    FloorGame_Register();
    Swarm_Register();
    GameConfig_Init();

    printf("\n-- the event queue\n");
    {
        static SimEventQueue q;
        SimEvent e;
        SimEvents_Clear(&q);
        Check(!SimEvents_Pop(&q, &e), "an empty queue gives nothing");
        SimEvents_PushToast(&q, "ONE"); SimEvents_PushSound(&q, SIMSND_SHOT, 7); SimEvents_Push(&q, SIMEV_FLOOR, 3);
        int ok = SimEvents_Pop(&q, &e) && e.kind == SIMEV_TOAST && !strcmp(e.text, "ONE");
        ok = ok && SimEvents_Pop(&q, &e) && e.kind == SIMEV_SOUND && e.sound == SIMSND_SHOT && e.arg == 7;
        ok = ok && SimEvents_Pop(&q, &e) && e.kind == SIMEV_FLOOR && e.arg == 3;
        Check(ok && !SimEvents_Pop(&q, &e), "events come out oldest first, each with what it carried");
        for (int round = 0; round < 7; ++round) {          // the ring wraps many times
            for (int i = 0; i < 40; ++i) SimEvents_Push(&q, SIMEV_FLOOR, round * 100 + i);
            ok = 1;
            for (int i = 0; i < 40; ++i) ok = ok && SimEvents_Pop(&q, &e) && e.arg == round * 100 + i;
        }
        Check(ok && q.count == 0, "the queue keeps its order while it wraps round its ring");
        SimEvents_Clear(&q);
        for (int i = 0; i < SIM_EVENT_MAX + 25; ++i) SimEvents_Push(&q, SIMEV_FLOOR, i);
        Check(q.count == SIM_EVENT_MAX && q.dropped == 25, "a full queue drops the newest and counts them: it never grows past its size");
        ok = SimEvents_Pop(&q, &e) && e.arg == 0;
        Check(ok, "what is kept is the oldest (nothing already queued is overwritten)");
        char longText[300];
        memset(longText, 'x', sizeof longText - 1); longText[sizeof longText - 1] = '\0';
        SimEvents_Clear(&q);
        SimEvents_PushToast(&q, longText); SimEvents_PushToast(&q, NULL);
        ok = SimEvents_Pop(&q, &e) && strlen(e.text) == SIM_EVENT_TEXT - 1 && SimEvents_Pop(&q, &e) && e.text[0] == '\0';
        Check(ok, "a long message is cut to fit and ends properly; no text at all is an empty message");
        SimEvents_Clear(&q);
        SimOut_Bind(NULL);
        SimOut_Toast("LOST"); SimOut_Sound(SIMSND_HURT, 0);
        Check(q.count == 0, "with no queue bound the sinks do nothing (a tool can run the stair clock with no queue)");
        SimOut_Bind(&q);
        SimOut_Toast("HELLO"); SimOut_Sound(SIMSND_COUNT_TICK, 9);
        ok = SimEvents_Pop(&q, &e) && e.kind == SIMEV_TOAST && !strcmp(e.text, "HELLO") && SimEvents_Pop(&q, &e) && e.sound == SIMSND_COUNT_TICK && e.arg == 9;
        Check(ok, "bound, the sinks post to that queue in order");
        SimOut_Bind(NULL);
    }

    printf("\n-- the start\n");
    Boot(1);
    {
        SimEvent e;
        GameSim_Init(&sim, 1);
        InitObstaclesSystem(); InitProjectilesSystem(); InitEnemiesSystem();
        GameSim_Begin(&sim);
        int floorEv = 0, toastEv = 0, junk = 0;
        while (GameSim_PopEvent(&sim, &e)) {
            if (e.kind == SIMEV_FLOOR && e.arg == 1) ++floorEv;
            else if (e.kind == SIMEV_TOAST && strstr(e.text, "FIND THE CONSOLE")) ++toastEv;
            else ++junk;
        }
        Check(floorEv == 1, "the first floor is announced once (the picture makes its lights for it)");
        Check(toastEv == 1, "the stair clock's first message comes out through the same queue");
        Check(junk == 0, "nothing else is said at the start");
        Check(sim.player.health.isAlive && sim.window.currentFloor == 1 && sim.map == FloorWindow_Current(&sim.window), "the player is alive on floor 1, on the floor the window holds");
        Check(sim.seq == SEQ_NONE && sim.steps == 0 && sim.events.count == 0, "no door is moving, no step has run, the queue is empty");
        Check(FloorGame_Phase() == 1, "the clock waits for the console");
    }

    printf("\n-- the flashlight, which says on and off\n");
    {
        Boot(1);
        Inventory_Add(&sim.inv, ITEM_FLASHLIGHT, 1);
        Flashlight_Give(&sim.torch, 90.0f);
        SimCmd c = None(); c.light = true;
        Stp(c);
        Check(sim.torch.on && T.snd[SIMSND_FLASHLIGHT] == 1 && T.sndArgs[SIMSND_FLASHLIGHT] == (1u << 1), "switched on: one sound, and it says ON");
        Stp(None());
        Stp(c);
        Check(!sim.torch.on && T.snd[SIMSND_FLASHLIGHT] == 2 && T.sndArgs[SIMSND_FLASHLIGHT] == ((1u << 1) | (1u << 0)), "switched off: a second sound, and it says OFF");
        Boot(1);
        c = None(); c.light = true;
        Stp(c);
        Check(!sim.torch.on && T.snd[SIMSND_FLASHLIGHT] == 0, "with no flashlight in the bag the button does nothing and is silent");
        Boot(1);
        Inventory_Add(&sim.inv, ITEM_FLASHLIGHT, 1);
        Flashlight_Give(&sim.torch, 0.2f);
        sim.torch.on = true;
        TallyClear();
        for (int i = 0; i < 120; ++i) GameSim_TorchFrame(&sim, 1.0f / 60.0f);
        Drain();
        Check(Said("FLASHLIGHT DIED") && T.snd[SIMSND_FLASHLIGHT] >= 1 && (T.sndArgs[SIMSND_FLASHLIGHT] & 1u), "a flat battery says so, and the sound says OFF");
        Check(!sim.torch.on, "and the flashlight is off");
    }

    printf("\n-- shots\n");
    {
        Boot(1);
        LoadoutGuns();
        const int cur = (int)sim.arsenal.current;
        Check(Weapon_Def(sim.arsenal.current)->kind == WEAPON_KIND_GUN, "the gun in hand is a gun (the test's own set-up)");
        SimCmd c = None(); c.fire = true;
        for (int i = 0; i < 40 && T.snd[SIMSND_SHOT] == 0; ++i) Stp(c);
        Check(T.snd[SIMSND_SHOT] >= 1 && T.sndArgs[SIMSND_SHOT] == (1u << (cur & 31)), "pulling the trigger fires, and the shot's sound carries WHICH weapon");
        for (int i = 0; i < 3000 && T.snd[SIMSND_DRYFIRE] == 0; ++i) Stp(c);
        Check(T.snd[SIMSND_DRYFIRE] >= 1, "keep firing and the ammunition runs out: the dry click");
        Check(sim.arsenal.ammo == 0, "and the gun really is empty");
    }

    printf("\n-- hits and kills of the swarm (the markers)\n");
    {
        Boot(1);
        Idle(2);
        TallyClear();
        const bool hit = Swarm_Hit(&sim.swarm, sim.swarm.core, 0.2f, 1.0f);
        Stp(None());
        Check(hit && T.kind[SIMEV_HIT] == 1 && T.snd[SIMSND_HITMARKER] == 1 && T.sndArgs[SIMSND_HITMARKER] == (1u << 0), "a hit on the swarm gives one hit marker and the hit sound (not the kill sound)");
        Stp(None());
        Check(T.kind[SIMEV_HIT] == 1 && T.snd[SIMSND_HITMARKER] == 1, "and one hit is reported once, not at every step after it");
        Swarm_Hit(&sim.swarm, sim.swarm.core, 0.2f, 100000.0f);
        Stp(None());
        Check(T.kind[SIMEV_KILL] == 1 && (T.sndArgs[SIMSND_HITMARKER] & (1u << 1)), "killing it gives the kill marker and the kill sound");
    }

    printf("\n-- being hurt\n");
    {
        Boot(1);
        const float full = sim.player.health.maxHp;
        Vector3 p = sim.swarm.core; p.y = 0.0f; p.x += 0.4f;
        GoTo(p);
        for (int i = 0; i < 150 && T.snd[SIMSND_HURT] == 0; ++i) Stp(None());
        Check(T.snd[SIMSND_HURT] >= 1 && sim.player.health.hp < full, "standing in the swarm: the hurt sound, and the health really goes down");
    }

    printf("\n-- the medkit\n");
    {
        Boot(1);
        Inventory_Add(&sim.inv, ITEM_MEDKIT, 1);
        DamagePlayer(&sim.player, 40.0f, sim.player.position);
        const float before = sim.player.health.hp;
        int slot = -1;
        for (int i = 0; i < INV_SLOTS; ++i) if (sim.inv.slots[i].item == ITEM_MEDKIT) slot = i;
        TallyClear();
        GameSim_UseSlot(&sim, slot);
        Drain();
        Check(sim.player.health.hp > before, "using a medkit heals");
        Check(T.kind[SIMEV_HEAL_FLASH] == 1, "and asks for the green flash, once");
        Check(T.snd[SIMSND_USEITEM] == 1 && Said("USED"), "and says what was used, with the sound");
        Check(!Inventory_Has(&sim.inv, ITEM_MEDKIT), "and the medkit is gone from the bag");
    }

    printf("\n-- pickups and the full message\n");
    {
        Boot(1);
        Vector3 box = { 0 };
        bool found = false;
        for (int k = 0; k < MAX_PICKUPS && !found; ++k) { const Pickup *p = Pickups_Get(k); if (p->active && Pickup_IsAmmo(p->kind)) { box = p->pos; found = true; } }
        Check(found, "floor 1 of seed 1 has a box of ammunition (the test's own set-up)");
        box.x += 0.3f; box.y = 0.0f;
        GoTo(box);
        TallyClear();
        Idle(3);
        Check(T.snd[SIMSND_PICKUP] >= 1 && (Said("PICKED UP") || Said("+")), "walking onto a box takes it, with the pickup sound and the message");
        // a second box is not needed: fill the stacks, put a new one down, and stand on it for 20 seconds
        Boot(1);
        for (int w = WEAPON_HANDS + 1; w < WEAPON_COUNT; ++w) { const WeaponDef *d = Weapon_Def((WeaponId)w); if (d->kind == WEAPON_KIND_GUN) { const ItemId it = Item_ForAmmo(d->ammo); while (Inventory_CanAdd(&sim.inv, it, 1)) Inventory_Add(&sim.inv, it, 1); } }
        found = false;
        for (int k = 0; k < MAX_PICKUPS && !found; ++k) { const Pickup *p = Pickups_Get(k); if (p->active && Pickup_IsAmmo(p->kind)) { box = p->pos; found = true; } }
        box.x += 0.3f; box.y = 0.0f;
        GoTo(box);
        TallyClear();
        for (int i = 0; i < 600; ++i) { God(); Stp(None()); }
        int fulls = 0;
        for (const char *s = T.all; (s = strstr(s, " FULL")) != NULL; ++s) ++fulls;
        Check(fulls >= 8 && fulls <= 11, "with the stack full the message comes about once every two seconds, not at every step (10 in 20 seconds)");
    }

    printf("\n-- the stair clock, the door and a new floor\n");
    {
        Boot(1);
        GoTo(ConsolePos());
        SimCmd hold = None(); hold.useHeld = true;
        TallyClear();
        for (int i = 0; i < 13 * 30; ++i) { God(); GoTo(ConsolePos()); Stp(hold); }       // pinned there: the monsters the console calls would push a player off it
        Check(T.snd[SIMSND_CONSOLE_START] >= 1, "holding USE at the console: the console starts, with its sound");
        Check(T.snd[SIMSND_UNLOCKED] == 1 && Said("STAIRS UNLOCKED"), "the hold finishes: the unlocked sound and message, once");
        Check(FloorGame_Phase() == 2, "the clock is counting down");
        for (int i = 0; i < 32 * 30; ++i) { God(); Stp(None()); }
        unsigned ticks = T.sndArgs[SIMSND_COUNT_TICK];
        int allTen = 1;
        for (int s = 1; s <= 10; ++s) if (!(ticks & (1u << s))) allTen = 0;
        Check(allTen, "the last ten seconds are counted out loud, each with its number");
        Check(T.snd[SIMSND_COLLAPSE] == 1, "the collapse begins, with its sound, once");
        Check(Said("THE DOOR IS OPEN"), "the top door opens and says so");
        float cx, cz, cr;
        FloorGame_CollapseWave(&cx, &cz, &cr);
        Check(FloorGame_Phase() == 3 && cr >= 0.0f, "the collapse wave is read back from the clock (not pushed to a renderer), and has started");
        // up through the door
        float dx, dz;
        Staircase_DoorPosition(DOOR_UP, &dx, &dz);
        Vector3 at = { dx - 1.0f, 0.0f, dz };
        for (float y = 0.0f; y < 12.0f; y += 0.25f) { Vector3 p = { dx - 1.0f, y, dz }; if (Staircase_DoorInRange(DOOR_UP, p)) { at = p; break; } }
        GoTo(at);
        TallyClear();
        SimCmd use = None();
        for (int i = 0; i < 4 * 30; ++i) { use.use = (i == 3); God(); Stp(use); }
        Check(T.snd[SIMSND_DOOR] == 1, "using the open door: the door sound, once");
        Check(sim.window.currentFloor == 2 && T.floorArg == 2 && T.kind[SIMEV_FLOOR] == 1, "the floor changes to 2 and is announced once");
        Check(T.kind[SIMEV_FADE_IN] == 1, "and the screen is asked to fade in, once");
        Check(sim.seq == SEQ_NONE && sim.fadeOut == 0.0f, "the door sequence has finished");
        FloorGame_CollapseWave(&cx, &cz, &cr);
        Check(cr < 0.0f && FloorGame_Phase() == 1, "the new floor has no collapse wave and a clock waiting for its console");
    }

    printf("\n-- death, and coming back\n");
    {
        Boot(1);
        DamagePlayer(&sim.player, 100000.0f, sim.player.position);
        Check(!sim.player.health.isAlive, "the player is dead (the test's own set-up)");
        TallyClear();
        int back = -1;
        for (int i = 0; i < 200; ++i) { Stp(None()); if (sim.player.health.isAlive) { back = i + 1; break; } }
        Check(back >= 76 && back <= 80, "the player comes back after DEATH_TOTAL_SECS (2.6 s: 78 steps)");
        Check(T.kind[SIMEV_RESET_AUDIO] == 1 && T.kind[SIMEV_FADE_IN] == 1, "coming back resets the sound system once and fades in once");
        Check(Near(sim.player.health.hp, sim.player.health.maxHp, 0.001f) && sim.deathT == 0.0f, "with full health, and the death clock stopped");
        Check(sim.window.currentFloor == 1, "on the same floor");
    }

    printf("\n-- the queue does not fill up when nobody takes from it\n");
    {
        Boot(1);
        LoadoutGuns();
        for (int i = 0; i < 400; ++i) { SimCmd c = ScriptCmd(3, i); c.fire = true; GameSim_Step(&sim, c); }      // nobody pops
        Check(sim.events.count <= SIM_EVENT_MAX && sim.events.count > 0, "400 steps with no one listening: the queue holds at most its size");
        Check(sim.steps == 400, "and every step still ran");
    }

    printf("\n-- the same every time, at any frame rate\n");
    {
        const int N = 900;
        const unsigned a = RunScript(7, N, 30.0f);
        const unsigned b = RunScript(7, N, 30.0f);
        Check(a == b, "the same seed and the same commands give the same world, run twice");
        const unsigned c = RunScript(8, N, 30.0f);
        Check(a != c, "another seed gives another world (the number is not a constant)");
        const unsigned f60 = RunScript(7, N, 60.0f), f144 = RunScript(7, N, 144.0f), f37 = RunScript(7, N, 37.0f), f20 = RunScript(7, N, 20.0f);
        Check(f60 == a && f144 == a, "60 and 144 frames a second give the same world as 30");
        Check(f37 == a && f20 == a, "an uneven 37 and a slow 20 frames a second give the same world too (a frame takes 0 to 2 steps)");
        Check(sim.steps == (unsigned long)N, "each run took exactly the steps that were asked for");
    }

    printf("\n-- no window\n");
    {
        Check(g_clockCalls == 0, "the whole run, thousands of steps, never asked what time it is");
        static const char *const banned[] = { "GetTime", "GetFrameTime", "GetRandomValue", "SetRandomSeed", "BeginDrawing", "EndDrawing", "Draw", "Hud_", "AudioEvents_", "Audio_", "Lighting_",
                                              "IsKey", "GetTouch", "GetMouse", "IsMouse", "InitWindow", "rlgl", "rlBegin", "rlEnd", "rlVertex", "rlColor", "rlEnable", "rlDisable" };
        char found[200] = "";
        const int simBad = FileHas("src/sim/game_sim.c", banned, (int)(sizeof banned / sizeof banned[0]), found, sizeof found);
        Check(!simBad, simBad ? found : "game_sim.c names no window, clock, drawing, sound, HUD, lighting or input call in its code");
        const int evBad = FileHas("src/sim/sim_events.c", banned, (int)(sizeof banned / sizeof banned[0]), found, sizeof found);
        Check(!evBad, evBad ? found : "sim_events.c names none of them either");
        static const char *const clockBanned[] = { "AudioEvents_", "Hud_Toast", "DrawFloorMapCollapse" };
        const int fgBad = FileHas("src/floor/floor_game.c", clockBanned, 3, found, sizeof found);
        Check(!fgBad, fgBad ? found : "the stair clock talks to the outside only through the sinks (no sound, HUD or renderer call)");
    }

    printf("\n%s (%d failure%s of %d checks)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s", g_checks);
    return g_failures ? 1 : 0;
}
#endif
