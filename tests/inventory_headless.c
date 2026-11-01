// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/inventory.md, section "inventory_headless.c"
// ============================================================================
// Checks the item catalog and the inventory: kinds that inherit from each other, stacking, running out
// of room, removing, handles that go stale, moving slots, the event queue, and how the screen's
// buttons map to taps. Inside a macro so mray's build, which compiles every .c file, does not pick
// up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DINVENTORY_HEADLESS_TEST -Isrc -Iraylib/src tests/inventory_headless.c src/inventory/item_types.c src/inventory/inventory.c src/inventory/inventory_ui.c src/weapons/weapons.c -lm -o inventory_headless
// Run:
//   ./inventory_headless
#ifdef INVENTORY_HEADLESS_TEST

#include <stdio.h>
#include <string.h>

#include "inventory/inventory.h"
#include "inventory/inventory_ui.h"

// inventory_ui.c draws with raylib, which is not linked here. Only the hit test is used.
bool CheckCollisionPointRec(Vector2 p, Rectangle r) { return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height; }
void DrawRectangle(int a, int b, int c, int d, Color e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void DrawRectangleRec(Rectangle r, Color c) { (void)r; (void)c; }
void DrawRectangleLines(int a, int b, int c, int d, Color e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void DrawRectangleLinesEx(Rectangle r, float t, Color c) { (void)r; (void)t; (void)c; }
void DrawLineEx(Vector2 a, Vector2 b, float t, Color c) { (void)a; (void)b; (void)t; (void)c; }
void DrawText(const char *t, int x, int y, int s, Color c) { (void)t; (void)x; (void)y; (void)s; (void)c; }
int MeasureText(const char *t, int s) { return (int)strlen(t) * s / 2; }
Color Fade(Color c, float a) { c.a = (unsigned char)(255.0f * a); return c; }
const char *TextFormat(const char *fmt, ...) { return fmt; }

static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static int CountEvents(Inventory *inv, InvEventKind k)
{
    int n = 0;
    InvEvent e;
    while (Inventory_PopEvent(inv, &e)) if (e.kind == k) ++n;
    return n;
}

int main(void)
{
    Inventory inv;

    printf("Kinds inherit from kinds\n");
    Check(Item_IsA(ITEM_PIPE, KIND_MELEE) && Item_IsA(ITEM_PIPE, KIND_WEAPON) && Item_IsA(ITEM_PIPE, KIND_ITEM), "a pipe is melee, a weapon and an item");
    Check(Item_IsA(ITEM_PISTOL, KIND_GUN) && Item_IsA(ITEM_PISTOL, KIND_WEAPON) && !Item_IsA(ITEM_PISTOL, KIND_MELEE), "a pistol is a firearm and a weapon, not melee");
    Check(!Item_IsA(ITEM_MEDKIT, KIND_WEAPON) && Item_IsA(ITEM_MEDKIT, KIND_CONSUMABLE), "a medkit is a consumable");
    Check(!Item_IsA(ITEM_NONE, KIND_ITEM) && !Item_IsA(ITEM_COUNT, KIND_ITEM), "nothing is not an item");
    Check(Item_StackMax(ITEM_PIPE) == 1 && Item_StackMax(ITEM_PISTOL) == 1, "weapons do not stack (said once, on WEAPON)");
    Check(Item_StackMax(ITEM_AMMO) == 99 && Item_StackMax(ITEM_MEDKIT) == 5, "ammo stacks to 99 and medkits to 5");
    Check(Item_Equippable(ITEM_PIPE) && Item_Equippable(ITEM_PISTOL) && !Item_Equippable(ITEM_AMMO) && !Item_Equippable(ITEM_MEDKIT), "weapons are equipped, other things are not");
    Check(Item_Usable(ITEM_MEDKIT) && !Item_Usable(ITEM_PIPE) && !Item_Usable(ITEM_AMMO), "only the medkit is used");
    Check(Item_Droppable(ITEM_PIPE) && Item_Droppable(ITEM_AMMO) && Item_Droppable(ITEM_MEDKIT), "everything can be dropped");
    Check(Item_Weapon(ITEM_PIPE) == WEAPON_PIPE && Item_Weapon(ITEM_PISTOL) == WEAPON_PISTOL, "weapon items name their weapon");
    Check(Item_HealAmount(ITEM_MEDKIT) == 40.0f && Item_HealAmount(ITEM_PIPE) == 0.0f, "a medkit heals 40");
    int named = 1;
    for (int i = ITEM_PIPE; i < ITEM_COUNT; ++i) if (Item_Name((ItemId)i)[0] == 0 || Item_Description((ItemId)i)[0] == 0) named = 0;
    Check(named, "every item has a name and a description");
    Check(Item_StackMax(ITEM_NONE) == 1 && !Item_Usable(ITEM_COUNT), "bad ids get safe answers");

    printf("Adding\n");
    Inventory_Init(&inv);
    Check(Inventory_Add(&inv, ITEM_AMMO, 30) == 30 && Inventory_Count(&inv, ITEM_AMMO) == 30, "30 rounds go in");
    Check(Inventory_UsedSlots(&inv) == 1, "in one slot");
    Check(Inventory_Add(&inv, ITEM_AMMO, 50) == 50 && Inventory_UsedSlots(&inv) == 1, "50 more join the same stack");
    Check(Inventory_Add(&inv, ITEM_AMMO, 40) == 40 && Inventory_UsedSlots(&inv) == 2 && Inventory_Count(&inv, ITEM_AMMO) == 120, "past 99 a second stack starts");
    Check(inv.slots[0].count == 99 && inv.slots[1].count == 21, "the first is full, the second holds the rest");
    Check(Inventory_Add(&inv, ITEM_PIPE, 1) == 1 && Inventory_Add(&inv, ITEM_PIPE, 1) == 1 && Inventory_UsedSlots(&inv) == 4, "each pipe takes a slot of its own");
    Check(Inventory_Add(&inv, ITEM_NONE, 5) == 0 && Inventory_Add(&inv, ITEM_AMMO, 0) == 0 && Inventory_Add(&inv, ITEM_AMMO, -3) == 0, "nothing, zero and negatives add nothing");

    printf("Running out of room\n");
    Inventory_Init(&inv);
    for (int i = 0; i < INV_SLOTS; ++i) Inventory_Add(&inv, ITEM_PIPE, 1);
    Check(Inventory_UsedSlots(&inv) == INV_SLOTS, "twelve pipes fill twelve slots");
    CountEvents(&inv, INV_EV_ADDED);
    Check(!Inventory_CanAdd(&inv, ITEM_PISTOL, 1) && Inventory_Room(&inv, ITEM_AMMO) == 0, "there is no room for anything");
    Check(Inventory_Add(&inv, ITEM_PISTOL, 1) == 0, "so nothing is added");
    InvEvent ev;
    Check(Inventory_PopEvent(&inv, &ev) && ev.kind == INV_EV_FULL && ev.item == ITEM_PISTOL && ev.count == 1, "and the inventory says it is full, and what did not fit");
    Inventory_Init(&inv);
    Inventory_Add(&inv, ITEM_AMMO, 90);
    for (int i = 0; i < INV_SLOTS - 1; ++i) Inventory_Add(&inv, ITEM_PIPE, 1);
    CountEvents(&inv, INV_EV_ADDED);
    Check(Inventory_Room(&inv, ITEM_AMMO) == 9, "a nearly full stack of ammo has room for 9");
    Check(Inventory_Add(&inv, ITEM_AMMO, 20) == 9, "so 9 of 20 go in");
    Check(Inventory_PopEvent(&inv, &ev) && ev.kind == INV_EV_ADDED && ev.count == 9 && Inventory_PopEvent(&inv, &ev) && ev.kind == INV_EV_FULL && ev.count == 11, "and the other 11 are reported as not fitting");

    printf("Removing\n");
    Inventory_Init(&inv);
    Inventory_Add(&inv, ITEM_AMMO, 99); Inventory_Add(&inv, ITEM_AMMO, 40);
    Check(Inventory_Remove(&inv, ITEM_AMMO, 50) == 50 && Inventory_Count(&inv, ITEM_AMMO) == 89, "50 rounds come out");
    Check(inv.slots[1].count == 0 && inv.slots[1].item == ITEM_NONE && inv.slots[0].count == 89, "taken from the last stack first, which empties it");
    Check(Inventory_Remove(&inv, ITEM_AMMO, 500) == 89 && !Inventory_Has(&inv, ITEM_AMMO), "asking for more than there is takes what there is");
    Check(Inventory_Remove(&inv, ITEM_AMMO, 1) == 0 && Inventory_Remove(&inv, ITEM_NONE, 1) == 0, "and nothing more");
    Inventory_Add(&inv, ITEM_MEDKIT, 3);
    Check(Inventory_RemoveAt(&inv, 0, 1) == 1 && inv.slots[0].count == 2, "one medkit is used from a stack of three");
    Check(Inventory_RemoveAt(&inv, 0, 9) == 2 && inv.slots[0].item == ITEM_NONE, "the rest empties the slot");
    Check(Inventory_RemoveAt(&inv, 0, 1) == 0 && Inventory_RemoveAt(&inv, -1, 1) == 0 && Inventory_RemoveAt(&inv, 99, 1) == 0, "an empty or missing slot gives nothing");

    printf("Handles\n");
    Inventory_Init(&inv);
    Inventory_Add(&inv, ITEM_PIPE, 1);
    const ItemHandle h = Inventory_HandleAt(&inv, 0);
    int slot = -1;
    Check(h.gen != 0 && Inventory_Resolve(&inv, h, &slot) && slot == 0, "a handle finds the item it was made for");
    Check(Inventory_HandleAt(&inv, 5).gen == 0, "an empty slot has no handle");
    Inventory_RemoveAt(&inv, 0, 1);
    Check(!Inventory_Resolve(&inv, h, &slot), "after the item is gone the handle is stale");
    Inventory_Add(&inv, ITEM_PISTOL, 1);
    Check(!Inventory_Resolve(&inv, h, &slot), "and stays stale when a different item takes the slot");
    Check(Inventory_Resolve(&inv, Inventory_HandleAt(&inv, 0), &slot), "while the new item has a handle of its own");
    Check(!Inventory_Resolve(&inv, (ItemHandle){ 0, 0 }, NULL) && !Inventory_Resolve(&inv, (ItemHandle){ 99, 1 }, NULL), "an empty or out-of-range handle never resolves");

    printf("Moving slots\n");
    Inventory_Init(&inv);
    Inventory_Add(&inv, ITEM_PIPE, 1); Inventory_Add(&inv, ITEM_PISTOL, 1);
    Check(Inventory_Move(&inv, 0, 5) && inv.slots[5].item == ITEM_PIPE && inv.slots[0].item == ITEM_NONE, "a slot moves into an empty one");
    Check(Inventory_Move(&inv, 5, 1) && inv.slots[1].item == ITEM_PIPE && inv.slots[5].item == ITEM_PISTOL, "onto a full one it swaps");
    Inventory_Init(&inv);
    Inventory_Add(&inv, ITEM_AMMO, 99); Inventory_Add(&inv, ITEM_AMMO, 10);     // slot 0 full, slot 1 holds 10
    Check(!Inventory_Move(&inv, 1, 0) && inv.slots[0].count == 99 && inv.slots[1].count == 10, "a stack moved onto a full stack of the same item changes nothing");
    Inventory_RemoveAt(&inv, 0, 20);                                            // slot 0 now 79
    Check(Inventory_Move(&inv, 1, 0) && inv.slots[0].count == 89 && inv.slots[1].item == ITEM_NONE, "onto one with room it merges, and the emptied slot is free");
    Inventory_Add(&inv, ITEM_AMMO, 50);                                         // 89 -> 99, 39 to slot 1
    Check(Inventory_Count(&inv, ITEM_AMMO) == 139 && inv.slots[0].count == 99 && inv.slots[1].count == 40, "and no rounds are lost");
    Check(!Inventory_Move(&inv, 2, 2) && !Inventory_Move(&inv, -1, 2) && !Inventory_Move(&inv, 2, 40), "moving to itself or out of range does nothing");

    printf("Events\n");
    Inventory_Init(&inv);
    Inventory_Add(&inv, ITEM_AMMO, 5);
    Inventory_Remove(&inv, ITEM_AMMO, 2);
    Inventory_Post(&inv, INV_EV_USED, ITEM_MEDKIT, 1);
    Check(Inventory_PopEvent(&inv, &ev) && ev.kind == INV_EV_ADDED && ev.item == ITEM_AMMO && ev.count == 5, "events come out in the order they happened");
    Check(Inventory_PopEvent(&inv, &ev) && ev.kind == INV_EV_REMOVED && ev.count == 2, "removal is reported");
    Check(Inventory_PopEvent(&inv, &ev) && ev.kind == INV_EV_USED, "and so is a posted event");
    Check(!Inventory_PopEvent(&inv, &ev), "then the queue is empty");
    for (int i = 0; i < 40; ++i) Inventory_Post(&inv, INV_EV_ADDED, ITEM_AMMO, i);
    int n = 0, last = -1;
    while (Inventory_PopEvent(&inv, &ev)) { ++n; last = ev.count; }
    Check(n == INV_EVENT_CAP && last == 39, "a queue that fills up forgets the oldest and keeps the newest");

    printf("The screen's taps\n");
    Inventory_Init(&inv);
    Inventory_Add(&inv, ITEM_MEDKIT, 2); Inventory_Add(&inv, ITEM_PIPE, 1);
    const Rectangle p = InvUI_Panel();
    const Rectangle open = InvUI_OpenButton();
    Check(open.x >= 400 && open.x + open.width <= 800 && open.y + open.height <= 250, "the INV button is on the right half, clear of the controls, inside the screen");
    Check(p.x >= 150 && p.x + p.width <= 700 && p.y + p.height <= 340, "the panel stays clear of the move stick and the bottom controls");
    int slotsInside = 1, slotsApart = 1;
    for (int i = 0; i < INV_SLOTS; ++i) {
        const Rectangle r = InvUI_SlotRect(i);
        if (r.x < p.x || r.y < p.y || r.x + r.width > p.x + p.width || r.y + r.height > p.y + p.height) slotsInside = 0;
        for (int j = 0; j < i; ++j) { const Rectangle q = InvUI_SlotRect(j); if (r.x < q.x + q.width && q.x < r.x + r.width && r.y < q.y + q.height && q.y < r.y + r.height) slotsApart = 0; }
    }
    Check(slotsInside && slotsApart, "all twelve slots lie inside the panel and none overlaps another");
    const Rectangle s0 = InvUI_SlotRect(0);
    InvUiResult r = InvUI_Tap(&inv, (Vector2){ s0.x + 5, s0.y + 5 });
    Check(r.action == INVUI_SELECT && r.slot == 0, "a tap on a slot selects it");
    r = InvUI_Tap(&inv, (Vector2){ 10, 10 });
    Check(r.action == INVUI_CLOSE, "a tap outside the panel closes it");
    r = InvUI_Tap(&inv, (Vector2){ p.x + p.width - 30, p.y + 15 });
    Check(r.action == INVUI_CLOSE, "so does the X");
    r = InvUI_Tap(&inv, (Vector2){ p.x + 330 + 70, p.y + 215 });
    Check(r.action == INVUI_NONE, "the buttons do nothing until a slot is selected");
    inv.selected = 0;       // the medkits
    r = InvUI_Tap(&inv, (Vector2){ p.x + 330 + 70, p.y + 215 });
    Check(r.action == INVUI_USE && r.slot == 0, "with a medkit selected the main button is USE");
    r = InvUI_Tap(&inv, (Vector2){ p.x + 330 + 70, p.y + 255 });
    Check(r.action == INVUI_DROP && r.slot == 0, "and the lower one is DROP");
    inv.selected = 1;       // the pipe
    r = InvUI_Tap(&inv, (Vector2){ p.x + 330 + 70, p.y + 215 });
    Check(r.action == INVUI_EQUIP && r.slot == 1, "with a pipe selected the main button is EQUIP");
    inv.selected = 7;       // empty
    r = InvUI_Tap(&inv, (Vector2){ p.x + 330 + 70, p.y + 215 });
    Check(r.action == INVUI_NONE, "and with an empty slot selected it does nothing");
    r = InvUI_Tap(&inv, (Vector2){ p.x + 400, p.y + 150 });
    Check(r.action == INVUI_NONE, "a tap on bare panel does nothing and does not close it");

    printf("The shotgun, the rifle and their ammo\n");
    Check(Item_IsA(ITEM_SHOTGUN, KIND_GUN) && Item_IsA(ITEM_RIFLE, KIND_GUN) && Item_IsA(ITEM_SHELLS, KIND_AMMO) && Item_IsA(ITEM_RIFLE_AMMO, KIND_AMMO), "the new guns are firearms and the new ammo is ammunition");
    Check(Item_StackMax(ITEM_SHOTGUN) == 1 && Item_StackMax(ITEM_RIFLE) == 1 && Item_StackMax(ITEM_SHELLS) == 99 && Item_StackMax(ITEM_RIFLE_AMMO) == 99, "guns do not stack and each kind of ammo stacks to 99 (said once, on the kinds)");
    Check(Item_Equippable(ITEM_SHOTGUN) && Item_Equippable(ITEM_RIFLE) && !Item_Equippable(ITEM_SHELLS) && !Item_Equippable(ITEM_RIFLE_AMMO), "guns are equipped, ammo is not");
    Check(Item_Weapon(ITEM_SHOTGUN) == WEAPON_SHOTGUN && Item_Weapon(ITEM_RIFLE) == WEAPON_RIFLE, "gun items name their weapon");
    Check(Item_ForWeapon(WEAPON_PIPE) == ITEM_PIPE && Item_ForWeapon(WEAPON_PISTOL) == ITEM_PISTOL && Item_ForWeapon(WEAPON_SHOTGUN) == ITEM_SHOTGUN
          && Item_ForWeapon(WEAPON_RIFLE) == ITEM_RIFLE && Item_ForWeapon(WEAPON_HANDS) == ITEM_NONE, "a weapon finds its item, and the hands have none");
    Check(Item_AmmoType(ITEM_AMMO) == AMMO_PISTOL && Item_AmmoType(ITEM_SHELLS) == AMMO_SHELLS && Item_AmmoType(ITEM_RIFLE_AMMO) == AMMO_RIFLE && Item_AmmoType(ITEM_PISTOL) == AMMO_NONE && Item_AmmoType(ITEM_NONE) == AMMO_NONE, "ammo items name their kind of rounds");
    Check(Item_ForAmmo(AMMO_PISTOL) == ITEM_AMMO && Item_ForAmmo(AMMO_SHELLS) == ITEM_SHELLS && Item_ForAmmo(AMMO_RIFLE) == ITEM_RIFLE_AMMO && Item_ForAmmo(AMMO_NONE) == ITEM_NONE, "a kind of rounds finds its item");
    {
        int roundTrip = 1, namesMatch = 1;
        for (int t = AMMO_PISTOL; t < AMMO_TYPE_COUNT; ++t) {
            if (Item_AmmoType(Item_ForAmmo((AmmoType)t)) != (AmmoType)t) roundTrip = 0;
            if (strcmp(Item_Name(Item_ForAmmo((AmmoType)t)), Ammo_Name((AmmoType)t)) != 0) namesMatch = 0;
        }
        Check(roundTrip, "ammo kind to item and back is the same kind, for every kind");
        Check(namesMatch, "an ammo item is named what the weapons code calls that ammo");
    }
    Check(ITEM_AMMO == 3 && ITEM_MEDKIT == 4, "the ids of the first four items did not change when the new ones were added");
    {
        Inventory sg;
        Inventory_Init(&sg);
        Check(Inventory_Add(&sg, ITEM_SHELLS, 6) == 6 && Inventory_Add(&sg, ITEM_AMMO, 12) == 12 && Inventory_Add(&sg, ITEM_RIFLE_AMMO, 30) == 30, "the three kinds of ammo each take their own slot");
        Check(Inventory_UsedSlots(&sg) == 3 && Inventory_Count(&sg, ITEM_SHELLS) == 6 && Inventory_Count(&sg, ITEM_AMMO) == 12 && Inventory_Count(&sg, ITEM_RIFLE_AMMO) == 30, "and are counted apart");
        Check(Inventory_Remove(&sg, ITEM_SHELLS, 2) == 2 && Inventory_Count(&sg, ITEM_SHELLS) == 4 && Inventory_Count(&sg, ITEM_AMMO) == 12, "spending shells leaves the other kinds alone");
        Check(Inventory_Add(&sg, ITEM_SHOTGUN, 1) == 1 && Inventory_Add(&sg, ITEM_RIFLE, 1) == 1 && Inventory_Has(&sg, ITEM_SHOTGUN) && Inventory_Has(&sg, ITEM_RIFLE), "both guns can be carried");
    }
    Check(Item_IsA(ITEM_FLASHLIGHT, KIND_TOOL) && Item_IsA(ITEM_BATTERY, KIND_CONSUMABLE), "the flashlight is a tool and the battery a consumable");
    Check(Item_StackMax(ITEM_FLASHLIGHT) == 1 && Item_StackMax(ITEM_BATTERY) == 6, "one flashlight to a slot, six batteries");
    Check(Item_Usable(ITEM_FLASHLIGHT) && Item_Usable(ITEM_BATTERY) && !Item_Equippable(ITEM_FLASHLIGHT) && !Item_Equippable(ITEM_BATTERY) && Item_Droppable(ITEM_FLASHLIGHT) && Item_Droppable(ITEM_BATTERY), "both are used (not equipped) and both can be dropped");
    Check(Item_HealAmount(ITEM_BATTERY) == 0.0f && Item_AmmoType(ITEM_BATTERY) == AMMO_NONE && Item_Weapon(ITEM_FLASHLIGHT) == WEAPON_HANDS, "a battery does not heal, spend as ammo or name a weapon");
    Check(ITEM_COUNT == 11, "eleven item ids: none, pipe, pistol, pistol ammo, medkit, shotgun, rifle, shells, rifle ammo, flashlight, battery (a new item needs an icon in inventory_ui.c too)");

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
