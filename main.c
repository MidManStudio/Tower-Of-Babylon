// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "main.c"
// ============================================================================
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "platform/platform.h"

#if TOWER_MAIN_LOOP_CALLBACK
#include <emscripten/emscripten.h>
#endif

#include "dungeon/dungeon_types.h"
#include "dungeon/floor_window.h"
#include "dungeon/dungeon_renderer.h"
#include "dungeon/staircase.h"
#include "obstacles/obstacles.h"
#include "player/player_controller.h"
#include "projectiles/projectiles.h"
#include "config/game_config.h"
#include "flashlight/flashlight.h"
#include "weapons/gunfire.h"
#include "enemies/enemy_manager.h"
#include "ui/screen_fx.h"
#include "weapons/weapons.h"
#include "weapons/pickups.h"
#include "weapons/viewmodel.h"
#include "lighting/lighting.h"
#include "inventory/inventory.h"
#include "inventory/inventory_ui.h"
#include "ui/view_clip.h"
#include "ui/highp_shader.h"
#include "audio/audio.h"
#include "audio/audio_events.h"
#include "input/input.h"
#include "hud/hud.h"
#include "floor/floor_game.h"
#include "core/fixed_step.h"
#include "input/input_latch.h"
#include "characters/characters.h"
#include "characters/character_select.h"
#include "characters/character_select_ui.h"

// Three floors live in static storage for WASM performance.
static FloorWindow g_window;

// The depth range of the 3D view lives in ui/view_clip.h: the phone's browser may give a 16 bit depth
// buffer, so the world uses a near plane of 0.25 and the hands get a range of their own.

// What the player carries. Starts with hands only. The inventory owns the items and the ammo count;
// the arsenal is the combat state of whatever is in hand and is kept in step with it.
static Inventory inv;
static bool invOpen = false;
static Arsenal arsenal;

// A short message at the bottom of the screen, such as what was just picked up (the HUD draws it).
static void Toast(const char *text) { Hud_Toast(text); }

// What the browser gave the page, shown in the diagnostic overlay; -1 means unknown.
static int g_depthBits = -1, g_msaaSamples = -1;
static Flashlight torch;                                        // its charge, flicker and beam direction
static bool torchLowWarned = false;
// The "3d" stage split into parts, in milliseconds: the worst of recent frames for each, so a spike is not missed.
enum { SUB_LIGHT, SUB_WORLD, SUB_ENTS, SUB_HANDS, SUB_COUNT };
static float subMs[SUB_COUNT];
static void SubNote(int which, double t0, double t1)
{
    const float ms = (float)((t1 - t0) * 1000.0);
    subMs[which] = ms > subMs[which] ? ms : subMs[which] * 0.985f + ms * 0.015f;
}
static float healFlash = 0.0f;
static float gunNoiseT = 0.0f;                     // the last shot can be heard for this long...
static float gunNoiseM = 0.0f;                     // ...out to this many metres (both come from the gun's row in weapons.mdix)
static uint32_t shotRng = 0x9e3779b9u;             // pellet spread and speed
static float blockToastT = 0.0f;                   // keeps "AMMO FULL" from repeating every frame

// ---- items and pickups -------------------------------------------------------------------------------
static ItemId ItemForPickup(PickupKind k)
{
    if (k == PICKUP_FLASHLIGHT) return ITEM_FLASHLIGHT;
    if (k == PICKUP_BATTERY) return ITEM_BATTERY;
    if (Pickup_IsAmmo(k)) return Item_ForAmmo(Pickup_AmmoType(k));
    if (k == PICKUP_MEDKIT) return ITEM_MEDKIT;
    return Item_ForWeapon(Pickup_Weapon(k));          // the pipe and the guns
}

static PickupKind PickupForItem(ItemId it)
{
    if (it == ITEM_MEDKIT) return PICKUP_MEDKIT;
    if (it == ITEM_FLASHLIGHT) return PICKUP_FLASHLIGHT;
    if (it == ITEM_BATTERY) return PICKUP_BATTERY;
    if (Item_AmmoType(it) != AMMO_NONE) return Pickup_ForAmmo(Item_AmmoType(it));
    if (Item_Equippable(it)) return Pickup_ForWeapon(Item_Weapon(it));
    return PICKUP_AMMO;
}

static void SpawnFloorPickups(const FloorMap *map, int floorNumber)
{
    unsigned owned = 0;                                // which weapons the player carries, so a weapon already owned turns into its ammo
    for (int w = WEAPON_HANDS + 1; w < WEAPON_COUNT; ++w)
        if (Inventory_Has(&inv, Item_ForWeapon((WeaponId)w))) owned |= WEAPON_BIT(w);
    if (Inventory_Has(&inv, ITEM_FLASHLIGHT)) owned |= OWNED_FLASHLIGHT;
    Pickups_SpawnForFloor(map, floorNumber, owned);
}

// Shows what the inventory reported this frame.
static void DrainInventoryEvents(void)
{
    InvEvent ev;
    while (Inventory_PopEvent(&inv, &ev)) {
        switch (ev.kind) {
            case INV_EV_ADDED:
                Toast(ev.count > 1 ? TextFormat("+%d %s", ev.count, Item_Name(ev.item)) : TextFormat("PICKED UP: %s", Item_Name(ev.item)));
                AudioEvents_Pickup();
                break;
            case INV_EV_FULL:     Toast("INVENTORY FULL"); break;
            case INV_EV_EQUIPPED: Toast(TextFormat("IN HAND: %s", Item_Name(ev.item))); break;
            case INV_EV_USED:     Toast(TextFormat("USED %s", Item_Name(ev.item))); AudioEvents_UseItem(); break;
            default: break;
        }
    }
}

// Using a door: the door slides open, the screen fades out, then the floor changes.
#define DOOR_OPEN_SECS 0.7f
#define DOOR_FADE_SECS 0.4f

typedef enum { SEQ_NONE = 0, SEQ_OPENING, SEQ_FADING } DoorSeq;

// True on a floor the player came down into: the stair clock is not run there.
static bool floorCleared = false;

