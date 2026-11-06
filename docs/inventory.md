# Inventory: items, slots, the screen and how it plugs into the game

## Overview

The player carries items in a grid of 12 slots (4 by 3). An item is a weapon (pipe, pistol, shotgun, rifle), ammo (pistol ammo, shells, rifle ammo), or a medkit. Weapons do not stack; each kind of ammo stacks to 99 per slot; medkits to 5. Picking something up puts it in the inventory and, for a weapon, takes it in hand. A gun spends the ammo of its own kind from the inventory. The INV button (top right, under the frame graph) opens the screen: tap a slot, then USE (medkit), EQUIP (weapon) or DROP. The world keeps running while it is open (the game is meant to become multiplayer), the move stick still works, and look, jump and attack are off until it is closed.

The shape follows the patterns in the player's Unity package `com.midmanstudio.patterns` (MidManStudio_Unity), kept to the three that earn their place here:

- **Type Object** (`item_types.c`): items are rows of data that name a kind, and the kinds form a tree with inherited defaults.
- **Event Queue** (`inventory.c`): the inventory records what happened (added, did not fit, used, equipped) and the game drains the queue once a frame to show messages, and later to play sounds, without the inventory knowing about the screen.
- **Data Locality / generational handles** (`ItemHandle`): a handle names what is in a slot right now and stops resolving the moment that item leaves, so anything holding one (a hotbar, a quest, a trade offer) can never reach a different item that took the slot.

Not used, on purpose: Command (no undo needed), Broker Chain (no stat modifiers yet), Spatial Partition (not needed at this scale). The Unity repository has no inventory code to copy; its weapon and projectile packages were used for the shape of "definition table + runtime state".

## Modules (all in `src/inventory/`)

### `item_types.h` and `item_types.c`

`ItemId` (NONE, PIPE, PISTOL, AMMO, MEDKIT, SHOTGUN, RIFLE, SHELLS, RIFLE_AMMO; the last four were added after the first five, whose numbers did not change; `ITEM_AMMO` is pistol ammo and is shown as PISTOL AMMO) and `ItemKind`: ITEM > WEAPON > MELEE and FIREARM; ITEM > CONSUMABLE; ITEM > AMMUNITION. Each kind row has a stack size, usable, equippable and droppable, where -1 means "ask the parent". The ITEM root says: stack 1, not usable, not equippable, droppable. WEAPON says equippable. CONSUMABLE says usable, stack 5. AMMUNITION says stack 99. An item row names its kind and may override, and also says which `WeaponId` an equippable item is and how much a consumable heals. Ammunition rows also say which `AmmoType` they hold: `Item_AmmoType(item)`, `Item_ForAmmo(type)` and `Item_ForWeapon(weapon)` let the game go from a gun to its ammo and from a pickup to an item without a table of its own. So adding a new item is one row in `k_items` (and an icon in `inventory_ui.c`), and adding a behaviour that many items share is one new kind. `Item_IsA(id, kind)` walks the tree.

### `inventory.h` and `inventory.c`

Plain data and rules, no raylib. `Inventory_Add` tops up existing stacks first and then fills empty slots first to last, returns how many went in, and posts ADDED for those and FULL for what did not fit. `Inventory_Remove` takes from the last stacks first. `Inventory_RemoveAt` takes from one slot. `Inventory_Move` moves, swaps or merges slots. `Inventory_Room` and `Inventory_CanAdd` answer without changing anything, which `main.c` uses to refuse a pickup that does not fit without picking it up. The event queue is a ring of 16; a full ring forgets the oldest event.

### `inventory_ui.h` and `inventory_ui.c`

The screen, laid out for 800 by 450: the panel is x 160 to 650 and y 24 to 324, which keeps the move stick (left) and the jump and attack buttons (bottom right) clear; the INV button is at (698, 178), clear of the frame graph above it. The slots are drawn with simple shapes (no image files), "HELD" marks the weapon in hand, and the details side shows name, kind, description and buttons. It never changes the inventory: `InvUI_Tap(inventory, position)` returns an action (CLOSE, SELECT, USE, EQUIP, DROP and the slot) and `main.c` carries it out, because using a medkit touches the player and dropping a weapon touches the arsenal and the floor. A tap outside the panel closes it, as does the X.

### `main.c` wiring

