// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/inventory.md, section "inventory_ui.h and inventory_ui.c"
// ============================================================================
#ifndef INVENTORY_UI_H
#define INVENTORY_UI_H

#include <stdbool.h>

#include "raylib.h"
#include "inventory.h"

// The inventory screen, for a touch screen 800 by 450: a 4 by 3 grid of slots on the left, the selected
// item's name, description and buttons on the right. It does not pause the game. It never changes the
// inventory itself: a tap comes back as an action and main.c carries it out, because using a medkit
// touches the player and dropping a weapon touches the arsenal and the floor.

typedef enum { INVUI_NONE = 0, INVUI_CLOSE, INVUI_SELECT, INVUI_USE, INVUI_EQUIP, INVUI_DROP } InvUiAction;

typedef struct { InvUiAction action; int slot; } InvUiResult;

// The small INV button shown while the screen is closed.
Rectangle InvUI_OpenButton(void);

// The panel's rectangle: everything inside it belongs to the screen, a tap outside closes it.
Rectangle InvUI_Panel(void);

// What a tap at pos means while the screen is open. A tap on the selected slot's buttons returns USE, EQUIP or DROP
// for that slot; a tap on a slot returns SELECT; the X or a tap outside the panel returns CLOSE.
InvUiResult InvUI_Tap(const Inventory *inv, Vector2 pos);

// Draws the screen. held is the weapon in hand (its slot is marked); the open button is drawn by InvUI_DrawOpenButton.
void InvUI_Draw(const Inventory *inv, WeaponId held);
void InvUI_DrawOpenButton(const Inventory *inv, bool pressedDown);

// Rectangle of one slot, for tests.
Rectangle InvUI_SlotRect(int slot);

#endif
