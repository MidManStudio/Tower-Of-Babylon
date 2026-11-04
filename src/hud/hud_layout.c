// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud_layout.h and hud_layout.c"
// ============================================================================
#include "hud_layout.h"

UiRect HudLayout_StatsButton(void)    { return (UiRect){ 8.0f, 8.0f, 78.0f, 32.0f }; }
UiRect HudLayout_StatsPanel(void)     { return (UiRect){ 236.0f, 10.0f, 330.0f, 270.0f }; }
UiRect HudLayout_StatsClose(void)     { const UiRect p = HudLayout_StatsPanel(); return (UiRect){ p.x + p.w - 52.0f, p.y + 6.0f, 46.0f, 36.0f }; }
UiRect HudLayout_DiagToggle(void)     { const UiRect p = HudLayout_StatsPanel(); return (UiRect){ p.x + 16.0f, p.y + p.h - 46.0f, 200.0f, 34.0f }; }
UiRect HudLayout_DebugShaderRow(void) { return (UiRect){ 6.0f, 46.0f, 300.0f, 20.0f }; }
UiRect HudLayout_DebugSoundRow(void)  { return (UiRect){ 6.0f, 150.0f, 300.0f, 20.0f }; }
