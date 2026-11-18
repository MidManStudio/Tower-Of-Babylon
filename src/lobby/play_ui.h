// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "play_ui.h and play_ui.c"
// ============================================================================
#ifndef PLAY_UI_H
#define PLAY_UI_H

#include <stdbool.h>

#include "raylib.h"

#include "../net/net_play.h"
#include "../net/net_session.h"

// The picture of walking in a networked game: the floor of the session (the client's own map, made from the host's seed), the stairs, and the
// other players as simple figures with their numbers over them; then the few things on the screen (who and how many, the ping, the stamina bar,
// the touch controls, LEAVE). No weapons, monsters or pickups are in a session yet.
// PlayUI_Begin is called once when the walk starts (it builds the floor's lighting from the session's map; solo's is rebuilt by whoever starts
// a run again); the others are called every frame: PlayUI_Draw3D inside BeginMode3D with the view clip applied, the rest after EndMode3D.
void PlayUI_Begin(const NetSession *s);
void PlayUI_Draw3D(const NetSession *s, const Camera3D *cam, double nowMs);
void PlayUI_DrawTags(const NetSession *s, const Camera3D *cam, double nowMs);
// leaveArmed: the keyboard's leave has been pressed once and waits for a second press (shown as a line of text).
void PlayUI_DrawOverlay(const NetSession *s, bool showKeys, bool leaveArmed);

// The colour of a player's figure and number, by player id (each of the four has their own).
Color PlayUI_PlayerColor(int id);

#endif
