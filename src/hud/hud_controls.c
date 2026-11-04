// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud_controls.c"
// ============================================================================
#include "hud.h"

#include "../input/input.h"

static Rectangle R(UiRect r) { return (Rectangle){ r.x, r.y, r.w, r.h }; }

static void Button(UiRect r, Color c, const char *label, int size)
{
    DrawRectangleRec(R(r), Fade(c, 0.7f));
    DrawRectangleLines((int)r.x, (int)r.y, (int)r.w, (int)r.h, WHITE);
    DrawText(label, (int)(r.x + (r.w - (float)MeasureText(label, size)) / 2.0f), (int)(r.y + (r.h - (float)size) / 2.0f), size, WHITE);
}

// The sprint icon: two chevrons pointing up inside a ring. White while the knob is on its way out, a ring filling up as it
// gets close, yellow once the sprint has started, orange when the player is too tired to.
static void SprintIcon(float cx, float cy, float fill, bool sprint, bool tired, float alpha)
{
    const Color col = (sprint && tired) ? ORANGE : (sprint ? YELLOW : WHITE);
    DrawCircle((int)cx, (int)cy, 20.0f, Fade(BLACK, 0.40f * alpha));
    if (fill > 0.0f && !sprint) DrawRing((Vector2){ cx, cy }, 21.0f, 25.0f, -90.0f, -90.0f + 360.0f * fill, 36, Fade(WHITE, 0.8f * alpha));
    DrawRing((Vector2){ cx, cy }, 20.0f, 23.0f, 0.0f, 360.0f, 36, Fade(col, (sprint ? 1.0f : 0.45f) * alpha));
    const Color g = Fade(col, (sprint ? 1.0f : 0.8f) * alpha);
    DrawLineEx((Vector2){ cx - 8.0f, cy + 1.0f }, (Vector2){ cx, cy - 7.0f }, 3.0f, g);
    DrawLineEx((Vector2){ cx, cy - 7.0f }, (Vector2){ cx + 8.0f, cy + 1.0f }, 3.0f, g);
    DrawLineEx((Vector2){ cx - 8.0f, cy + 9.0f }, (Vector2){ cx, cy + 1.0f }, 3.0f, g);
    DrawLineEx((Vector2){ cx, cy + 1.0f }, (Vector2){ cx + 8.0f, cy + 9.0f }, 3.0f, g);
    if (sprint && tired) DrawText("TIRED", (int)cx - 18, (int)cy - 38, 12, ORANGE);
}

void HudControls_Draw(const HudView *v)
{
    const TcLayout *L = Tc_Layout();
    const TcOut *t = Input_Touch();

    // The floating stick: where the thumb is, or a faint ring where it would go.
    const bool on = t->stickActive;
    const float ax = on ? t->anchorX : L->idleX, ay = on ? t->anchorY : L->idleY;
    const float kx = on ? t->knobX : ax, ky = on ? t->knobY : ay;
    const float a = on ? 1.0f : 0.30f;
    DrawCircleV((Vector2){ ax, ay }, L->stickRadius, Fade(LIGHTGRAY, 0.5f * a));
    DrawCircleV((Vector2){ ax, ay }, L->stickRadius - 15.0f, Fade(GRAY, 0.3f * a));
    DrawCircleV((Vector2){ kx, ky }, L->knobRadius, Fade(DARKGRAY, 0.8f * a));
    SprintIcon(ax, ay - L->stickRadius - L->sprintIconGap - 20.0f, on ? t->sprintFill : 0.0f, on && t->sprint, v->exhausted, on ? 1.0f : 0.30f);

    // The fire pad: held it fires, and the finger that holds it also turns the camera.
    const Color fireCol = t->fireHeld ? RED : DARKGRAY;
    DrawCircleV((Vector2){ L->firePad.x, L->firePad.y }, L->firePad.r, Fade(fireCol, 0.60f));
    DrawCircleLines((int)L->firePad.x, (int)L->firePad.y, L->firePad.r, WHITE);
    DrawCircleLines((int)L->firePad.x, (int)L->firePad.y, L->firePad.r - 14.0f, Fade(WHITE, 0.35f));
    const char *attack = v->weaponIsGun ? "FIRE" : (v->weaponIsMelee ? "SWING" : "PUNCH");
    DrawText(attack, (int)(L->firePad.x - (float)MeasureText(attack, 16) / 2.0f), (int)L->firePad.y - 8, 16, WHITE);

    Button(L->jump, t->jumpHeld ? BLUE : DARKGRAY, "JUMP", 16);

    if (v->useShown) Button(L->use, v->useLocked ? MAROON : DARKGREEN, v->useLabel, 18);
    if (v->weaponsOwned > 1) Button(L->swap, DARKGRAY, "SWAP", 16);
    if (v->slideShown) Button(L->slide, v->sliding ? SKYBLUE : (v->tooTiredToSlide ? ORANGE : (Color){ 40, 120, 170, 255 }), v->sliding ? "SLIDING" : "SLIDE", 16);
    if (v->hasTorch) {
        const bool blink = v->torchOn && v->torchLow && ((int)(GetTime() * 4.0) & 1);
        const Color lc = v->torchOn ? (blink ? RED : (v->torchLow ? ORANGE : (Color){ 190, 160, 30, 255 })) : (v->torchFlat ? MAROON : DARKGRAY);
        DrawRectangleRec(R(L->light), Fade(lc, 0.7f));
        DrawRectangleLines((int)L->light.x, (int)L->light.y, (int)L->light.w, (int)L->light.h, WHITE);
        const char *label = v->torchOn ? "LIGHT ON" : (v->torchFlat ? "FLAT" : "LIGHT");
        DrawText(label, (int)(L->light.x + (L->light.w - (float)MeasureText(label, 16)) / 2.0f), (int)L->light.y + 14, 16, WHITE);
        DrawRectangle((int)L->light.x + 6, (int)L->light.y + 46, (int)((L->light.w - 12.0f) * v->torchFraction), 8, v->torchLow ? ORANGE : (Color){ 255, 233, 160, 255 });
        DrawRectangleLines((int)L->light.x + 6, (int)L->light.y + 46, (int)L->light.w - 12, 8, Fade(WHITE, 0.6f));
    }
}
