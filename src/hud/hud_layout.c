// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud_layout.h and hud_layout.c"
// ============================================================================
#include "hud_layout.h"

UiRect HudLayout_StaminaBar(void)     { return (UiRect){ 300.0f, 436.0f, 200.0f, 10.0f }; }     // 200 x 10 (the thin 5 and 6 px bars it replaces were hard to read); clear of the weapon readout (x 520 to 620), the RELOAD, SLIDE and fire-pad circles, and the toast above it
UiRect HudLayout_StatsButton(void)    { return (UiRect){ 8.0f, 8.0f, 78.0f, 32.0f }; }
UiRect HudLayout_StatsPanel(void)     { return (UiRect){ 236.0f, 10.0f, 330.0f, 270.0f }; }
UiRect HudLayout_StatsClose(void)     { const UiRect p = HudLayout_StatsPanel(); return (UiRect){ p.x + p.w - 52.0f, p.y + 6.0f, 46.0f, 36.0f }; }
UiRect HudLayout_DiagToggle(void)     { const UiRect p = HudLayout_StatsPanel(); return (UiRect){ p.x + 16.0f, p.y + p.h - 46.0f, 200.0f, 34.0f }; }
UiRect HudLayout_DebugShaderRow(void) { return (UiRect){ 6.0f, 46.0f, 300.0f, 20.0f }; }
UiRect HudLayout_DebugSoundRow(void)  { return (UiRect){ 6.0f, 150.0f, 300.0f, 20.0f }; }
