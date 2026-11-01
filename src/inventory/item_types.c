// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/inventory.md, section "item_types.h and item_types.c"
// ============================================================================
#include "item_types.h"

typedef struct {
    const char *name;
    ItemKind    parent;
    int         stackMax;    // -1 inherit
    int         usable;      // -1 inherit, else 0 or 1
    int         equippable;
    int         droppable;
} KindRow;

static const KindRow k_kinds[KIND_COUNT] = {
    //  name          parent         stack use equip drop
    { "ITEM",       KIND_ITEM,       1,    0,  0,    1 },
    { "WEAPON",     KIND_ITEM,      -1,   -1,  1,   -1 },
    { "MELEE",      KIND_WEAPON,    -1,   -1, -1,   -1 },
    { "FIREARM",    KIND_WEAPON,    -1,   -1, -1,   -1 },
    { "CONSUMABLE", KIND_ITEM,       5,    1, -1,   -1 },
    { "AMMUNITION", KIND_ITEM,      99,   -1, -1,   -1 },
    { "TOOL",       KIND_ITEM,       1,    1,  0,    1 },     // used (switched on and off), not equipped
};

typedef struct {
    const char *name;
    const char *desc;
    ItemKind    kind;
    int         stackMax, usable, equippable, droppable;   // -1 inherit
    WeaponId    weapon;
    float       heal;
    AmmoType    ammo;        // ammunition: which kind of rounds
} ItemRow;

static const ItemRow k_items[ITEM_COUNT] = {
    { "",            "",                                                              KIND_ITEM,       -1, -1, -1, -1, WEAPON_HANDS,   0.0f, AMMO_NONE   },
    { "PIPE",        "A steel pipe, taped at the grip. Hits hard. Slashes down.",      KIND_MELEE,      -1, -1, -1, -1, WEAPON_PIPE,    0.0f, AMMO_NONE   },
    { "PISTOL",      "Fires while the trigger is held. Needs pistol ammo.",            KIND_GUN,        -1, -1, -1, -1, WEAPON_PISTOL,  0.0f, AMMO_NONE   },
    { "PISTOL AMMO", "Rounds for the pistol. Stacks to 99.",                           KIND_AMMO,       -1, -1, -1, -1, WEAPON_HANDS,   0.0f, AMMO_PISTOL },
    { "MEDKIT",      "Heals 40 health. Stacks to 5.",                                  KIND_CONSUMABLE, -1, -1, -1, -1, WEAPON_HANDS,  40.0f, AMMO_NONE   },
    { "SHOTGUN",     "Pump action. Eight pellets a blast, deadly up close. Needs shells.", KIND_GUN,    -1, -1, -1, -1, WEAPON_SHOTGUN, 0.0f, AMMO_NONE   },
    { "RIFLE",       "Automatic. Climbs off target when held down. Needs rifle ammo.", KIND_GUN,        -1, -1, -1, -1, WEAPON_RIFLE,   0.0f, AMMO_NONE   },
    { "SHELLS",      "Shells for the shotgun. Stacks to 99.",                          KIND_AMMO,       -1, -1, -1, -1, WEAPON_HANDS,   0.0f, AMMO_SHELLS },
    { "RIFLE AMMO",  "Rounds for the rifle. Stacks to 99.",                            KIND_AMMO,       -1, -1, -1, -1, WEAPON_HANDS,   0.0f, AMMO_RIFLE  },
    { "FLASHLIGHT",  "Use it to switch it on and off. Runs on batteries. Dies in the dark.", KIND_TOOL,   -1, -1, -1, -1, WEAPON_HANDS,   0.0f, AMMO_NONE   },
    { "BATTERY",     "Loads into the flashlight: 150 seconds more light. Stacks to 6.", KIND_CONSUMABLE,  6, -1, -1, -1, WEAPON_HANDS,   0.0f, AMMO_NONE   },
};

const char *ItemKind_Name(ItemKind k) { return (k >= 0 && k < KIND_COUNT) ? k_kinds[k].name : ""; }
ItemKind ItemKind_Parent(ItemKind k) { return (k >= 0 && k < KIND_COUNT) ? k_kinds[k].parent : KIND_ITEM; }

bool Item_IsA(ItemId id, ItemKind k)
{
    if (id <= ITEM_NONE || id >= ITEM_COUNT) return false;
    ItemKind at = k_items[id].kind;
    for (int guard = 0; guard < KIND_COUNT; ++guard) {
        if (at == k) return true;
        if (at == KIND_ITEM) return false;
        at = k_kinds[at].parent;
    }
    return false;
}

const char *Item_Name(ItemId id) { return (id > ITEM_NONE && id < ITEM_COUNT) ? k_items[id].name : ""; }
const char *Item_Description(ItemId id) { return (id > ITEM_NONE && id < ITEM_COUNT) ? k_items[id].desc : ""; }
ItemKind Item_Kind(ItemId id) { return (id > ITEM_NONE && id < ITEM_COUNT) ? k_items[id].kind : KIND_ITEM; }

// Looks an attribute up the chain: the item's own value, then its kind, then the kind's parent, and so on.
static int Resolve(ItemId id, int ItemRow_field, int fallback)
{
    if (id <= ITEM_NONE || id >= ITEM_COUNT) return fallback;
    const ItemRow *r = &k_items[id];
    const int own = ItemRow_field == 0 ? r->stackMax : (ItemRow_field == 1 ? r->usable : (ItemRow_field == 2 ? r->equippable : r->droppable));
    if (own >= 0) return own;
    ItemKind at = r->kind;
    for (int guard = 0; guard < KIND_COUNT + 1; ++guard) {
        const KindRow *k = &k_kinds[at];
        const int v = ItemRow_field == 0 ? k->stackMax : (ItemRow_field == 1 ? k->usable : (ItemRow_field == 2 ? k->equippable : k->droppable));
        if (v >= 0) return v;
        if (at == KIND_ITEM) break;
        at = k->parent;
    }
    return fallback;
}

int  Item_StackMax(ItemId id)   { return Resolve(id, 0, 1); }
bool Item_Usable(ItemId id)     { return Resolve(id, 1, 0) != 0; }
bool Item_Equippable(ItemId id) { return Resolve(id, 2, 0) != 0; }
bool Item_Droppable(ItemId id)  { return Resolve(id, 3, 1) != 0; }

WeaponId Item_Weapon(ItemId id) { return (id > ITEM_NONE && id < ITEM_COUNT) ? k_items[id].weapon : WEAPON_HANDS; }
float Item_HealAmount(ItemId id) { return (id > ITEM_NONE && id < ITEM_COUNT) ? k_items[id].heal : 0.0f; }

AmmoType Item_AmmoType(ItemId id) { return (id > ITEM_NONE && id < ITEM_COUNT) ? k_items[id].ammo : AMMO_NONE; }

ItemId Item_ForAmmo(AmmoType t)
{
    if (t == AMMO_NONE) return ITEM_NONE;
    for (int i = ITEM_NONE + 1; i < ITEM_COUNT; ++i) if (k_items[i].ammo == t) return (ItemId)i;
    return ITEM_NONE;
}

ItemId Item_ForWeapon(WeaponId w)
{
    if (w == WEAPON_HANDS) return ITEM_NONE;
    for (int i = ITEM_NONE + 1; i < ITEM_COUNT; ++i) if (k_items[i].weapon == w && Item_IsA((ItemId)i, KIND_WEAPON)) return (ItemId)i;
    return ITEM_NONE;
}
