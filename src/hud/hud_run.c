// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud_run.c"
// ============================================================================
#include "hud.h"

#include <math.h>

// The stair clock on the screen, top and middle: a bar while the console is being worked, the countdown, then the collapse.
// A placeholder like the rest of the layout (the HUD is to become customisable).
static void CenterText(const char *s, int y, int size, Color c)
{
    DrawText(s, (800 - MeasureText(s, size)) / 2, y, size, c);
}

void HudRun_Draw(const HudView *v)
{
    if (v->stairPhase == 1 && v->consoleProgress > 0.0f) {
        const int w = 220, x = (800 - w) / 2, y = 52;
        CenterText(v->consoleHeld ? "UNLOCKING THE STAIRS" : "UNLOCKING... (HOLD USE)", y - 22, 16, WHITE);
        DrawRectangle(x, y, w, 12, Fade(BLACK, 0.6f));
        DrawRectangle(x + 2, y + 2, (int)((float)(w - 4) * v->consoleProgress), 8, v->consoleHeld ? (Color){ 60, 230, 110, 255 } : (Color){ 230, 150, 30, 255 });
        DrawRectangleLines(x, y, w, 12, WHITE);
    } else if (v->stairPhase == 2) {
        const int secs = (int)ceilf(v->stairCountdown);
        const bool urgent = secs <= 10;
        const float blink = urgent ? 0.6f + 0.4f * sinf((float)GetTime() * 8.0f) : 1.0f;
        CenterText("STAIRS UNLOCKED", 8, 14, Fade(WHITE, 0.85f));
        CenterText(TextFormat("%d:%02d", secs / 60, secs % 60), 24, 40, Fade(urgent ? RED : WHITE, blink));
    } else if (v->stairPhase == 3) {
        const float blink = 0.6f + 0.4f * sinf((float)GetTime() * 6.0f);
        CenterText("THE FLOOR IS COLLAPSING", 8, 18, Fade(RED, blink));
        const int w = 200, x = (800 - w) / 2, y = 32;
        DrawRectangle(x, y, w, 10, Fade(BLACK, 0.6f));
        DrawRectangle(x + 2, y + 2, (int)((float)(w - 4) * v->collapseLeft01), 6, Fade(RED, 0.9f));
        DrawRectangleLines(x, y, w, 10, WHITE);
        CenterText("GET TO THE DOOR", 48, 14, Fade(WHITE, 0.85f));
    }
}