// Where the camera was at the start of the last step: the picture blends from here to where it is now (docs/sim.md). Reset whenever
// the player is put somewhere new, or the blend would sweep across the floor.
static Vector3 g_prevEye;

// Moves the player through a door to the next or previous floor.
static FloorMap *UseDoor(PlayerController *player, DoorId id)
{
    const bool up = (id == DOOR_UP);
    if (!FloorWindow_Advance(&g_window, up ? +1 : -1)) return FloorWindow_Current(&g_window);

    FloorMap *map = FloorWindow_Current(&g_window);
    Projectiles_Clear();                        // nothing flies from one floor into the next
    SpawnEnemiesForFloor(map, g_window.currentFloor);
    SpawnFloorPickups(map, g_window.currentFloor);
    Lighting_BuildForFloor(map, g_window.currentFloor);
    floorCleared = !up;                         // coming down into a floor already climbed: its stairs are open and there is no clock
    FloorGame_NewFloor(map, g_window.currentFloor, floorCleared);

    Vector3 pos;
    float yaw;
    if (up) Staircase_SpawnBelow(&pos, &yaw);   // came up: stand in front of this floor's down door
    else    Staircase_SpawnAbove(&pos, &yaw);   // came down: stand on the top landing of the flight
    TeleportPlayer(player, pos, yaw);
    g_prevEye = player->camera.position;
    return map;
}

// Game state. File scope, because on the web the frame function is called by the browser.
static const int screenWidth = 800;
static const int screenHeight = 450;

static FloorMap *currentFloorMap;
static PlayerController player;
// The slide button is there only while the player is running (or sliding): a slide starts from a sprint.
static bool SlideButtonShown(void) { return player.health.isAlive && (player.sprinting || player.sliding); }

static void TrySlide(void)
{
    if (PlayerTrySlide(&player)) return;
    if (player.sprinting && player.stamina < STAMINA_SLIDE_COST) Toast("TOO TIRED TO SLIDE");
}

static float fade = 0.0f;          // fade in after arriving
static float fadeOut = 0.0f;       // fade out while leaving through a door
static float doorOpen[DOOR_COUNT] = { 0.0f, 0.0f };
static DoorSeq seq = SEQ_NONE;
static DoorId seqDoor = DOOR_UP;

// The largest look movement accepted in one frame, in canvas pixels.
#define MAX_LOOK_PX 90.0f

// Death: the screen fades out and the player comes back at the start of the same floor.
#define DEATH_FADE_SECS 1.2f
#define DEATH_TOTAL_SECS 2.6f
static float deathT = 0.0f;

// Feedback for shots that land: a hit marker at the centre of the screen.
static unsigned lastHitCount = 0, lastKillCount = 0;
static float hitMarker = 0.0f, killMarker = 0.0f;
#define HIT_MARKER_SECS  0.25f
#define KILL_MARKER_SECS 0.50f

static void RespawnPlayer(void)
{
    ResetPlayerHealth(&player);
    Vector3 pos;
    float yaw;
    Staircase_SpawnBelow(&pos, &yaw);
    TeleportPlayer(&player, pos, yaw);
    g_prevEye = player.camera.position;
    Projectiles_Clear();
    SpawnEnemiesForFloor(currentFloorMap, g_window.currentFloor);
    SpawnFloorPickups(currentFloorMap, g_window.currentFloor);
    FloorGame_NewFloor(currentFloorMap, g_window.currentFloor, floorCleared);       // the clock starts again on a floor that had one
    doorOpen[DOOR_UP] = doorOpen[DOOR_DOWN] = 0.0f;
    seq = SEQ_NONE;
    fadeOut = 0.0f;
    fade = 1.0f;
    deathT = 0.0f;
    if (torch.on) Flashlight_Toggle(&torch);       // it comes back switched off, with what charge it had
    AudioEvents_Reset();
}

// ---- inventory actions --------------------------------------------------------------------------------
// Forward on the floor plane, from the camera.
static void FlatForward(float *fx, float *fz)
{
    const float x = player.camera.target.x - player.camera.position.x, z = player.camera.target.z - player.camera.position.z;
    const float l = sqrtf(x * x + z * z);
    *fx = l > 0.001f ? x / l : 0.0f;
    *fz = l > 0.001f ? z / l : 1.0f;
}

static void DropSlot(int slot)
{
    if (slot < 0 || slot >= INV_SLOTS || inv.slots[slot].item == ITEM_NONE) return;
    const ItemId item = inv.slots[slot].item;
    const int count = inv.slots[slot].count;
    if (player.position.y > 0.3f) { Toast("CANNOT DROP HERE"); return; }   // pickups lie on the ground floor, not on the stairs
    float fx, fz;
    FlatForward(&fx, &fz);
    const int dropCount = item == ITEM_FLASHLIGHT ? (int)(torch.charge + 0.5f) : count;     // a dropped flashlight keeps its charge (seconds)
    if (!Pickups_Drop(currentFloorMap, player.position, fx, fz, PickupForItem(item), dropCount)) { Toast("NO ROOM TO DROP"); return; }
    Inventory_RemoveAt(&inv, slot, count);
    if (item == ITEM_FLASHLIGHT) Flashlight_Init(&torch);
    if (Item_Equippable(item)) Arsenal_Remove(&arsenal, Item_Weapon(item));
    Toast(TextFormat("DROPPED %s", Item_Name(item)));
}

// Loads one battery from the inventory into the flashlight.
static void LoadBattery(void)
{
    if (!Inventory_Has(&inv, ITEM_FLASHLIGHT)) { Toast("NO FLASHLIGHT TO LOAD IT INTO"); return; }
    const float added = Flashlight_AddBattery(&torch);
    if (added <= 0.0f) { Toast("FLASHLIGHT IS FULL"); return; }
    Inventory_Remove(&inv, ITEM_BATTERY, 1);
    Toast(TextFormat("BATTERY LOADED   +%d SECONDS", (int)(added + 0.5f)));
    AudioEvents_Battery();
    torchLowWarned = false;
}

