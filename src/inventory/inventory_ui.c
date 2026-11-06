// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/inventory.md, section "inventory_ui.h and inventory_ui.c"
// ============================================================================
#include "inventory_ui.h"

#include <stdio.h>
#include <string.h>

#define PANEL_X 160
#define PANEL_Y 24
#define PANEL_W 490
#define PANEL_H 300
#define SLOT    70
#define GAP     6
#define GRID_X  (PANEL_X + 16)
#define GRID_Y  (PANEL_Y + 42)

static const Rectangle OPEN_BTN  = { 698.0f, 8.0f, 92.0f, 36.0f };      // top right corner, mirroring STATS: the layout sketch has the round buttons where it used to be
static const Rectangle CLOSE_BTN = { PANEL_X + PANEL_W - 52.0f, PANEL_Y + 6.0f, 40.0f, 26.0f };
static const Rectangle MAIN_BTN  = { PANEL_X + 330.0f, PANEL_Y + 202.0f, 150.0f, 30.0f };
static const Rectangle DROP_BTN  = { PANEL_X + 330.0f, PANEL_Y + 240.0f, 150.0f, 30.0f };

Rectangle InvUI_OpenButton(void) { return OPEN_BTN; }
Rectangle InvUI_Panel(void) { return (Rectangle){ PANEL_X, PANEL_Y, PANEL_W, PANEL_H }; }

Rectangle InvUI_SlotRect(int slot)
{
    const int c = slot % INV_COLS, r = slot / INV_COLS;
    return (Rectangle){ (float)(GRID_X + c * (SLOT + GAP)), (float)(GRID_Y + r * (SLOT + GAP)), SLOT, SLOT };
}

InvUiResult InvUI_Tap(const Inventory *inv, Vector2 pos)
{
    InvUiResult none = { INVUI_NONE, -1 };
    if (!CheckCollisionPointRec(pos, InvUI_Panel())) return (InvUiResult){ INVUI_CLOSE, -1 };
    if (CheckCollisionPointRec(pos, CLOSE_BTN)) return (InvUiResult){ INVUI_CLOSE, -1 };
    for (int i = 0; i < INV_SLOTS; ++i)
        if (CheckCollisionPointRec(pos, InvUI_SlotRect(i))) return (InvUiResult){ INVUI_SELECT, i };
    const int sel = inv->selected;
    if (sel >= 0 && sel < INV_SLOTS && inv->slots[sel].item != ITEM_NONE) {
        const ItemId it = inv->slots[sel].item;
        if (CheckCollisionPointRec(pos, MAIN_BTN)) {
            if (Item_Usable(it)) return (InvUiResult){ INVUI_USE, sel };
            if (Item_Equippable(it)) return (InvUiResult){ INVUI_EQUIP, sel };
        }
        if (CheckCollisionPointRec(pos, DROP_BTN) && Item_Droppable(it)) return (InvUiResult){ INVUI_DROP, sel };
    }
    return none;
}

