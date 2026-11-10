// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/sim.md, section "game_sim.h and game_sim.c"
// ============================================================================
#include "game_sim.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "raymath.h"
#include "../projectiles/projectiles.h"
#include "../weapons/gunfire.h"
#include "../weapons/pickups.h"
#include "../enemies/enemy_manager.h"
#include "../floor/floor_game.h"
#include "../swarm/swarm_config.h"

// This file is what SimTick and the functions around it in main.c became, moved as they were (docs/sim.md, "How it was moved"). The
// three differences, all in how the outside world is told: a message is Say(), a sound is Snd(), and the lights, the fade and the
// markers are events (SimEvent): the code that used to call Toast, AudioEvents_* and set the fade and marker variables now posts them
// to s->events, and main.c plays them after the steps.

static void Say(GameSim *s, const char *text) { SimEvents_PushToast(&s->events, text); }
static void SayF(GameSim *s, const char *fmt, ...)
{
    char buf[SIM_EVENT_TEXT];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SimEvents_PushToast(&s->events, buf);
}
static void Snd(GameSim *s, SimSound sound, int arg) { SimEvents_PushSound(&s->events, sound, arg); }

// ResolveProjectileHits takes a function with no user pointer, so the step points this at the simulation it is running.
static GameSim *g_hitSim = NULL;

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

static void SpawnFloorPickups(GameSim *s, const FloorMap *map, int floorNumber)
{
    unsigned owned = 0;                                // which weapons the player carries, so a weapon already owned turns into its ammo
    for (int w = WEAPON_HANDS + 1; w < WEAPON_COUNT; ++w)
        if (Inventory_Has(&s->inv, Item_ForWeapon((WeaponId)w))) owned |= WEAPON_BIT(w);
    if (Inventory_Has(&s->inv, ITEM_FLASHLIGHT)) owned |= OWNED_FLASHLIGHT;
    Pickups_SpawnForFloor(map, floorNumber, owned);
}

// What the inventory reported, as messages and sounds.
static void DrainInventory(GameSim *s)
{
    InvEvent ev;
    while (Inventory_PopEvent(&s->inv, &ev)) {
        switch (ev.kind) {
            case INV_EV_ADDED:
                if (ev.count > 1) SayF(s, "+%d %s", ev.count, Item_Name(ev.item));
                else              SayF(s, "PICKED UP: %s", Item_Name(ev.item));
                Snd(s, SIMSND_PICKUP, 0);
                break;
            case INV_EV_FULL:     Say(s, "INVENTORY FULL"); break;
            case INV_EV_EQUIPPED: SayF(s, "IN HAND: %s", Item_Name(ev.item)); break;
            case INV_EV_USED:     SayF(s, "USED %s", Item_Name(ev.item)); Snd(s, SIMSND_USEITEM, 0); break;
            default: break;
        }
    }
}

static unsigned SwarmHitsTotal(const GameSim *s)  { return s->swarmHitsBase + Swarm_HitsLanded(&s->swarm); }
static unsigned SwarmKillsTotal(const GameSim *s) { return s->swarmKillsBase + Swarm_KillsLanded(&s->swarm); }

// ---- the swarm, doors, death ---------------------------------------------------------------------
// One swarm per floor from first_floor, then every_n_floors, placed from the floor's own seed. Its health is a box in the middle of it.
static void SpawnFloorSwarm(GameSim *s, const FloorMap *map, int floor)
{
    s->swarmHitsBase += Swarm_HitsLanded(&s->swarm);
    s->swarmKillsBase += Swarm_KillsLanded(&s->swarm);
    Swarm_Clear(&s->swarm);
    const SwarmConfig *c = SwarmConfig_Get();
    if (floor < c->firstFloor || (floor - c->firstFloor) % c->everyNFloors != 0) return;
    Swarm_Spawn(&s->swarm, c, map, map->seed * 2654435761u + (uint32_t)floor);
}

static bool HitAnything(Vector3 pos, float radius, float damage)
{
    return CheckAndHandleEnemyDamage(pos, radius, damage) || Swarm_Hit(&g_hitSim->swarm, pos, radius, damage);
}

