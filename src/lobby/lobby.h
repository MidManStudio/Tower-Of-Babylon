// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "lobby.h and lobby.c"
// ============================================================================
#ifndef LOBBY_H
#define LOBBY_H

#include <stdbool.h>

#include "../input/ui_rect.h"
#include "lobby_backend.h"

// The lobby: the first screen of the game, and the place the game comes back to between choices. Two screens of its own. HOME shows who the player
// is (the chosen character) and four buttons: choose a character, play solo, find games, host a game. FIND lists the games found on the network.
// This file is the logic and the layout with no raylib in it (lobby_ui.c draws it), in canvas pixels (800 x 450), so the tests need only a C
// compiler. It does not open the character screen or start the game: it says what it wants (Lobby_TakeWant) and main.c does it.

typedef enum { LOBBY_HOME = 0, LOBBY_FIND } LobbyScreen;
typedef enum { LB_CHARACTER = 0, LB_PLAY, LB_FIND, LB_HOST, LB_COUNT } LobbyButton;     // HOME's buttons, in the order the keyboard steps through them
typedef enum { LOBBY_WANT_NONE = 0, LOBBY_WANT_CHARACTER, LOBBY_WANT_PLAY } LobbyWant;
typedef enum { LOBBY_SEARCH_OFF = 0, LOBBY_SEARCHING, LOBBY_SEARCH_UNAVAILABLE, LOBBY_SEARCH_FAILED } LobbySearch;

#define LOBBY_ROWS        5          // games shown at once on FIND
#define LOBBY_FOCUS_BACK  (-1)       // FIND's focus: the BACK button (otherwise the index of a game)
#define LOBBY_WHY_LEN     64

typedef struct {
    LobbyScreen screen;
    int         focus;               // HOME: a LobbyButton. FIND: LOBBY_FOCUS_BACK or the index of the highlighted game
    int         rosterCount;         // from the game, every frame (Lobby_SetContext): how many characters there are
    bool        hasChoice;           //   and whether one is chosen
    LobbyWant   want;                // what the screen asks of the game, until Lobby_TakeWant
    LobbySearch search;
    char        noSearchWhy[LOBBY_WHY_LEN];
    int         entryCount, scroll;  // FIND: the games found (a copy, refreshed every frame while searching) and the first one shown
    LobbyEntry  entries[LOBBY_MAX_ENTRIES];
} Lobby;

void Lobby_Init(Lobby *l);
void Lobby_SetContext(Lobby *l, int rosterCount, bool hasChoice);   // every frame, before the input
bool Lobby_Enabled(const Lobby *l, LobbyButton b);                   // HOME's buttons: HOST never is (nothing in the game can host yet)
void Lobby_Move(Lobby *l, int dir);                                  // left (-1) or right (+1): the next button that is on, or the next game, wrapping round
void Lobby_Press(Lobby *l);                                          // the confirm key
void Lobby_Tap(Lobby *l, float x, float y);                          // a button that is on; on FIND: BACK, the arrows, a game; anywhere else nothing
// Once per frame: starts the search when FIND is open and stops it when it is not (the only place the backend is called), then copies the games
// found. The backend may be NULL (nothing can search).
void      Lobby_Update(Lobby *l, const LobbyBackend *b, double nowMs);
LobbyWant Lobby_TakeWant(Lobby *l);                                  // what the screen wants, once

// Layout.
UiRect Lobby_CharacterPanel(void);                                   // HOME: the box showing the chosen character
UiRect Lobby_Button(LobbyButton b);                                  // HOME: LB_CHARACTER sits at the foot of the panel, the others in a column on the right
UiRect Lobby_StatusBox(void);                                        // HOME: under the column
UiRect Lobby_Back(void);                                             // FIND: top right (the same place as the BACK of the character screen)
UiRect Lobby_ListPanel(void);
UiRect Lobby_Row(int shownIndex);                                    // FIND: row 0..LOBBY_ROWS-1 of the games on view
UiRect Lobby_ScrollUp(void);
UiRect Lobby_ScrollDown(void);
int    Lobby_ShownRows(const Lobby *l);                              // how many rows have a game in them

#endif