// ---- icons: simple shapes, no image files ---------------------------------------------------------
static void DrawIcon(ItemId it, Rectangle r)
{
    const float x = r.x, y = r.y, w = r.width, h = r.height;
    switch (it) {
        case ITEM_PIPE:
            DrawLineEx((Vector2){ x + 14, y + h - 14 }, (Vector2){ x + w - 14, y + 14 }, 8.0f, (Color){ 130, 134, 142, 255 });
            DrawLineEx((Vector2){ x + 14, y + h - 14 }, (Vector2){ x + 26, y + h - 26 }, 9.0f, (Color){ 30, 30, 34, 255 });
            DrawLineEx((Vector2){ x + w - 24, y + 24 }, (Vector2){ x + w - 21, y + 21 }, 9.0f, (Color){ 170, 30, 30, 255 });
            break;
        case ITEM_PISTOL:
            DrawRectangle((int)x + 12, (int)y + 20, 46, 13, (Color){ 74, 78, 90, 255 });
            DrawRectangle((int)x + 56, (int)y + 23, 8, 6, (Color){ 30, 30, 34, 255 });
            DrawRectangle((int)x + 14, (int)y + 31, 13, 24, (Color){ 96, 70, 52, 255 });
            DrawRectangle((int)x + 28, (int)y + 33, 12, 3, (Color){ 74, 78, 90, 255 });
            break;
        case ITEM_SHOTGUN:
            DrawRectangle((int)x + 18, (int)y + 24, 46, 7, (Color){ 74, 78, 90, 255 });      // barrel
            DrawRectangle((int)x + 20, (int)y + 33, 32, 5, (Color){ 52, 54, 62, 255 });      // magazine tube
            DrawRectangle((int)x + 36, (int)y + 32, 12, 8, (Color){ 110, 80, 56, 255 });     // pump
            DrawRectangle((int)x + 8, (int)y + 27, 14, 12, (Color){ 74, 78, 90, 255 });      // receiver
            DrawRectangle((int)x + 3, (int)y + 31, 8, 14, (Color){ 110, 80, 56, 255 });      // stock
            break;
        case ITEM_RIFLE:
            DrawRectangle((int)x + 12, (int)y + 24, 22, 10, (Color){ 52, 54, 62, 255 });     // receiver
            DrawRectangle((int)x + 32, (int)y + 26, 34, 5, (Color){ 74, 78, 90, 255 });      // barrel and handguard
            DrawRectangle((int)x + 62, (int)y + 24, 6, 9, (Color){ 30, 30, 34, 255 });       // muzzle
            DrawRectangle((int)x + 20, (int)y + 33, 8, 20, (Color){ 44, 46, 52, 255 });      // magazine
            DrawRectangle((int)x + 2, (int)y + 25, 12, 8, (Color){ 40, 44, 40, 255 });       // stock
            DrawRectangle((int)x + 20, (int)y + 20, 10, 4, (Color){ 30, 30, 34, 255 });      // sight
            break;
        case ITEM_FLASHLIGHT:
            DrawRectangle((int)x + 14, (int)y + 31, 36, 9, (Color){ 52, 54, 62, 255 });      // body
            DrawRectangle((int)x + 48, (int)y + 27, 10, 17, (Color){ 120, 124, 136, 255 });  // head
            DrawRectangle((int)x + 58, (int)y + 29, 4, 13, (Color){ 255, 233, 160, 255 });   // lens
            DrawRectangle((int)x + 24, (int)y + 33, 8, 5, (Color){ 200, 70, 50, 255 });      // switch
            break;
        case ITEM_BATTERY:
            DrawRectangle((int)x + 22, (int)y + 20, 26, 40, (Color){ 40, 120, 70, 255 });
            DrawRectangle((int)x + 22, (int)y + 20, 26, 10, (Color){ 214, 178, 60, 255 });
            DrawRectangle((int)x + 31, (int)y + 14, 8, 6, (Color){ 214, 178, 60, 255 });
            DrawRectangleLines((int)x + 22, (int)y + 20, 26, 40, (Color){ 14, 40, 24, 255 });
            break;
        case ITEM_SHELLS:
            DrawRectangle((int)x + 14, (int)y + 24, 42, 28, (Color){ 150, 38, 34, 255 });
            DrawRectangle((int)x + 14, (int)y + 40, 42, 12, (Color){ 214, 178, 60, 255 });
            DrawRectangleLines((int)x + 14, (int)y + 24, 42, 28, (Color){ 40, 14, 12, 255 });
            break;
        case ITEM_RIFLE_AMMO:
            DrawRectangle((int)x + 14, (int)y + 24, 42, 28, (Color){ 70, 76, 64, 255 });
            DrawRectangle((int)x + 14, (int)y + 24, 42, 8, (Color){ 214, 178, 60, 255 });
            DrawRectangleLines((int)x + 14, (int)y + 24, 42, 28, (Color){ 24, 28, 22, 255 });
            break;
        case ITEM_AMMO:
            DrawRectangle((int)x + 14, (int)y + 24, 42, 28, (Color){ 84, 96, 60, 255 });
            DrawRectangle((int)x + 14, (int)y + 34, 42, 8, (Color){ 214, 178, 60, 255 });
            DrawRectangleLines((int)x + 14, (int)y + 24, 42, 28, (Color){ 30, 36, 24, 255 });
            break;
        case ITEM_MEDKIT:
            DrawRectangle((int)x + 14, (int)y + 20, 42, 32, (Color){ 226, 226, 220, 255 });
            DrawRectangle((int)x + 31, (int)y + 25, 8, 22, (Color){ 190, 28, 28, 255 });
            DrawRectangle((int)x + 24, (int)y + 32, 22, 8, (Color){ 190, 28, 28, 255 });
            DrawRectangleLines((int)x + 14, (int)y + 20, 42, 32, (Color){ 90, 90, 90, 255 });
            break;
        default: break;
    }
}

// Writes text word by word inside a width, returns the next line's y.
static int Wrapped(const char *text, int x, int y, int width, int size, Color col)
{
    char line[96] = "";
    char word[48];
    const char *p = text;
    while (*p) {
        int n = 0;
        while (*p && *p != ' ' && n < 47) word[n++] = *p++;
        word[n] = 0;
        while (*p == ' ') ++p;
        char trial[96];
        snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && MeasureText(trial, size) > width) {
            DrawText(line, x, y, size, col);
            y += size + 3;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }
    if (line[0]) { DrawText(line, x, y, size, col); y += size + 3; }
    return y;
}

static void Button(Rectangle r, const char *label, Color fill)
{
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 1.0f, WHITE);
    DrawText(label, (int)(r.x + (r.width - (float)MeasureText(label, 16)) * 0.5f), (int)r.y + 7, 16, WHITE);
}