static FloorMap *UseDoor(GameSim *s, DoorId id)
{
    const bool up = (id == DOOR_UP);
    if (!FloorWindow_Advance(&s->window, up ? +1 : -1)) return FloorWindow_Current(&s->window);

    FloorMap *map = FloorWindow_Current(&s->window);
    Projectiles_Clear();                        // nothing flies from one floor into the next
    SpawnEnemiesForFloor(map, s->window.currentFloor);
    SpawnFloorSwarm(s, map, s->window.currentFloor);
    SpawnFloorPickups(s, map, s->window.currentFloor);
    SimEvents_Push(&s->events, SIMEV_FLOOR, s->window.currentFloor);     // the lights of this floor are made by whoever draws it
    s->floorCleared = !up;                         // coming down into a floor already climbed: its stairs are open and there is no clock
    FloorGame_NewFloor(map, s->window.currentFloor, s->floorCleared);

    Vector3 pos;
    float yaw;
    if (up) Staircase_SpawnBelow(&pos, &yaw);   // came up: stand in front of this floor's down door
    else    Staircase_SpawnAbove(&pos, &yaw);   // came down: stand on the top landing of the flight
    TeleportPlayer(&s->player, pos, yaw);
    s->prevEye = s->player.camera.position;
    return map;
}

static void TrySlide(GameSim *s)
{
    if (PlayerTrySlide(&s->player)) return;
    if (s->player.sprinting && s->player.stamina < STAMINA_SLIDE_COST) Say(s, "TOO TIRED TO SLIDE");
}

static void RespawnPlayer(GameSim *s)
{
    ResetPlayerHealth(&s->player);
    Vector3 pos;
    float yaw;
    Staircase_SpawnBelow(&pos, &yaw);
    TeleportPlayer(&s->player, pos, yaw);
    s->prevEye = s->player.camera.position;
    Projectiles_Clear();
    SpawnEnemiesForFloor(s->map, s->window.currentFloor);
    SpawnFloorSwarm(s, s->map, s->window.currentFloor);
    SpawnFloorPickups(s, s->map, s->window.currentFloor);
    FloorGame_NewFloor(s->map, s->window.currentFloor, s->floorCleared);       // the clock starts again on a floor that had one
    s->doorOpen[DOOR_UP] = s->doorOpen[DOOR_DOWN] = 0.0f;
    s->seq = SEQ_NONE;
    s->fadeOut = 0.0f;
    SimEvents_Push(&s->events, SIMEV_FADE_IN, 0);
    s->deathT = 0.0f;
    if (s->torch.on) Flashlight_Toggle(&s->torch);       // it comes back switched off, with what charge it had
    SimEvents_Push(&s->events, SIMEV_RESET_AUDIO, 0);
}

// ---- inventory actions ---------------------------------------------------------------------------
void GameSim_FlatForward(const GameSim *s, float *fx, float *fz)
{
    const float x = s->player.camera.target.x - s->player.camera.position.x, z = s->player.camera.target.z - s->player.camera.position.z;
    const float l = sqrtf(x * x + z * z);
    *fx = l > 0.001f ? x / l : 0.0f;
    *fz = l > 0.001f ? z / l : 1.0f;
}

static void DropSlotImpl(GameSim *s, int slot)
{
    if (slot < 0 || slot >= INV_SLOTS || s->inv.slots[slot].item == ITEM_NONE) return;
    const ItemId item = s->inv.slots[slot].item;
    const int count = s->inv.slots[slot].count;
    if (s->player.position.y > 0.3f) { Say(s, "CANNOT DROP HERE"); return; }   // pickups lie on the ground floor, not on the stairs
    float fx, fz;
    GameSim_FlatForward(s, &fx, &fz);
    const int dropCount = item == ITEM_FLASHLIGHT ? (int)(s->torch.charge + 0.5f) : count;     // a dropped flashlight keeps its charge (seconds)
    if (!Pickups_Drop(s->map, s->player.position, fx, fz, PickupForItem(item), dropCount)) { Say(s, "NO ROOM TO DROP"); return; }
    Inventory_RemoveAt(&s->inv, slot, count);
    if (item == ITEM_FLASHLIGHT) Flashlight_Init(&s->torch);
    if (Item_Equippable(item)) Arsenal_Remove(&s->arsenal, Item_Weapon(item));
    SayF(s, "DROPPED %s", Item_Name(item));
}