// The flashlight button, the L key and the inventory's USE all come here. Flat, it takes a spare battery if there is one.
static void ToggleTorch(void)
{
    if (!Inventory_Has(&inv, ITEM_FLASHLIGHT) || !player.health.isAlive) return;
    if (!torch.on && torch.charge <= 0.0f) {
        if (Inventory_Has(&inv, ITEM_BATTERY)) LoadBattery();            // loading into a flat light also switches it on
        else Toast("FLASHLIGHT IS FLAT. NO BATTERY");
        return;
    }
    Flashlight_Toggle(&torch);
    AudioEvents_Flashlight(torch.on);
}

static void UseSlot(int slot)
{
    const ItemId item = inv.slots[slot].item;
    if (item == ITEM_FLASHLIGHT) { ToggleTorch(); return; }
    if (item == ITEM_BATTERY) { LoadBattery(); return; }
    if (Item_HealAmount(item) > 0.0f) {
        if (HealPlayer(&player, Item_HealAmount(item))) {
            Inventory_RemoveAt(&inv, slot, 1);
            Inventory_Post(&inv, INV_EV_USED, item, 1);
            healFlash = 1.0f;
        } else {
            Toast("HEALTH IS FULL");
        }
    }
}

// The weapon's button in the inventory: equip it from the bag, swap it for the weapon in hand when both places are taken, take an
// equipped one in hand, or put the one in hand away (it stays in the bag).
static void EquipSlot(int slot)
{
    const ItemId item = inv.slots[slot].item;
    const WeaponId w = Item_Weapon(item);
    if (Arsenal_IsEquipped(&arsenal, w)) {
        if (arsenal.current == w) { Arsenal_Remove(&arsenal, w); Toast(TextFormat("PUT AWAY %s. STILL IN THE BAG", Item_Name(item))); }
        else if (Arsenal_Equip(&arsenal, w)) Inventory_Post(&inv, INV_EV_EQUIPPED, item, 1);
        return;
    }
    if (Arsenal_Give(&arsenal, w)) { Inventory_Post(&inv, INV_EV_EQUIPPED, item, 1); return; }
    if (arsenal.current == WEAPON_HANDS) { Toast("BOTH PLACES TAKEN. HOLD ONE TO SWAP IT"); return; }
    const WeaponId out = arsenal.current;
    if (Arsenal_Replace(&arsenal, w, out)) {
        Toast(TextFormat("SWAPPED IN %s. %s IS IN THE BAG", Item_Name(item), Weapon_Def(out)->name));
        Inventory_Post(&inv, INV_EV_EQUIPPED, item, 1);
    }
}

static void DoInventoryTap(Vector2 tap)
{
    const InvUiResult r = InvUI_Tap(&inv, tap);
    switch (r.action) {
        case INVUI_CLOSE:  invOpen = false; break;
        case INVUI_SELECT: inv.selected = (inv.selected == r.slot) ? -1 : r.slot; break;
        case INVUI_USE:    UseSlot(r.slot); break;
        case INVUI_EQUIP:  EquipSlot(r.slot); break;
        case INVUI_DROP:   DropSlot(r.slot); break;
        default: break;
    }
}

// Longest step the simulation takes in one frame. A hitch or a tab switch must not
// turn into a jump across the map.
#define MAX_FRAME_DT 0.05f

// ---- The rules run 30 times a second ---------------------------------------------------------------------------
// The screen is drawn at whatever pace the display runs (60, 144 frames a second). The rules (movement, weapons, monsters, shots, the
// stair clock) step at a fixed 30 a second, so the game plays the same at any frame rate and a server can run the same steps.
//   every frame:  input, UI taps, turning the camera (aim never waits for a step), the sound and the flashlight, drawing
//   every step:   SimTick(): everything that decides what happens
// A press that comes and goes between two steps is kept until the next step takes it (input/input_latch.h), and the picture blends
// between the last two steps (the camera, the monsters, the shots). See docs/sim.md.
#define SIM_STEP_SECS (1.0f / 30.0f)
#define SIM_MAX_STEPS_PER_FRAME 5
static FixedStep g_simStep;
static SimCmd g_simCmd;

// Which door, pickup or console is within reach right now.
static void FindNear(int *nearDoor, int *nearPickup, bool *nearConsole)
{
    *nearDoor = -1;
    if (seq == SEQ_NONE) {
        for (int i = 0; i < DOOR_COUNT; ++i)
            if (Staircase_DoorInRange((DoorId)i, player.position)) { *nearDoor = i; break; }
    }
    *nearPickup = -1;
    if (seq == SEQ_NONE && player.health.isAlive && *nearDoor < 0) *nearPickup = Pickups_NearestWeapon(player.position, PICKUP_REACH);
    // The unlock console: USE is held there, not tapped. A door or a pickup in reach comes first.
    *nearConsole = *nearDoor < 0 && *nearPickup < 0 && seq == SEQ_NONE && player.health.isAlive && FloorGame_NearConsole(player.position);
}

