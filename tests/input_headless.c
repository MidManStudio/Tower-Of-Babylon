// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/input.md, section "tests/input_headless.c"
// ============================================================================
// The touch controls and the HUD layout without a screen: the floating stick, the sprint at the rim, the fire pad that also
// aims, the look finger, the buttons that exist only when they have something to act on, the tap-only regions, the locked
// and walk-only states, and that no two controls sit on top of each other.
// Inside a macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DINPUT_HEADLESS_TEST -Isrc tests/input_headless.c src/input/touch_controls.c src/hud/hud_layout.c -lm -o input_headless
// Run:
//   ./input_headless
#ifdef INPUT_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hud/hud_layout.h"
#include "input/touch_controls.h"

static int g_failed = 0;
static void Check(int ok, const char *what) { printf("  %s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++g_failed; }
static int Near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

static TcState S;
static TcOut O;
static TcContext C;
static void Reset(void) { Tc_Init(&S); memset(&C, 0, sizeof C); memset(&O, 0, sizeof O); }
static void Step(const TcTouch *t, int n) { Tc_Update(&S, t, n, &C, &O); }
static void Step1(int id, float x, float y) { const TcTouch t = { id, x, y }; Step(&t, 1); }
static void Step2(int a, float ax, float ay, int b, float bx, float by) { const TcTouch t[2] = { { a, ax, ay }, { b, bx, by } }; Step(t, 2); }

int main(void)
{
    const TcLayout *L = Tc_Layout();
    const float R = L->stickRadius;                  // every pixel offset below is a fraction of it, so tuning the layout does not break the tests

    printf("The floating move stick\n");
    Reset();
    Step1(1, 200, 300);
    Check(O.stickActive && Near(O.anchorX, 200, 0.01f) && Near(O.anchorY, 300, 0.01f), "the stick appears exactly where the thumb lands");
    Check(O.moveX == 0.0f && O.moveY == 0.0f && !O.sprint, "untouched centre: no movement");
    Step1(1, 200 + 0.5f * R, 300);
    Check(Near(O.moveX, (0.5f - L->deadzone) / (1.0f - L->deadzone), 0.01f) && Near(O.moveY, 0.0f, 0.01f), "half way out to the right is a bit under half speed (the dead zone is taken off)");
    Check(Near(O.anchorX, 200, 0.01f), "the stick stays where it appeared while the finger moves");
    Step1(1, 200, 300 - R);
    Check(Near(O.moveY, -1.0f, 0.01f) && Near(O.moveX, 0.0f, 0.01f), "pushed up the screen is forward: y is -1, like W");
    Step1(1, 400, 100);
    Check(Near(sqrtf(O.moveX * O.moveX + O.moveY * O.moveY), 1.0f, 0.01f) && Near(hypotf(O.knobX - O.anchorX, O.knobY - O.anchorY), L->stickRadius, 0.01f), "far past the ring: full speed, and the knob stops on the ring");
    Step1(1, 203, 301);
    Check(O.moveX == 0.0f && O.moveY == 0.0f, "inside the dead zone: nothing");
    Step(NULL, 0);
    Check(!O.stickActive && O.moveX == 0.0f, "the finger lifts: the stick is gone");

    Reset();
    Step1(1, 10, 60);
    Check(Near(O.anchorX, L->stickMinX, 0.01f) && Near(O.anchorY, L->stickMinY, 0.01f), "a touch in the top left corner gives a stick moved in so its whole ring is on the screen");
    Reset();
    Step1(1, 395, 445);
    Check(Near(O.anchorX, L->stickMaxX, 0.01f) && Near(O.anchorY, L->stickMaxY, 0.01f), "and the same in the bottom right of the left half");
    Reset();
    Step1(1, 200, 20);
    Check(!O.stickActive, "above the stick zone (where the STATS button is) no stick starts");
    Reset();
    Step1(1, 399, 300);
    Check(O.stickActive, "the left half ends at x = 400: 399 is still the stick");
    Reset();
    Step1(1, 401, 300);
    Check(!O.stickActive && O.lookActive, "and 401 is the camera");

    printf("Sprint: the knob at the rim, ahead\n");
    Reset();
    Step1(1, 200, 300);
    Step1(1, 200, 300 - 0.80f * R);
    Check(!O.sprint && O.sprintFill > 0.4f && O.sprintFill < 1.0f, "0.80 of the radius ahead: not yet, and the icon's ring is part full");
    Step1(1, 200, 300 - 0.93f * R);
    Check(O.sprint && Near(O.sprintFill, 1.0f, 0.001f), "0.93 ahead: sprint, ring full");
    Step1(1, 200, 300 - 0.84f * R);
    Check(O.sprint, "easing back to 0.84 keeps the sprint (it does not flicker at the edge)");
    Step1(1, 200, 300 - 0.76f * R);
    Check(!O.sprint, "below 0.80 it ends");
    Step1(1, 200 + R, 300);
    Check(!O.sprint, "the rim sideways is not a sprint");
    Step1(1, 200, 300 + R);
    Check(!O.sprint, "the rim backwards is not a sprint");
    Step1(1, 200 + 0.72f * R, 300 - 0.72f * R);
    Check(O.sprint, "the rim ahead and to the right (45 degrees) is");
    Step1(1, 200 + R, 300);
    Check(!O.sprint, "swinging the knob round to the side ends it");
    Step1(1, 200, 300 - R);
    Step(NULL, 0);
    Check(!O.sprint && O.sprintFill == 0.0f, "lifting the finger ends it");

    printf("Fingers keep their jobs\n");
    Reset();
    Step1(1, 200, 300);
    Step2(1, 200, 300, 2, 100, 200);
    Check(O.stickActive && Near(O.moveX, 0.0f, 0.01f), "a second finger in the left half does nothing");
    Reset();
    Step1(5, 500, 100);
    Check(O.lookActive && O.lookDX == 0.0f && O.lookDY == 0.0f, "a finger landing on the right is the camera, and its first frame turns nothing");
    Step1(5, 510, 105);
    Check(Near(O.lookDX, 10, 0.01f) && Near(O.lookDY, 5, 0.01f), "then it turns by exactly how far it moved");
    Step1(5, L->jump.x, L->jump.y);
    Check(!O.jumpHeld && O.lookActive, "sliding the camera finger over JUMP does not press it");
    Step2(5, L->jump.x, L->jump.y + 10, 6, 450, 100);
    Check(Near(O.lookDY, 10, 0.01f) && O.lookActive, "a second free finger on the right does not take the camera over (the first still turns it by its own 10 px)");
    Reset();
    Step1(1, 200, 300);
    Step1(1, 600, 300);
    Check(O.stickActive, "a stick finger that wanders into the right half is still the stick");

    printf("The fire pad aims as well\n");
    Reset();
    Step1(3, L->firePad.x, L->firePad.y);
    Check(O.fireHeld && !O.lookActive, "a finger on the pad fires");
    Step1(3, L->firePad.x + 14, L->firePad.y - 12);
    Check(O.fireHeld && Near(O.lookDX, 14, 0.01f) && Near(O.lookDY, -12, 0.01f), "moving that finger turns the camera while it fires");
    Step1(3, 560, 120);
    Check(O.fireHeld, "dragged far off the pad it keeps firing");
    Step(NULL, 0);
    Check(!O.fireHeld, "lifted: it stops");
    Reset();
    Step1(4, 450, 200);
    Step1(4, L->firePad.x, L->firePad.y);
    Check(!O.fireHeld, "a camera finger that slides onto the pad does not fire");
    Reset();
    Step1(3, L->firePad.x, L->firePad.y);
    Step2(3, L->firePad.x, L->firePad.y, 4, L->firePad.x + 5, L->firePad.y + 5);
    Check(O.fireHeld && O.lookDX == 0.0f, "a second finger on an occupied pad is ignored");

    printf("Buttons exist only when they have something to do\n");
    Reset();
    Step1(7, L->jump.x, L->jump.y);
    Check(O.jumpHeld, "JUMP is always there");
    Reset();
    Step1(7, L->jump.x + L->jump.r * 0.9f, L->jump.y);
    Check(O.jumpHeld, "the whole of its disc counts, to the edge");
    Reset();
    Step1(7, L->jump.x + L->jump.r * 0.8f, L->jump.y + L->jump.r * 0.8f);
    Check(!O.jumpHeld && O.lookActive, "but a round button has no corners: the corner of its bounding square is the camera");
    Reset();
    Step1(8, L->use.x, L->use.y);
    Check(!O.useHeld && O.lookActive, "with nothing to use, USE is not a button: the finger is the camera");
    Reset(); C.useAvailable = true;
    Step1(8, L->use.x, L->use.y);
    Check(O.useHeld && !O.lookActive, "with a door or pickup in reach it is");
    Reset(); C.swapAvailable = true;
    Step1(8, L->swap.x, L->swap.y);
    Check(O.swapHeld, "SWAP with more than one weapon");
    Reset(); C.lightAvailable = true;
    Step1(9, L->light.x, L->light.y);
    Check(O.lightPressed, "LIGHT with a flashlight, on the frame the finger lands");
    Step1(9, L->light.x, L->light.y);
    Check(!O.lightPressed, "and only on that frame (a tap, not a hold)");
    Reset(); C.slideAvailable = true;
    Step1(9, L->slide.x, L->slide.y);
    Check(O.slidePressed, "SLIDE while sprinting, once per tap");
    Reset();
    Step1(9, L->slide.x, L->slide.y);
    Check(!O.slidePressed, "no SLIDE button when not sprinting");
    Reset(); C.reloadAvailable = true;
    Step1(10, L->reload.x, L->reload.y);
    Check(O.reloadPressed && !O.fireHeld && !O.jumpHeld, "RELOAD with a gun that can reload, on the frame the finger lands, and it is not a fire or a jump");
    Step1(10, L->reload.x, L->reload.y);
    Check(!O.reloadPressed, "and only on that frame (a tap, not a hold)");
    Reset();
    Step1(10, L->reload.x, L->reload.y);
    Check(!O.reloadPressed, "no RELOAD button when there is nothing to load (the touch falls through to the look finger)");
    Reset(); C.reloadAvailable = true; C.walkOnly = true;
    Step1(10, L->reload.x, L->reload.y);
    Check(!O.reloadPressed, "and none while only walking is allowed");

    printf("Tap-only regions\n");
    Reset(); C.blocked[0] = HudLayout_StatsButton() ; C.blockedCount = 1;
    Step1(1, 40, 20);
    Check(!O.stickActive, "a finger on the STATS button does not start the stick");
    Reset(); C.blocked[0] = (UiRect){ 698, 8, 92, 36 }; C.blockedCount = 1;
    Step1(1, 740, 26);
    Check(!O.lookActive, "nor does one on the INV button start the camera");
    Reset(); C.blocked[0] = HudLayout_StatsPanel(); C.blockedCount = 1;
    Step1(1, 300, 150);
    Check(!O.stickActive && !O.lookActive, "nor one on the open popup, even over where the stick zone is");
    Reset(); C.blocked[0] = HudLayout_StatsPanel(); C.blockedCount = 1;
    Step1(2, 150, 300);
    Check(O.stickActive, "a finger outside the popup starts the stick as usual");

    printf("The game's state\n");
    Reset(); C.walkOnly = true;
    Step1(1, 200, 300); Step1(1, 200, 300 - R);
    Check(Near(O.moveY, -1.0f, 0.01f), "with the inventory open, walking works");
    Reset(); C.walkOnly = true;
    Step1(2, 500, 100); Step1(2, 520, 110);
    Check(!O.lookActive && O.lookDX == 0.0f, "but the right half is only for taps: no camera");
    Reset();
    Step1(2, 500, 100); Step1(2, 520, 110);
    Check(O.lookActive, "(the camera finger was working before)");
    C.walkOnly = true;
    Step1(2, 540, 120);
    Check(O.lookDX == 0.0f && !O.lookActive, "the inventory opens: the camera finger lets go at once");
    C.walkOnly = false;
    Step1(2, 560, 130);
    Check(O.lookDX == 0.0f, "and it stays let go until that finger lifts");
    Reset(); C.walkOnly = true;
    Step1(3, L->firePad.x, L->firePad.y);
    Check(!O.fireHeld, "the fire pad does nothing with the inventory open");
    Reset(); C.locked = true;
    Step1(1, 200, 300); Step1(1, 200, 300 - R);
    Check(O.moveY == 0.0f && !O.sprint && O.stickActive, "locked (a door, or dead): no movement, but the stick is still drawn");
    C.locked = false;
    Step1(1, 200, 300 - R);
    Check(Near(O.moveY, -1.0f, 0.01f), "unlocked with the same finger: it carries on");
    {
        TcTouch many[12];
        for (int i = 0; i < 12; ++i) many[i] = (TcTouch){ 20 + i, 100.0f + (float)i, 300.0f };
        Reset();
        Step(many, 12);
        Check(O.stickActive && O.anchorX > 0.0f, "twelve fingers at once do not overrun the table: the first still gets the stick");
    }

    printf("Layout\n");
    {
        const UiRect inv = { 698, 8, 92, 36 };             // the INV button (src/inventory/inventory_ui.c), top right
        const UiCircle round[] = { L->jump, L->use, L->swap, L->light, L->slide, L->reload, L->firePad };
        const char *rnames[] = { "jump", "use", "swap", "light", "slide", "reload", "fire pad" };
        const int nr = (int)(sizeof round / sizeof round[0]);
        const UiRect flat[] = { inv, HudLayout_StatsButton() };
        const char *fnames[] = { "inv", "stats" };
        int overlaps = 0, outside = 0;
        for (int i = 0; i < nr; ++i) {
            if (round[i].x - round[i].r < 0 || round[i].y - round[i].r < 0 || round[i].x + round[i].r > TC_SCREEN_W || round[i].y + round[i].r > TC_SCREEN_H) { ++outside; printf("      outside the screen: %s\n", rnames[i]); }
            for (int j = i + 1; j < nr; ++j) if (UiCircle_Overlaps(round[i], round[j])) { ++overlaps; printf("      overlap: %s and %s\n", rnames[i], rnames[j]); }
            for (int j = 0; j < 2; ++j) if (UiCircle_OverlapsRect(round[i], flat[j])) { ++overlaps; printf("      overlap: %s and %s\n", rnames[i], fnames[j]); }
        }
        for (int j = 0; j < 2; ++j) if (flat[j].x < 0 || flat[j].y < 0 || flat[j].x + flat[j].w > TC_SCREEN_W || flat[j].y + flat[j].h > TC_SCREEN_H) { ++outside; printf("      outside the screen: %s\n", fnames[j]); }
        Check(outside == 0, "every control is inside the 800 x 450 screen");
        Check(overlaps == 0, "no two controls overlap (each is a place a thumb can land)");
        Check(UiRect_Overlaps(HudLayout_StatsButton(), L->stickZone) == 0, "the STATS button is outside the stick zone");
        Check(L->firePad.r >= 40.0f && L->jump.r >= 30.0f && L->use.r >= 30.0f && L->slide.r >= 30.0f && L->reload.r >= 28.0f && L->swap.r >= 24.0f && L->light.r >= 24.0f, "the controls are thumb-sized (a 60 px disc is about 9 mm on the phone)");
        Check(L->stickMinX - R >= 0.0f && L->stickMaxX + R <= 400.0f + 0.01f + 50.0f, "the stick's range keeps its ring on screen (and mostly in the left half)");
        const float iconTop = L->stickMinY - R - L->sprintIconGap - 2.0f * L->sprintIconRadius;
        Check(iconTop >= 0.0f && L->stickMaxY + R <= TC_SCREEN_H + 0.01f, "with room above it for the sprint icon and below for the ring");
        Check(L->idleX - R >= 0.0f && L->idleY + R <= TC_SCREEN_H && L->idleX >= L->stickMinX && L->idleY >= L->stickMinY && L->idleY <= L->stickMaxY, "the faint idle ring is a place the stick can really be");
        Check(L->stickBaseRadius < R && L->knobRadius < L->stickBaseRadius && L->fireInnerRadius < L->firePad.r, "the inner discs are inside their rings");
        const UiRect p = HudLayout_StatsPanel(), x = HudLayout_StatsClose(), d = HudLayout_DiagToggle(), b = HudLayout_StatsButton();
        Check(p.x >= 0 && p.y >= 0 && p.x + p.w <= TC_SCREEN_W && p.y + p.h <= TC_SCREEN_H, "the popup is inside the screen");
        Check(x.x >= p.x && x.y >= p.y && x.x + x.w <= p.x + p.w && x.y + x.h <= p.y + p.h, "its X is inside it");
        Check(x.w >= 40.0f && x.h >= 30.0f, "and big enough to hit with a thumb");
        Check(d.x >= p.x && d.x + d.w <= p.x + p.w && d.y + d.h <= p.y + p.h && !UiRect_Overlaps(x, d), "the diagnostics switch is inside it and apart from the X");
        Check(b.w >= 60.0f && b.h >= 28.0f, "the STATS button is big enough to hit");
        int popupOnControl = 0;
        for (int i = 0; i < nr; ++i) if (UiCircle_OverlapsRect(round[i], p)) { ++popupOnControl; printf("      the popup covers: %s\n", rnames[i]); }
        Check(popupOnControl == 0, "the popup leaves every touch control free (you can still move, shoot and jump with it open)");
    }

    printf("The layout is the one in the sketch\n");
    {
        // Measured off the sketch (canvas pixels). A placeholder: the HUD is to become customisable, then this block goes.
        const struct { const char *name; UiCircle c; } want[] = {
            { "fire pad", { 724.4f, 314.8f, 70.6f } }, { "jump", { 759.8f, 175.0f, 36.9f } }, { "pickup", { 639.0f, 172.5f, 45.6f } }, { "slide", { 606.2f, 357.1f, 36.6f } },
        };
        const UiCircle got[] = { L->firePad, L->jump, L->use, L->slide };
        for (int i = 0; i < 4; ++i)
            Check(Near(got[i].x, want[i].c.x, 2.0f) && Near(got[i].y, want[i].c.y, 2.0f) && Near(got[i].r, want[i].c.r, 2.0f), want[i].name);
        Check(Near(L->idleX, 128.3f, 2.0f) && Near(L->idleY, 326.5f, 2.0f) && Near(R, 87.0f, 1.0f) && Near(L->stickBaseRadius, 56.0f, 1.0f), "move stick: ring and inner disc");
        Check(Near(L->idleY - R - L->sprintIconGap - L->sprintIconRadius, 194.8f, 2.0f) && Near(L->sprintIconRadius, 21.9f, 1.5f), "sprint icon above the stick");
        Check(L->jump.y < L->firePad.y - L->firePad.r && L->jump.x > L->firePad.x && L->jump.x > L->use.x, "JUMP is above the aim joystick, out at the right edge, beyond PICKUP");
    }

    printf("\n%s (%d failed)\n", g_failed ? "FAILED" : "all passed", g_failed);
    return g_failed ? 1 : 0;
}

#endif // INPUT_HEADLESS_TEST
