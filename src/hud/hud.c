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

// The weapon in hand, always on the screen (the numbers matter in a fight, so they are not behind the STATS button): a gun shows what is
// loaded and the reserve in the bag, "12 / 36"; a reload shows its progress under it; a melee weapon that wears out shows a bar that
// empties. The hands show nothing. Right-aligned at x = 620, bottom centre, clear of the RELOAD, SLIDE and fire-pad circles.
static void WeaponReadout(const HudView *v)
{
    const int right = 620, top = 450 - 52;
    const Color dim = (Color){ 170, 170, 180, 255 };
    if (v->weaponIsGun) {
        const int loaded = v->magSize > 0 ? v->mag : v->ammo;
        const bool low = v->magSize > 0 ? loaded <= v->magSize / 4 : loaded <= 5;
        const Color c = loaded <= 0 ? RED : (low ? ORANGE : WHITE);
        const char *big = TextFormat("%d", loaded);
        const char *small = v->magSize > 0 ? TextFormat(" / %d", v->ammo) : "";
        const int bw = MeasureText(big, 28), sw = MeasureText(small, 16);
        DrawText(v->weaponName, right - MeasureText(v->weaponName, 12), top, 12, dim);
        DrawText(big, right - sw - bw, top + 14, 28, c);
        if (small[0]) DrawText(small, right - sw, top + 26, 16, v->ammo <= 0 ? RED : dim);
        if (v->reloading) {
            DrawText("RELOADING", right - MeasureText("RELOADING", 12), top - 14, 12, YELLOW);
            DrawRectangle(right - 100, top + 46, 100, 4, Fade(BLACK, 0.6f));
            DrawRectangle(right - 100, top + 46, (int)(100.0f * v->reloadFrac), 4, YELLOW);
        } else if (v->magSize > 0 && v->magEmpty) {
            const char *hint = v->ammo > 0 ? "RELOAD" : "NO AMMO";
            if ((int)(GetTime() * 3.0) & 1) DrawText(hint, right - MeasureText(hint, 12), top - 14, 12, RED);
        }
    } else if (v->weaponWears) {
        const float f = v->weaponCondition < 0.0f ? 0.0f : (v->weaponCondition > 1.0f ? 1.0f : v->weaponCondition);
        const Color c = f > 0.5f ? (Color){ 90, 220, 110, 255 } : (f > 0.25f ? ORANGE : RED);
        DrawText(v->weaponName, right - MeasureText(v->weaponName, 12), top + 14, 12, dim);
        DrawRectangle(right - 100, top + 30, 100, 6, Fade(BLACK, 0.6f));
        DrawRectangle(right - 100, top + 30, (int)(100.0f * f), 6, c);
        DrawRectangleLines(right - 100, top + 30, 100, 6, Fade(WHITE, 0.6f));
        if (f <= 0.25f && ((int)(GetTime() * 3.0) & 1)) DrawText("FALLING APART", right - MeasureText("FALLING APART", 12), top + 40, 12, RED);
    }
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
    if (!v->invOpen && v->alive) WeaponReadout(v);
    if (!v->invOpen && v->alive && v->stim01 > 0.0f) {                  // a stim shot: its time left, and the stamina it is filling
        const int w = 200, x = (800 - w) / 2, y = 450 - 26;
        const Color cyan = (Color){ 90, 220, 230, 255 };
        DrawText("STIM", x, y - 14, 12, cyan);
        DrawRectangle(x, y, w, 3, Fade(BLACK, 0.6f));
        DrawRectangle(x, y, (int)((float)w * (v->stim01 > 1.0f ? 1.0f : v->stim01)), 3, cyan);
        ScreenFx_DrawStaminaBar(x, y + 6, w, 6, v->stamina01, v->exhausted, v->exhaustRecover01, (float)GetTime());
    }

#if HUD_VITALS_ALWAYS_ON
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