static void LoadBattery(GameSim *s)
{
    if (!Inventory_Has(&s->inv, ITEM_FLASHLIGHT)) { Say(s, "NO FLASHLIGHT TO LOAD IT INTO"); return; }
    const float added = Flashlight_AddBattery(&s->torch);
    if (added <= 0.0f) { Say(s, "FLASHLIGHT IS FULL"); return; }
    Inventory_Remove(&s->inv, ITEM_BATTERY, 1);
    SayF(s, "BATTERY LOADED   +%d SECONDS", (int)(added + 0.5f));
    Snd(s, SIMSND_BATTERY, 0);
    s->torchLowWarned = false;
}

static void ToggleTorch(GameSim *s)
{
    if (!Inventory_Has(&s->inv, ITEM_FLASHLIGHT) || !s->player.health.isAlive) return;
    if (!s->torch.on && s->torch.charge <= 0.0f) {
        if (Inventory_Has(&s->inv, ITEM_BATTERY)) LoadBattery(s);            // loading into a flat light also switches it on
        else Say(s, "FLASHLIGHT IS FLAT. NO BATTERY");
        return;
    }
    Flashlight_Toggle(&s->torch);
    Snd(s, SIMSND_FLASHLIGHT, s->torch.on);
}

static void UseSlotImpl(GameSim *s, int slot)
{
    const ItemId item = s->inv.slots[slot].item;
    if (item == ITEM_FLASHLIGHT) { ToggleTorch(s); return; }
    if (item == ITEM_BATTERY) { LoadBattery(s); return; }
    if (Item_HealAmount(item) > 0.0f) {
        if (HealPlayer(&s->player, Item_HealAmount(item))) {
            Inventory_RemoveAt(&s->inv, slot, 1);
            Inventory_Post(&s->inv, INV_EV_USED, item, 1);
            SimEvents_Push(&s->events, SIMEV_HEAL_FLASH, 0);
        } else {
            Say(s, "HEALTH IS FULL");
        }
    }
}

static void EquipSlotImpl(GameSim *s, int slot)
{
    const ItemId item = s->inv.slots[slot].item;
    const WeaponId w = Item_Weapon(item);
    if (Arsenal_IsEquipped(&s->arsenal, w)) {
        if (s->arsenal.current == w) { Arsenal_Remove(&s->arsenal, w); SayF(s, "PUT AWAY %s. STILL IN THE BAG", Item_Name(item)); }
        else if (Arsenal_Equip(&s->arsenal, w)) Inventory_Post(&s->inv, INV_EV_EQUIPPED, item, 1);
        return;
    }
    if (Arsenal_Give(&s->arsenal, w)) { Inventory_Post(&s->inv, INV_EV_EQUIPPED, item, 1); return; }
    if (s->arsenal.current == WEAPON_HANDS) { Say(s, "BOTH PLACES TAKEN. HOLD ONE TO SWAP IT"); return; }
    const WeaponId out = s->arsenal.current;
    if (Arsenal_Replace(&s->arsenal, w, out)) {
        SayF(s, "SWAPPED IN %s. %s IS IN THE BAG", Item_Name(item), Weapon_Def(out)->name);
        Inventory_Post(&s->inv, INV_EV_EQUIPPED, item, 1);
    }
}

void GameSim_DropSlot(GameSim *s, int slot)  { DropSlotImpl(s, slot);  DrainInventory(s); }
void GameSim_UseSlot(GameSim *s, int slot)   { UseSlotImpl(s, slot);   DrainInventory(s); }
void GameSim_EquipSlot(GameSim *s, int slot) { EquipSlotImpl(s, slot); DrainInventory(s); }

