// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/inventory.md, section "inventory.h and inventory.c"
// ============================================================================
#include "inventory.h"

void Inventory_Init(Inventory *inv)
{
    for (int i = 0; i < INV_SLOTS; ++i) {
        inv->slots[i].item = ITEM_NONE;
        inv->slots[i].count = 0;
        inv->slots[i].gen = 1;
    }
    inv->selected = -1;
    inv->qHead = inv->qCount = 0;
}

void Inventory_Post(Inventory *inv, InvEventKind kind, ItemId item, int count)
{
    if (inv->qCount == INV_EVENT_CAP) { inv->qHead = (inv->qHead + 1) % INV_EVENT_CAP; --inv->qCount; }
    inv->queue[(inv->qHead + inv->qCount) % INV_EVENT_CAP] = (InvEvent){ kind, item, count };
    ++inv->qCount;
}

bool Inventory_PopEvent(Inventory *inv, InvEvent *out)
{
    if (inv->qCount == 0) return false;
    if (out) *out = inv->queue[inv->qHead];
    inv->qHead = (inv->qHead + 1) % INV_EVENT_CAP;
    --inv->qCount;
    return true;
}

int Inventory_Room(const Inventory *inv, ItemId item)
{
    if (item <= ITEM_NONE || item >= ITEM_COUNT) return 0;
    const int stack = Item_StackMax(item);
    int room = 0;
    for (int i = 0; i < INV_SLOTS; ++i) {
        if (inv->slots[i].item == ITEM_NONE) room += stack;
        else if (inv->slots[i].item == item) room += stack - inv->slots[i].count;
    }
    return room;
}

bool Inventory_CanAdd(const Inventory *inv, ItemId item, int count) { return count > 0 && Inventory_Room(inv, item) >= count; }

int Inventory_Add(Inventory *inv, ItemId item, int count)
{
    if (item <= ITEM_NONE || item >= ITEM_COUNT || count <= 0) return 0;
    const int stack = Item_StackMax(item);
    int left = count;
    // Top up stacks that exist.
    for (int i = 0; i < INV_SLOTS && left > 0; ++i) {
        InvSlot *s = &inv->slots[i];
        if (s->item != item || s->count >= stack) continue;
        const int put = (stack - s->count < left) ? stack - s->count : left;
        s->count += put;
        left -= put;
    }
    // Then empty slots, first to last.
    for (int i = 0; i < INV_SLOTS && left > 0; ++i) {
        InvSlot *s = &inv->slots[i];
        if (s->item != ITEM_NONE) continue;
        const int put = stack < left ? stack : left;
        s->item = item;
        s->count = put;
        left -= put;
    }
    const int added = count - left;
    if (added > 0) Inventory_Post(inv, INV_EV_ADDED, item, added);
    if (left > 0) Inventory_Post(inv, INV_EV_FULL, item, left);
    return added;
}

int Inventory_Count(const Inventory *inv, ItemId item)
{
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i) if (inv->slots[i].item == item && item != ITEM_NONE) n += inv->slots[i].count;
    return n;
}

bool Inventory_Has(const Inventory *inv, ItemId item) { return Inventory_Count(inv, item) > 0; }

static void Empty(InvSlot *s)
{
    s->item = ITEM_NONE;
    s->count = 0;
    ++s->gen;
    if (s->gen == 0) s->gen = 1;
}

int Inventory_RemoveAt(Inventory *inv, int slot, int count)
{
    if (slot < 0 || slot >= INV_SLOTS || count <= 0) return 0;
    InvSlot *s = &inv->slots[slot];
    if (s->item == ITEM_NONE) return 0;
    const ItemId item = s->item;
    const int take = count < s->count ? count : s->count;
    s->count -= take;
    if (s->count == 0) Empty(s);
    Inventory_Post(inv, INV_EV_REMOVED, item, take);
    return take;
}

int Inventory_Remove(Inventory *inv, ItemId item, int count)
{
    if (item <= ITEM_NONE || count <= 0) return 0;
    int left = count;
    for (int i = INV_SLOTS - 1; i >= 0 && left > 0; --i) {
        InvSlot *s = &inv->slots[i];
        if (s->item != item) continue;
        const int take = left < s->count ? left : s->count;
        s->count -= take;
        left -= take;
        if (s->count == 0) Empty(s);
    }
    const int taken = count - left;
    if (taken > 0) Inventory_Post(inv, INV_EV_REMOVED, item, taken);
    return taken;
}

ItemHandle Inventory_HandleAt(const Inventory *inv, int slot)
{
    if (slot < 0 || slot >= INV_SLOTS || inv->slots[slot].item == ITEM_NONE) return (ItemHandle){ 0, 0 };
    return (ItemHandle){ (uint16_t)slot, inv->slots[slot].gen };
}

bool Inventory_Resolve(const Inventory *inv, ItemHandle h, int *slot)
{
    if (h.gen == 0 || h.slot >= INV_SLOTS) return false;
    const InvSlot *s = &inv->slots[h.slot];
    if (s->gen != h.gen || s->item == ITEM_NONE) return false;
    if (slot) *slot = h.slot;
    return true;
}

bool Inventory_Move(Inventory *inv, int a, int b)
{
    if (a < 0 || a >= INV_SLOTS || b < 0 || b >= INV_SLOTS || a == b) return false;
    InvSlot *sa = &inv->slots[a], *sb = &inv->slots[b];
    if (sa->item == ITEM_NONE) return false;
    if (sb->item == sa->item && Item_StackMax(sa->item) > 1) {
        const int room = Item_StackMax(sa->item) - sb->count;
        const int put = room < sa->count ? room : sa->count;
        sb->count += put;
        sa->count -= put;
        if (sa->count == 0) Empty(sa);
        return put > 0;
    }
    const InvSlot tmp = *sa;
    sa->item = sb->item; sa->count = sb->count;
    sb->item = tmp.item; sb->count = tmp.count;
    ++sa->gen; ++sb->gen;
    if (sa->gen == 0) sa->gen = 1;
    if (sb->gen == 0) sb->gen = 1;
    return true;
}

int Inventory_UsedSlots(const Inventory *inv)
{
    int n = 0;
    for (int i = 0; i < INV_SLOTS; ++i) n += inv->slots[i].item != ITEM_NONE ? 1 : 0;
    return n;
}
