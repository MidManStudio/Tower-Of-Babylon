// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud_controls.c"
// ============================================================================
#include "hud.h"

#include <string.h>

#include "../input/input.h"

static void Ring(float cx, float cy, float r, float thick, Color c) { DrawRing((Vector2){ cx, cy }, r - thick, r, 0.0f, 360.0f, 48, c); }

// A round button: a translucent disc, a white ring, and a label of one or two lines (a '\n' splits it) that is shrunk until it fits.
static void RoundButton(UiCircle c, Color fill, const char *label, bool held)
{
    DrawCircleV((Vector2){ c.x, c.y }, c.r, Fade(fill, held ? 0.85f : 0.55f));
    Ring(c.x, c.y, c.r, 2.5f, Fade(WHITE, held ? 0.95f : 0.65f));

    char l1[32] = { 0 }, l2[32] = { 0 };
    const char *nl = strchr(label, '\n');
    if (nl) { strncpy(l1, label, (size_t)(nl - label) < 31 ? (size_t)(nl - label) : 31); strncpy(l2, nl + 1, 31); }
    else    { strncpy(l1, label, 31); }

    int size = 18;
    for (; size > 9; --size) {
        const int w = MeasureText(l1, size) > MeasureText(l2, size) ? MeasureText(l1, size) : MeasureText(l2, size);
        if ((float)w <= c.r * 1.55f) break;
    }
    if (l2[0]) {
        DrawText(l1, (int)(c.x - (float)MeasureText(l1, size) / 2.0f), (int)(c.y - (float)size - 1.0f), size, WHITE);
        DrawText(l2, (int)(c.x - (float)MeasureText(l2, size) / 2.0f), (int)(c.y + 1.0f), size, WHITE);
    } else {
        DrawText(l1, (int)(c.x - (float)MeasureText(l1, size) / 2.0f), (int)(c.y - (float)size / 2.0f), size, WHITE);
    }
}

// The sprint icon: two chevrons pointing up inside a ring. White while the knob is on its way out, a ring filling up as it
// gets close, yellow once the sprint has started, orange when the player is too tired to.
static void SprintIcon(float cx, float cy, float r, float fill, bool sprint, bool tired, float alpha)
{
    const float k = r / 20.0f;
    const Color col = (sprint && tired) ? ORANGE : (sprint ? YELLOW : WHITE);
    DrawCircle((int)cx, (int)cy, r, Fade(BLACK, 0.40f * alpha));
    if (fill > 0.0f && !sprint) DrawRing((Vector2){ cx, cy }, r + 1.0f * k, r + 5.0f * k, -90.0f, -90.0f + 360.0f * fill, 36, Fade(WHITE, 0.8f * alpha));
    DrawRing((Vector2){ cx, cy }, r, r + 3.0f * k, 0.0f, 360.0f, 36, Fade(col, (sprint ? 1.0f : 0.45f) * alpha));
    const Color g = Fade(col, (sprint ? 1.0f : 0.8f) * alpha);
    DrawLineEx((Vector2){ cx - 8.0f * k, cy + 1.0f * k }, (Vector2){ cx, cy - 7.0f * k }, 3.0f, g);
    DrawLineEx((Vector2){ cx, cy - 7.0f * k }, (Vector2){ cx + 8.0f * k, cy + 1.0f * k }, 3.0f, g);
    DrawLineEx((Vector2){ cx - 8.0f * k, cy + 9.0f * k }, (Vector2){ cx, cy + 1.0f * k }, 3.0f, g);
    DrawLineEx((Vector2){ cx, cy + 1.0f * k }, (Vector2){ cx + 8.0f * k, cy + 9.0f * k }, 3.0f, g);
    if (sprint && tired) DrawText("TIRED", (int)cx - 18, (int)(cy - r - 18.0f), 12, ORANGE);
}

// A small bullet pointing up and to the right (the icon in the middle of the fire pad, as in the sketch).
static void BulletIcon(float cx, float cy, Color c)
{
    DrawLineEx((Vector2){ cx - 10.0f, cy + 10.0f }, (Vector2){ cx + 3.0f, cy - 3.0f }, 9.0f, c);
    DrawCircleV((Vector2){ cx + 3.0f, cy - 3.0f }, 4.5f, c);
    DrawLineEx((Vector2){ cx - 14.0f, cy + 6.0f }, (Vector2){ cx - 6.0f, cy + 14.0f }, 3.0f, c);
}