// The slide button is there only while the player is running (or sliding): a slide starts from a sprint.
bool GameSim_SlideButtonShown(const GameSim *s) { return s->player.health.isAlive && (s->player.sprinting || s->player.sliding); }

// ---- the step ----------------------------------------------------------------------------------------
void GameSim_FindNear(const GameSim *s, int *nearDoor, int *nearPickup, bool *nearConsole)
{
    *nearDoor = -1;
    if (s->seq == SEQ_NONE) {
        for (int i = 0; i < DOOR_COUNT; ++i)
            if (Staircase_DoorInRange((DoorId)i, s->player.position)) { *nearDoor = i; break; }
    }
    *nearPickup = -1;
    if (s->seq == SEQ_NONE && s->player.health.isAlive && *nearDoor < 0) *nearPickup = Pickups_NearestWeapon(s->player.position, PICKUP_REACH);
    // The unlock console: USE is held there, not tapped. A door or a pickup in reach comes first.
    *nearConsole = *nearDoor < 0 && *nearPickup < 0 && s->seq == SEQ_NONE && s->player.health.isAlive && FloorGame_NearConsole(s->player.position);
}

// One step of the rules. cmd is what the player wanted since the last step.
void GameSim_Step(GameSim *s, SimCmd cmd)
{
    const float dt = SIM_STEP_SECS;
    SimOut_Bind(&s->events);                      // the stair clock (floor/floor_game.c) posts its sounds and messages to the same queue
    if (s->blockToastT > 0.0f) s->blockToastT -= dt;
    Enemies_BeginTick();                          // remembers where the monsters are, for drawing between steps
    Swarm_BeginTick(&s->swarm);                    // and every dot of the swarm
    s->prevEye = s->player.camera.position;
    Pickups_Update(dt);

    // Ammo boxes are picked up by walking over them; a weapon or a medkit needs the USE button.
    if (s->seq == SEQ_NONE && s->player.health.isAlive) {
        for (int box = 0; box < MAX_PICKUPS; ++box) {
            const Pickup *pk = Pickups_Get(box);
            if (!pk->active || !Pickup_IsAmmo(pk->kind) || pk->ignoreT > 0.0f) continue;
            const float bx = s->player.position.x - pk->pos.x, bz = s->player.position.z - pk->pos.z;
            if (bx * bx + bz * bz > AMMO_AUTO_REACH * AMMO_AUTO_REACH) continue;
            const ItemId boxItem = Item_ForAmmo(Pickup_AmmoType(pk->kind));
            const int rounds = pk->count;
            if (!Inventory_CanAdd(&s->inv, boxItem, rounds)) {       // a full stack of one kind does not stop the other kinds being picked up
                if (s->blockToastT <= 0.0f) { SayF(s, "%s FULL", Item_Name(boxItem)); s->blockToastT = 2.0f; }
                continue;
            }
            Inventory_Add(&s->inv, boxItem, rounds);
            Pickups_Take(box, NULL);
        }
    }
    int nearDoor, nearPickup; bool nearConsole;
    GameSim_FindNear(s, &nearDoor, &nearPickup, &nearConsole);

    // Nothing is live while a door opens or the player is dead, and with the inventory open only walking is.
    if (s->seq != SEQ_NONE || !s->player.health.isAlive || s->invOpen) {
        cmd.jump = cmd.fire = cmd.use = cmd.swap = cmd.light = cmd.slide = cmd.useHeld = false;
        if (s->seq != SEQ_NONE || !s->player.health.isAlive) cmd.move = (Vector2){ 0 };
    }
    if (cmd.light && Inventory_Has(&s->inv, ITEM_FLASHLIGHT)) ToggleTorch(s);
    if (cmd.slide) TrySlide(s);

    // Start using a door. The player stands still while it opens.
    if (cmd.use && nearDoor >= 0 && !Staircase_DoorLocked((DoorId)nearDoor, s->map)) {
        s->seq = SEQ_OPENING;
        s->seqDoor = (DoorId)nearDoor;
        Snd(s, SIMSND_DOOR, 0);
    }
    if (cmd.use && nearDoor < 0 && nearPickup >= 0 && s->seq == SEQ_NONE && s->player.health.isAlive) {
        const Pickup *pk = Pickups_Get(nearPickup);
        const ItemId item = ItemForPickup(pk->kind);
        const int count = pk->count;
        const int addCount = item == ITEM_FLASHLIGHT ? 1 : count;       // a flashlight's count is the seconds of charge it holds
        if (Inventory_CanAdd(&s->inv, item, addCount)) {
            PickupKind got;
            Pickups_Take(nearPickup, &got);
            Inventory_Add(&s->inv, item, addCount);
            if (item == ITEM_FLASHLIGHT) { Flashlight_Give(&s->torch, (float)count); Say(s, "FLASHLIGHT. TAP LIGHT, OR PRESS L"); }
            // A new weapon goes straight into the hand when there is a free place for it. With both places taken it stays in the
            // bag, to be swapped in from the inventory screen.
            if (Item_Equippable(item) && !Arsenal_Give(&s->arsenal, Item_Weapon(item)) && !Arsenal_IsEquipped(&s->arsenal, Item_Weapon(item)))
                Say(s, "IN THE BAG. SWAP IT IN FROM THE INVENTORY");
            const WeaponDef *gun = Item_Equippable(item) ? Weapon_Def(Item_Weapon(item)) : NULL;
            if (gun && gun->kind == WEAPON_KIND_GUN) {
                // A gun comes with a clip of its own kind of ammo. Anything that does not fit is left on the floor beside the player.
                const int added = Inventory_Add(&s->inv, Item_ForAmmo(gun->ammo), gun->pickupAmmo);
                if (added < gun->pickupAmmo) {
                    float fx, fz;
                    GameSim_FlatForward(s, &fx, &fz);
                    Pickups_Drop(s->map, s->player.position, fx, fz, Pickup_ForAmmo(gun->ammo), gun->pickupAmmo - added);
                }
            }
        } else {
            Inventory_Post(&s->inv, INV_EV_FULL, item, count);
        }
    }
    if (cmd.swap && s->seq == SEQ_NONE && s->player.health.isAlive) Arsenal_Cycle(&s->arsenal);
    if (s->seq != SEQ_NONE || !s->player.health.isAlive) {
        cmd.move = (Vector2){ 0 };
        cmd.jump = false;
        cmd.fire = false;
    }

    // Sprint: the move stick pushed all the way out ahead (the icon above it turns yellow), or Shift. The controller holds
    // it back when the player is too tired. The camera was already turned this frame; the step only moves the body.
    s->player.wantSprint = cmd.sprint;
    PlayerStep(&s->player, cmd.move, dt, s->map);
    if (cmd.jump) PlayerTryJump(&s->player);

    // --- DOORS: open, fade out, change floor ---
    if (s->seq == SEQ_OPENING) {
        s->doorOpen[s->seqDoor] += dt / DOOR_OPEN_SECS;
        if (s->doorOpen[s->seqDoor] >= 1.0f) { s->doorOpen[s->seqDoor] = 1.0f; s->seq = SEQ_FADING; s->fadeOut = 0.0f; }
    } else if (s->seq == SEQ_FADING) {
        s->fadeOut += dt / DOOR_FADE_SECS;
        if (s->fadeOut >= 1.0f) {
            s->map = UseDoor(s, s->seqDoor);
            s->doorOpen[DOOR_UP] = s->doorOpen[DOOR_DOWN] = 0.0f;
            s->seq = SEQ_NONE;
            s->fadeOut = 0.0f;
            SimEvents_Push(&s->events, SIMEV_FADE_IN, 0);
        }
    }

    Vector3 lookDir = Vector3Normalize(Vector3Subtract(s->player.camera.target, s->player.camera.position));
    // Attack with whatever is in hand: punch, swing or fire.
    const AimFrame aim = Aim_FromCamera(&s->player.camera);
    ArsenalEvents weaponEv;
    // The inventory holds the rounds, of the kind the weapon in hand uses; the arsenal spends them.
    const ItemId ammoItem = Item_ForAmmo(Weapon_Def(s->arsenal.current)->ammo);     // ITEM_NONE for the hands and the pipe
    s->arsenal.ammo = ammoItem != ITEM_NONE ? Inventory_Count(&s->inv, ammoItem) : 0;
    const int ammoBefore = s->arsenal.ammo;
    const float spreadBefore = Arsenal_SpreadDeg(&s->arsenal);                      // a shot uses the spread from before it heated the gun
    Arsenal_Update(&s->arsenal, dt, cmd.fire, &weaponEv);
    if (ammoItem != ITEM_NONE && s->arsenal.ammo < ammoBefore) Inventory_Remove(&s->inv, ammoItem, ammoBefore - s->arsenal.ammo);
    if (weaponEv.shot) { Gunfire_Shoot(weaponEv.def, &aim, s->map, spreadBefore, &s->shotRng); Snd(s, SIMSND_SHOT, weaponEv.def->id); }
    if (weaponEv.dryFire) Snd(s, SIMSND_DRYFIRE, 0);
    if (weaponEv.impact) {
        // Aim along the floor plane: a swing hits what is in front of the player, level with the eye.
        const float fl = sqrtf(lookDir.x * lookDir.x + lookDir.z * lookDir.z);
        const float fx = fl > 0.001f ? lookDir.x / fl : 0.0f, fz = fl > 0.001f ? lookDir.z / fl : 1.0f;
        const int meleeHits = EnemiesMeleeHit(s->map, s->player.camera.position, fx, fz, weaponEv.def->reach, weaponEv.def->arcDeg,
                                              weaponEv.def->damage, weaponEv.def->knockback)
                              + Swarm_MeleeHit(&s->swarm, s->player.camera.position, fx, fz, weaponEv.def->reach, weaponEv.def->arcDeg, weaponEv.def->damage);
        Snd(s, SIMSND_MELEE, meleeHits > 0);
    }

    // Move projectiles, then let any that touch an active enemy damage it and disappear
    UpdateProjectilesSystem(dt);
    g_hitSim = s;                                 // the callback has no user pointer
    ResolveProjectileHits(s->map, HitAnything);

    // A change in the running totals means a shot landed, or killed, since the last step.
    const unsigned hitsTotal = EnemyHitsLanded() + SwarmHitsTotal(s), killsTotal = EnemyKillsLanded() + SwarmKillsTotal(s);
    const bool hitNow = hitsTotal != s->lastHitCount, killNow = killsTotal != s->lastKillCount;
    if (hitNow) { s->lastHitCount = hitsTotal; SimEvents_Push(&s->events, SIMEV_HIT, 0); }
    if (killNow) { s->lastKillCount = killsTotal; SimEvents_Push(&s->events, SIMEV_KILL, 0); }
    if (killNow) Snd(s, SIMSND_HITMARKER, 1); else if (hitNow) Snd(s, SIMSND_HITMARKER, 0);

    // Update Enemy AI and tracking
    // How far the player can be heard: footsteps and breathing, and for a moment after a shot, the shot.
    if (weaponEv.shot) { s->gunNoiseT = weaponEv.def->noiseSecs; s->gunNoiseM = weaponEv.def->noiseMetres; }   // each gun is as loud as its row says
    else if (s->gunNoiseT > 0.0f) s->gunNoiseT -= dt;
    {
        const float steps = s->player.health.isAlive ? PlayerNoise(&s->player) : 0.0f;
        Enemies_SetPlayerNoise(s->gunNoiseT > 0.0f ? (steps > s->gunNoiseM ? steps : s->gunNoiseM) : steps);
    }
    UpdateEnemiesSystem(dt, s->player.position, s->map);
    const SwarmTarget swarmTarget = { s->player.position, s->player.radius, s->player.height, s->player.health.isAlive };
    Swarm_Step(&s->swarm, dt, s->map, &swarmTarget);            // the swarm crawls on, wakes, and is pushed out of walls and the player

    // The stair clock: the console, the countdown, the collapse. Whoever is still on a floor that has fallen dies.
    {
        FloorGameIn fin = { dt, s->map, s->player.position, s->player.health.isAlive, cmd.useHeld && nearConsole && !s->invOpen, s->window.currentFloor };
        FloorGameOut fout;
        FloorGame_Tick(&fin, &fout);
        if (fout.killPlayer && s->player.health.isAlive) { ApplyDamage(&s->player.health, s->player.health.maxHp * 10.0f); Say(s, "THE FLOOR GAVE WAY"); }
    }

    // Blows that land on the player: damage, knockback, red flash and camera shake.
    if (s->player.health.isAlive) {
        float blow;
        Vector3 blowFrom;
        if (EnemiesAttackPlayer(dt, s->player.position, s->player.height, &blow, &blowFrom)) {
            DamagePlayer(&s->player, blow, blowFrom);
            Snd(s, SIMSND_HURT, 0);
        }
        float bite;
        Vector3 biteFrom;
        if (Swarm_Bite(&s->swarm, dt, &swarmTarget, &bite, &biteFrom)) {       // the dots touching the player bite, all together
            DamagePlayer(&s->player, bite, biteFrom);
            Snd(s, SIMSND_HURT, 0);
        }
    } else {
        // Dead: cancel any door in progress, fade out, then come back.
        s->seq = SEQ_NONE;
        s->deathT += dt;
        if (s->deathT >= DEATH_TOTAL_SECS) RespawnPlayer(s);
    }
    DrainInventory(s);
    ++s->steps;
    SimOut_Bind(NULL);
}

