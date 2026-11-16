// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "session_ui.h and session_ui.c"
// ============================================================================
#ifndef SESSION_UI_H
#define SESSION_UI_H

#include <stdbool.h>

#include "../input/ui_rect.h"
#include "../net/net_session.h"

// The screen of a networked game, for now: who is in it, how to reach it, and how the connection is doing. LEAVE is the one button (top right,
// where BACK is on the other screens).
static inline UiRect SessionUI_Leave(void) { return (UiRect){ 698.0f, 8.0f, 94.0f, 30.0f }; }

// Draws it (clearing the background itself), between BeginDrawing and EndDrawing.
void SessionUI_Draw(const NetSession *s, bool showKeys);

#endif
