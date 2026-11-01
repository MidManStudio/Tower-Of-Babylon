// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/inventory.md, section "inventory.h and inventory.c"
// ============================================================================
#ifndef INVENTORY_H
#define INVENTORY_H

#include <stdbool.h>
#include <stdint.h>

#include "item_types.h"

// A grid of slots. Each slot holds one kind of item and a count up to that item's stack size.
// The inventory knows nothing about the arsenal, the player or the screen: it stores items and tells
// the rest of the game what happened through an event queue (the Event Queue pattern), which the
// game drains once a frame to show toasts and, later, play sounds.

#define INV_COLS  4
#define INV_ROWS  3
#define INV_SLOTS (INV_COLS * INV_ROWS)

typedef struct {
    ItemId   item;     // ITEM_NONE when empty
    int      count;
    uint16_t gen;      // goes up every time the slot is emptied, so an old handle never reaches a new item
} InvSlot;

// A stable name for what is in a slot right now (the generational handle from Data Locality).
// gen 0 is never valid.
typedef struct { uint16_t slot; uint16_t gen; } ItemHandle;

typedef enum {
    INV_EV_ADDED = 0,    // item and count: what went in
    INV_EV_REMOVED,      // used up or dropped
    INV_EV_FULL,         // item and count: what did not fit
    INV_EV_USED,         // a consumable was used
    INV_EV_EQUIPPED      // a weapon was taken in hand
} InvEventKind;

typedef struct { InvEventKind kind; ItemId item; int count; } InvEvent;

#define INV_EVENT_CAP 16

typedef struct {
    InvSlot  slots[INV_SLOTS];
    int      selected;                 // the highlighted slot, or -1
    InvEvent queue[INV_EVENT_CAP];     // a ring; when it is full the oldest event is dropped
    int      qHead, qCount;
} Inventory;

void Inventory_Init(Inventory *inv);

// How many of `count` would fit right now.
int  Inventory_Room(const Inventory *inv, ItemId item);
bool Inventory_CanAdd(const Inventory *inv, ItemId item, int count);

// Adds up to count: tops up existing stacks of the item first, then fills empty slots. Returns how many
// went in. Posts INV_EV_ADDED for what went in and INV_EV_FULL for what did not.
int  Inventory_Add(Inventory *inv, ItemId item, int count);

int  Inventory_Count(const Inventory *inv, ItemId item);   // total across all stacks
bool Inventory_Has(const Inventory *inv, ItemId item);

// Takes count away (from the last stacks first). Returns how many were taken. Posts INV_EV_REMOVED.
int  Inventory_Remove(Inventory *inv, ItemId item, int count);
int  Inventory_RemoveAt(Inventory *inv, int slot, int count);

ItemHandle Inventory_HandleAt(const Inventory *inv, int slot);
bool       Inventory_Resolve(const Inventory *inv, ItemHandle h, int *slot);

// Moves what is in slot a to slot b, or swaps the two, or merges them when they hold the same stackable item.
bool Inventory_Move(Inventory *inv, int a, int b);

int  Inventory_UsedSlots(const Inventory *inv);

// Posts an event from outside (the game uses it for USED and EQUIPPED).
void Inventory_Post(Inventory *inv, InvEventKind kind, ItemId item, int count);
// Takes the oldest event off the queue. False when empty.
bool Inventory_PopEvent(Inventory *inv, InvEvent *out);

#endif
