// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "lobby.h and lobby.c"
// ============================================================================
#include "lobby.h"

#include <stdio.h>
#include <string.h>

bool Lobby_Enabled(const Lobby *l, LobbyButton b)
{
    switch (b) {
        case LB_CHARACTER: return l->rosterCount > 0;
        case LB_PLAY:      return l->rosterCount == 0 || l->hasChoice;    // with no roster at all the game starts without a choice, as it always did
        case LB_FIND:      return true;
        default:           return false;                                  // HOST
    }
}

// HOME: if the focus is on a button that is off, move it to the first one that is on.
static void FixHomeFocus(Lobby *l)
{
    if (l->screen != LOBBY_HOME) return;
    if (l->focus >= 0 && l->focus < LB_COUNT && Lobby_Enabled(l, (LobbyButton)l->focus)) return;
    for (int i = 0; i < LB_COUNT; ++i) if (Lobby_Enabled(l, (LobbyButton)i)) { l->focus = i; return; }
}

static int MaxScroll(const Lobby *l) { return l->entryCount > LOBBY_ROWS ? l->entryCount - LOBBY_ROWS : 0; }

static void ClampScroll(Lobby *l)
{
    if (l->scroll > MaxScroll(l)) l->scroll = MaxScroll(l);
    if (l->scroll < 0) l->scroll = 0;
}

// Brings the highlighted game into view (the keyboard moves the highlight; the arrows on screen move the view and leave it alone).
static void EnsureVisible(Lobby *l)
{
    if (l->focus >= 0) {
        if (l->focus < l->scroll) l->scroll = l->focus;
        if (l->focus >= l->scroll + LOBBY_ROWS) l->scroll = l->focus - LOBBY_ROWS + 1;
    }
    ClampScroll(l);
}

void Lobby_Init(Lobby *l)
{
    memset(l, 0, sizeof *l);
    l->screen = LOBBY_HOME;
    l->focus = LB_CHARACTER;
    l->search = LOBBY_SEARCH_OFF;
    FixHomeFocus(l);
}

void Lobby_SetContext(Lobby *l, int rosterCount, bool hasChoice)
{
    const bool justChose = hasChoice && !l->hasChoice;
    l->rosterCount = rosterCount < 0 ? 0 : rosterCount;
    l->hasChoice = hasChoice;
    // Coming back from the character screen with somebody chosen, the next thing is to play: the keyboard's focus goes to PLAY.
    if (l->screen == LOBBY_HOME && justChose && Lobby_Enabled(l, LB_PLAY)) l->focus = LB_PLAY;
    FixHomeFocus(l);
}

static void GoHome(Lobby *l)
{
    l->screen = LOBBY_HOME;
    l->focus = LB_FIND;
    FixHomeFocus(l);
}

static void Activate(Lobby *l, LobbyButton b)
{
    if (!Lobby_Enabled(l, b)) return;
    switch (b) {
        case LB_CHARACTER: l->want = LOBBY_WANT_CHARACTER; break;
        case LB_PLAY:      l->want = LOBBY_WANT_PLAY; break;
        case LB_FIND:      l->screen = LOBBY_FIND; l->focus = LOBBY_FOCUS_BACK; l->scroll = 0; break;   // the search itself starts in Lobby_Update
        default: break;
    }
}

void Lobby_Move(Lobby *l, int dir)
{
    if (dir == 0) return;
    const int d = dir > 0 ? 1 : -1;
    if (l->screen == LOBBY_HOME) {
        int f = l->focus;
        for (int i = 0; i < LB_COUNT; ++i) {
            f = (f + d + LB_COUNT) % LB_COUNT;
            if (Lobby_Enabled(l, (LobbyButton)f)) { l->focus = f; return; }
        }
        return;
    }
    // FIND: a ring of BACK, then the games in order. BACK is position 0, game i is position i + 1.
    const int ring = l->entryCount + 1;
    const int pos = (((l->focus + 1) + d) % ring + ring) % ring;
    l->focus = pos - 1;
    EnsureVisible(l);
}

void Lobby_Press(Lobby *l)
{
    if (l->screen == LOBBY_HOME) Activate(l, (LobbyButton)l->focus);
    else if (l->focus == LOBBY_FOCUS_BACK) GoHome(l);          // a game cannot be joined yet: Enter on one does nothing
}

int Lobby_ShownRows(const Lobby *l)
{
    int n = l->entryCount - l->scroll;
    if (n > LOBBY_ROWS) n = LOBBY_ROWS;
    return n < 0 ? 0 : n;
}

