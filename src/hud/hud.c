// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud.h and hud.c"
// ============================================================================
#include "hud.h"

#include <stdio.h>

#include "../input/input.h"
#include "../platform/platform.h"
#include "../ui/screen_fx.h"

static char  g_toast[48];
static float g_toastT = 0.0f;
static bool  g_statsOpen = false;
static bool  g_diagOn = false;                  // the diagnostics overlay: off until the popup's switch is used

static Rectangle R(UiRect r) { return (Rectangle){ r.x, r.y, r.w, r.h }; }
static bool Hit(UiRect r, Vector2 p) { return UiRect_Contains(r, p.x, p.y); }

void Hud_Init(void)
{
    g_toast[0] = 0; g_toastT = 0.0f; g_statsOpen = false; g_diagOn = false;
}

void Hud_Toast(const char *text) { snprintf(g_toast, sizeof g_toast, "%s", text); g_toastT = 1.8f; }
void Hud_Update(float dt) { if (g_toastT > 0.0f) g_toastT -= dt; }

void Hud_SetStatsOpen(bool open) { g_statsOpen = open; }
void Hud_ToggleStats(void) { g_statsOpen = !g_statsOpen; }
bool Hud_StatsOpen(void) { return g_statsOpen; }
bool Hud_DiagnosticsOn(void) { return g_diagOn; }

bool Hud_HandleTap(Vector2 p, bool inventoryOpen)
{
    if (inventoryOpen) return false;
    if (Hit(HudLayout_StatsButton(), p)) { g_statsOpen = !g_statsOpen; return true; }
    if (g_statsOpen) {
        if (Hit(HudLayout_StatsClose(), p)) { g_statsOpen = false; return true; }
        if (Hit(HudLayout_DiagToggle(), p)) { g_diagOn = !g_diagOn; return true; }
        if (Hit(HudLayout_StatsPanel(), p)) return true;                       // the popup swallows taps on itself
    }
    if (g_diagOn && HudDebug_HandleTap(p)) return true;
    return false;
}

int Hud_BlockedRects(UiRect *out, int cap)
{
    int n = 0;
    if (n < cap) out[n++] = HudLayout_StatsButton();
    if (g_statsOpen && n < cap) out[n++] = HudLayout_StatsPanel();
    if (g_diagOn && n + 2 <= cap) { out[n++] = HudLayout_DebugShaderRow(); out[n++] = HudLayout_DebugSoundRow(); }
    return n;
}

void Hud_Draw(const HudView *v)
{
#if TOWER_INPUT_TOUCH
    if (Input_Scheme() == INPUT_SCHEME_TOUCH) HudControls_Draw(v);
#endif
    if (!v->invOpen) {
        const bool open = g_statsOpen;
        DrawRectangleRec(R(HudLayout_StatsButton()), Fade(open ? DARKBLUE : DARKGRAY, 0.75f));
        DrawRectangleLines((int)HudLayout_StatsButton().x, (int)HudLayout_StatsButton().y, (int)HudLayout_StatsButton().w, (int)HudLayout_StatsButton().h, WHITE);
        DrawText("STATS", (int)HudLayout_StatsButton().x + 10, (int)HudLayout_StatsButton().y + 9, 14, WHITE);
    }

    if (!v->invOpen) HudRun_Draw(v);

#if HUD_VITALS_ALWAYS_ON
    DrawText(v->weaponIsGun ? TextFormat("%s  %d", v->weaponName, v->ammo) : v->weaponName, 800 / 2 + 108, 450 - 24, 16, v->weaponDry ? RED : WHITE);
    ScreenFx_DrawHealthBar(800 / 2 - 100, 450 - 22, 200, 14, v->hp, v->maxHp);
    ScreenFx_DrawStaminaBar(800 / 2 - 100, 450 - 6, 200, 5, v->stamina01, v->exhausted, v->exhaustRecover01, (float)GetTime());
#endif

    if (g_toastT > 0.0f) {
        const int tw = MeasureText(g_toast, 18);
        DrawText(g_toast, (800 - tw) / 2, 450 - 56, 18, Fade(YELLOW, g_toastT > 1.0f ? 1.0f : g_toastT));
    }
    if (g_diagOn) HudDebug_Draw(v);
    if (g_statsOpen && !v->invOpen) HudStats_Draw(v);
}
