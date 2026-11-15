// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "lobby_ui.h and lobby_ui.c"
// ============================================================================
#ifndef LOBBY_UI_H
#define LOBBY_UI_H

#include <stdbool.h>

#include "lobby.h"

// Draws the lobby (it clears the background itself): between BeginDrawing and EndDrawing, with nothing else going on. showKeys marks the button the
// keyboard is on and adds the line naming the keys, for a player using the keyboard.
void LobbyUI_Draw(const Lobby *l, bool showKeys);

#endif
