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

#if defined(__EMSCRIPTEN__)
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

// Three floors live in static storage for WASM performance.
static FloorWindow g_window;

// The depth range of the 3D view lives in ui/view_clip.h: the phone's browser may give a 16 bit depth
// buffer, so the world uses a near plane of 0.25 and the hands get a range of their own.

// What the player carries. Starts with hands only. The inventory owns the items and the ammo count;
// the arsenal is the combat state of whatever is in hand and is kept in step with it.
static Inventory inv;
static bool invOpen = false;
static Arsenal arsenal;
static Rectangle swapButtonRec = { 450, 320, 100, 80 };   // only active while the player owns more than one weapon
static bool swapDownPrev = false, useDownPrev = false;

// A short message at the bottom of the screen, such as what was just picked up.
static char toastText[48];
static float toastT = 0.0f;
static void Toast(const char *text)
{
    snprintf(toastText, sizeof toastText, "%s", text);
    toastT = 1.8f;
}

// What the browser gave the page, shown in the diagnostic overlay; -1 means unknown.
static int g_depthBits = -1, g_msaaSamples = -1;
static bool showDebug = true;                      // the frame graph and the depth readout; tap the FPS counter to toggle
static const Rectangle fpsToggleRec = { 6, 68, 120, 26 };
static const Rectangle shaderToggleRec = { 6, 94, 300, 20 };   // tap the depth readout (while the diagnostics show) to compare shader precision
static const Rectangle lightButtonRec = { 450, 150, 100, 66 };  // the flashlight: tap to switch it on or off (only once one is carried)
static const Rectangle slideButtonRec = { 570, 150, 110, 66 };  // appears while running: tap to slide
static Flashlight torch;                                        // its charge, flicker and beam direction
static bool torchLowWarned = false;
static const Rectangle runButtonRec = { 450, 225, 100, 70 };   // tap to start running; it stops by itself when you stop or tire
static bool  runToggle = false;
// The "3d" stage split into parts, in milliseconds: the worst of recent frames for each, so a spike is not missed.
enum { SUB_LIGHT, SUB_WORLD, SUB_ENTS, SUB_HANDS, SUB_COUNT };
static float subMs[SUB_COUNT];
static void SubNote(int which, double t0, double t1)
{
    const float ms = (float)((t1 - t0) * 1000.0);
    subMs[which] = ms > subMs[which] ? ms : subMs[which] * 0.985f + ms * 0.015f;
}
static float runIdleT = 0.0f;
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
                break;
            case INV_EV_FULL:     Toast("INVENTORY FULL"); break;
            case INV_EV_EQUIPPED: Toast(TextFormat("IN HAND: %s", Item_Name(ev.item))); break;
            case INV_EV_USED:     Toast(TextFormat("USED %s", Item_Name(ev.item))); break;
            default: break;
        }
    }
}

// Using a door: the door slides open, the screen fades out, then the floor changes.
#define DOOR_OPEN_SECS 0.7f
#define DOOR_FADE_SECS 0.4f

typedef enum { SEQ_NONE = 0, SEQ_OPENING, SEQ_FADING } DoorSeq;

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

    Vector3 pos;
    float yaw;
    if (up) Staircase_SpawnBelow(&pos, &yaw);   // came up: stand in front of this floor's down door
    else    Staircase_SpawnAbove(&pos, &yaw);   // came down: stand on the top landing of the flight
    TeleportPlayer(player, pos, yaw);
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

static Vector2 leftJoyCenter = { 100, 350 };
static float leftJoyRadius = 50.0f;
static Vector2 leftJoyPos = { 100, 350 };
static bool leftJoyActive = false;

static Rectangle jumpButtonRec = { 560, 320, 90, 80 };
static Rectangle shootButtonRec = { 660, 320, 110, 80 };
static Rectangle useButtonRec = { 560, 225, 210, 70 };   // only active while a door is in reach
static bool jumpPressedState = false;
static bool shootPressedState = false;

// Touches are followed by their id, so a finger keeps its job for as long as it is down:
// the one that started on the left is the joystick, the one that started on the right
// (not on a button) is the camera. A look finger can slide over the JUMP and SHOOT buttons
// without pressing them, and a second finger can no longer take over the look.
static int moveTouchId = -1;
static int lookTouchId = -1;
static Vector2 lastRightTouch = { 0 };

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
    Projectiles_Clear();
    SpawnEnemiesForFloor(currentFloorMap, g_window.currentFloor);
    SpawnFloorPickups(currentFloorMap, g_window.currentFloor);
    doorOpen[DOOR_UP] = doorOpen[DOOR_DOWN] = 0.0f;
    seq = SEQ_NONE;
    fadeOut = 0.0f;
    fade = 1.0f;
    deathT = 0.0f;
    if (torch.on) Flashlight_Toggle(&torch);       // it comes back switched off, with what charge it had
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