// One step of the rules. cmd is what the player wanted since the last step.
static void SimTick(SimCmd cmd)
{
    const float dt = SIM_STEP_SECS;
    Enemies_BeginTick();                          // remembers where the monsters are, for drawing between steps
    g_prevEye = player.camera.position;
    Pickups_Update(dt);

    // Ammo boxes are picked up by walking over them; a weapon or a medkit needs the USE button.
    if (seq == SEQ_NONE && player.health.isAlive) {
        for (int box = 0; box < MAX_PICKUPS; ++box) {
            const Pickup *pk = Pickups_Get(box);
            if (!pk->active || !Pickup_IsAmmo(pk->kind) || pk->ignoreT > 0.0f) continue;
            const float bx = player.position.x - pk->pos.x, bz = player.position.z - pk->pos.z;
            if (bx * bx + bz * bz > AMMO_AUTO_REACH * AMMO_AUTO_REACH) continue;
            const ItemId boxItem = Item_ForAmmo(Pickup_AmmoType(pk->kind));
            const int rounds = pk->count;
            if (!Inventory_CanAdd(&inv, boxItem, rounds)) {       // a full stack of one kind does not stop the other kinds being picked up
                if (blockToastT <= 0.0f) { Toast(TextFormat("%s FULL", Item_Name(boxItem))); blockToastT = 2.0f; }
                continue;
            }
            Inventory_Add(&inv, boxItem, rounds);
            Pickups_Take(box, NULL);
        }
    }
    int nearDoor, nearPickup; bool nearConsole;
    FindNear(&nearDoor, &nearPickup, &nearConsole);

    // Nothing is live while a door opens or the player is dead, and with the inventory open only walking is.
    if (seq != SEQ_NONE || !player.health.isAlive || invOpen) {
        cmd.jump = cmd.fire = cmd.use = cmd.swap = cmd.light = cmd.slide = cmd.useHeld = false;
        if (seq != SEQ_NONE || !player.health.isAlive) cmd.move = (Vector2){ 0 };
    }
    if (cmd.light && Inventory_Has(&inv, ITEM_FLASHLIGHT)) ToggleTorch();
    if (cmd.slide) TrySlide();

    // Start using a door. The player stands still while it opens.
    if (cmd.use && nearDoor >= 0 && !Staircase_DoorLocked((DoorId)nearDoor, currentFloorMap)) {
        seq = SEQ_OPENING;
        seqDoor = (DoorId)nearDoor;
        AudioEvents_Door();
    }
    if (cmd.use && nearDoor < 0 && nearPickup >= 0 && seq == SEQ_NONE && player.health.isAlive) {
        const Pickup *pk = Pickups_Get(nearPickup);
        const ItemId item = ItemForPickup(pk->kind);
        const int count = pk->count;
        const int addCount = item == ITEM_FLASHLIGHT ? 1 : count;       // a flashlight's count is the seconds of charge it holds
        if (Inventory_CanAdd(&inv, item, addCount)) {
            PickupKind got;
            Pickups_Take(nearPickup, &got);
            Inventory_Add(&inv, item, addCount);
            if (item == ITEM_FLASHLIGHT) { Flashlight_Give(&torch, (float)count); Toast("FLASHLIGHT. TAP LIGHT, OR PRESS L"); }
            // A new weapon goes straight into the hand when there is a free place for it. With both places taken it stays in the
            // bag, to be swapped in from the inventory screen.
            if (Item_Equippable(item) && !Arsenal_Give(&arsenal, Item_Weapon(item)) && !Arsenal_IsEquipped(&arsenal, Item_Weapon(item)))
                Toast("IN THE BAG. SWAP IT IN FROM THE INVENTORY");
            const WeaponDef *gun = Item_Equippable(item) ? Weapon_Def(Item_Weapon(item)) : NULL;
            if (gun && gun->kind == WEAPON_KIND_GUN) {
                // A gun comes with a clip of its own kind of ammo. Anything that does not fit is left on the floor beside the player.
                const int added = Inventory_Add(&inv, Item_ForAmmo(gun->ammo), gun->pickupAmmo);
                if (added < gun->pickupAmmo) {
                    float fx, fz;
                    FlatForward(&fx, &fz);
                    Pickups_Drop(currentFloorMap, player.position, fx, fz, Pickup_ForAmmo(gun->ammo), gun->pickupAmmo - added);
                }
            }
        } else {
            Inventory_Post(&inv, INV_EV_FULL, item, count);
        }
    }
    if (cmd.swap && seq == SEQ_NONE && player.health.isAlive) Arsenal_Cycle(&arsenal);
    if (seq != SEQ_NONE || !player.health.isAlive) {
        cmd.move = (Vector2){ 0 };
        cmd.jump = false;
        cmd.fire = false;
    }

    // Sprint: the move stick pushed all the way out ahead (the icon above it turns yellow), or Shift. The controller holds
    // it back when the player is too tired. The camera was already turned this frame; the step only moves the body.
    player.wantSprint = cmd.sprint;
    PlayerStep(&player, cmd.move, dt, currentFloorMap);
    if (cmd.jump) PlayerTryJump(&player);

    // --- DOORS: open, fade out, change floor ---
    if (seq == SEQ_OPENING) {
        doorOpen[seqDoor] += dt / DOOR_OPEN_SECS;
        if (doorOpen[seqDoor] >= 1.0f) { doorOpen[seqDoor] = 1.0f; seq = SEQ_FADING; fadeOut = 0.0f; }
    } else if (seq == SEQ_FADING) {
        fadeOut += dt / DOOR_FADE_SECS;
        if (fadeOut >= 1.0f) {
            currentFloorMap = UseDoor(&player, seqDoor);
            doorOpen[DOOR_UP] = doorOpen[DOOR_DOWN] = 0.0f;
            seq = SEQ_NONE;
            fadeOut = 0.0f;
            fade = 1.0f;
        }
    }

    Vector3 lookDir = Vector3Normalize(Vector3Subtract(player.camera.target, player.camera.position));
    // Attack with whatever is in hand: punch, swing or fire.
    const AimFrame aim = Aim_FromCamera(&player.camera);
    ArsenalEvents weaponEv;
    // The inventory holds the rounds, of the kind the weapon in hand uses; the arsenal spends them.
    const ItemId ammoItem = Item_ForAmmo(Weapon_Def(arsenal.current)->ammo);     // ITEM_NONE for the hands and the pipe
    arsenal.ammo = ammoItem != ITEM_NONE ? Inventory_Count(&inv, ammoItem) : 0;
    const int ammoBefore = arsenal.ammo;
    const float spreadBefore = Arsenal_SpreadDeg(&arsenal);                      // a shot uses the spread from before it heated the gun
    Arsenal_Update(&arsenal, dt, cmd.fire, &weaponEv);
    if (ammoItem != ITEM_NONE && arsenal.ammo < ammoBefore) Inventory_Remove(&inv, ammoItem, ammoBefore - arsenal.ammo);
    if (weaponEv.shot) { Gunfire_Shoot(weaponEv.def, &aim, currentFloorMap, spreadBefore, &shotRng); AudioEvents_Shot(weaponEv.def->id); }
    if (weaponEv.dryFire) AudioEvents_DryFire();
    if (weaponEv.impact) {
        // Aim along the floor plane: a swing hits what is in front of the player, level with the eye.
        const float fl = sqrtf(lookDir.x * lookDir.x + lookDir.z * lookDir.z);
        const float fx = fl > 0.001f ? lookDir.x / fl : 0.0f, fz = fl > 0.001f ? lookDir.z / fl : 1.0f;
        const int meleeHits = EnemiesMeleeHit(currentFloorMap, player.camera.position, fx, fz, weaponEv.def->reach, weaponEv.def->arcDeg,
                                              weaponEv.def->damage, weaponEv.def->knockback);
        AudioEvents_Melee(meleeHits > 0);
    }

    // Move projectiles, then let any that touch an active enemy damage it and disappear
    UpdateProjectilesSystem(dt);
    ResolveProjectileHits(currentFloorMap, CheckAndHandleEnemyDamage);

    // A change in the running totals means a shot landed, or killed, since the last step.
    const bool hitNow = EnemyHitsLanded() != lastHitCount, killNow = EnemyKillsLanded() != lastKillCount;
    if (hitNow) { lastHitCount = EnemyHitsLanded(); hitMarker = 1.0f; }
    if (killNow) { lastKillCount = EnemyKillsLanded(); killMarker = 1.0f; }
    if (killNow) AudioEvents_HitMarker(true); else if (hitNow) AudioEvents_HitMarker(false);

    // Update Enemy AI and tracking
    // How far the player can be heard: footsteps and breathing, and for a moment after a shot, the shot.
    if (weaponEv.shot) { gunNoiseT = weaponEv.def->noiseSecs; gunNoiseM = weaponEv.def->noiseMetres; }   // each gun is as loud as its row says
    else if (gunNoiseT > 0.0f) gunNoiseT -= dt;
    {
        const float steps = player.health.isAlive ? PlayerNoise(&player) : 0.0f;
        Enemies_SetPlayerNoise(gunNoiseT > 0.0f ? (steps > gunNoiseM ? steps : gunNoiseM) : steps);
    }
    UpdateEnemiesSystem(dt, player.position, currentFloorMap);

    // The stair clock: the console, the countdown, the collapse. Whoever is still on a floor that has fallen dies.
    {
        FloorGameIn fin = { dt, currentFloorMap, player.position, player.health.isAlive, cmd.useHeld && nearConsole && !invOpen, g_window.currentFloor };
        FloorGameOut fout;
        FloorGame_Tick(&fin, &fout);
        if (fout.killPlayer && player.health.isAlive) { ApplyDamage(&player.health, player.health.maxHp * 10.0f); Toast("THE FLOOR GAVE WAY"); }
    }

    // Blows that land on the player: damage, knockback, red flash and camera shake.
    if (player.health.isAlive) {
        float blow;
        Vector3 blowFrom;
        if (EnemiesAttackPlayer(dt, player.position, player.height, &blow, &blowFrom)) {
            DamagePlayer(&player, blow, blowFrom);
            AudioEvents_Hurt();
        }
    } else {
        // Dead: cancel any door in progress, fade out, then come back.
        seq = SEQ_NONE;
        deathT += dt;
        if (deathT >= DEATH_TOTAL_SECS) RespawnPlayer();
    }
}