- **Ownership.** The inventory is the truth for what the player has. Before each `Arsenal_Update`, `arsenal.ammo` is set from the inventory's count of the ammo the weapon in hand uses (nothing for the hands and the pipe), and afterwards the rounds spent are taken off that stack. The arsenal keeps the combat state (which weapon, cooldowns, recoil) and `Arsenal_Give`, `Arsenal_Equip` and `Arsenal_Remove` keep its owned list in step.
- **Pickups.** Ammo boxes are collected by walking over them, but only if the whole box fits (otherwise "SHELLS FULL" or the like, at most every 2 s; a full stack of one kind does not stop the other kinds being picked up). A weapon or medkit uses the USE button; if it does not fit, "INVENTORY FULL" and it stays where it is. A gun comes with a clip of its own ammo (pistol 12, shotgun 6, rifle 30, from `weapons.mdix`); whatever does not fit is left as a box next to the player. Floors spawn pickups as `weapons.md` describes; a weapon the player already holds turns into its ammo.
- **Use.** A medkit heals 40 (`HealPlayer`, capped at the maximum, refused at full health with a message), flashes the screen green, and uses one up.
- **Equip.** The EQUIP button takes an owned weapon in hand (the SWAP button still cycles).
- **Drop.** DROP puts the whole slot on the floor 1.4 m ahead (or nearer if that tile is a wall, down to the player's own feet), removes it from the inventory and, for a weapon, from the arsenal; dropping the weapon in hand puts the hands back. A dropped ammo box cannot be picked up by walking for 2 s, so it is not taken straight back. Refused on the stairs (pickups lie on the ground floor) and when all 24 pickup slots are in use.
- **Messages.** Events from the inventory become the one-line messages above the health bar: "+8 AMMO", "PICKED UP: PIPE", "INVENTORY FULL", "USED MEDKIT", "IN HAND: PIPE".
- **Input.** Touches: a new finger is a "tap" (checked once per frame by `NewTap`, which remembers which finger ids were down last frame; a mouse click counts when there are no touches). While the screen is open, new fingers on the right half are taps and nothing else; the move stick on the left still works. Keyboard: I or Tab toggles the screen.

### `inventory_headless.c`

Checks the kind tree and inherited values, the new guns and ammo (kinds, stacks, the lookups between a weapon, its item and its ammo, that an ammo item is named what the weapons code calls it, that the first five ids did not move), stacking and overflow into a second stack, running out of room and what the FULL event reports, partial fits, removal order, handles going stale (also when another item takes the slot), moving, swapping and merging slots, the event order and the full queue, and the screen's hit testing: the panel stays clear of the controls, all 12 slots are inside it and do not overlap, and each button maps to the right action for the selected item (including none for an empty slot). `inventory_ui.c` is linked with stand-in draw calls. Build line in the file header. Whole file sits inside `INVENTORY_HEADLESS_TEST`.

## Fixes and Problems

- Pickups used to be only a kind. They now carry a count and a "just dropped" timer, so a dropped stack comes back with the same number of rounds. `Pickups_CollectAmmoNear` now returns rounds, not boxes (nothing in `main.c` uses it any more; the tests do).
- The pistol's rounds (now every gun's) used to live in the arsenal, capped at 99 in total. They now live in the inventory, where a stack is capped at 99 but several stacks can exist. `AMMO_MAX` is still used by `Arsenal_AddAmmo`, which `main.c` no longer calls.

## Tuning

Slot count: `INV_COLS`, `INV_ROWS` (the screen has room for 4 by 3; more needs a new layout). Stack sizes and heal amounts: the tables in `item_types.c`. Screen layout: the constants at the top of `inventory_ui.c`.

## Known limits

- Dropped items and floor pickups vanish when the floor is rebuilt, which happens each time it is entered.
- No weight, no equipment slots, no hotbar, no item use on the world (keys, doors), no reload. Medkits and the pipe are the only items with effects.
- The screen has no drag and drop; `Inventory_Move` exists and is tested but has no button.
- A finger that closes the screen by tapping outside it, over a game button, will press that button until lifted.
- Only checked headless on x86; how the screen looks and feels on the phone is not verified.

## The flashlight and the battery

`ITEM_FLASHLIGHT` is the first of a new kind, `KIND_TOOL`: one to a slot, droppable, with a USE button (which switches the light on and off) and no EQUIP; it is not a weapon, so it does not take the hand. `ITEM_BATTERY` is a consumable that stacks to 6; its USE loads it into the flashlight (`flashlight.md`). A flashlight on the floor carries its charge in the pickup's count, and a dropped one keeps what it had. `ITEM_COUNT` is 11.

### Two equipped weapons: the weapon button
The bag can hold more weapons than can be equipped; only two may be equipped at once (`weapons.md`). The weapon's main button has four states, drawn from an `InvLoadout` (held weapon, equipped mask, count, maximum) that main.c builds: **EQUIP** (in the bag, a place is free: equipped and in hand), **SWAP IN** (in the bag, both places taken, a weapon in hand: it replaces the one in hand, which stays in the bag), **HOLD ONE FIRST** (both places taken and the hands are in hand: greyed; hold the weapon to replace, then tap), **HOLD** (equipped, not in hand: take it in hand) and **UNEQUIP** (in hand: put it away, it stays in the bag). The grid marks HELD in green and EQUIPPED in blue, and the header shows WEAPONS EQUIPPED n / 2. A weapon picked up with both places taken goes to the bag with a toast. Dropping an equipped weapon unequips it. The fists are always free and never count. `InvUI_Tap` still answers EQUIP for any weapon: main.c's `EquipSlot` decides what that means. Not seen on a screen; the UI states are drawn, not tested (the tests cover the arsenal rules).
