// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "lobby_ui.h and lobby_ui.c"
// ============================================================================
#include "lobby_ui.h"

#include <string.h>

#include "raylib.h"

#include "../characters/character_select.h"     // CharSelect_Wrap: the same word wrap
#include "../characters/characters.h"

static const Color COL_BG      = { 10, 8, 12, 255 };
static const Color COL_ACCENT  = { 230, 170, 60, 255 };
static const Color COL_DIM     = { 130, 130, 140, 255 };
static const Color COL_TEXT    = { 220, 220, 225, 255 };
static const Color COL_FILL    = { 40, 36, 48, 255 };
static const Color COL_EDGE    = { 150, 150, 160, 255 };
static const Color COL_GREEN   = { 20, 90, 40, 255 };
static const Color COL_OFF     = { 22, 20, 26, 255 };
static const Color COL_OFF_EDGE = { 55, 55, 62, 255 };
static const Color COL_OFF_TEXT = { 100, 100, 108, 255 };
static const Color COL_ROW     = { 24, 22, 28, 255 };
static const Color COL_ROW_ON  = { 70, 24, 24, 255 };
static const Color COL_BAD     = { 200, 90, 80, 255 };

static Rectangle R(UiRect r) { return (Rectangle){ r.x, r.y, r.w, r.h }; }

static float MeasureRaylib(const char *text, int len, int fontSize, void *user)
{
    (void)user;
    char buf[CHARACTER_OVERVIEW_LEN + 8];
    if (len > (int)sizeof buf - 1) len = (int)sizeof buf - 1;
    memcpy(buf, text, (size_t)len);
    buf[len] = 0;
    return (float)MeasureText(buf, fontSize);
}

// Text wrapped into at most maxLines lines from (x, y); a text that does not fit ends in "...". Returns the height used.
static int DrawWrapped(const char *text, int x, int y, float width, int maxLines, int fontSize, int lineH, Color color)
{
    CharSelectLine lines[12];
    bool cut = false;
    const int n = CharSelect_Wrap(text, width, fontSize, MeasureRaylib, NULL, lines, maxLines < 12 ? maxLines : 12, &cut);
    for (int i = 0; i < n; ++i) {
        char buf[CHARACTER_OVERVIEW_LEN + 8];
        int len = lines[i].len;
        if (len > (int)sizeof buf - 8) len = (int)sizeof buf - 8;
        memcpy(buf, text + lines[i].start, (size_t)len);
        buf[len] = 0;
        if (cut && i == n - 1) { if (len > 3) len -= 3; memcpy(buf + len, "...", 4); }
        DrawText(buf, x, y + i * lineH, fontSize, color);
    }
    return n * lineH;
}

static void DrawCentered(const char *text, UiRect r, int y, int size, Color c)
{
    DrawText(text, (int)(r.x + (r.w - (float)MeasureText(text, size)) / 2.0f), y, size, c);
}

// A button: a label, an optional second line under it, and a look for on, off and "the keyboard is here".
static void DrawButton(UiRect r, const char *label, int size, const char *sub, Color onFill, bool enabled, bool focused)
{
    DrawRectangleRec(R(r), enabled ? onFill : COL_OFF);
    DrawRectangleLines((int)r.x, (int)r.y, (int)r.w, (int)r.h, enabled ? COL_EDGE : COL_OFF_EDGE);
    if (focused) { DrawRectangleLinesEx(R(r), 3.0f, YELLOW); }
    const Color text = enabled ? WHITE : COL_OFF_TEXT;
    if (sub) {
        DrawCentered(label, r, (int)r.y + (int)(r.h * 0.5f) - size + 2, size, text);
        DrawCentered(sub, r, (int)r.y + (int)(r.h * 0.5f) + 6, 12, enabled ? COL_TEXT : COL_OFF_TEXT);
    } else {
        DrawCentered(label, r, (int)r.y + (int)((r.h - (float)size) / 2.0f), size, text);
    }
}

static void DrawBack(bool focused)
{
    const UiRect b = Lobby_Back();
    DrawRectangleRec(R(b), COL_FILL);
    DrawRectangleLines((int)b.x, (int)b.y, (int)b.w, (int)b.h, COL_EDGE);
    if (focused) DrawRectangleLinesEx(R(b), 3.0f, YELLOW);
    DrawCentered("BACK", b, (int)b.y + 7, 16, WHITE);
}