// ---- The character selection screen, once, at launch -------------------------------------------------------------
// Nothing of the game runs behind it (no steps, no monsters, no clock): the screen is only a row of cards, a story and a button.
// Choosing stores the character's id (characters/characters.h) and the game begins as if it had just been loaded. docs/characters.md.
static CharSelect g_charSel;
static bool g_selecting = false;

static void SelectFrame(void)
{
    float dt = GetFrameTime();
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    GameConfig_Poll();                                    // the web build swaps the real characters.mdix in when the wasm is up
    CharSelect_Resync(&g_charSel, Characters_Count());

    InputMenu menu;
    Input_Menu(&menu);
    Vector2 tap = { 0 };
    if (Input_NewTap(&tap)) CharSelect_Tap(&g_charSel, tap.x, tap.y);
    if (menu.step != 0) CharSelect_Move(&g_charSel, menu.step);
    if (menu.confirm) CharSelect_Press(&g_charSel);

    float lfx, lfz;
    FlatForward(&lfx, &lfz);
    Audio_Update(dt, player.camera.position, lfx, lfz, menu.gesture);     // the sound card opens after a touch or a key, and this is the first one

    if (g_charSel.done) {
        const CharacterDef *c = Characters_Get(g_charSel.focus);
        if (c) Characters_Select(c->id);
        g_selecting = false;
        FixedStep_Init(&g_simStep, SIM_STEP_SECS, SIM_MAX_STEPS_PER_FRAME);   // the game starts from a clean clock
        SimCmd_Init(&g_simCmd);
        fade = 1.0f;                                                          // and fades in, as after a door
    }

    BeginDrawing();
        CharSelectUI_Draw(&g_charSel, Input_Scheme() == INPUT_SCHEME_KEYBOARD);
    EndDrawing();
}

