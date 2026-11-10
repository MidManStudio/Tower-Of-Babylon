// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud_stats.c"
// ============================================================================
#include "hud.h"

#include "../ui/screen_fx.h"

static Rectangle R(UiRect r) { return (Rectangle){ r.x, r.y, r.w, r.h }; }

void HudStats_Draw(const HudView *v)
{
    const UiRect p = HudLayout_StatsPanel(), x = HudLayout_StatsClose(), d = HudLayout_DiagToggle();
    DrawRectangleRec(R(p), Fade(BLACK, 0.88f));
    DrawRectangleLines((int)p.x, (int)p.y, (int)p.w, (int)p.h, WHITE);
    DrawText("STATS", (int)p.x + 16, (int)p.y + 14, 20, YELLOW);
    if (v->characterName && v->characterName[0]) DrawText(v->characterName, (int)p.x + 100, (int)p.y + 18, 15, (Color){ 230, 170, 60, 255 });

    DrawRectangleRec(R(x), Fade(MAROON, 0.9f));                                  // the way out
    DrawRectangleLines((int)x.x, (int)x.y, (int)x.w, (int)x.h, WHITE);
    DrawText("X", (int)(x.x + (x.w - (float)MeasureText("X", 22)) / 2.0f), (int)x.y + 7, 22, WHITE);

    const int lx = (int)p.x + 16, vx = (int)p.x + 122;
    int y = (int)p.y + 52;
    const Color label = (Color){ 170, 170, 180, 255 };
    DrawText("FLOOR", lx, y, 15, label);   DrawText(TextFormat("%d", v->floorNumber), vx, y, 15, WHITE);              y += 22;
    DrawText("HEALTH", lx, y, 15, label);
    ScreenFx_DrawHealthBar(vx, y, 120, 13, v->hp, v->maxHp);
    DrawText(TextFormat("%d / %d", (int)(v->hp + 0.5f), (int)(v->maxHp + 0.5f)), vx + 130, y, 15, WHITE);              y += 22;
    DrawText("STAMINA", lx, y, 15, label);
    ScreenFx_DrawStaminaBar(vx, y + 3, 120, 8, v->stamina01, v->exhausted, v->exhaustRecover01, (float)GetTime());
    DrawText(v->exhausted ? "TIRED" : TextFormat("%d%%", (int)(v->stamina01 * 100.0f + 0.5f)), vx + 130, y, 15, v->exhausted ? ORANGE : WHITE); y += 22;
    DrawText("WEAPON", lx, y, 15, label);
    DrawText(v->weaponIsGun ? (v->magSize > 0 ? TextFormat("%s  %d / %d", v->weaponName, v->mag, v->ammo) : TextFormat("%s  %d rounds", v->weaponName, v->ammo))
                            : (v->weaponWears ? TextFormat("%s  %d swings left", v->weaponName, v->durabilityLeft) : v->weaponName),
             vx, y, 15, v->weaponDry ? RED : WHITE); y += 22;
    DrawText("ENEMIES", lx, y, 15, label); DrawText(TextFormat("%d", v->enemies), vx, y, 15, WHITE);                  y += 22;
    if (v->hasTorch) {
        DrawText("LIGHT", lx, y, 15, label);
        DrawText(TextFormat("%s   %d s left", v->torchOn ? "on" : "off", (int)(v->torchSecs + 0.5f)), vx, y, 15, v->torchLow ? ORANGE : WHITE);
        y += 22;
    }
    DrawText("FPS", lx, y, 15, label);     DrawText(TextFormat("%d", GetFPS()), vx, y, 15, WHITE);                      y += 24;
    DrawText(v->floorInfo, lx, y, 11, (Color){ 130, 130, 140, 255 });

    DrawRectangleRec(R(d), Fade(Hud_DiagnosticsOn() ? DARKGREEN : DARKGRAY, 0.9f));
    DrawRectangleLines((int)d.x, (int)d.y, (int)d.w, (int)d.h, WHITE);
    DrawText(Hud_DiagnosticsOn() ? "DIAGNOSTICS: ON" : "DIAGNOSTICS: OFF", (int)d.x + 14, (int)d.y + 9, 16, WHITE);
}