// ---- the flashlight, every frame ---------------------------------------------------------------------------------
// It drains by time (so the frame rate does not change it), flickers when low, and its beam follows where the camera looks, which
// is turned every frame, so the beam is never behind the view.
void GameSim_TorchFrame(GameSim *s, float dt)
{
    const Vector3 lookNow = { s->player.camera.target.x - s->player.camera.position.x, s->player.camera.target.y - s->player.camera.position.y,
                              s->player.camera.target.z - s->player.camera.position.z };
    if (s->torch.on && !s->player.health.isAlive) Flashlight_Toggle(&s->torch);
    Flashlight_Update(&s->torch, dt, lookNow);
    if (s->torch.died) { Say(s, Inventory_Has(&s->inv, ITEM_BATTERY) ? "FLASHLIGHT DIED. TAP LIGHT FOR A NEW BATTERY" : "FLASHLIGHT DIED"); Snd(s, SIMSND_FLASHLIGHT, 0); }
    if (s->torch.on && Flashlight_IsLow(&s->torch) && !s->torchLowWarned) { s->torchLowWarned = true; Say(s, "THE FLASHLIGHT IS FAILING"); }
    if (!Flashlight_IsLow(&s->torch)) s->torchLowWarned = false;
}

// ---- start ---------------------------------------------------------------------------------------------
void GameSim_Init(GameSim *s, uint32_t towerSeed)
{
    memset(s, 0, sizeof *s);
    Flashlight_Init(&s->torch);
    s->shotRng = towerSeed * 2654435761u | 1u;
    FloorWindow_Init(&s->window, towerSeed, 1);
    s->map = FloorWindow_Current(&s->window);
}

void GameSim_Begin(GameSim *s)
{
    SimOut_Bind(&s->events);
    SpawnEnemiesForFloor(s->map, s->window.currentFloor);
    SpawnFloorSwarm(s, s->map, s->window.currentFloor);
    Arsenal_Init(&s->arsenal);
    Inventory_Init(&s->inv);
    SpawnFloorPickups(s, s->map, s->window.currentFloor);
    SimEvents_Push(&s->events, SIMEV_FLOOR, s->window.currentFloor);
    FloorGame_NewFloor(s->map, s->window.currentFloor, false);
    {
        Vector3 spawn;
        float spawnYaw;
        Staircase_SpawnBelow(&spawn, &spawnYaw);
        InitPlayerController(&s->player, spawn.x, spawn.z);
        TeleportPlayer(&s->player, spawn, spawnYaw);
    }
    s->prevEye = s->player.camera.position;
    SimOut_Bind(NULL);
}

bool GameSim_PopEvent(GameSim *s, SimEvent *out) { return SimEvents_Pop(&s->events, out); }