static void Frame(void)
{
    if (g_selecting) { SelectFrame(); return; }
    const double tFrame0 = GetTime();
    const float rawDt = GetFrameTime();
    float dt = rawDt;
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    GameConfig_Poll();                // swaps in the .mdix files as soon as the DixScript wasm has started (web builds)

    int nearDoor, nearPickup; bool nearConsole;
    FindNear(&nearDoor, &nearPickup, &nearConsole);

    // Taps this frame: the STATS button and popup, the INV button, the inventory screen.
    const bool wasOpen = invOpen;
    Vector2 tap = { 0 };
    const bool tapped = Input_NewTap(&tap);
    if (tapped && Hud_HandleTap(tap, wasOpen)) { /* the HUD used it */ }
    else if (tapped && !wasOpen && CheckCollisionPointRec(tap, InvUI_OpenButton())) invOpen = true;
    else if (tapped && wasOpen) DoInventoryTap(tap);
    if (invOpen) Hud_SetStatsOpen(false);
    Hud_Update(dt);
    if (blockToastT > 0.0f) blockToastT -= dt;

    bool useAvailable = (nearDoor >= 0 || nearPickup >= 0 || nearConsole) && !invOpen;

    // What the player wants this frame, from whatever the platform has: fingers, mouse, keyboard (src/input). The context says
    // which controls are live: nothing while a door opens or the player is dead, only walking with the inventory open,
    // and a button only when it has something to act on.
    InputContext ictx = { 0 };
    ictx.locked = seq != SEQ_NONE || !player.health.isAlive;
    ictx.walkOnly = invOpen;
    ictx.useAvailable = useAvailable;
    ictx.swapAvailable = Arsenal_OwnedCount(&arsenal) > 1;
    ictx.lightAvailable = Inventory_Has(&inv, ITEM_FLASHLIGHT);
    ictx.slideAvailable = SlideButtonShown();
    ictx.blockedCount = Hud_BlockedRects(ictx.blocked, TC_MAX_BLOCKED - 1);
    {
        const Rectangle ib = InvUI_OpenButton();
        ictx.blocked[ictx.blockedCount++] = (UiRect){ ib.x, ib.y, ib.width, ib.height };
    }
    InputFrame in;
    Input_Update(&ictx, &in);
    if (in.inventoryToggle) invOpen = !invOpen;
    if (in.statsToggle && !invOpen) Hud_ToggleStats();
    SimCmd_Add(&g_simCmd, &in);                       // the rules take it at the next step

    // Aim: turned every frame, so it never waits for a step. Size of this frame's look movement as the input gave it, for the
    // frame graph, then capped.
    Vector2 lookDelta = in.look;
    const float lookRaw = sqrtf(lookDelta.x * lookDelta.x + lookDelta.y * lookDelta.y);
    if (lookRaw > MAX_LOOK_PX) lookDelta = Vector2Scale(lookDelta, MAX_LOOK_PX / lookRaw);
    FrameGraph_Push(rawDt * 1000.0f, lookRaw);
    if (seq == SEQ_NONE && player.health.isAlive) PlayerApplyLook(&player, lookDelta);
    PlayerSyncCamera(&player);                        // the camera's direction follows; its position is as the last step left it

    const double tInput = GetTime();
    // The rules: as many fixed steps as the time since the last frame is worth (a stall is capped, and the rest dropped).
    const int steps = FixedStep_Advance(&g_simStep, rawDt);
    for (int i = 0; i < steps; ++i) SimTick(SimCmd_Take(&g_simCmd));
    const double tSim = GetTime();
    const float alpha = FixedStep_Alpha(&g_simStep);  // how far the next step has got: the picture is blended that far

    // What the steps changed, as the screen and the speakers see it.
    FindNear(&nearDoor, &nearPickup, &nearConsole);
    useAvailable = (nearDoor >= 0 || nearPickup >= 0 || nearConsole) && !invOpen;

    // The flashlight: drains while on, flickers when low, and its beam follows where the camera looks (every frame, so the beam
    // is never behind the view; its charge drains by time, so the frame rate does not change it).
    {
        const Vector3 lookNow = { player.camera.target.x - player.camera.position.x, player.camera.target.y - player.camera.position.y,
                                  player.camera.target.z - player.camera.position.z };
        if (torch.on && !player.health.isAlive) Flashlight_Toggle(&torch);
        Flashlight_Update(&torch, dt, lookNow);
        if (torch.died) { Toast(Inventory_Has(&inv, ITEM_BATTERY) ? "FLASHLIGHT DIED. TAP LIGHT FOR A NEW BATTERY" : "FLASHLIGHT DIED"); AudioEvents_Flashlight(false); }
        if (torch.on && Flashlight_IsLow(&torch) && !torchLowWarned) { torchLowWarned = true; Toast("THE FLASHLIGHT IS FAILING"); }
        if (!Flashlight_IsLow(&torch)) torchLowWarned = false;
    }

    // Sound: where the listener is, then what the body is doing (footsteps, jump, landing, slide, heartbeat).
    {
        float lfx, lfz;
        FlatForward(&lfx, &lfz);
        Audio_Update(dt, player.camera.position, lfx, lfz, in.gesture);             // a browser opens the sound card only after a touch or a key
        AudioEvents_Movement(dt, sqrtf(player.moveVel.x * player.moveVel.x + player.moveVel.z * player.moveVel.z), player.isGrounded, player.velocityY,
                             player.sprinting, player.sliding, player.exhausted, player.health.isAlive);
        AudioEvents_Health(player.health.hp / player.health.maxHp, player.health.isAlive);
        AudioEvents_Ambience(player.health.isAlive);
        Vector3 awakeAt = { 0 };
        float awakeDist = 0.0f;
        const bool awake = player.health.isAlive && Enemies_NearestAwake(player.position, 40.0f, &awakeAt, &awakeDist);
        AudioEvents_Enemies(dt, awake, awakeAt, awakeDist);
    }

    // Screen effects that fade by the clock, not by the step.
    if (fade > 0.0f) fade -= dt * 1.6f;
    hitMarker -= dt / HIT_MARKER_SECS;   if (hitMarker < 0.0f) hitMarker = 0.0f;
    killMarker -= dt / KILL_MARKER_SECS; if (killMarker < 0.0f) killMarker = 0.0f;
    if (healFlash > 0.0f) healFlash -= dt * 2.0f;
    DrainInventoryEvents();

    // Drawn between the last two steps: the camera, the monsters and the shots are blended that far from where the last step began.
    Enemies_SetRenderAlpha(alpha);
    Projectiles_SetRenderLag((1.0f - alpha) * SIM_STEP_SECS);
    Camera3D viewCam = player.camera;
    {
        const Vector3 dir = Vector3Subtract(player.camera.target, player.camera.position);
        viewCam.position = Vector3Lerp(g_prevEye, player.camera.position, alpha);
        viewCam.target = Vector3Add(viewCam.position, dir);
    }
    const AimFrame viewAim = Aim_FromCamera(&viewCam);

    const double tAi = GetTime();
    const double tLight0 = GetTime();
    Lighting_Update((float)GetTime());
    SubNote(SUB_LIGHT, tLight0, GetTime());
    BeginDrawing();
        ClearBackground(BLACK);
        Highp_Begin();                    // from here on, vertex math is 32 bit even on a phone (see ui/highp_shader.h)

        BeginMode3D(viewCam);
            ViewClip_Apply(&viewCam, WORLD_NEAR, WORLD_FAR);
            // Only what is inside a cone around where the camera looks is drawn, up or down as well as sideways.
            Vector3 look = { viewCam.target.x - viewCam.position.x, viewCam.target.y - viewCam.position.y,
                             viewCam.target.z - viewCam.position.z };
            // The flashlight: held a little to the right of and below the eye. The renderer keeps the dark surfaces the beam reaches,
            // and the beam shader lights them (or, with no shader, a soft glow stands in).
            const Vector3 torchPos = { viewCam.position.x + viewAim.forward.x * 0.10f + viewAim.right.x * 0.12f - viewAim.up.x * 0.08f,
                                       viewCam.position.y + viewAim.forward.y * 0.10f + viewAim.right.y * 0.12f - viewAim.up.y * 0.08f,
                                       viewCam.position.z + viewAim.forward.z * 0.10f + viewAim.right.z * 0.12f - viewAim.up.z * 0.08f };
            if (torch.level > 0.0f) {
                const FlashlightConfig *fc = Flashlight_Config();
                Highp_SetFlashlight(torchPos, torch.dir, cosf(fc->beamInnerDeg * DEG2RAD), cosf(fc->beamOuterDeg * DEG2RAD), fc->range, fc->strength * torch.level, fc->tint);
            }
            DrawFloorMapFlashlight(&torch, torchPos, !Highp_FlashlightAvailable());
            const double tWorld0 = GetTime();
            DrawFloorMapView(currentFloorMap, viewCam.position, look, 0.0f);
            SubNote(SUB_WORLD, tWorld0, GetTime());
            const double tEnts0 = GetTime();
            DrawObstaclesSystem();
            Pickups_Draw(player.position, (float)GetTime());
            DrawEnemiesSystem(player.position); // Render the enemies, with attack and hit animations

            DrawStairs(currentFloorMap, doorOpen, player.position);
            FloorGame_Draw(player.position, (float)GetTime());
            Highp_FlashlightOff();                              // the tracers, the hands and the screen are not lit by the beam
            DrawProjectilesSystem(viewCam.position);      // light that adds to what is behind it: after everything opaque
            SubNote(SUB_ENTS, tEnts0, GetTime());
            // The hands and what they hold, over everything else in the 3D view.
            const double tHands0 = GetTime();
            if (player.health.isAlive) {
                const float walk01 = PlayerSpeed(&player) / PLAYER_WALK_SPEED > 1.5f ? 1.5f : PlayerSpeed(&player) / PLAYER_WALK_SPEED;
                Viewmodel_Draw(&arsenal, &viewAim, player.isGrounded ? walk01 : 0.0f, (float)GetTime(), Lighting_AtPoint(player.position));
            }
            SubNote(SUB_HANDS, tHands0, GetTime());
        EndMode3D();
        const double tScene = GetTime();

        ScreenFx_DrawHealthEffects(screenWidth, screenHeight, player.health.hp / player.health.maxHp,
                                   player.hitFlash, (float)GetTime());
        if (healFlash > 0.0f) DrawRectangle(0, 0, screenWidth, screenHeight, Fade((Color){ 60, 220, 100, 255 }, 0.22f * healFlash));
        ScreenFx_DrawCrosshair(screenWidth, screenHeight);
        ScreenFx_DrawHitMarker(screenWidth, screenHeight, hitMarker, killMarker);

        if (fade > 0.0f) DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, fade > 1.0f ? 1.0f : fade));
        if (fadeOut > 0.0f) DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, fadeOut > 1.0f ? 1.0f : fadeOut));

        if (!player.health.isAlive) {
            const float a = deathT / DEATH_FADE_SECS;
            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, a > 1.0f ? 1.0f : a));
            if (a > 0.5f) {
                const int tw = MeasureText("YOU DIED", 48);
                DrawText("YOU DIED", (screenWidth - tw) / 2, screenHeight / 2 - 30, 48, Fade((Color){ 190, 20, 20, 255 }, a > 1.0f ? 1.0f : (a - 0.5f) * 2.0f));
            }
        }

        // Everything drawn over the view that is not a screen effect: the touch controls, the STATS button and popup, the prompt, the toast.
        {
            HudView hv = { 0 };
            hv.alive = player.health.isAlive; hv.sprinting = player.sprinting; hv.sliding = player.sliding; hv.exhausted = player.exhausted;
            hv.hp = player.health.hp; hv.maxHp = player.health.maxHp;
            hv.stamina01 = player.stamina / PLAYER_STAMINA_MAX;
            hv.exhaustRecover01 = STAMINA_EXHAUST_RECOVER / PLAYER_STAMINA_MAX;
            hv.tooTiredToSlide = player.stamina < STAMINA_SLIDE_COST;
            const WeaponDef *held = Weapon_Def(arsenal.current);
            hv.weaponName = held->name; hv.weaponIsGun = held->kind == WEAPON_KIND_GUN; hv.weaponIsMelee = held->kind == WEAPON_KIND_MELEE;
            hv.ammo = arsenal.ammo; hv.weaponDry = arsenal.dry > 0.0f; hv.weaponsOwned = Arsenal_OwnedCount(&arsenal);
            hv.hasTorch = Inventory_Has(&inv, ITEM_FLASHLIGHT); hv.torchOn = torch.on; hv.torchLow = Flashlight_IsLow(&torch);
            hv.torchFlat = torch.charge <= 0.0f; hv.torchFraction = Flashlight_Fraction(&torch);
            hv.torchSecs = torch.charge; hv.torchLevel = torch.level;
            hv.floorNumber = g_window.currentFloor; hv.enemies = CountActiveEnemies();
            char floorInfo[112];
            snprintf(floorInfo, sizeof floorInfo, "attempt %d  route %d  walls %d  pillars %d  open %d%%%s", currentFloorMap->attempts, currentFloorMap->pathLength,
                     currentFloorMap->partitions, currentFloorMap->pillars, (int)(currentFloorMap->walkableFraction * 100.0f), currentFloorMap->usedFallback ? "  FALLBACK" : "");
            hv.floorInfo = floorInfo;
            hv.useShown = useAvailable;
            if (useAvailable) {
                hv.useLocked = nearDoor >= 0 && Staircase_DoorLocked((DoorId)nearDoor, currentFloorMap);
                hv.useLabel = hv.useLocked ? "LOCKED" : (nearDoor == DOOR_UP ? "DOOR\nUP" : "DOOR\nDOWN");
                if (nearDoor < 0 && nearPickup >= 0) {
                    static char pick[40];
                    snprintf(pick, sizeof pick, "PICKUP\n%s", Pickup_Label(Pickups_Get(nearPickup)->kind));
                    hv.useLabel = pick;
                } else if (nearDoor < 0) {
                    hv.useLabel = "HOLD\nUNLOCK";                  // the stair clock's console
                }
            }
            hv.slideShown = SlideButtonShown(); hv.invOpen = invOpen;
            hv.characterName = Characters_SelectedCall();
            hv.stairPhase = FloorGame_Phase(); hv.consoleProgress = FloorGame_ConsoleProgress(); hv.consoleHeld = FloorGame_ConsoleHeld();
            hv.stairCountdown = FloorGame_CountdownLeft(); hv.collapseLeft01 = FloorGame_CollapseLeft01();
            hv.debug = (HudDebug){ g_depthBits, g_msaaSamples, { subMs[SUB_LIGHT], subMs[SUB_WORLD], subMs[SUB_ENTS], subMs[SUB_HANDS] } };
            Hud_Draw(&hv);
        }

        if (invOpen) {
            InvLoadout lo = { arsenal.current, 0u, Arsenal_EquippedCount(&arsenal), ARSENAL_MAX_EQUIPPED };
            for (int w = WEAPON_HANDS + 1; w < WEAPON_COUNT; ++w) if (arsenal.owned[w]) lo.equippedMask |= 1u << w;
            InvUI_Draw(&inv, &lo);
        }
        else         InvUI_DrawOpenButton(&inv, false);

    const double tUi = GetTime();
    Highp_End();
    EndDrawing();
    const double tEnd = GetTime();

    // Where our own time went this frame, in ms. Late frames with small numbers here were
    // held up outside the game.
    const float stages[FRAME_STAGES] = {
        (float)((tInput - tFrame0) * 1000.0), (float)((tSim - tInput) * 1000.0), (float)((tAi - tSim) * 1000.0),
        (float)((tScene - tAi) * 1000.0), (float)((tUi - tScene) * 1000.0), (float)((tEnd - tUi) * 1000.0)
    };
    FrameGraph_PushStages(stages);

}

