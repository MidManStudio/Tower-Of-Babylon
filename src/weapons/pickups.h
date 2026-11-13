// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "pickups.h and pickups.c"
// ============================================================================
#ifndef PICKUPS_H
#define PICKUPS_H

#include <stdbool.h>

#include "raylib.h"
#include "../dungeon/dungeon_types.h"
#include "weapons.h"

// PICKUP_AMMO is pistol ammo (its id is older than the other guns). The rest were added after, so the ids above are unchanged.
typedef enum {
    PICKUP_PIPE = 0, PICKUP_PISTOL, PICKUP_AMMO, PICKUP_MEDKIT,
    PICKUP_SHOTGUN, PICKUP_RIFLE, PICKUP_SHELLS, PICKUP_RIFLE_AMMO,
    PICKUP_FLASHLIGHT,    // its count is the seconds of charge it holds
    PICKUP_BATTERY,
    PICKUP_STIM
} PickupKind;

bool       Pickup_IsAmmo(PickupKind kind);          // a box of rounds: picked up by walking over it
AmmoType   Pickup_AmmoType(PickupKind kind);        // which rounds a box holds; AMMO_NONE for the rest
PickupKind Pickup_ForAmmo(AmmoType type);           // the box that holds that kind of rounds
PickupKind Pickup_ForWeapon(WeaponId id);           // the pickup that is that weapon (the hands have none: PICKUP_PIPE is returned)
WeaponId   Pickup_Weapon(PickupKind kind);          // which weapon a weapon pickup is; WEAPON_HANDS for the rest

#define WEAPON_BIT(id) (1u << (id))
#define OWNED_FLASHLIGHT 0x100u         // in the `owned` mask of Pickups_SpawnForFloor: the player has a flashlight

typedef struct {
    bool       active;
    PickupKind kind;
    Vector3    pos;       // on the floor
    float      phase;     // so they do not all bob together
    int        count;     // how many it holds: rounds in a box or dropped stack, medkits in a dropped stack
    float      ignoreT;   // seconds left during which a dropped ammo box is not picked up by walking over it
} Pickup;

#define MAX_PICKUPS 24
#define PICKUP_REACH       1.8f   // how close the player must be to pick one up
#define AMMO_AUTO_REACH    1.1f   // ammo boxes are picked up by walking over them

void Pickups_Clear(void);

// Scatters this floor's pickups on open tiles the player can walk to, at least 10 steps from the
// arrival point and apart from each other, the same way every time for the same floor.
//   * Floor 1 has a pipe and a pistol, so both can be tried; floor 2 holds a shotgun and floor 3 a rifle; floors after
//     that hold one weapon chosen from all of them. A weapon the player already has (`owned` has its WEAPON_BIT)
//     turns into a box of its ammo; the pipe, which has none, into pistol ammo.
//   * Every floor also has a medkit, a stim shot and two ammo boxes: pistol ammo on floor 1, and from floor 2 one pistol box and one
//     of a kind picked among the guns that exist by then.
//   * Floor 1 holds a flashlight and two batteries. Later floors hold one battery and, four times in ten, a second.
//     A player without a flashlight (it was dropped and left behind) finds one on every floor until they have it, so it
//     is never lost for good. A flashlight on the floor holds flashlight.mdix's start_secs of charge.
// Call it after the config is loaded: box sizes come from weapons.mdix.
void Pickups_SpawnForFloor(const FloorMap *f, int floorNumber, unsigned owned);

int  Pickups_Count(void);
const Pickup *Pickups_Get(int i);

// Counts down the "just dropped" timers.
void Pickups_Update(float dt);

// Puts a stack on the floor in front of the player: tries 1.4, 1.0, 0.6 and 0 m along (fx, fz) (a unit
// direction on the floor plane) and uses the first spot on an open tile. A dropped ammo box is not
// picked up by walking for 2 seconds, so dropping it does not take it straight back. False when the
// pool is full.
bool Pickups_Drop(const FloorMap *f, Vector3 from, float fx, float fz, PickupKind kind, int count);

// Index of the nearest active ammo box (of any kind) within reach of p that can be collected by walking, or -1.
int  Pickups_NearestAmmo(Vector3 p, float reach);

// Index of the nearest active pickup within reach of p, or -1.
int  Pickups_Nearest(Vector3 p, float reach);

// Like Pickups_Nearest, but ignoring ammo boxes: the one the USE button would pick up.
int  Pickups_NearestWeapon(Vector3 p, float reach);

// Picks up every ammo box within reach of p and returns the rounds in them, all kinds added together (the game collects
// one box at a time with Pickups_NearestAmmo and Pickups_Take so it can tell the kinds apart).
int  Pickups_CollectAmmoNear(Vector3 p, float reach);

// Removes pickup i and says what it was. False if it is not active.
bool Pickups_Take(int i, PickupKind *kind);

const char *Pickup_Label(PickupKind kind);

// Draws the pickups within 40 m of viewer: a bobbing, turning prop, a glowing ring and a thin beam.
void Pickups_Draw(Vector3 viewer, float timeSecs);

#endif
