// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/inventory.md, section "item_types.h and item_types.c"
// ============================================================================
#ifndef ITEM_TYPES_H
#define ITEM_TYPES_H

#include <stdbool.h>

#include "../weapons/weapons.h"

// Items are described by data, in two layers (the Type Object pattern from the patterns package):
//   * a small tree of kinds, each with defaults: ITEM > WEAPON > MELEE, GUN; ITEM > CONSUMABLE; ITEM > AMMO
//   * one row per item, which names its kind and overrides only what differs.
// A value of -1 means "ask the parent", so "weapons do not stack" is written once, on WEAPON.
// A new item is one new row in k_items; a new behaviour is a new kind.

// ITEM_AMMO is pistol ammo (its id is older than the other guns). The rest were added after, so the ids above are unchanged.
typedef enum {
    ITEM_NONE = 0, ITEM_PIPE, ITEM_PISTOL, ITEM_AMMO, ITEM_MEDKIT,
    ITEM_SHOTGUN, ITEM_RIFLE, ITEM_SHELLS, ITEM_RIFLE_AMMO,
    ITEM_FLASHLIGHT, ITEM_BATTERY,
    ITEM_COUNT
} ItemId;

typedef enum { KIND_ITEM = 0, KIND_WEAPON, KIND_MELEE, KIND_GUN, KIND_CONSUMABLE, KIND_AMMO, KIND_TOOL, KIND_COUNT } ItemKind;

const char *ItemKind_Name(ItemKind k);
ItemKind    ItemKind_Parent(ItemKind k);        // KIND_ITEM is its own parent
bool        Item_IsA(ItemId id, ItemKind k);    // true when id's kind is k or descends from k

const char *Item_Name(ItemId id);
const char *Item_Description(ItemId id);
ItemKind    Item_Kind(ItemId id);

int  Item_StackMax(ItemId id);       // 1 for things that do not stack
bool Item_Usable(ItemId id);         // has a USE button: a medkit heals
bool Item_Equippable(ItemId id);     // has an EQUIP button: a weapon goes in hand
bool Item_Droppable(ItemId id);

WeaponId Item_Weapon(ItemId id);     // which weapon an equippable item is; WEAPON_HANDS for others
AmmoType Item_AmmoType(ItemId id);   // which kind of rounds an ammo item is; AMMO_NONE for others
ItemId   Item_ForAmmo(AmmoType t);   // the item that holds that kind of rounds; ITEM_NONE for AMMO_NONE
ItemId   Item_ForWeapon(WeaponId w); // the item that is that weapon; ITEM_NONE for the hands
float    Item_HealAmount(ItemId id); // health a consumable gives; 0 for others

#endif