static void EquipSlot(int slot)
{
    const ItemId item = inv.slots[slot].item;
    if (Arsenal_Equip(&arsenal, Item_Weapon(item))) Inventory_Post(&inv, INV_EV_EQUIPPED, item, 1);
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

// A finger that went down this frame (or a mouse click when there are no touches): where, if any.
// Called once per frame, because it remembers which fingers were down last frame.
static bool NewTap(Vector2 *out)
{
    static int prevIds[16];
    static int prevN = 0;
    bool got = false;
    const int n = GetTouchPointCount();
    int ids[16];
    const int m = n < 16 ? n : 16;
    for (int i = 0; i < m; ++i) {
        ids[i] = GetTouchPointId(i);
        bool seen = false;
        for (int j = 0; j < prevN; ++j) if (prevIds[j] == ids[i]) { seen = true; break; }
        if (!seen && !got) { *out = GetTouchPosition(i); got = true; }
    }
    for (int i = 0; i < m; ++i) prevIds[i] = ids[i];
    prevN = m;
    if (n == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { *out = GetMousePosition(); got = true; }
    return got;
}

// Longest step the simulation takes in one frame. A hitch or a tab switch must not
// turn into a jump across the map.
#define MAX_FRAME_DT 0.05f

static void Frame(void)
{
    const double tFrame0 = GetTime();
    const float rawDt = GetFrameTime();
    float dt = rawDt;
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    GameConfig_Poll();                // swaps in the .mdix files as soon as the DixScript wasm has started (web builds)
    Vector2 moveDelta = { 0 };
    Vector2 lookDelta = { 0 };
    bool moveSeen = false;
    bool lookSeen = false;
    leftJoyActive = false;
    bool triggerShoot = false;
    bool triggerJump = false;
    bool triggerUse = false;
    bool triggerSwap = false;
    jumpPressedState = false;
    shootPressedState = false;

    // Which door, if any, is within reach right now.
    int nearDoor = -1;
    if (seq == SEQ_NONE) {
        for (int i = 0; i < DOOR_COUNT; ++i)
            if (Staircase_DoorInRange((DoorId)i, player.position)) { nearDoor = i; break; }
    }

    // Taps this frame: the INV button, the inventory screen, the FPS counter that toggles the diagnostics.
    const bool wasOpen = invOpen;
    Vector2 tap = { 0 };
    const bool tapped = NewTap(&tap);
    if (tapped && !wasOpen && CheckCollisionPointRec(tap, fpsToggleRec)) showDebug = !showDebug;
    if (tapped && !wasOpen && showDebug && CheckCollisionPointRec(tap, shaderToggleRec)) Highp_SetEnabled(!Highp_Enabled());
    if (tapped && !wasOpen && CheckCollisionPointRec(tap, runButtonRec)) runToggle = !runToggle;
    if (tapped && !wasOpen && SlideButtonShown() && CheckCollisionPointRec(tap, slideButtonRec)) TrySlide();
    if (!wasOpen && (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_LEFT_CONTROL))) TrySlide();
    if (tapped && !wasOpen && Inventory_Has(&inv, ITEM_FLASHLIGHT) && CheckCollisionPointRec(tap, lightButtonRec)) ToggleTorch();
    if (!wasOpen && IsKeyPressed(KEY_L)) ToggleTorch();
    if (tapped && !wasOpen && CheckCollisionPointRec(tap, InvUI_OpenButton())) { invOpen = true; lookTouchId = -1; }
    if (IsKeyPressed(KEY_I) || IsKeyPressed(KEY_TAB)) { invOpen = !invOpen; lookTouchId = -1; }
    if (tapped && wasOpen) DoInventoryTap(tap);
    if (blockToastT > 0.0f) blockToastT -= dt;
    Pickups_Update(dt);

    // Ammo boxes are picked up by walking over them; a weapon or a medkit needs the USE button.
    int nearPickup = -1;
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
        if (nearDoor < 0) nearPickup = Pickups_NearestWeapon(player.position, PICKUP_REACH);
    }
    const bool useAvailable = (nearDoor >= 0 || nearPickup >= 0) && !invOpen;
    bool swapDown = false, useDown = false;

    int touchCount = GetTouchPointCount();
    if (touchCount > 0) {
        for (int i = 0; i < touchCount; i++) {
            const int id = GetTouchPointId(i);
            const Vector2 tPos = GetTouchPosition(i);

            if (id == moveTouchId || (moveTouchId < 0 && tPos.x < screenWidth / 2 && id != lookTouchId &&
                                      !CheckCollisionPointRec(tPos, fpsToggleRec) && !(showDebug && CheckCollisionPointRec(tPos, shaderToggleRec)))) {
                // Joystick finger.
                moveTouchId = id;
                moveSeen = true;
                leftJoyActive = true;
                float dist = Vector2Distance(tPos, leftJoyCenter);
                if (dist <= leftJoyRadius) {
                    leftJoyPos = tPos;
                } else {
                    Vector2 dir = Vector2Normalize(Vector2Subtract(tPos, leftJoyCenter));
                    leftJoyPos = Vector2Add(leftJoyCenter, Vector2Scale(dir, leftJoyRadius));
                }
                moveDelta.x = (leftJoyPos.x - leftJoyCenter.x) / leftJoyRadius;
                moveDelta.y = (leftJoyPos.y - leftJoyCenter.y) / leftJoyRadius;
            } else if (id == lookTouchId) {
                // Camera finger: wherever it goes, it keeps turning the camera.
                lookSeen = true;
                lookDelta = Vector2Subtract(tPos, lastRightTouch);
                lastRightTouch = tPos;
            } else if (tPos.x >= screenWidth / 2) {
                // A new finger on the right: a button, or else it becomes the camera finger.
                // While the inventory is open, and on the INV button, it is a tap and nothing else.
                if (invOpen || CheckCollisionPointRec(tPos, InvUI_OpenButton()) || CheckCollisionPointRec(tPos, runButtonRec) || (Inventory_Has(&inv, ITEM_FLASHLIGHT) && CheckCollisionPointRec(tPos, lightButtonRec)) || (SlideButtonShown() && CheckCollisionPointRec(tPos, slideButtonRec))) {
                    // handled as a tap above
                } else if (useAvailable && CheckCollisionPointRec(tPos, useButtonRec)) {
                    useDown = true;
                } else if (Arsenal_OwnedCount(&arsenal) > 1 && CheckCollisionPointRec(tPos, swapButtonRec)) {
                    swapDown = true;
                } else if (CheckCollisionPointRec(tPos, jumpButtonRec)) {
                    jumpPressedState = true;
                    triggerJump = true;
                } else if (CheckCollisionPointRec(tPos, shootButtonRec)) {
                    shootPressedState = true;
                    triggerShoot = true;
                } else if (lookTouchId < 0) {
                    lookTouchId = id;
                    lastRightTouch = tPos;
                    lookSeen = true;
                }
            }
        }
    } else {
        Vector2 mousePos = GetMousePosition();
        if (invOpen || CheckCollisionPointRec(mousePos, InvUI_OpenButton())) {
            // the inventory screen and its button take the mouse; they are handled as a tap above
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (useAvailable && CheckCollisionPointRec(mousePos, useButtonRec)) useDown = true;
            else if (Arsenal_OwnedCount(&arsenal) > 1 && CheckCollisionPointRec(mousePos, swapButtonRec)) swapDown = true;
            else if (CheckCollisionPointRec(mousePos, jumpButtonRec)) triggerJump = true;
        }
        if (!invOpen && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, shootButtonRec)) { shootPressedState = true; triggerShoot = true; }
        if (!invOpen && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mousePos.x >= screenWidth / 2) {
            if (!CheckCollisionPointRec(mousePos, InvUI_OpenButton()) && !CheckCollisionPointRec(mousePos, runButtonRec) && !(Inventory_Has(&inv, ITEM_FLASHLIGHT) && CheckCollisionPointRec(mousePos, lightButtonRec)) && !(SlideButtonShown() && CheckCollisionPointRec(mousePos, slideButtonRec)) && !CheckCollisionPointRec(mousePos, jumpButtonRec) && !CheckCollisionPointRec(mousePos, shootButtonRec) &&
                !(useAvailable && CheckCollisionPointRec(mousePos, useButtonRec)) &&
                !(Arsenal_OwnedCount(&arsenal) > 1 && CheckCollisionPointRec(mousePos, swapButtonRec))) {
                Vector2 mouseDelta = GetMouseDelta();
                lookDelta.x = mouseDelta.x;
                lookDelta.y = mouseDelta.y;
            }
        }
    }

    // A finger that lifted gives up its job.
    if (!moveSeen) moveTouchId = -1;
    if (!lookSeen) lookTouchId = -1;

    // Size of this frame's look movement as the input gave it, for the frame graph, then capped.
    const float lookRaw = sqrtf(lookDelta.x * lookDelta.x + lookDelta.y * lookDelta.y);
    if (lookRaw > MAX_LOOK_PX) lookDelta = Vector2Scale(lookDelta, MAX_LOOK_PX / lookRaw);
    FrameGraph_Push(rawDt * 1000.0f, lookRaw);

    if (!leftJoyActive) leftJoyPos = leftJoyCenter;

    if (IsKeyDown(KEY_W)) moveDelta.y = -1.0f;
    if (IsKeyDown(KEY_S)) moveDelta.y = 1.0f;
    if (IsKeyDown(KEY_A)) moveDelta.x = -1.0f;
    if (IsKeyDown(KEY_D)) moveDelta.x = 1.0f;
    if (IsKeyPressed(KEY_SPACE)) triggerJump = true;
    if (IsKeyDown(KEY_F)) triggerShoot = true;
    if (IsKeyPressed(KEY_E)) useDown = true;
    if (IsKeyPressed(KEY_Q)) swapDown = true;
    if (invOpen) {                               // the screen is open: walking still works, the rest waits
        lookDelta = (Vector2){ 0 };
        triggerJump = triggerShoot = false;
        useDown = false;
    }

    // USE and SWAP act once per press, not on every frame they are held.
    triggerUse = useDown && !useDownPrev;
    triggerSwap = swapDown && !swapDownPrev;
    useDownPrev = useDown;
    swapDownPrev = swapDown;

    // Start using a door. The player stands still while it opens.
    if (triggerUse && nearDoor >= 0 && !Staircase_DoorLocked((DoorId)nearDoor, currentFloorMap)) {
        seq = SEQ_OPENING;
        seqDoor = (DoorId)nearDoor;
    }
    if (triggerUse && nearDoor < 0 && nearPickup >= 0 && seq == SEQ_NONE && player.health.isAlive) {
        const Pickup *pk = Pickups_Get(nearPickup);
        const ItemId item = ItemForPickup(pk->kind);
        const int count = pk->count;
        const int addCount = item == ITEM_FLASHLIGHT ? 1 : count;       // a flashlight's count is the seconds of charge it holds
        if (Inventory_CanAdd(&inv, item, addCount)) {
            PickupKind got;
            Pickups_Take(nearPickup, &got);
            Inventory_Add(&inv, item, addCount);
            if (item == ITEM_FLASHLIGHT) { Flashlight_Give(&torch, (float)count); Toast("FLASHLIGHT. TAP LIGHT, OR PRESS L"); }
            if (Item_Equippable(item)) Arsenal_Give(&arsenal, Item_Weapon(item));      // a new weapon goes straight into the hand
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
    if (triggerSwap && seq == SEQ_NONE && player.health.isAlive) Arsenal_Cycle(&arsenal);
    if (seq != SEQ_NONE || !player.health.isAlive) {
        moveDelta = (Vector2){ 0 };
        lookDelta = (Vector2){ 0 };
        triggerJump = false;
        triggerShoot = false;
    }
    if (toastT > 0.0f) toastT -= dt;

    // Running: a tap on RUN (or holding Shift). It stops by itself when the stick is let go for a moment, or when the
    // player is exhausted, so it never has to be turned off by hand.
    if (Vector2Length(moveDelta) < 0.1f) runIdleT += dt; else runIdleT = 0.0f;
    if (runIdleT > 0.4f || player.exhausted || seq != SEQ_NONE || !player.health.isAlive) runToggle = false;
    player.wantSprint = runToggle || IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    const double tInput = GetTime();
    UpdatePlayerController(&player, moveDelta, lookDelta, dt, currentFloorMap);

    if (triggerJump) PlayerTryJump(&player);

    // The flashlight: drains while on, flickers when low, and its beam follows where the camera looks.
    {
        const Vector3 lookNow = { player.camera.target.x - player.camera.position.x, player.camera.target.y - player.camera.position.y,
                                  player.camera.target.z - player.camera.position.z };
        if (torch.on && !player.health.isAlive) Flashlight_Toggle(&torch);
        Flashlight_Update(&torch, dt, lookNow);
        if (torch.died) Toast(Inventory_Has(&inv, ITEM_BATTERY) ? "FLASHLIGHT DIED. TAP LIGHT FOR A NEW BATTERY" : "FLASHLIGHT DIED");
        if (torch.on && Flashlight_IsLow(&torch) && !torchLowWarned) { torchLowWarned = true; Toast("THE FLASHLIGHT IS FAILING"); }
        if (!Flashlight_IsLow(&torch)) torchLowWarned = false;
    }

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
    if (fade > 0.0f) fade -= dt * 1.6f;

    Vector3 lookDir = Vector3Normalize(Vector3Subtract(player.camera.target, player.camera.position));
    // Attack with whatever is in hand: punch, swing or fire.
    const AimFrame aim = Aim_FromCamera(&player.camera);
    ArsenalEvents weaponEv;
    // The inventory holds the rounds, of the kind the weapon in hand uses; the arsenal spends them.
    const ItemId ammoItem = Item_ForAmmo(Weapon_Def(arsenal.current)->ammo);     // ITEM_NONE for the hands and the pipe
    arsenal.ammo = ammoItem != ITEM_NONE ? Inventory_Count(&inv, ammoItem) : 0;
    const int ammoBefore = arsenal.ammo;
    const float spreadBefore = Arsenal_SpreadDeg(&arsenal);                      // a shot uses the spread from before it heated the gun
    Arsenal_Update(&arsenal, dt, triggerShoot, &weaponEv);
    if (ammoItem != ITEM_NONE && arsenal.ammo < ammoBefore) Inventory_Remove(&inv, ammoItem, ammoBefore - arsenal.ammo);
    if (weaponEv.shot) Gunfire_Shoot(weaponEv.def, &aim, currentFloorMap, spreadBefore, &shotRng);
    if (weaponEv.impact) {
        // Aim along the floor plane: a swing hits what is in front of the player, level with the eye.
        const float fl = sqrtf(lookDir.x * lookDir.x + lookDir.z * lookDir.z);
        const float fx = fl > 0.001f ? lookDir.x / fl : 0.0f, fz = fl > 0.001f ? lookDir.z / fl : 1.0f;
        EnemiesMeleeHit(currentFloorMap, player.camera.position, fx, fz, weaponEv.def->reach, weaponEv.def->arcDeg,
                        weaponEv.def->damage, weaponEv.def->knockback);
    }

    // Move projectiles, then let any that touch an active enemy damage it and disappear
    UpdateProjectilesSystem(dt);
    ResolveProjectileHits(currentFloorMap, CheckAndHandleEnemyDamage);

    // A change in the running totals means a shot landed, or killed, since the last frame.
    if (EnemyHitsLanded() != lastHitCount) { lastHitCount = EnemyHitsLanded(); hitMarker = 1.0f; }
    if (EnemyKillsLanded() != lastKillCount) { lastKillCount = EnemyKillsLanded(); killMarker = 1.0f; }
    hitMarker -= dt / HIT_MARKER_SECS;   if (hitMarker < 0.0f) hitMarker = 0.0f;
    killMarker -= dt / KILL_MARKER_SECS; if (killMarker < 0.0f) killMarker = 0.0f;
    const double tSim = GetTime();

    // Update Enemy AI and tracking
    // How far the player can be heard: footsteps and breathing, and for a moment after a shot, the shot.
    if (weaponEv.shot) { gunNoiseT = weaponEv.def->noiseSecs; gunNoiseM = weaponEv.def->noiseMetres; }   // each gun is as loud as its row says
    else if (gunNoiseT > 0.0f) gunNoiseT -= dt;
    {
        const float steps = player.health.isAlive ? PlayerNoise(&player) : 0.0f;
        Enemies_SetPlayerNoise(gunNoiseT > 0.0f ? (steps > gunNoiseM ? steps : gunNoiseM) : steps);
    }
    UpdateEnemiesSystem(dt, player.position, currentFloorMap);

    // Blows that land on the player: damage, knockback, red flash and camera shake.
    if (player.health.isAlive) {
        float blow;
        Vector3 blowFrom;
        if (EnemiesAttackPlayer(dt, player.position, player.height, &blow, &blowFrom))
            DamagePlayer(&player, blow, blowFrom);
    } else {
        // Dead: cancel any door in progress, fade out, then come back.
        seq = SEQ_NONE;
        deathT += dt;
        if (deathT >= DEATH_TOTAL_SECS) RespawnPlayer();
    }

    if (healFlash > 0.0f) healFlash -= dt * 2.0f;
    DrainInventoryEvents();

    const double tAi = GetTime();
    const double tLight0 = GetTime();
    Lighting_Update((float)GetTime());
    SubNote(SUB_LIGHT, tLight0, GetTime());
    BeginDrawing();
        ClearBackground(BLACK);
        Highp_Begin();                    // from here on, vertex math is 32 bit even on a phone (see ui/highp_shader.h)

        BeginMode3D(player.camera);
            ViewClip_Apply(&player.camera, WORLD_NEAR, WORLD_FAR);
            // Only what is inside a cone around where the camera looks is drawn, up or down as well as sideways.
            Vector3 look = { player.camera.target.x - player.camera.position.x, player.camera.target.y - player.camera.position.y,
                             player.camera.target.z - player.camera.position.z };
            // The flashlight: held a little to the right of and below the eye. The renderer keeps the dark surfaces the beam reaches,
            // and the beam shader lights them (or, with no shader, a soft glow stands in).
            const Vector3 torchPos = { player.camera.position.x + aim.forward.x * 0.10f + aim.right.x * 0.12f - aim.up.x * 0.08f,
                                       player.camera.position.y + aim.forward.y * 0.10f + aim.right.y * 0.12f - aim.up.y * 0.08f,
                                       player.camera.position.z + aim.forward.z * 0.10f + aim.right.z * 0.12f - aim.up.z * 0.08f };
            if (torch.level > 0.0f) {
                const FlashlightConfig *fc = Flashlight_Config();
                Highp_SetFlashlight(torchPos, torch.dir, cosf(fc->beamInnerDeg * DEG2RAD), cosf(fc->beamOuterDeg * DEG2RAD), fc->range, fc->strength * torch.level, fc->tint);
            }
            DrawFloorMapFlashlight(&torch, torchPos, !Highp_FlashlightAvailable());
            const double tWorld0 = GetTime();
            DrawFloorMapView(currentFloorMap, player.camera.position, look, 0.0f);
            SubNote(SUB_WORLD, tWorld0, GetTime());
            const double tEnts0 = GetTime();
            DrawObstaclesSystem();
            Pickups_Draw(player.position, (float)GetTime());
            DrawEnemiesSystem(player.position); // Render the enemies, with attack and hit animations

            DrawStairs(currentFloorMap, doorOpen, player.position);
            Highp_FlashlightOff();                              // the tracers, the hands and the screen are not lit by the beam
            DrawProjectilesSystem(player.camera.position);      // light that adds to what is behind it: after everything opaque
            SubNote(SUB_ENTS, tEnts0, GetTime());
            // The hands and what they hold, over everything else in the 3D view.
            const double tHands0 = GetTime();
            if (player.health.isAlive) {
                const float walk01 = PlayerSpeed(&player) / PLAYER_WALK_SPEED > 1.5f ? 1.5f : PlayerSpeed(&player) / PLAYER_WALK_SPEED;
                Viewmodel_Draw(&arsenal, &aim, player.isGrounded ? walk01 : 0.0f, (float)GetTime(), Lighting_AtPoint(player.position));
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

        if (useAvailable) {
            const bool locked = nearDoor >= 0 && Staircase_DoorLocked((DoorId)nearDoor, currentFloorMap);
            DrawRectangleRec(useButtonRec, Fade(locked ? MAROON : DARKGREEN, 0.75f));
            DrawRectangleLines((int)useButtonRec.x, (int)useButtonRec.y, (int)useButtonRec.width, (int)useButtonRec.height, WHITE);
            const char *label = locked ? "LOCKED" : (nearDoor == DOOR_UP ? "USE DOOR - UP" : "USE DOOR - DOWN");
            if (nearDoor < 0) {
                static char pick[40];
                snprintf(pick, sizeof pick, "PICK UP %s", Pickup_Label(Pickups_Get(nearPickup)->kind));
                label = pick;
            }
            DrawText(label, (int)useButtonRec.x + 14, (int)useButtonRec.y + 26, 18, WHITE);
        }

        DrawCircleV(leftJoyCenter, leftJoyRadius, Fade(LIGHTGRAY, 0.5f));
        DrawCircleV(leftJoyCenter, leftJoyRadius - 15, Fade(GRAY, 0.3f));
        DrawCircleV(leftJoyPos, 22, Fade(DARKGRAY, 0.8f));

        if (!invOpen) {
            // RUN: green while running, amber when too tired to start, grey otherwise.
            const bool tired = player.exhausted;
            const Color runCol = player.sprinting ? GREEN : (runToggle ? (tired ? ORANGE : DARKGREEN) : (tired ? ORANGE : DARKGRAY));
            DrawRectangleRec(runButtonRec, Fade(runCol, 0.7f));
            DrawRectangleLines((int)runButtonRec.x, (int)runButtonRec.y, (int)runButtonRec.width, (int)runButtonRec.height, WHITE);
            DrawText(tired ? "TIRED" : (player.sprinting ? "RUNNING" : "RUN"), (int)runButtonRec.x + (tired ? 22 : (player.sprinting ? 12 : 31)), (int)runButtonRec.y + 28, 16, WHITE);
        }
        if (SlideButtonShown()) {
            const Color sc = player.sliding ? SKYBLUE : (player.stamina < STAMINA_SLIDE_COST ? ORANGE : (Color){ 40, 120, 170, 255 });
            DrawRectangleRec(slideButtonRec, Fade(sc, 0.7f));
            DrawRectangleLines((int)slideButtonRec.x, (int)slideButtonRec.y, (int)slideButtonRec.width, (int)slideButtonRec.height, WHITE);
            DrawText(player.sliding ? "SLIDING" : "SLIDE", (int)slideButtonRec.x + (player.sliding ? 16 : 28), (int)slideButtonRec.y + 24, 16, WHITE);
        }
        if (Inventory_Has(&inv, ITEM_FLASHLIGHT)) {
            const bool failing = torch.on && Flashlight_IsLow(&torch);
            const bool blink = failing && ((int)(GetTime() * 4.0) & 1);
            const Color lc = torch.on ? (blink ? RED : (failing ? ORANGE : (Color){ 190, 160, 30, 255 })) : (torch.charge <= 0.0f ? MAROON : DARKGRAY);
            DrawRectangleRec(lightButtonRec, Fade(lc, 0.7f));
            DrawRectangleLines((int)lightButtonRec.x, (int)lightButtonRec.y, (int)lightButtonRec.width, (int)lightButtonRec.height, WHITE);
            const char *lightLabel = torch.on ? "LIGHT ON" : (torch.charge <= 0.0f ? "FLAT" : "LIGHT");
            DrawText(lightLabel, (int)lightButtonRec.x + (torch.on ? 12 : (torch.charge <= 0.0f ? 30 : 28)), (int)lightButtonRec.y + 18, 16, WHITE);
            // The charge, as a bar along the bottom of the button; it turns orange when the light starts to fail.
            const float frac = Flashlight_Fraction(&torch);
            DrawRectangle((int)lightButtonRec.x + 6, (int)lightButtonRec.y + 46, (int)((lightButtonRec.width - 12.0f) * frac), 8, Flashlight_IsLow(&torch) ? ORANGE : (Color){ 255, 233, 160, 255 });
            DrawRectangleLines((int)lightButtonRec.x + 6, (int)lightButtonRec.y + 46, (int)lightButtonRec.width - 12, 8, Fade(WHITE, 0.6f));
        }
        Color jumpCol = jumpPressedState ? BLUE : DARKGRAY;
        DrawRectangleRec(jumpButtonRec, Fade(jumpCol, 0.7f));
        DrawRectangleLines((int)jumpButtonRec.x, (int)jumpButtonRec.y, (int)jumpButtonRec.width, (int)jumpButtonRec.height, WHITE);
        DrawText("JUMP", (int)jumpButtonRec.x + 22, (int)jumpButtonRec.y + 30, 16, WHITE);

        Color shootCol = shootPressedState ? RED : DARKGRAY;
        DrawRectangleRec(shootButtonRec, Fade(shootCol, 0.7f));
        DrawRectangleLines((int)shootButtonRec.x, (int)shootButtonRec.y, (int)shootButtonRec.width, (int)shootButtonRec.height, WHITE);
        const WeaponDef *held = Weapon_Def(arsenal.current);
        const char *attackLabel = held->kind == WEAPON_KIND_GUN ? "FIRE" : (held->kind == WEAPON_KIND_MELEE ? "SWING" : "PUNCH");
        DrawText(attackLabel, (int)shootButtonRec.x + 24, (int)shootButtonRec.y + 30, 16, WHITE);

        if (Arsenal_OwnedCount(&arsenal) > 1) {
            DrawRectangleRec(swapButtonRec, Fade(DARKGRAY, 0.7f));
            DrawRectangleLines((int)swapButtonRec.x, (int)swapButtonRec.y, (int)swapButtonRec.width, (int)swapButtonRec.height, WHITE);
            DrawText("SWAP", (int)swapButtonRec.x + 24, (int)swapButtonRec.y + 30, 16, WHITE);
        }

        // What is in hand, and ammo for the gun.
        DrawText(held->kind == WEAPON_KIND_GUN ? TextFormat("%s  %d", held->name, arsenal.ammo) : held->name,
                 screenWidth / 2 + 108, screenHeight - 24, 16, arsenal.dry > 0.0f ? RED : WHITE);
        if (toastT > 0.0f) {
            const int tw = MeasureText(toastText, 18);
            DrawText(toastText, (screenWidth - tw) / 2, screenHeight - 56, 18, Fade(YELLOW, toastT > 1.0f ? 1.0f : toastT));
        }

        DrawText(TextFormat("TOWER OF BABYLON - FLOOR %d", g_window.currentFloor), 10, 10, 18, YELLOW);
        DrawText(TextFormat("attempt %d  route %d  walls %d  pillars %d  open %d%%%s",
                            currentFloorMap->attempts, currentFloorMap->pathLength, currentFloorMap->partitions,
                            currentFloorMap->pillars, (int)(currentFloorMap->walkableFraction * 100.0f),
                            currentFloorMap->usedFallback ? "  FALLBACK" : ""), 10, 32, 14, LIGHTGRAY);
        DrawText(TextFormat("enemies %d", CountActiveEnemies()), 10, 52, 14, LIGHTGRAY);
        DrawFPS(10, 72);
        if (showDebug) {
            DrawText(TextFormat("depth %d bit  msaa %d  shader %s (tap)", g_depthBits, g_msaaSamples,
                                Highp_Enabled() ? "highp" : (Highp_Available() ? "raylib default" : "default")), 10, 96, 14, (Color){ 120, 200, 255, 255 });
            DrawText(TextFormat("3d ms (submit): light %.1f  world %.1f  ents %.1f  hands %.1f", subMs[SUB_LIGHT], subMs[SUB_WORLD], subMs[SUB_ENTS], subMs[SUB_HANDS]),
                     10, 132, 14, (Color){ 120, 200, 255, 255 });
            DrawText(TextFormat("lights on %d  dim %d  flicker %d  dead %d", Lighting_CountMode(LIGHT_ON), Lighting_CountMode(LIGHT_DIM),
                                Lighting_CountMode(LIGHT_FLICKER), Lighting_CountMode(LIGHT_DEAD)), 10, 114, 14, (Color){ 120, 200, 255, 255 });
            {
                const ConfigStatus *cs = GameConfig_Status();
                const char *wasmState = cs->wasm == MDIX_WASM_READY ? "wasm ready" : (cs->wasm == MDIX_WASM_LOADING ? "wasm loading" : (cs->wasm == MDIX_WASM_FAILED ? "wasm off" : "wasm idle"));
                DrawText(TextFormat("config: %s  (%s)%s", cs->source == CONFIG_FROM_FILES ? "from the .mdix files" : "baked copy", wasmState, cs->bakedStale ? "  baked copy is older than the files" : ""),
                         10, 150, 14, cs->note[0] ? ORANGE : (Color){ 120, 200, 255, 255 });
                if (cs->note[0]) DrawText(cs->note, 10, 166, 12, ORANGE);
                if (Inventory_Has(&inv, ITEM_FLASHLIGHT))
                    DrawText(TextFormat("flashlight: %s  %.0f s left  level %.2f  beam by %s", torch.on ? "on" : "off", (double)torch.charge, (double)torch.level,
                                        Highp_FlashlightAvailable() ? "shader" : "glow (no beam shader)"), 10, 182, 14, (Color){ 255, 233, 160, 255 });
            }
        }

        ScreenFx_DrawHealthBar(screenWidth / 2 - 100, screenHeight - 22, 200, 14, player.health.hp, player.health.maxHp);
        ScreenFx_DrawStaminaBar(screenWidth / 2 - 100, screenHeight - 6, 200, 5, player.stamina / PLAYER_STAMINA_MAX, player.exhausted,
                                STAMINA_EXHAUST_RECOVER / PLAYER_STAMINA_MAX, (float)GetTime());
        if (showDebug) FrameGraph_Draw(screenWidth - 210, 6, 204);

        if (invOpen) InvUI_Draw(&inv, arsenal.current);
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
    GameConfig_Init();                // weapon and projectile numbers: the baked copy now, the .mdix files once the wasm is up
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

    {
        Vector3 spawn;
        float spawnYaw;
        Staircase_SpawnBelow(&spawn, &spawnYaw);
        InitPlayerController(&player, spawn.x, spawn.z);
        TeleportPlayer(&player, spawn, spawnYaw);
    }

#if defined(__EMSCRIPTEN__)
    // The browser drives the loop from requestAnimationFrame (fps 0), so frames are paced
    // by the display. No SetTargetFPS, no sleeping, and no Asyncify needed for the loop.
    emscripten_set_main_loop(Frame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) Frame();
#endif

    ScreenFx_Unload();
    UnloadProjectilesSystem();
    UnloadEnemiesSystem();
    CloseWindow();
    return 0;
}
