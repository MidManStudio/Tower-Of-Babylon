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
    int snd[24];
    unsigned sndArgs[24];              // bit n set: a sound came with the number n (0..31)
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
    h = Fnv(h, sim.arsenal.mag, sizeof sim.arsenal.mag);
    h = Fnv(h, sim.arsenal.wear, sizeof sim.arsenal.wear);
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
    c.sprint = ((r >> 16) & 7) == 0; c.jump = ((r >> 19) & 31) == 0; c.fire = ((r >> 3) & 7) == 0; c.use = ((r >> 24) & 15) == 0; c.slide = ((r >> 20) & 63) == 0; c.reload = ((r >> 27) & 7) == 0;
    return c;
}
static void LoadoutGuns(void)
{
    int given = 0;
    for (int w = WEAPON_HANDS + 1; w < WEAPON_COUNT; ++w) {
        Inventory_Add(&sim.inv, Item_ForWeapon((WeaponId)w), 1);
        const WeaponDef *d = Weapon_Def((WeaponId)w);
        if (d->kind == WEAPON_KIND_GUN) { Inventory_Add(&sim.inv, Item_ForAmmo(d->ammo), 40); Arsenal_Fill(&sim.arsenal, (WeaponId)w, d->magSize); }   // a full magazine and 40 in the bag
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

    printf("\n-- shots and the magazine\n");
    {
        Boot(1);
        LoadoutGuns();
        const int cur = (int)sim.arsenal.current;
        const WeaponDef *gd = Weapon_Def(sim.arsenal.current);
        Check(gd->kind == WEAPON_KIND_GUN && gd->magSize > 0, "the gun in hand is a gun with a magazine (the test's own set-up)");
        SimCmd c = None(); c.fire = true;
        for (int i = 0; i < 40 && T.snd[SIMSND_SHOT] == 0; ++i) Stp(c);
        Check(T.snd[SIMSND_SHOT] >= 1 && T.sndArgs[SIMSND_SHOT] == (1u << (cur & 31)), "pulling the trigger fires, and the shot's sound carries WHICH weapon");
        Check(Inventory_Count(&sim.inv, Item_ForAmmo(gd->ammo)) == 40, "a shot comes out of the magazine: the bag is untouched");
        for (int i = 0; i < 3000 && T.snd[SIMSND_DRYFIRE] == 0; ++i) Stp(c);
        Check(T.snd[SIMSND_DRYFIRE] >= 1, "keep firing and the magazine runs out: the dry click");
        Check(sim.arsenal.mag[cur] == 0, "and the magazine really is empty");
        Check(sim.arsenal.reloading && (T.sndArgs[SIMSND_RELOAD] & 1u), "the empty gun starts reloading by itself, and says so");
        for (int i = 0; i < 200 && sim.arsenal.reloading; ++i) Stp(None());
        Check(!sim.arsenal.reloading && sim.arsenal.mag[cur] == gd->magSize, "the reload finishes with a full magazine");
        Check(Inventory_Count(&sim.inv, Item_ForAmmo(gd->ammo)) == 40 - gd->magSize, "and the rounds came out of the bag, not out of thin air");
        Check(T.sndArgs[SIMSND_RELOAD] & ((1u << 2) | (1u << 3)), "with the 'loaded' sound");
    }

    printf("\n-- the reload button\n");
    {
        Boot(1);
        const ItemId pistolAmmo = Item_ForAmmo(Weapon_Def(WEAPON_PISTOL)->ammo);
        Inventory_Add(&sim.inv, Item_ForWeapon(WEAPON_PISTOL), 1);
        Arsenal_Give(&sim.arsenal, WEAPON_PISTOL);
        const int mag = Weapon_Def(WEAPON_PISTOL)->magSize;
        Inventory_Add(&sim.inv, pistolAmmo, 20);
        Arsenal_Fill(&sim.arsenal, WEAPON_PISTOL, mag);
        Idle(12);                                                    // the gun is up
        SimCmd r = None(); r.reload = true;
        TallyClear();
        Stp(r);
        Check(!sim.arsenal.reloading && Said("MAGAZINE FULL") && T.snd[SIMSND_RELOAD] == 0, "reload with a full magazine: it says so and does nothing");
        sim.arsenal.mag[WEAPON_PISTOL] = mag - 3;
        TallyClear();
        Stp(r);
        Check(sim.arsenal.reloading && (T.sndArgs[SIMSND_RELOAD] & 1u), "reload with room in the magazine starts one, with its sound");
        Check(Inventory_Count(&sim.inv, pistolAmmo) == 20, "the bag is only charged when the magazine is in");
        for (int i = 0; i < 200 && sim.arsenal.reloading; ++i) Stp(None());
        Check(sim.arsenal.mag[WEAPON_PISTOL] == mag && Inventory_Count(&sim.inv, pistolAmmo) == 17, "it tops the magazine up and takes just the 3 rounds it needed");

        sim.arsenal.mag[WEAPON_PISTOL] = 2;
        Inventory_Remove(&sim.inv, pistolAmmo, Inventory_Count(&sim.inv, pistolAmmo));
        TallyClear();
        Stp(r);
        Check(!sim.arsenal.reloading && Said("NO SPARE AMMO"), "reload with an empty bag: it says there is nothing to load");

        Inventory_Add(&sim.inv, pistolAmmo, 5);
        sim.invOpen = true;
        TallyClear();
        Stp(r);
        Check(!sim.arsenal.reloading, "with the inventory open the button does nothing");
        sim.invOpen = false;
        Stp(r);
        Check(sim.arsenal.reloading, "and with it shut it does");
        Arsenal_Equip(&sim.arsenal, WEAPON_HANDS);
        Check(!sim.arsenal.reloading && sim.arsenal.mag[WEAPON_PISTOL] == 2, "putting the gun away stops the reload, and the rounds in it stay in it");
    }

    printf("\n-- holding fire aims down the sights\n");
    {
        Boot(1);
        LoadoutGuns();
        Check(Arsenal_Replace(&sim.arsenal, WEAPON_RIFLE, WEAPON_PIPE) && sim.arsenal.current == WEAPON_RIFLE, "the rifle in hand (the test's own set-up)");
        const WeaponDef *rd = Weapon_Def(WEAPON_RIFLE);
        Idle(12);
        God();
        Check(sim.player.aimBlend == 0.0f && Near(sim.player.camera.fovy, 60.0f, 0.01f), "at the hip to begin with: the normal view");
        const float hipSpread = GameSim_SpreadDeg(&sim);
        Check(Near(hipSpread, rd->spreadDeg, 0.001f), "and the gun's own cold spread");
        SimCmd h = None(); h.fireHeld = true;                               // holding the trigger, as SimCmd_Take reports it
        h.fire = true;
        Stp(h); Stp(h);
        Check(sim.player.wantAim && sim.player.aimBlend > 0.0f && sim.player.aimBlend < 1.0f, "holding the fire pad with a gun starts aiming");
        for (int i = 0; i < 12; ++i) { God(); Stp(h); }
        Check(sim.player.aimBlend == 1.0f && Near(sim.player.camera.fovy, rd->adsFov, 0.05f), "and a moment later the view is the rifle's own zoom");
        Check(Near(GameSim_SpreadDeg(&sim), Arsenal_SpreadDeg(&sim.arsenal) * rd->adsSpreadMul, 0.01f) && GameSim_SpreadDeg(&sim) < hipSpread * 1.01f + 5.0f, "the shot's cone is the gun's, narrowed to the ADS share");
        Check(sim.prevAim <= sim.player.aimBlend && sim.prevLean == 0.0f, "(the sim keeps the previous blends for the picture to blend from)");
        SimCmd rel = None();
        Stp(rel);
        for (int i = 0; i < 12; ++i) { God(); Stp(rel); }
        Check(sim.player.aimBlend == 0.0f && Near(sim.player.camera.fovy, 60.0f, 0.01f), "let go of fire: the view comes back out");

        // Sprinting and ADS exclude each other: holding fire cancels the sprint, letting go brings it back.
        sim.arsenal.mag[WEAPON_RIFLE] = rd->magSize;
        SimCmd run = None(); run.move = (Vector2){ 0.0f, -1.0f }; run.sprint = true;
        for (int i = 0; i < 20; ++i) { God(); Stp(run); }
        Check(sim.player.sprinting, "(running at the hip)");
        SimCmd runfire = run; runfire.fireHeld = true; runfire.fire = true;
        Stp(runfire);
        Check(!sim.player.sprinting, "holding fire while running ends the sprint");
        for (int i = 0; i < 20; ++i) { God(); Stp(run); }
        Check(sim.player.sprinting, "and lifting the thumb lets the sprint carry on (the stick is still at the rim)");

        // Not for everything in the hand, nor at every moment.
        Arsenal_Equip(&sim.arsenal, WEAPON_HANDS);
        Idle(12);
        for (int i = 0; i < 12; ++i) { God(); Stp(h); }
        Check(sim.player.aimBlend == 0.0f, "fists do not aim down sights: holding fire just punches");
        Arsenal_Equip(&sim.arsenal, WEAPON_RIFLE);
        Idle(12);
        sim.arsenal.mag[WEAPON_RIFLE] = 5;
        SimCmd rl = None(); rl.reload = true; rl.fireHeld = true;
        Stp(rl);
        Stp(h);                                                             // (the reload starts inside the step, so the next one is the first to see it)
        Check(sim.arsenal.reloading && !sim.player.wantAim, "a reload takes you out of the sights, even with fire held");
        for (int i = 0; i < 200 && sim.arsenal.reloading; ++i) Stp(None());
        sim.invOpen = true;
        for (int i = 0; i < 6; ++i) Stp(h);
        Check(sim.player.aimBlend == 0.0f, "with the inventory open there is no aiming");
        sim.invOpen = false;
        sim.player.health.isAlive = true;
    }

    // An open stretch of the floor to run in: the first spot with floor all round it, and a 13 by 13 tile square made level round it.
    int openX = -1, openZ = -1;

    printf("\n-- leaning at a corner (automatic)\n");
    {
        Boot(1);
        LoadoutGuns();                                                      // a gun in hand: leaning is for guns
        Idle(12);
        God();
        for (int z = 8; z < MAP_HEIGHT - 8 && openX < 0; ++z) for (int x = 8; x < MAP_WIDTH - 8 && openX < 0; ++x) {
            int open = 1;
            for (int dz = -2; dz <= 2 && open; ++dz) for (int dx = -2; dx <= 2; ++dx)
                if (sim.map->grid[z + dz][x + dx].type != TILE_FLOOR) { open = 0; break; }
            if (open) { openX = x; openZ = z; }
        }
        Check(openX >= 0, "the floor has a spot with open floor all round it (the test's own set-up)");
        if (openX >= 0) {
            for (int dz = -6; dz <= 6; ++dz) for (int dx = -6; dx <= 6; ++dx) sim.map->grid[openZ + dz][openX + dx].type = TILE_FLOOR;
            // A slab of wall three tiles long east of the spot: its face is at x = 2 * openX + 1 and it ends at z = 2 * openZ + 1, with open floor past the end.
            for (int dz = -2; dz <= 0; ++dz) sim.map->grid[openZ + dz][openX + 1].type = TILE_WALL;
            const float cx = (float)openX * TILE_SIZE - 0.2f, cz = (float)openZ * TILE_SIZE + 0.8f;     // 1.2 m from the face, 0.2 m short of the slab's end
            GoTo((Vector3){ cx, 0.0f, cz });
            sim.player.yaw = 1.5708f;                                       // facing +x, the slab; right is +z, toward the opening
            for (int i = 0; i < 3; ++i) { God(); Stp(None()); }
            for (int i = 0; i < 1; ++i) { God(); Stp(None()); }
            Check(sim.player.leanDir == 1 && sim.player.leanBlend > 0.0f && sim.player.leanBlend < 1.0f, "a gun in hand at the corner: after a moment it leans toward the opening, with no button, easing in");
            Check(sim.prevLean > 0.0f && sim.prevLean < sim.player.leanBlend, "(and the sim keeps the previous blend for the picture to blend from)");
            for (int i = 0; i < 12; ++i) { God(); Stp(None()); }
            Check(sim.player.leanBlend == 1.0f && Near(sim.player.camera.position.z, cz + PLAYER_LEAN_SHIFT, 0.02f) && Near(sim.player.camera.position.x, cx, 0.02f), "and it goes to full: the eye is a lean to the right of the body");

            // The gun decides.
            Arsenal_Equip(&sim.arsenal, WEAPON_HANDS);
            for (int i = 0; i < 12; ++i) { God(); Stp(None()); }
            Check(sim.player.leanBlend == 0.0f && sim.player.leanDir == 0, "put the gun away and the lean lets go, with the corner still there");
            Arsenal_Equip(&sim.arsenal, WEAPON_PISTOL);
            Idle(12);
            for (int i = 0; i < 12; ++i) { God(); Stp(None()); }
            Check(sim.player.leanBlend == 1.0f, "a gun back in hand: it leans again");
            sim.player.yaw = 0.0f;                                          // turn to face along the slab
            for (int i = 0; i < 12; ++i) { God(); Stp(None()); }
            Check(sim.player.leanBlend == 0.0f, "turn away from the corner and it lets go");

            // A flat wall, and the open.
            GoTo((Vector3){ cx, 0.0f, (float)openZ * TILE_SIZE - 3.0f });
            sim.player.yaw = 1.5708f;
            for (int i = 0; i < 12; ++i) { God(); Stp(None()); }
            Check(sim.player.leanBlend == 0.0f, "facing the middle of the slab (a flat wall) there is no lean");
            GoTo((Vector3){ (float)openX * TILE_SIZE - 5.0f, 0.0f, cz });
            sim.player.yaw = 1.5708f;
            for (int i = 0; i < 12; ++i) { God(); Stp(None()); }
            Check(sim.player.leanBlend == 0.0f, "and far from the wall there is none either");
        }
    }

    printf("\n-- a slide goes forward only, and the dive\n");
    {
        Boot(1);
        LoadoutGuns();
        Idle(12);
        God();
        Check(openX >= 0, "(the test's own set-up from above)");
        if (openX >= 0) {
            for (int dz = -6; dz <= 6; ++dz) for (int dx = -6; dx <= 6; ++dx) sim.map->grid[openZ + dz][openX + dx].type = TILE_FLOOR;
            const Vector3 start = { (float)openX * TILE_SIZE, 0.0f, (float)openZ * TILE_SIZE - 6.0f };
            SimCmd fwd = None(); fwd.move = (Vector2){ 0.0f, -1.0f }; fwd.sprint = true;
            SimCmd side = None(); side.move = (Vector2){ 1.0f, 0.0f }; side.sprint = true;
            SimCmd back = None(); back.move = (Vector2){ 0.0f, 1.0f }; back.sprint = true;

            GoTo(start);
            for (int i = 0; i < 20; ++i) { God(); Stp(fwd); }
            Check(sim.player.sprinting && GameSim_SlideButtonShown(&sim) && !GameSim_DiveButtonShown(&sim), "running forward: the SLIDE button is there, and no DIVE on the ground");
            SimCmd sl = fwd; sl.slide = true;
            Stp(sl);
            Check(sim.player.sliding, "and a slide starts");
            GoTo(start);
            for (int i = 0; i < 20; ++i) { God(); Stp(side); }
            Check(sim.player.sprinting && !GameSim_SlideButtonShown(&sim), "running sideways: no SLIDE button");
            const float sb = sim.player.stamina;
            SimCmd ss = side; ss.slide = true;
            Stp(ss);
            Check(!sim.player.sliding && sim.player.stamina >= sb - 1.0f, "and a slide asked for anyway does nothing and costs nothing");
            GoTo(start);
            for (int i = 0; i < 20; ++i) { God(); Stp(back); }
            Check(sim.player.sprinting && !GameSim_SlideButtonShown(&sim), "backwards is the same");

            // Run, jump, dive.
            GoTo(start);
            for (int i = 0; i < 24; ++i) { God(); Stp(fwd); }
            SimCmd jmp = fwd; jmp.jump = true;
            Stp(jmp);
            Stp(fwd); Stp(fwd);
            Check(!sim.player.isGrounded && GameSim_DiveButtonShown(&sim) && !GameSim_SlideButtonShown(&sim), "in the air after a sprinting jump the DIVE button is there (in the slide's place)");
            TallyClear();
            const float before = sim.player.stamina;
            SimCmd dv = fwd; dv.dive = true; dv.move = (Vector2){ 1.0f, 0.0f }; dv.fireHeld = true; dv.fire = true;     // the stick to the right, and the thumb on the fire pad
            Stp(dv);
            Check(sim.player.diving && !GameSim_DiveButtonShown(&sim) && Near(before - sim.player.stamina, STAMINA_DIVE_COST, 1.0f), "the tap dives, and costs the stamina");
            for (int i = 0; i < 4; ++i) { God(); Stp(dv); }
            Check(sim.player.moveVel.x < -4.0f && fabsf(sim.player.moveVel.z) < 0.5f, "to the way the stick was pushed at the tap (right is -x here), even though the stick has changed since");
            SimCmd later = dv; later.dive = false; later.move = (Vector2){ 0.0f, 1.0f };
            for (int i = 0; i < 3; ++i) { God(); Stp(later); }
            Check(sim.player.moveVel.x < -4.0f, "and the stick does not steer it");
            Check(sim.player.wantAim && sim.player.aimBlend > 0.3f, "holding fire during the dive aims down the sights (you can ADS in a dive)");
            for (int i = 0; i < 40; ++i) { God(); Stp(None()); }
            Check(!sim.player.diving && !sim.player.rolling, "after the roll it is over");
            Check(sim.player.lying && sim.player.diveBlend > 0.9f && sim.player.moveVel.x * sim.player.moveVel.x + sim.player.moveVel.z * sim.player.moveVel.z < 0.01f, "and the player is lying on the floor, not standing up");
            SimCmd jmp2 = None(); jmp2.jump = true;
            for (int i = 0; i < 30; ++i) { God(); Stp(None()); }
            Stp(jmp2);
            Check(sim.player.lying && sim.player.isGrounded, "a second later, with a jump asked for, still down (the jump is refused while lying)");
            SimCmd push = None(); push.move = (Vector2){ 0.0f, -1.0f };
            for (int i = 0; i < 3; ++i) { God(); Stp(push); }
            Check(!sim.player.lying && PlayerSpeed(&sim.player) > 0.5f, "pushing the stick gets the player up and walking");

            // A walking jump has none, and too tired says so.
            GoTo(start);
            SimCmd walk = None(); walk.move = (Vector2){ 0.0f, -1.0f };
            for (int i = 0; i < 20; ++i) { God(); Stp(walk); }
            SimCmd wj = walk; wj.jump = true;
            Stp(wj); Stp(walk); Stp(walk);
            Check(!sim.player.isGrounded && !GameSim_DiveButtonShown(&sim), "after a walking jump there is no DIVE button");
            GoTo(start);
            for (int i = 0; i < 24; ++i) { God(); Stp(fwd); }
            Stp(jmp); Stp(fwd); Stp(fwd);
            sim.player.stamina = STAMINA_DIVE_COST - 5.0f;
            TallyClear();
            SimCmd dv2 = fwd; dv2.dive = true;
            Stp(dv2);
            Check(!sim.player.diving && Said("TOO TIRED TO DIVE") && GameSim_DiveButtonShown(&sim), "with too little stamina it says TOO TIRED TO DIVE, and the button stays (drawn orange)");
            sim.player.health.isAlive = false;
            Stp(dv2);
            Check(!sim.player.diving, "a dead player does not dive");
            sim.player.health.isAlive = true;
        }
    }

    printf("\n-- the stim shot\n");
    {
        Boot(1);
        Inventory_Add(&sim.inv, ITEM_STIM, 2);
        DamagePlayer(&sim.player, 30.0f, sim.player.position);
        sim.player.stamina = 20.0f;
        sim.player.health.invulnerabilityTimer = 0.0f;
        const float hp0 = sim.player.health.hp;
        int slot = -1;
        for (int i = 0; i < INV_SLOTS; ++i) if (sim.inv.slots[i].item == ITEM_STIM) slot = i;
        TallyClear();
        GameSim_UseSlot(&sim, slot);
        Drain();
        Check(sim.player.stimLeft > PLAYER_STIM_SECS - 0.1f && Inventory_Count(&sim.inv, ITEM_STIM) == 1, "using a stim shot starts it and takes one from the bag");
        Check(T.snd[SIMSND_USEITEM] == 1 && Said("USED STIM SHOT"), "and says what was used, with the sound");
        Check(T.kind[SIMEV_HEAL_FLASH] == 0, "without the medkit's green flash (it heals a little, over time)");
        TallyClear();
        GameSim_UseSlot(&sim, slot);
        Drain();
        Check(Said("STIM IS STILL WORKING") && Inventory_Count(&sim.inv, ITEM_STIM) == 1, "a second one while it works is refused, and not used up");
        for (int i = 0; i < 30; ++i) Stp(None());                                  // one second
        Check(sim.player.stamina > 20.0f + 0.8f * STAMINA_STIM_RATE && sim.player.health.hp > hp0 + 2.0f, "a second later stamina is well up and health is coming back");
        for (int i = 0; i < 120; ++i) Stp(None());                                 // five seconds in all
        Check(sim.player.stimLeft == 0.0f && sim.player.stamina >= PLAYER_STAMINA_MAX - 0.01f, "when its time is up the bar is full");
        Check(sim.player.health.hp > hp0 + PLAYER_STIM_HEAL - 1.0f, "and the health it gives is back");
        TallyClear();
        GameSim_UseSlot(&sim, slot);
        Drain();
        Check(sim.player.stimLeft > 0.0f && !Inventory_Has(&sim.inv, ITEM_STIM), "the next one can be used once it is over");

        // Dropping it leaves a stim shot on the floor.
        Boot(1);
        Inventory_Add(&sim.inv, ITEM_STIM, 3);
        slot = -1;
        for (int i = 0; i < INV_SLOTS; ++i) if (sim.inv.slots[i].item == ITEM_STIM) slot = i;
        int before = 0, after = 0;
        for (int k = 0; k < MAX_PICKUPS; ++k) if (Pickups_Get(k)->active && Pickups_Get(k)->kind == PICKUP_STIM) ++before;
        GameSim_DropSlot(&sim, slot);
        Drain();
        int dropped = 0;
        for (int k = 0; k < MAX_PICKUPS; ++k) if (Pickups_Get(k)->active && Pickups_Get(k)->kind == PICKUP_STIM) { ++after; if (Pickups_Get(k)->count == 3) dropped = 1; }
        Check(after == before + 1 && dropped && !Inventory_Has(&sim.inv, ITEM_STIM), "a dropped stack of stim shots lies on the floor as a stim shot pickup holding the three");
    }

    printf("\n-- picking a gun up loads it, dropping it gives the rounds back\n");
    {
        Boot(1);
        Vector3 at = { 0 };
        PickupKind kind = PICKUP_PIPE;
        bool found = false;
        for (int k = 0; k < MAX_PICKUPS && !found; ++k) {
            const Pickup *pk = Pickups_Get(k);
            if (pk->active && !Pickup_IsAmmo(pk->kind) && Weapon_Def(Pickup_Weapon(pk->kind))->kind == WEAPON_KIND_GUN) { at = pk->pos; kind = pk->kind; found = true; }
        }
        Check(found, "floor 1 of seed 1 has a gun on the floor (the test's own set-up)");
        const WeaponId w = Pickup_Weapon(kind);
        const WeaponDef *d = Weapon_Def(w);
        const ItemId ammoItem = Item_ForAmmo(d->ammo);
        at.x += 0.3f; at.y = 0.0f;
        GoTo(at);
        SimCmd use = None(); use.use = true;
        Stp(use);
        const int loaded = d->pickupAmmo < d->magSize ? d->pickupAmmo : d->magSize;
        Check(Arsenal_IsEquipped(&sim.arsenal, w) && sim.arsenal.mag[w] == loaded, "picking a gun up puts its clip into the gun");
        Check(Inventory_Count(&sim.inv, ammoItem) == d->pickupAmmo - loaded, "and whatever the clip holds beyond the magazine goes into the bag");

        sim.arsenal.mag[w] = 7;
        const int bagBefore = Inventory_Count(&sim.inv, ammoItem);
        int slot = -1;
        for (int i = 0; i < INV_SLOTS; ++i) if (sim.inv.slots[i].item == Item_ForWeapon(w)) slot = i;
        Check(slot >= 0, "the gun is in the bag (the test's own set-up)");
        GameSim_DropSlot(&sim, slot);
        Drain();
        Check(!Arsenal_IsEquipped(&sim.arsenal, w) && sim.arsenal.mag[w] == 0, "dropping the gun empties its magazine");
        Check(Inventory_Count(&sim.inv, ammoItem) == bagBefore + 7, "and the 7 rounds that were in it are in the bag");
    }

    printf("\n-- the pipe wears out\n");
    {
        Boot(1);
        const WeaponDef *pd = Weapon_Def(WEAPON_PIPE);
        Check(pd->durability > 1, "the pipe wears out after more than one swing (the config's own value)");
        Inventory_Add(&sim.inv, Item_ForWeapon(WEAPON_PIPE), 1);
        Arsenal_Give(&sim.arsenal, WEAPON_PIPE);
        Idle(12);
        SimCmd swing = None(); swing.fire = true;
        Stp(swing);
        Idle(40);
        Check(sim.arsenal.wear[WEAPON_PIPE] == 0, "a swing at nothing does not wear it");
        sim.arsenal.wear[WEAPON_PIPE] = pd->durability - 2;
        TallyClear();
        // Hold the trigger beside the swarm's core, facing it, and put the player back there every step: the swarm wakes at the first hit
        // and shoves the player about, and this test is about the pipe, not about where the swarm goes.
        bool sawOneLeft = false;
        SimCmd held = None(); held.fire = true;
        for (int i = 0; i < 900 && Arsenal_IsEquipped(&sim.arsenal, WEAPON_PIPE); ++i) {
            Vector3 p = sim.swarm.core; p.y = 0.0f; p.x += 0.4f;
            TeleportPlayer(&sim.player, p, -1.5708f); sim.prevEye = sim.player.camera.position;
            God();
            Stp(held);
            if (sim.arsenal.wear[WEAPON_PIPE] == pd->durability - 1) sawOneLeft = true;
        }
        Check(sawOneLeft, "a swing that lands wears it by one");
        Check(!Arsenal_IsEquipped(&sim.arsenal, WEAPON_PIPE) && sim.arsenal.current == WEAPON_HANDS, "the swing that uses it up breaks it: the hands are back");
        Check(!Inventory_Has(&sim.inv, Item_ForWeapon(WEAPON_PIPE)), "it is gone from the bag too");
        Check(Said("BROKE") && T.snd[SIMSND_BREAK] == 1, "with a message and the breaking sound, once");
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
