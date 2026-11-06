// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/characters.md, section "character_select_ui.h and character_select_ui.c"
// ============================================================================
#ifndef CHARACTER_SELECT_UI_H
#define CHARACTER_SELECT_UI_H

#include <stdbool.h>

#include "character_select.h"

// Draws the whole screen (it clears the background itself): between BeginDrawing and EndDrawing, with nothing else going on.
// showKeys adds the line naming the keyboard keys, for a player using the keyboard.
void CharSelectUI_Draw(const CharSelect *s, bool showKeys);

#endif