void HudControls_Draw(const HudView *v)
{
    const TcLayout *L = Tc_Layout();
    const TcOut *t = Input_Touch();

    // The floating stick: where the thumb is, or a faint ring where it would go. An outer ring, an inner disc, a knob.
    const bool on = t->stickActive;
    const float ax = on ? t->anchorX : L->idleX, ay = on ? t->anchorY : L->idleY;
    const float kx = on ? t->knobX : ax, ky = on ? t->knobY : ay;
    const float a = on ? 1.0f : 0.35f;
    DrawCircleV((Vector2){ ax, ay }, L->stickRadius, Fade(BLACK, 0.18f * a));
    Ring(ax, ay, L->stickRadius, 3.0f, Fade(WHITE, 0.55f * a));
    DrawCircleV((Vector2){ ax, ay }, L->stickBaseRadius, Fade(WHITE, 0.10f * a));
    Ring(ax, ay, L->stickBaseRadius, 2.0f, Fade(WHITE, 0.35f * a));
    DrawCircleV((Vector2){ kx, ky }, L->knobRadius, Fade(DARKGRAY, 0.85f * a));
    Ring(kx, ky, L->knobRadius, 2.0f, Fade(WHITE, 0.70f * a));
    SprintIcon(ax, ay - L->stickRadius - L->sprintIconGap - L->sprintIconRadius, L->sprintIconRadius, on ? t->sprintFill : 0.0f,
               on && t->sprint, v->exhausted, on ? 1.0f : 0.35f);

    // The fire pad: held it fires, and the finger that holds it also turns the camera. An outer ring (where a finger counts as
    // being on it) and a button disc with a bullet in it.
    DrawCircleV((Vector2){ L->firePad.x, L->firePad.y }, L->firePad.r, Fade(BLACK, 0.18f));
    Ring(L->firePad.x, L->firePad.y, L->firePad.r, 3.0f, Fade(WHITE, 0.55f));
    DrawCircleV((Vector2){ L->firePad.x, L->firePad.y }, L->fireInnerRadius, Fade(t->fireHeld ? RED : DARKGRAY, 0.65f));
    Ring(L->firePad.x, L->firePad.y, L->fireInnerRadius, 2.0f, Fade(WHITE, 0.70f));
    BulletIcon(L->firePad.x, L->firePad.y - 8.0f, Fade(WHITE, 0.90f));
    const char *attack = v->weaponIsGun ? "FIRE" : (v->weaponIsMelee ? "SWING" : "PUNCH");
    DrawText(attack, (int)(L->firePad.x - (float)MeasureText(attack, 12) / 2.0f), (int)L->firePad.y + 20, 12, WHITE);

    RoundButton(L->jump, t->jumpHeld ? BLUE : DARKGRAY, "JUMP", t->jumpHeld);

    if (v->useShown) RoundButton(L->use, v->useLocked ? MAROON : DARKGREEN, v->useLabel, t->useHeld);
    if (v->weaponsOwned > 1) RoundButton(L->swap, DARKGRAY, "SWAP", t->swapHeld);
    if (v->slideShown) RoundButton(L->slide, v->sliding ? SKYBLUE : (v->tooTiredToSlide ? ORANGE : (Color){ 40, 120, 170, 255 }), v->sliding ? "SLIDING" : "SLIDE", false);
    if (v->reloadShown) {
        // Grey while a reload can start, maroon when the magazine is empty, amber (and not pressable) while one runs, with a ring filling round it.
        const Color rc = v->reloading ? (Color){ 170, 125, 25, 255 } : (v->magEmpty ? MAROON : DARKGRAY);
        RoundButton(L->reload, rc, v->reloading ? "LOADING" : "RELOAD", v->reloading);
        const float f = v->reloadFrac < 0.0f ? 0.0f : (v->reloadFrac > 1.0f ? 1.0f : v->reloadFrac);
        if (v->reloading && f > 0.0f) DrawRing((Vector2){ L->reload.x, L->reload.y }, L->reload.r + 3.0f, L->reload.r + 7.0f, -90.0f, -90.0f + 360.0f * f, 48, YELLOW);
    }
    if (v->hasTorch) {
        const bool blink = v->torchOn && v->torchLow && ((int)(GetTime() * 4.0) & 1);
        const Color lc = v->torchOn ? (blink ? RED : (v->torchLow ? ORANGE : (Color){ 190, 160, 30, 255 })) : (v->torchFlat ? MAROON : DARKGRAY);
        RoundButton(L->light, lc, v->torchOn ? "LIGHT\nON" : (v->torchFlat ? "FLAT" : "LIGHT"), false);
        // The charge as a ring round the button.
        const float f = v->torchFraction < 0.0f ? 0.0f : (v->torchFraction > 1.0f ? 1.0f : v->torchFraction);
        if (f > 0.0f) DrawRing((Vector2){ L->light.x, L->light.y }, L->light.r + 3.0f, L->light.r + 7.0f, -90.0f, -90.0f + 360.0f * f, 48,
                               v->torchLow ? ORANGE : (Color){ 255, 233, 160, 255 });
    }
}
