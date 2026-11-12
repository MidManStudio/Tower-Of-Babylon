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
    int   pickupAmmo;    // rounds that come with the gun (they go into its magazine first, then into the bag)
    int   ammoBox;       // rounds in a floor box of this gun's ammo
    int   magSize;       // rounds the gun holds. 0 = no magazine: it fires straight from the bag, as it did before reloading existed
    float reloadSecs;    // seconds to reload a whole magazine; with reloadPerRound, seconds to load ONE round
    int   reloadPerRound;// 1: rounds go in one at a time (a shotgun's tube): pulling the trigger stops the reload and keeps what is in
    // aiming down sights (ADS): holding the fire pad with the gun in hand
    float adsFov;        // field of view in degrees at full ADS (the base is 60: lower is more zoom)
    float adsSecs;       // seconds from the hip to full ADS
    float adsMoveMul;    // share of the walking speed left while aimed
    float adsSpreadMul;  // share of the spread left at full ADS (1 = no tighter)
    // melee
    int   durability;    // melee: swings that connect before it breaks. 0 = it never wears (the hands)
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
    int      ammo;            // the RESERVE: rounds in the bag of the weapon in hand's ammo type. The caller sets it each frame from the
                              // inventory and takes back whatever the arsenal used (a reload moves rounds from here into the magazine)
    int      mag[WEAPON_COUNT];   // rounds loaded in each gun. Kept per weapon, so a gun put away keeps what is in it
    int      wear[WEAPON_COUNT];  // swings that connected, per melee weapon: durability left = durability - wear. Reset when it breaks
    bool     reloading;       // a reload is running (only the weapon in hand can reload; switching weapons stops it)
    float    reloadT;         // seconds into the current reload step
    bool     reloadPending;   // a reload was started since the last Arsenal_Update: it reports ArsenalEvents.reloadStart
    float    reloadBlend;     // 0..1, eases up while reloading and back down after: the hands' reload pose (viewmodel.c) comes in and out with it
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
    bool dryFire;            // the trigger was pulled with no ammo in the gun
    const WeaponDef *def;    // the weapon that did it
    bool reloadStart;        // a reload began (asked for, or started by itself on an empty gun)
    bool reloadRound;        // one round went in (a tube-fed gun)
    bool reloadDone;         // the reload finished: the magazine is as full as the bag allowed
} ArsenalEvents;

// The camera's frame, for placing things relative to where the player looks.
typedef struct { Vector3 eye, forward, right, up; } AimFrame;
AimFrame Aim_FromCamera(const Camera3D *cam);
// Where a shot leaves the gun: the pistol's muzzle, and each weapon's own (a long gun's barrel ends further out).
Vector3 Aim_Muzzle(const AimFrame *aim);
Vector3 Aim_MuzzleFor(const AimFrame *aim, WeaponId id);

void Arsenal_Init(Arsenal *a);                        // hands only, no ammo
// Only ARSENAL_MAX_EQUIPPED weapons (not counting the hands) can be equipped at once. "owned" in the struct means equipped; a
// weapon the player merely carries is in the inventory bag and not here. Giving up an equipped weapon (Arsenal_Remove) leaves
// it in the bag; dropping it is the inventory's job.
#define ARSENAL_MAX_EQUIPPED 2
bool Arsenal_Give(Arsenal *a, WeaponId id);           // false when already equipped, or when ARSENAL_MAX_EQUIPPED are; otherwise equipped and in hand
bool Arsenal_IsEquipped(const Arsenal *a, WeaponId id);   // the hands count as always equipped
int  Arsenal_EquippedCount(const Arsenal *a);         // equipped weapons, not counting the hands
bool Arsenal_Replace(Arsenal *a, WeaponId in, WeaponId out);   // equips "in" in place of the equipped "out" and takes it in hand; false if either is not as required
bool Arsenal_Equip(Arsenal *a, WeaponId id);          // takes an owned weapon in hand; false if not owned or already in hand
bool Arsenal_Remove(Arsenal *a, WeaponId id);         // gives a weapon up (dropped); hands again if it was in hand. Hands cannot be removed
void Arsenal_AddAmmo(Arsenal *a, int n);
void Arsenal_Cycle(Arsenal *a);                       // next owned weapon
int  Arsenal_OwnedCount(const Arsenal *a);
void Arsenal_Update(Arsenal *a, float dt, bool attackHeld, ArsenalEvents *out);

// The cone half-angle a shot of the weapon in hand has right now: spreadDeg when cold, spreadMaxDeg when hot.
float Arsenal_SpreadDeg(const Arsenal *a);

// ---- magazines and reloading ---------------------------------------------------------------------------------------
// A gun with magSize > 0 fires from its magazine (mag[]) and reloads from the reserve (ammo). One with magSize 0 fires from the
// reserve directly. Rounds in the magazine are not in the bag: the inventory only ever holds the reserve.
// Starts reloading the gun in hand. False (and nothing happens) for anything but a gun with a magazine, when the magazine is full,
// when the reserve is empty, when a reload is already running, or while the gun is still being raised.
bool  Arsenal_StartReload(Arsenal *a);
bool  Arsenal_CanReload(const Arsenal *a);                       // the same test, without starting: whether the RELOAD button exists
// Rounds the gun in hand can fire before it needs a reload: its magazine, or the reserve for a gun with no magazine.
int   Arsenal_Loaded(const Arsenal *a);
float Arsenal_ReloadFraction(const Arsenal *a);                  // 0 to 1 through the current reload step; 0 when not reloading
// Loads up to rounds into a gun's magazine (it need not be in hand); returns how many went in. Used when a gun is picked up.
int   Arsenal_Fill(Arsenal *a, WeaponId id, int rounds);
// Empties a gun's magazine and returns what was in it (a dropped gun gives its rounds back).
int   Arsenal_TakeMag(Arsenal *a, WeaponId id);

// ---- melee durability ----------------------------------------------------------------------------------------------
// Wears a melee weapon by `hits` connecting swings. True when it has just broken: its wear is reset (the next one is new) and it is
// up to the caller to take it out of the hand and the bag. The hands and anything with durability 0 never wear.
bool  Arsenal_Wear(Arsenal *a, WeaponId id, int hits);
// 1 for new (and for anything that does not wear), down to 0 as it nears breaking.
float Arsenal_Condition(const Arsenal *a, WeaponId id);

#endif
