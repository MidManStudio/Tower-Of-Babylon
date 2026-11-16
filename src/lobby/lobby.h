// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "lobby.h and lobby.c"
// ============================================================================
#ifndef LOBBY_H
#define LOBBY_H

#include <stdbool.h>

#include "../input/ui_rect.h"
#include "lobby_backend.h"

// The lobby: the first screen of the game, and the place the game comes back to between choices. Three screens of its own. HOME shows who the player
// is (the chosen character) and four buttons: choose a character, play solo, find games, host a game. FIND lists the games found on the network and
// has a JOIN button for the highlighted one. CONNECTING waits for the host to answer a join, with a CANCEL button.
// This file is the logic and the layout with no raylib in it (lobby_ui.c draws it), in canvas pixels (800 x 450), so the tests need only a C
// compiler. It does not open the character screen, start the game, host or join: it says what it wants (Lobby_TakeWant) and main.c does it.

typedef enum { LOBBY_HOME = 0, LOBBY_FIND, LOBBY_CONNECTING } LobbyScreen;
typedef enum { LB_CHARACTER = 0, LB_PLAY, LB_FIND, LB_HOST, LB_COUNT } LobbyButton;     // HOME's buttons, in the order the keyboard steps through them
typedef enum { LOBBY_WANT_NONE = 0, LOBBY_WANT_CHARACTER, LOBBY_WANT_PLAY, LOBBY_WANT_HOST, LOBBY_WANT_JOIN, LOBBY_WANT_CANCEL } LobbyWant;
typedef enum { LOBBY_SEARCH_OFF = 0, LOBBY_SEARCHING, LOBBY_SEARCH_UNAVAILABLE, LOBBY_SEARCH_FAILED } LobbySearch;

#define LOBBY_ROWS        5          // games shown at once on FIND
#define LOBBY_FOCUS_BACK  (-1)       // FIND's focus: the BACK button (otherwise the index of a game)
#define LOBBY_WHY_LEN     64
#define LOBBY_NOTICE_LEN  64         // a line in red: why a host or a join did not work, why a game ended

typedef struct {
    LobbyScreen screen;
    int         focus;               // HOME: a LobbyButton. FIND: LOBBY_FOCUS_BACK or the index of the highlighted game
    int         rosterCount;         // from the game, every frame (Lobby_SetContext): how many characters there are
    bool        hasChoice;           //   and whether one is chosen
    bool        canHost;             //   and whether this platform can host a game (a browser cannot)
    LobbyWant   want;                // what the screen asks of the game, until Lobby_TakeWant
    LobbySearch search;
    char        noSearchWhy[LOBBY_WHY_LEN];
    int         entryCount, scroll;  // FIND: the games found (a copy, refreshed every frame while searching) and the first one shown
    LobbyEntry  entries[LOBBY_MAX_ENTRIES];
    LobbyEntry  joinTarget;          // the game a JOIN was asked for, copied when it was asked (the list may change before main.c looks)
    char        notice[LOBBY_NOTICE_LEN];       // shown in red until the player does something else
    char        connectName[LOBBY_NAME_MAX + 1];// CONNECTING: the game being joined
} Lobby;

void Lobby_Init(Lobby *l);
void Lobby_SetContext(Lobby *l, int rosterCount, bool hasChoice, bool canHost);   // every frame, before the input
bool Lobby_Enabled(const Lobby *l, LobbyButton b);                   // HOME's buttons; HOST is on when the platform can host and the player has a character (or there is no roster), as PLAY is
void Lobby_Move(Lobby *l, int dir);                                  // left (-1) or right (+1): the next button that is on, or the next game, wrapping round
void Lobby_Press(Lobby *l);                                          // the confirm key
void Lobby_Tap(Lobby *l, float x, float y);                          // a button that is on; on FIND: BACK, the arrows, a game; anywhere else nothing
// Once per frame: starts the search when FIND is open and stops it when it is not (the only place the backend is called), then copies the games
// found. The backend may be NULL (nothing can search).
void      Lobby_Update(Lobby *l, const LobbyBackend *b, double nowMs);
LobbyWant Lobby_TakeWant(Lobby *l);                                  // what the screen wants, once

// What main.c tells the screen about a host or a join it has started.
bool Lobby_CanJoin(const Lobby *l);                                  // FIND: the highlighted game can be joined (JOIN is on)
bool Lobby_JoinTarget(const Lobby *l, LobbyEntry *out);              // after LOBBY_WANT_JOIN: the game asked for, as it was when JOIN was pressed
void Lobby_BeginConnect(Lobby *l, const char *gameName);             // a join has started: show CONNECTING (the search stops)
void Lobby_ConnectFailed(Lobby *l, const char *why);                 // the join did not work: back to FIND, with the reason in red
void Lobby_BackHome(Lobby *l);                                       // a game was joined or hosted and is over, or is starting: HOME again (the notice is kept)
void Lobby_SetNotice(Lobby *l, const char *text);                    // a line in red on HOME and FIND; "" clears it

// Layout.
UiRect Lobby_CharacterPanel(void);                                   // HOME: the box showing the chosen character
UiRect Lobby_Button(LobbyButton b);                                  // HOME: LB_CHARACTER sits at the foot of the panel, the others in a column on the right
UiRect Lobby_StatusBox(void);                                        // HOME: under the column
UiRect Lobby_Back(void);                                             // FIND: top right (the same place as the BACK of the character screen)
UiRect Lobby_ListPanel(void);
UiRect Lobby_Row(int shownIndex);                                    // FIND: row 0..LOBBY_ROWS-1 of the games on view
UiRect Lobby_ScrollUp(void);
UiRect Lobby_ScrollDown(void);
UiRect Lobby_Join(void);                                             // FIND: JOIN, under the list on the right
UiRect Lobby_Cancel(void);                                           // CONNECTING: CANCEL, in the middle
int    Lobby_ShownRows(const Lobby *l);                              // how many rows have a game in them

#endif
