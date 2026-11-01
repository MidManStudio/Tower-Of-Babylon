// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "weapons.h and weapons.c"
// ============================================================================
#ifndef WEAPONS_H
#define WEAPONS_H

#include <stdbool.h>

#include "raylib.h"

// The order of WeaponId is the order of the rows in assets/config/weapons.mdix.
typedef enum {
    WEAPON_HANDS = 0,   // always owned: punches
    WEAPON_PIPE,        // melee: swings
    WEAPON_PISTOL,      // gun: one projectile, light
    WEAPON_SHOTGUN,     // gun: a pump-action blast of pellets, close range
    WEAPON_RIFLE,       // gun: automatic fire that gets less accurate the longer it runs
    WEAPON_COUNT
} WeaponId;

// These three enums mirror the enums in weapons.mdix by value (game_config.c checks it at build time).
typedef enum { WEAPON_KIND_UNARMED = 0, WEAPON_KIND_MELEE, WEAPON_KIND_GUN } WeaponKind;
typedef enum { FIRE_SEMI = 0, FIRE_AUTO, FIRE_PUMP } FireMode;          // one shot per press / repeats while held / one per press, then a pump
typedef enum { AMMO_NONE = 0, AMMO_PISTOL, AMMO_SHELLS, AMMO_RIFLE, AMMO_TYPE_COUNT } AmmoType;

// One row per weapon, filled from assets/config/weapons.mdix by src/config/game_config.c. A new weapon is a new row there,
// a WeaponId here, a part list in viewmodel.c, a pickup and an item.
typedef struct {
    WeaponId id;
    char  name[16];
    WeaponKind kind;
    float damage;        // per hit; a gun's per pellet, before the projectile's distance falloff
    float cooldown;      // seconds from one attack to the next
    float windup;        // melee: seconds from the start of the swing to the moment it connects
    float duration;      // seconds the attack animation runs
    float reach;         // melee: metres from the eye
    float arcDeg;        // melee: half-angle in front of the player that is hit
    float knockback;     // melee: metres a hit enemy is pushed
    // guns
    FireMode fireMode;
    AmmoType ammo;
    int   ammoPerShot;
    int   projectile;    // index into the projectile configs (src/projectiles/projectile_config.h)
    int   pellets;       // projectiles per pull
    int   pattern;       // ShotPattern: cone or fan
    float spreadDeg;     // cone half-angle when cold
    float spreadMaxDeg;  // and when hot
    float heatPerShot;   // 1 = fully hot
    float heatRecover;   // cooling per second
    float kick;          // how hard the gun jumps in the hands; 1 is the pistol
    float noiseMetres;   // how far enemies hear a shot
    float noiseSecs;     // for how long
    int   pickupAmmo;    // rounds that come with the gun
    int   ammoBox;       // rounds in a floor box of this gun's ammo
} WeaponDef;

// Installs the table (WEAPON_COUNT rows). The config loader calls it; until then every row is zero, which is not a usable
// weapon, so a program that reads weapons loads the config first (GameConfig_LoadBaked or GameConfig_Init).
void Weapons_SetDefs(const WeaponDef defs[WEAPON_COUNT]);
const WeaponDef *Weapon_Def(WeaponId id);
const char *Weapon_IdString(WeaponId id);         // "pistol": the id the row has in weapons.mdix

const char *Ammo_Name(AmmoType t);                // "PISTOL AMMO"
int  Ammo_BoxAmount(AmmoType t);                  // rounds in a floor box, from the gun that uses it
#define AMMO_MAX 99                               // most of one kind of ammo that can be carried (an inventory stack)

typedef struct {
    bool     owned[WEAPON_COUNT];
    int      ammo;            // rounds of the weapon in hand's ammo type: the caller sets it each frame from the inventory
    WeaponId current;
    float    cooldown;        // seconds until the next attack is allowed
    float    attackT;         // seconds into the current attack, or -1 when not attacking
    bool     impactDone;      // a melee attack connects once
    int      punchHand;       // which fist throws the next punch: 0 right, 1 left
    int      lastPunchHand;   // which fist is punching now
    float    recoil;          // 1 right after a shot, fades to 0
    float    flash;           // muzzle flash, fades fast
    float    dry;             // 1 right after an empty click, fades
    float    equipT;          // 0 right after switching, rises to 1: the weapon is brought up
    float    heat;            // 0 cold to 1 hot: sustained fire opens the spread
    bool     heldPrev;        // whether the trigger was held last frame, to see a fresh press
    float    pressBuf;        // a press that came during the cooldown is kept this long and fires when it ends
} Arsenal;

// What happened this frame, for the caller to act on: the arsenal knows nothing about enemies
// or projectiles.
typedef struct {
    bool shot;               // a shot should be fired now (the caller spawns the projectiles)
    bool impact;             // a melee attack connects now
    bool dryFire;            // the trigger was pulled with no ammo
    const WeaponDef *def;    // the weapon that did it
} ArsenalEvents;

// The camera's frame, for placing things relative to where the player looks.
typedef struct { Vector3 eye, forward, right, up; } AimFrame;
AimFrame Aim_FromCamera(const Camera3D *cam);
// Where a shot leaves the gun: the pistol's muzzle, and each weapon's own (a long gun's barrel ends further out).
Vector3 Aim_Muzzle(const AimFrame *aim);
Vector3 Aim_MuzzleFor(const AimFrame *aim, WeaponId id);

void Arsenal_Init(Arsenal *a);                        // hands only, no ammo
bool Arsenal_Give(Arsenal *a, WeaponId id);           // false when already owned; otherwise owned and equipped
bool Arsenal_Equip(Arsenal *a, WeaponId id);          // takes an owned weapon in hand; false if not owned or already in hand
bool Arsenal_Remove(Arsenal *a, WeaponId id);         // gives a weapon up (dropped); hands again if it was in hand. Hands cannot be removed
void Arsenal_AddAmmo(Arsenal *a, int n);
void Arsenal_Cycle(Arsenal *a);                       // next owned weapon
int  Arsenal_OwnedCount(const Arsenal *a);
void Arsenal_Update(Arsenal *a, float dt, bool attackHeld, ArsenalEvents *out);

// The cone half-angle a shot of the weapon in hand has right now: spreadDeg when cold, spreadMaxDeg when hot.
float Arsenal_SpreadDeg(const Arsenal *a);

#endif