void InvUI_DrawOpenButton(const Inventory *inv, bool pressedDown)
{
    DrawRectangleRec(OPEN_BTN, Fade(pressedDown ? BLUE : DARKGRAY, 0.7f));
    DrawRectangleLinesEx(OPEN_BTN, 1.0f, WHITE);
    DrawText(TextFormat("INV %d/%d", Inventory_UsedSlots(inv), INV_SLOTS), (int)OPEN_BTN.x + 10, (int)OPEN_BTN.y + 10, 16, WHITE);
}

void InvUI_Draw(const Inventory *inv, const InvLoadout *lo)
{
    const Rectangle panel = InvUI_Panel();
    DrawRectangle(0, 0, 800, 450, Fade(BLACK, 0.45f));
    DrawRectangleRec(panel, (Color){ 14, 14, 18, 235 });
    DrawRectangleLinesEx(panel, 2.0f, (Color){ 170, 140, 30, 255 });
    DrawText(TextFormat("INVENTORY   %d / %d", Inventory_UsedSlots(inv), INV_SLOTS), PANEL_X + 16, PANEL_Y + 10, 18, YELLOW);
    Button(CLOSE_BTN, "X", (Color){ 110, 30, 30, 230 });
    DrawText(TextFormat("WEAPONS EQUIPPED %d / %d", lo->equippedCount, lo->equippedMax), PANEL_X + 215, PANEL_Y + 13, 14, (Color){ 90, 200, 230, 255 });

    for (int i = 0; i < INV_SLOTS; ++i) {
        const Rectangle r = InvUI_SlotRect(i);
        const InvSlot *s = &inv->slots[i];
        DrawRectangleRec(r, (Color){ 34, 34, 42, 255 });
        const bool sel = i == inv->selected;
        DrawRectangleLinesEx(r, sel ? 3.0f : 1.0f, sel ? YELLOW : (Color){ 90, 90, 100, 255 });
        if (s->item == ITEM_NONE) continue;
        DrawIcon(s->item, r);
        if (s->count > 1) {
            const char *t = TextFormat("x%d", s->count);
            DrawText(t, (int)(r.x + r.width) - MeasureText(t, 14) - 5, (int)(r.y + r.height) - 18, 14, WHITE);
        }
        if (Item_Equippable(s->item)) {
            const WeaponId w = Item_Weapon(s->item);
            if (w == lo->held) DrawText("HELD", (int)r.x + 5, (int)r.y + 4, 12, GREEN);
            else if ((lo->equippedMask >> w) & 1u) DrawText("EQUIPPED", (int)r.x + 5, (int)r.y + 4, 12, (Color){ 90, 200, 230, 255 });
        }
    }

    const int dx = PANEL_X + 330;
    if (inv->selected >= 0 && inv->selected < INV_SLOTS && inv->slots[inv->selected].item != ITEM_NONE) {
        const InvSlot *s = &inv->slots[inv->selected];
        DrawText(Item_Name(s->item), dx, PANEL_Y + 46, 22, WHITE);
        DrawText(ItemKind_Name(Item_Kind(s->item)), dx, PANEL_Y + 74, 14, (Color){ 170, 170, 180, 255 });
        if (s->count > 1) DrawText(TextFormat("Carrying %d", s->count), dx, PANEL_Y + 92, 14, (Color){ 170, 170, 180, 255 });
        Wrapped(Item_Description(s->item), dx, PANEL_Y + 116, 150, 14, LIGHTGRAY);
        if (Item_Usable(s->item)) Button(MAIN_BTN, "USE", (Color){ 24, 110, 50, 235 });
        else if (Item_Equippable(s->item)) {
            // Three states: in the bag (equip it, or swap it for the weapon in hand when both places are taken), equipped (take it
            // in hand), in hand (put it away: it stays in the bag).
            const WeaponId w = Item_Weapon(s->item);
            const bool equipped = (lo->equippedMask >> w) & 1u, inHand = w == lo->held, full = lo->equippedCount >= lo->equippedMax;
            if (equipped) Button(MAIN_BTN, inHand ? "UNEQUIP" : "HOLD", inHand ? (Color){ 90, 70, 30, 235 } : (Color){ 24, 90, 130, 235 });
            else if (!full) Button(MAIN_BTN, "EQUIP", (Color){ 24, 90, 130, 235 });
            else if (lo->held != WEAPON_HANDS) Button(MAIN_BTN, "SWAP IN", (Color){ 24, 90, 130, 235 });
            else Button(MAIN_BTN, "HOLD ONE FIRST", (Color){ 60, 60, 66, 235 });
        }
        if (Item_Droppable(s->item)) Button(DROP_BTN, "DROP", (Color){ 110, 60, 20, 235 });
    } else {
        DrawText("Tap an item.", dx, PANEL_Y + 50, 16, GRAY);
        Wrapped("The game keeps running while this is open.", dx, PANEL_Y + 80, 150, 14, (Color){ 130, 130, 140, 255 });
    }
}
