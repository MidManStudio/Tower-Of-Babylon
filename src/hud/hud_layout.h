// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud_layout.h and hud_layout.c"
// ============================================================================
#ifndef HUD_LAYOUT_H
#define HUD_LAYOUT_H

#include "../input/ui_rect.h"

// Where the parts of the HUD sit on the 800 x 450 canvas, as plain rectangles with no raylib in them, so the drawing, the
// tap handling and the tests all use the same numbers.

UiRect HudLayout_StatsButton(void);     // top left: opens the stats popup
UiRect HudLayout_StatsPanel(void);      // the popup
UiRect HudLayout_StatsClose(void);      // the X in the popup's corner
UiRect HudLayout_DiagToggle(void);      // the diagnostics switch inside the popup
UiRect HudLayout_DebugShaderRow(void);  // a line of the diagnostics overlay that switches the shader when tapped
UiRect HudLayout_DebugSoundRow(void);   // the line that plays the next sound when tapped

#endif