static void DrawHome(const Lobby *l, bool showKeys)
{
    DrawText("TOWER OF BABYLON", 10, 8, 28, YELLOW);
    DrawText("HORROR GAME SHOW", 12, 40, 14, COL_ACCENT);

    // Who the player is.
    const UiRect p = Lobby_CharacterPanel();
    DrawRectangleRec(R(p), Fade(BLACK, 0.7f));
    DrawRectangleLines((int)p.x, (int)p.y, (int)p.w, (int)p.h, COL_OFF_EDGE);
    DrawText("YOUR CONDEMNED", (int)p.x + 14, (int)p.y + 10, 12, COL_ACCENT);
    const CharacterDef *c = Characters_Selected() >= 0 ? Characters_Get(Characters_Selected()) : NULL;
    if (c) {
        DrawText(c->call, (int)p.x + 14, (int)p.y + 30, 32, WHITE);
        DrawText(c->name, (int)p.x + 14, (int)p.y + 70, 16, COL_TEXT);
        DrawText(c->role, (int)p.x + 14, (int)p.y + 92, 14, COL_ACCENT);
        DrawWrapped(c->overview, (int)p.x + 14, (int)p.y + 120, p.w - 28.0f, 6, 12, 14, COL_TEXT);
    } else if (l->rosterCount > 0) {
        DrawText("NOBODY CHOSEN YET", (int)p.x + 14, (int)p.y + 40, 22, YELLOW);
        DrawText("Choose who climbs the tower.", (int)p.x + 14, (int)p.y + 76, 14, COL_DIM);
    } else {
        DrawText("NO CHARACTERS", (int)p.x + 14, (int)p.y + 40, 22, COL_BAD);
    }
    const char *chooseLabel = l->rosterCount < 1 ? "NO CHARACTERS" : (c ? "CHANGE CHARACTER" : "CHOOSE CHARACTER");
    DrawButton(Lobby_Button(LB_CHARACTER), chooseLabel, 18, NULL, COL_FILL, Lobby_Enabled(l, LB_CHARACTER), showKeys && l->focus == LB_CHARACTER);

    // What the player can do.
    DrawButton(Lobby_Button(LB_PLAY), "PLAY SOLO", 26, Lobby_Enabled(l, LB_PLAY) ? NULL : "CHOOSE A CHARACTER FIRST", COL_GREEN, Lobby_Enabled(l, LB_PLAY), showKeys && l->focus == LB_PLAY);
    DrawButton(Lobby_Button(LB_FIND), "FIND GAMES", 26, "ON THIS NETWORK", COL_FILL, Lobby_Enabled(l, LB_FIND), showKeys && l->focus == LB_FIND);
    DrawButton(Lobby_Button(LB_HOST), "HOST A GAME", 26, "NOT YET: THE GAME HAS NO SERVER IN IT", COL_FILL, Lobby_Enabled(l, LB_HOST), showKeys && l->focus == LB_HOST);

    const UiRect s = Lobby_StatusBox();
    const char *line = l->hasChoice || l->rosterCount == 0 ? "Ready: nobody else is in this lobby." : "Choose a character to play.";
    DrawText(line, (int)s.x, (int)s.y + 2, 14, COL_TEXT);
    DrawWrapped("Joining and hosting come with the in-game network, which is not built yet.", (int)s.x, (int)s.y + 22, s.w, 2, 12, 14, COL_DIM);

    if (showKeys) DrawText("A / D or arrows: move     ENTER: press", 10, 428, 11, COL_DIM);
}

static void DrawFind(const Lobby *l, bool showKeys)
{
    DrawText("FIND A GAME", 10, 12, 22, YELLOW);
    DrawBack(showKeys && l->focus == LOBBY_FOCUS_BACK);

    const UiRect p = Lobby_ListPanel();
    DrawRectangleRec(R(p), Fade(BLACK, 0.7f));
    DrawRectangleLines((int)p.x, (int)p.y, (int)p.w, (int)p.h, COL_OFF_EDGE);

    const int shown = Lobby_ShownRows(l);
    for (int i = 0; i < shown; ++i) {
        const LobbyEntry *e = &l->entries[l->scroll + i];
        const UiRect r = Lobby_Row(i);
        const bool on = l->focus == l->scroll + i;
        DrawRectangleRec(R(r), on ? COL_ROW_ON : COL_ROW);
        DrawRectangleLines((int)r.x, (int)r.y, (int)r.w, (int)r.h, on ? YELLOW : COL_OFF_EDGE);
        DrawText(e->name, (int)r.x + 10, (int)r.y + 5, 18, e->joinable ? WHITE : COL_OFF_TEXT);
        DrawText(TextFormat("%d / %d PLAYERS     %s", e->players, e->maxPlayers, e->transport), (int)r.x + 10, (int)r.y + 28, 12, COL_DIM);
        if (!e->joinable) DrawText(e->why, (int)(r.x + r.w) - 10 - MeasureText(e->why, 14), (int)r.y + 16, 14, COL_BAD);
    }

    const bool canScroll = l->entryCount > LOBBY_ROWS;
    const UiRect up = Lobby_ScrollUp(), down = Lobby_ScrollDown();
    if (canScroll) {
        DrawButton(up, "UP", 16, NULL, COL_FILL, l->scroll > 0, false);
        DrawButton(down, "DOWN", 16, NULL, COL_FILL, l->scroll < l->entryCount - LOBBY_ROWS, false);
    }

    const char *status = "";
    Color statusColor = YELLOW;
    switch (l->search) {
        case LOBBY_SEARCHING:
            if (l->entryCount == 0) { const int dots = (int)(GetTime() * 2.0) % 4; status = TextFormat("SEARCHING THE NETWORK%.*s", dots, "..."); }
            else status = TextFormat("%d %s FOUND", l->entryCount, l->entryCount == 1 ? "GAME" : "GAMES");
            break;
        case LOBBY_SEARCH_UNAVAILABLE: status = l->noSearchWhy; statusColor = COL_BAD; break;
        case LOBBY_SEARCH_FAILED:      status = "COULD NOT START THE SEARCH. IS THERE A NETWORK?"; statusColor = COL_BAD; break;
        default: break;
    }
    DrawText(status, 16, 342, 18, statusColor);
    DrawText("Games on this Wi-Fi or hotspot appear here by themselves.", 16, 370, 12, COL_DIM);
    DrawText("Joining is not in the game yet: this list only shows what is found.", 16, 386, 12, COL_DIM);
    if (showKeys) DrawText("A / D or arrows: move     ENTER: press", 10, 428, 11, COL_DIM);
}

void LobbyUI_Draw(const Lobby *l, bool showKeys)
{
    ClearBackground(COL_BG);
    if (l->screen == LOBBY_HOME) DrawHome(l, showKeys);
    else DrawFind(l, showKeys);
}