int main(void)
{
    // Smooths polygon edges and the thin corner lines, which crawl visibly while turning.
    // A phone's browser may ignore the request; nothing breaks if it does.
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Tower of Babylon - Horror Game Show");
    Flashlight_Init(&torch);
    Input_Init();
    Hud_Init();
    Audio_Init();                     // hands sounds.mdix to the config layer, so it must come before GameConfig_Init
    FloorGame_Register();             // and floor.mdix, the stair clock's numbers
    Characters_Register();            // and characters.mdix, the roster
    GameConfig_Init();                // weapon and projectile numbers: the baked copy now, the .mdix files once the wasm is up
    g_selecting = Characters_Count() > 0;          // the selection screen runs once, before the game (no roster, no screen)
    CharSelect_Init(&g_charSel, Characters_Count());
    ScreenFx_Init();
    Highp_Init();
    ViewClip_QueryBuffers(&g_depthBits, &g_msaaSamples);
    TraceLog(LOG_INFO, "DISPLAY: depth buffer %d bits, MSAA samples %d", g_depthBits, g_msaaSamples);

    // Seed with system time so every launch creates a unique tower layout.
    uint32_t towerSeed = (uint32_t)time(NULL);
    shotRng = towerSeed * 2654435761u | 1u;
    FloorWindow_Init(&g_window, towerSeed, 1);
    currentFloorMap = FloorWindow_Current(&g_window);

    InitObstaclesSystem();
    InitProjectilesSystem();
    InitEnemiesSystem();

    // Spawn initial wave of procedural tall-cylinder enemies for floor 1
    SpawnEnemiesForFloor(currentFloorMap, g_window.currentFloor);
    Arsenal_Init(&arsenal);
    Inventory_Init(&inv);
    SpawnFloorPickups(currentFloorMap, g_window.currentFloor);
    Lighting_BuildForFloor(currentFloorMap, g_window.currentFloor);
    FloorGame_NewFloor(currentFloorMap, g_window.currentFloor, false);

    {
        Vector3 spawn;
        float spawnYaw;
        Staircase_SpawnBelow(&spawn, &spawnYaw);
        InitPlayerController(&player, spawn.x, spawn.z);
        TeleportPlayer(&player, spawn, spawnYaw);
    }
    g_prevEye = player.camera.position;
    FixedStep_Init(&g_simStep, SIM_STEP_SECS, SIM_MAX_STEPS_PER_FRAME);
    SimCmd_Init(&g_simCmd);

#if TOWER_MAIN_LOOP_CALLBACK
    // The browser drives the loop from requestAnimationFrame (fps 0), so frames are paced
    // by the display. No SetTargetFPS, no sleeping, and no Asyncify needed for the loop.
    emscripten_set_main_loop(Frame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) Frame();
#endif

    Audio_Shutdown();
    ScreenFx_Unload();
    UnloadProjectilesSystem();
    UnloadEnemiesSystem();
    CloseWindow();
    return 0;
}