void Lobby_Tap(Lobby *l, float x, float y)
{
    if (l->screen == LOBBY_HOME) {
        for (int b = 0; b < LB_COUNT; ++b) {
            if (!UiRect_Contains(Lobby_Button((LobbyButton)b), x, y)) continue;
            if (Lobby_Enabled(l, (LobbyButton)b)) { l->focus = b; Activate(l, (LobbyButton)b); }
            return;
        }
        return;
    }
    if (UiRect_Contains(Lobby_Back(), x, y)) { GoHome(l); return; }
    if (UiRect_Contains(Lobby_ScrollUp(), x, y))   { --l->scroll; ClampScroll(l); return; }
    if (UiRect_Contains(Lobby_ScrollDown(), x, y)) { ++l->scroll; ClampScroll(l); return; }
    const int shown = Lobby_ShownRows(l);
    for (int i = 0; i < shown; ++i)
        if (UiRect_Contains(Lobby_Row(i), x, y)) { l->focus = l->scroll + i; return; }
}

void Lobby_Update(Lobby *l, const LobbyBackend *b, double nowMs)
{
    if (l->screen == LOBBY_FIND) {
        if (l->search == LOBBY_SEARCH_OFF) {
            if (!b || !b->canSearch) {
                l->search = LOBBY_SEARCH_UNAVAILABLE;
                snprintf(l->noSearchWhy, sizeof l->noSearchWhy, "%s", b && b->cannotSearchReason ? b->cannotSearchReason : "NO NETWORK");
            } else if (b->searchStart && !b->searchStart(b->user)) {
                l->search = LOBBY_SEARCH_FAILED;
            } else {
                l->search = LOBBY_SEARCHING;
            }
        }
        if (l->search == LOBBY_SEARCHING && b) {
            if (b->update) b->update(b->user, nowMs);
            int n = b->count ? b->count(b->user) : 0;
            if (n > LOBBY_MAX_ENTRIES) n = LOBBY_MAX_ENTRIES;
            int got = 0;
            for (int i = 0; i < n && got < LOBBY_MAX_ENTRIES; ++i)
                if (b->entry && b->entry(b->user, i, &l->entries[got])) ++got;
            l->entryCount = got;
            if (l->focus >= got) l->focus = got > 0 ? got - 1 : LOBBY_FOCUS_BACK;
            ClampScroll(l);
        }
        return;
    }
    // Not on FIND: no search runs, and nothing found is kept.
    if (l->search == LOBBY_SEARCHING && b && b->searchStop) b->searchStop(b->user);
    l->search = LOBBY_SEARCH_OFF;
    l->entryCount = 0;
    l->scroll = 0;
}

LobbyWant Lobby_TakeWant(Lobby *l)
{
    const LobbyWant w = l->want;
    l->want = LOBBY_WANT_NONE;
    return w;
}

// ---- layout ---------------------------------------------------------------------------------------------------------------------------
UiRect Lobby_CharacterPanel(void) { return (UiRect){ 8.0f, 66.0f, 440.0f, 300.0f }; }

UiRect Lobby_Button(LobbyButton b)
{
    switch (b) {
        case LB_CHARACTER: return (UiRect){ 20.0f, 306.0f, 416.0f, 48.0f };
        case LB_PLAY:      return (UiRect){ 464.0f, 66.0f, 328.0f, 70.0f };
        case LB_FIND:      return (UiRect){ 464.0f, 148.0f, 328.0f, 70.0f };
        default:           return (UiRect){ 464.0f, 230.0f, 328.0f, 70.0f };       // HOST
    }
}

UiRect Lobby_StatusBox(void)   { return (UiRect){ 464.0f, 312.0f, 328.0f, 54.0f }; }
UiRect Lobby_Back(void)        { return (UiRect){ 698.0f, 8.0f, 94.0f, 30.0f }; }
UiRect Lobby_ListPanel(void)   { return (UiRect){ 8.0f, 50.0f, 784.0f, 282.0f }; }
UiRect Lobby_Row(int i)        { return (UiRect){ 16.0f, 58.0f + (float)i * 54.0f, 700.0f, 48.0f }; }
UiRect Lobby_ScrollUp(void)    { return (UiRect){ 724.0f, 58.0f, 60.0f, 120.0f }; }
UiRect Lobby_ScrollDown(void)  { return (UiRect){ 724.0f, 190.0f, 60.0f, 120.0f }; }
