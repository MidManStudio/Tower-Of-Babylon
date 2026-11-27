// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/input.md, section "tests/input_headless.c"
// ============================================================================
// The touch controls and the HUD layout without a screen: the floating stick (quick to respond), the sprint at the rim in any
// direction, the fixed fire pad that also aims (by dragging, the default, or as a joystick), the look finger, the DIVE button, the buttons that exist only when they have something to act on, the tap-only regions, the locked
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
static void ResetJoy(float dt) { Reset(); C.dt = dt; C.aimMode = TC_AIM_JOYSTICK; }      // the pad as a stick, for its own tests
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
    Step1(1, 200 + 0.25f * R, 300);
    Check(Near(O.moveX, (0.25f - L->deadzone) / (L->stickFullAt - L->deadzone), 0.01f) && Near(O.moveY, 0.0f, 0.01f), "a quarter of the way out is a part-speed walk (the dead zone is taken off, and full speed is stickFullAt, not the rim)");
    Step1(1, 200 + L->stickFullAt * R, 300);
    Check(Near(O.moveX, 1.0f, 0.01f), "at stickFullAt of the ring the walk is already at full speed");
    Step1(1, 200 + 0.5f * R, 300);
    Check(Near(O.moveX, 1.0f, 0.01f) && !O.sprint, "half way out is full speed, and not yet a sprint");
    Check(Near(O.anchorX, 200, 0.01f), "the stick stays where it appeared while the finger moves");
    Step1(1, 200, 300 - R);
    Check(Near(O.moveY, -1.0f, 0.01f) && Near(O.moveX, 0.0f, 0.01f), "pushed up the screen is forward: y is -1, like W");
    Step1(1, 400, 100);
    Check(Near(sqrtf(O.moveX * O.moveX + O.moveY * O.moveY), 1.0f, 0.01f) && Near(hypotf(O.knobX - O.anchorX, O.knobY - O.anchorY), L->stickRadius, 0.01f), "far past the ring: full speed, and the knob stops on the ring");
    Step1(1, 200 + 0.5f * L->deadzone * R, 300);
    Check(O.moveX == 0.0f && O.moveY == 0.0f, "inside the dead zone: nothing");
    Step(NULL, 0);
    Check(!O.stickActive && O.moveX == 0.0f, "the finger lifts: the stick is gone");

    printf("The stick is quick (a short drag moves you)\n");
    Reset();
    Step1(1, 200, 300);
    Step1(1, 200 + 15.0f, 300);
    Check(O.moveX > 0.25f, "a 15 px drag, the 'I have to drag before anything moves' case, is already over a quarter speed");
    Step1(1, 200 + 30.0f, 300);
    Check(O.moveX > 0.6f, "and 30 px is most of the way to full speed");
    Check(L->deadzone <= 0.05f && L->stickFullAt <= 0.6f && L->stickFullAt > L->deadzone, "the dead zone is small and full speed comes well inside the ring");
    Step1(1, 200 + 4.0f, 300 + 4.0f);
    Check(O.moveX > 0.0f && O.moveY > 0.0f, "a drag of about 6 px in a diagonal already moves in that direction");

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

    printf("Sprint: the knob at the rim, in any direction\n");
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
    Step1(1, 200, 300);
    Step1(1, 200 + R, 300);
    Check(O.sprint, "the rim sideways is a sprint (omni sprint)");
    Step1(1, 200, 300);
    Step1(1, 200 - R, 300);
    Check(O.sprint, "the rim to the left too");
    Step1(1, 200, 300);
    Step1(1, 200, 300 + R);
    Check(O.sprint, "the rim backwards too");
    Step1(1, 200 + 0.72f * R, 300 - 0.72f * R);
    Check(O.sprint, "the rim ahead and to the right (45 degrees) too");
    Step1(1, 200 + R, 300);
    Check(O.sprint, "swinging the knob round the rim keeps it: only coming in ends it");
    Step1(1, 200 + 0.7f * R, 300);
    Check(!O.sprint, "pulled in to 0.70 of the radius, sideways: it ends");
    Step1(1, 200, 300 - R);
    Step(NULL, 0);
    Check(!O.sprint && O.sprintFill == 0.0f, "lifting the finger ends it");

    printf("The move stick glides (smooth: it ramps up and coasts to rest, by time, not by frame)\n");
    {
        const float dt = 1.0f / 60.0f;
        // Landing and pushing out: the value chases the finger at stickGlideRate ring radii per second.
        Reset(); C.dt = dt;
        Step1(1, 200, 300);
        Step1(1, 200 + R, 300);                                                   // the finger is at the rim at once; the stick is not
        const float first = O.moveX;
        Check(first > 0.0f && first < 0.9f && Near(hypotf(O.knobX - O.anchorX, O.knobY - O.anchorY), L->stickGlideRate * dt * R, 0.02f * R), "a thumb thrown to the rim moves the stick by one frame's worth, not all the way");
        Check(O.sprint, "the sprint still starts the moment the finger reaches the rim (it follows the finger, not the glide)");
        float prev = first; int mono = 1, frames = 1; float full = -1.0f;
        for (; frames < 12; ++frames) { Step1(1, 200 + R, 300); if (O.moveX + 0.0001f < prev) mono = 0; prev = O.moveX; if (full < 0.0f && O.moveX >= 0.999f) full = (float)frames; }
        Check(mono, "it only ever speeds up while the finger holds still at the rim");
        Check(full > 0.0f && full * dt <= (L->stickFullAt / L->stickGlideRate) + 2.0f * dt, "and is at full speed after about stickFullAt / stickGlideRate seconds (a few frames)");
        Check(Near(hypotf(O.knobX - O.anchorX, O.knobY - O.anchorY), R, 0.01f), "the knob, which is drawn from the same value, ends on the ring");

        // The same elapsed time gives the same stick at 20, 40, 100 and 200 frames a second: it is a rate per second, not a step per frame.
        const float fps[4] = { 20.0f, 40.0f, 100.0f, 200.0f }; const int frs[4] = { 1, 2, 5, 10 };       // each is 0.05 s
        float seen[4];
        for (int k = 0; k < 4; ++k) {
            Reset(); C.dt = 1.0f / fps[k];
            Step1(1, 200, 300);
            for (int f = 0; f < frs[k]; ++f) Step1(1, 200 + R, 300);
            seen[k] = hypotf(O.knobX - O.anchorX, O.knobY - O.anchorY) / R;
        }
        Check(Near(seen[0], 0.6f, 0.01f) && Near(seen[1], 0.6f, 0.01f) && Near(seen[2], 0.6f, 0.01f) && Near(seen[3], 0.6f, 0.01f), "0.05 s after the thumb lands the stick is at 12 * 0.05 = 0.6 of the ring at every frame rate");

        // Lifting: the stick coasts to rest instead of stopping dead, and the sprint ends at once.
        Reset(); C.dt = dt;
        Step1(1, 200, 300);
        for (int f = 0; f < 20; ++f) Step1(1, 200 + R, 300);
        Check(O.sprint && Near(O.moveX, 1.0f, 0.01f), "(set up: at the rim, sprinting)");
        Step(NULL, 0);
        Check(!O.sprint, "the thumb lifts: the sprint ends on that frame");
        Check(O.stickActive && O.moveX > 0.0f && O.moveX < 1.0f + 0.001f, "but the stick is still drawn and still moving the player, a little less");
        prev = O.moveX; mono = 1; int n = 0;
        while (O.stickActive && n < 60) { Step(NULL, 0); if (O.moveX > prev + 0.0001f) mono = 0; prev = O.moveX; ++n; }
        Check(mono, "it only ever slows");
        Check(!O.stickActive && O.moveX == 0.0f && O.moveY == 0.0f, "and comes to rest");
        Check((float)n * dt <= (1.0f / L->stickReleaseRate) + 2.0f * dt && (float)n * dt >= 0.5f * (1.0f / L->stickReleaseRate), "in about 1 / stickReleaseRate seconds from the rim (a tenth to a fifth of a second), not instantly and not slowly");

        // A drag back in is also smoothed, but a finger that stays put leaves it alone.
        Reset(); C.dt = dt;
        Step1(1, 200, 300);
        for (int f = 0; f < 20; ++f) Step1(1, 200 + R, 300);
        Step1(1, 200, 300);                                                       // thumb snaps back to the middle
        Check(O.moveX > 0.0f, "a thumb that snaps back to the middle does not stop the player in one frame");
        for (int f = 0; f < 30; ++f) Step1(1, 200, 300);
        Check(O.moveX == 0.0f, "and the stick is at rest a moment later");

        // Swinging the stick from one side to the other passes through the middle.
        Reset(); C.dt = dt;
        Step1(1, 200, 300);
        for (int f = 0; f < 20; ++f) Step1(1, 200 + R, 300);
        float least = 1.0f; float ymax = 0.0f;
        for (int f = 0; f < 30; ++f) { Step1(1, 200 - R, 300); if (fabsf(O.moveX) < least) least = fabsf(O.moveX); if (fabsf(O.moveY) > ymax) ymax = fabsf(O.moveY); }
        Check(least < 0.15f && ymax < 0.01f && Near(O.moveX, -1.0f, 0.01f), "swinging the thumb from right to left glides across the middle in a straight line and ends full speed the other way");

        // A frame that took five seconds is not a reason to jump.
        Reset(); C.dt = dt;
        Step1(1, 200, 300);
        for (int f = 0; f < 20; ++f) Step1(1, 200 + R, 300);
        C.dt = 5.0f; Step(NULL, 0);
        Check(O.stickActive && O.moveX > 0.0f, "one very long frame does not stop the stick: the step is capped at a tenth of a second");

        // No frame time: the stick is the finger, exactly.
        Reset();
        Step1(1, 200, 300); Step1(1, 200 + R, 300);
        Check(Near(O.moveX, 1.0f, 0.001f), "with no frame time the stick follows the finger exactly (every older test relies on it)");
        Step(NULL, 0);
        Check(!O.stickActive && O.moveX == 0.0f, "and lifting stops it at once");

        // Locked: nothing moves, but the stick carries on being tracked, so unlocking does not start from nothing.
        Reset(); C.dt = dt;
        Step1(1, 200, 300);
        C.locked = true;
        for (int f = 0; f < 20; ++f) Step1(1, 200 + R, 300);
        Check(O.moveX == 0.0f && O.moveY == 0.0f && !O.sprint, "locked: no movement while the thumb is down");
        C.locked = false; Step1(1, 200 + R, 300);
        Check(Near(O.moveX, 1.0f, 0.01f), "unlocked: the stick is already where the thumb is");
    }

    printf("Fingers keep their jobs\n");
    Reset();
    Step1(1, 200, 300);
    Step2(1, 200, 300, 2, 100, 200);
    Check(O.stickActive && Near(O.moveX, 0.0f, 0.01f), "a second finger in the left half does nothing");
    Reset();
    Step1(5, 500, 100);
    Check(O.lookActive && O.lookDX == 0.0f && O.lookDY == 0.0f, "a finger landing on the right is the camera, and its first frame turns nothing");
    Step1(5, 510, 105);
    Check(Near(O.lookDX, 10 * L->lookGain, 0.01f) && Near(O.lookDY, 5 * L->lookGain, 0.01f), "then it turns by how far it moved, times the look gain");
    Check(L->lookGain >= 1.5f, "the look gain makes a short swipe turn the camera well (the 'rotating is hard' case)");
    Step1(5, L->jump.x, L->jump.y);
    Check(!O.jumpHeld && O.lookActive, "sliding the camera finger over JUMP does not press it");
    Step2(5, L->jump.x, L->jump.y + 10, 6, 450, 100);
    Check(Near(O.lookDY, 10 * L->lookGain, 0.01f) && O.lookActive, "a second free finger on the right does not take the camera over (the first still turns it by its own 10 px)");
    Reset();
    Step1(1, 200, 300);
    Step1(1, 600, 300);
    Check(O.stickActive, "a stick finger that wanders into the right half is still the stick");

    printf("The fire pad: fixed, and it aims by dragging (the default, the original feel)\n");
    {
        const float PR = L->firePad.r;
        Reset();
        Check(C.aimMode == TC_AIM_DRAG && TC_DEFAULT_AIM_MODE == TC_AIM_DRAG, "a context that says nothing about it gets the drag, and so does the game's default");
        Check(Near(L->fireLookGain, 1.0f, 0.0001f), "the fire finger turns the camera one to one with the finger, as before (the look finger has its own, higher gain)");
        Step1(3, L->firePad.x, L->firePad.y);
        Check(O.fireHeld && !O.lookActive && O.lookDX == 0.0f && O.lookDY == 0.0f, "a finger on the pad fires and turns nothing on the first frame");
        Step1(3, L->firePad.x + 14, L->firePad.y - 12);
        Check(O.fireHeld && Near(O.lookDX, 14 * L->fireLookGain, 0.01f) && Near(O.lookDY, -12 * L->fireLookGain, 0.01f), "moving that finger turns the camera by how far it moved, while it fires");
        Check(!O.aimActive && O.aimX == 0.0f && O.aimY == 0.0f, "and nothing is a joystick: the disc stays where it is, it does not follow the finger");
        Step1(3, L->firePad.x + 14, L->firePad.y - 12);
        Check(O.lookDX == 0.0f && O.fireHeld, "held still it turns nothing (a drag, not a stick: the camera does not keep turning)");
        Step1(3, 560, 120);
        Check(O.fireHeld && Near(O.lookDX, (560 - (L->firePad.x + 14)) * L->fireLookGain, 0.01f), "dragged far off the pad it keeps firing and keeps turning");
        Step(NULL, 0);
        Check(!O.fireHeld && O.lookDX == 0.0f, "lifted: it stops");
        Reset();
        Step1(4, 450, 200);
        Step1(4, L->firePad.x, L->firePad.y);
        Check(!O.fireHeld, "a camera finger that slides onto the pad does not fire");
        Reset();
        Step1(3, L->firePad.x, L->firePad.y);
        Step2(3, L->firePad.x, L->firePad.y, 4, L->firePad.x + 5, L->firePad.y + 5);
        Check(O.fireHeld && O.lookDX == 0.0f, "a second finger on an occupied pad is ignored");
        Reset(); C.locked = true;
        Step1(3, L->firePad.x + PR, L->firePad.y);
        Check(!O.fireHeld && O.lookDX == 0.0f, "locked (a door, or dead): no fire and no turn");
    }

    printf("The fire pad as a joystick (the other aim mode, for a settings screen)\n");
    {
        const float PR = L->firePad.r;
        const float dt = 1.0f / 60.0f;
        const float tHalf = powf((0.5f - L->aimDeadzone) / (L->aimFullAt - L->aimDeadzone), L->aimCurve);     // the response at half the throw
        ResetJoy(dt);
        Step1(3, L->firePad.x, L->firePad.y);
        Check(O.fireHeld && !O.lookActive && O.aimActive, "a finger on the pad fires");
        Check(Near(O.aimKnobX, L->firePad.x, 0.01f) && Near(O.aimKnobY, L->firePad.y, 0.01f) && O.aimX == 0.0f && O.aimY == 0.0f && O.lookDX == 0.0f, "at the centre: the knob is in the middle and nothing turns");
        Step1(3, L->firePad.x + 0.5f * PR, L->firePad.y);
        Check(Near(O.aimKnobX, L->firePad.x + 0.5f * PR, 0.01f) && Near(O.aimKnobY, L->firePad.y, 0.01f), "dragged out, the knob follows the finger");
        Check(Near(O.aimX, tHalf, 0.01f) && Near(O.aimY, 0.0f, 0.01f) && O.aimX > 0.2f && O.aimX < 0.6f, "half way out is a gentle turn to the right (the response curve: fine aim near the middle)");
        Check(Near(O.lookDX, O.aimX * L->aimRatePx * dt, 0.01f) && O.lookDX > 0.0f && Near(O.lookDY, 0.0f, 0.01f), "which turns the camera at that rate times the frame's time");
        const float held = O.lookDX;
        Step1(3, L->firePad.x + 0.5f * PR, L->firePad.y);
        Check(Near(O.lookDX, held, 0.001f), "and it keeps turning while the finger is held still: a joystick, not a drag");
        Step1(3, L->firePad.x, L->firePad.y - 0.5f * PR);
        Check(O.aimY < 0.0f && Near(O.aimX, 0.0f, 0.01f) && O.lookDY < 0.0f, "dragged up it turns up");
        Step1(3, L->firePad.x + 0.04f * PR, L->firePad.y);
        Check(O.fireHeld && O.aimX == 0.0f && O.lookDX == 0.0f, "inside the pad's dead zone it fires and does not drift (a plain tap does not turn the camera)");
        Step1(3, L->firePad.x + 3.0f * PR, L->firePad.y);
        Check(Near(O.aimX, 1.0f, 0.001f) && Near(hypotf(O.aimKnobX - L->firePad.x, O.aimKnobY - L->firePad.y), PR, 0.01f), "far off the pad: full turn rate, and the knob stops on the ring");
        Check(Near(O.lookDX, L->aimRatePx * dt, 0.01f), "full deflection turns at aimRatePx a second");
        Step1(3, 560, 120);
        Check(O.fireHeld, "dragged far off the pad it keeps firing");
        Step(NULL, 0);
        Check(!O.fireHeld && !O.aimActive && O.lookDX == 0.0f, "lifted: it stops, and the camera with it");
        ResetJoy(dt);
        Step1(3, L->firePad.x + 0.9f * PR, L->firePad.y);
        Check(O.aimX > 0.8f, "the pad does not float: a finger landing near its edge is already a hard turn (the centre stays put)");
        ResetJoy(0.0f);                                    // no frame time, no turn
        Step1(3, L->firePad.x + 0.8f * PR, L->firePad.y);
        Check(O.fireHeld && O.aimX > 0.0f && O.lookDX == 0.0f, "(with no frame time given nothing turns: the rate needs it)");
        ResetJoy(dt);
        Step1(4, 450, 200);
        Step1(4, L->firePad.x, L->firePad.y);
        Check(!O.fireHeld, "a camera finger that slides onto the pad does not fire");
        ResetJoy(dt);
        Step1(3, L->firePad.x, L->firePad.y);
        Step2(3, L->firePad.x, L->firePad.y, 4, L->firePad.x + 5, L->firePad.y + 5);
        Check(O.fireHeld && O.lookDX == 0.0f, "a second finger on an occupied pad is ignored");
        ResetJoy(dt); C.locked = true;
        Step1(3, L->firePad.x + PR, L->firePad.y);
        Check(!O.fireHeld && O.aimX == 0.0f && O.lookDX == 0.0f, "locked (a door, or dead): no fire and no turn");
    }


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

    printf("The DIVE button (in the air after a sprinting jump, in SLIDE's place)\n");
    Reset(); C.diveAvailable = true;
    Step1(11, L->dive.x, L->dive.y);
    Check(O.divePressed && !O.slidePressed && !O.lookActive && !O.fireHeld && !O.jumpHeld, "a finger on DIVE presses it, on the frame it lands, and it is not a look finger, a slide or a jump");
    Step1(11, L->dive.x, L->dive.y);
    Check(!O.divePressed, "and only on that frame (a tap, not a hold)");
    Reset();
    Step1(11, L->dive.x, L->dive.y);
    Check(!O.divePressed && O.lookActive, "no DIVE button on the ground: the touch is a look finger and the area is whole");
    Reset(); C.diveAvailable = true; C.slideAvailable = true;
    Step1(11, L->dive.x, L->dive.y);
    Check(O.divePressed && !O.slidePressed, "(the two share a spot and are never up together; if they were, the dive wins)");
    Reset(); C.diveAvailable = true; C.walkOnly = true;
    Step1(11, L->dive.x, L->dive.y);
    Check(!O.divePressed, "none while only walking is allowed");
    Reset(); C.diveAvailable = true; C.locked = true;
    Step1(11, L->dive.x, L->dive.y);
    Check(!O.divePressed, "nor while locked (a door, or dead)");
    Reset(); C.diveAvailable = true;
    Step1(1, 200, 300); Step1(1, 200, 300 - R);
    Step2(1, 200, 300 - R, 11, L->dive.x, L->dive.y);
    Check(O.divePressed && O.stickActive && Near(O.moveY, -1.0f, 0.01f), "the stick finger stays on the stick through the tap: the dive reads the stick as it is then");
    Check(Near(L->dive.x, L->slide.x, 0.001f) && Near(L->dive.y, L->slide.y, 0.001f) && Near(L->dive.r, L->slide.r, 0.001f), "DIVE sits where SLIDE does");

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
        const UiCircle round[] = { L->jump, L->use, L->swap, L->light, L->slide, L->reload, L->firePad };    // (DIVE is in SLIDE's place on purpose)
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
        Check(L->firePad.r >= 40.0f && L->jump.r >= 30.0f && L->use.r >= 30.0f && L->slide.r >= 30.0f && L->reload.r >= 28.0f && L->dive.r >= 30.0f && L->swap.r >= 24.0f && L->light.r >= 24.0f, "the controls are thumb-sized (a 60 px disc is about 9 mm on the phone)");
        Check(L->stickMinX - R >= 0.0f && L->stickMaxX + R <= 400.0f + 0.01f + 50.0f, "the stick's range keeps its ring on screen (and mostly in the left half)");
        const float iconTop = L->stickMinY - R - L->sprintIconGap - 2.0f * L->sprintIconRadius;
        Check(iconTop >= 0.0f && L->stickMaxY + R <= TC_SCREEN_H + 0.01f, "with room above it for the sprint icon and below for the ring");
        Check(L->idleX - R >= 0.0f && L->idleY + R <= TC_SCREEN_H && L->idleX >= L->stickMinX && L->idleY >= L->stickMinY && L->idleY <= L->stickMaxY, "the faint idle ring is a place the stick can really be");
        Check(L->stickBaseRadius < R && L->knobRadius < L->stickBaseRadius && L->fireInnerRadius < L->firePad.r, "the inner discs are inside their rings");
        Check(L->aimDeadzone > 0.0f && L->aimDeadzone < L->aimFullAt && L->aimFullAt <= 1.0f && L->aimCurve >= 1.0f && L->aimRatePx > 0.0f, "the aim joystick's tuning is in order (dead zone < full throw <= ring)");
        const UiRect p = HudLayout_StatsPanel(), x = HudLayout_StatsClose(), d = HudLayout_DiagToggle(), b = HudLayout_StatsButton();
        Check(p.x >= 0 && p.y >= 0 && p.x + p.w <= TC_SCREEN_W && p.y + p.h <= TC_SCREEN_H, "the popup is inside the screen");
        Check(x.x >= p.x && x.y >= p.y && x.x + x.w <= p.x + p.w && x.y + x.h <= p.y + p.h, "its X is inside it");
        Check(x.w >= 40.0f && x.h >= 30.0f, "and big enough to hit with a thumb");
        Check(d.x >= p.x && d.x + d.w <= p.x + p.w && d.y + d.h <= p.y + p.h && !UiRect_Overlaps(x, d), "the diagnostics switch is inside it and apart from the X");
        Check(b.w >= 60.0f && b.h >= 28.0f, "the STATS button is big enough to hit");
        int popupOnControl = 0;
        for (int i = 0; i < nr; ++i) if (UiCircle_OverlapsRect(round[i], p)) { ++popupOnControl; printf("      the popup covers: %s\n", rnames[i]); }
        Check(popupOnControl == 0, "the popup leaves every touch control free (you can still move, shoot and jump with it open)");
        const UiRect sb = HudLayout_StaminaBar();
        int barOnControl = 0;
        for (int i = 0; i < nr; ++i) if (UiCircle_OverlapsRect(round[i], sb)) { ++barOnControl; printf("      the stamina bar covers: %s\n", rnames[i]); }
        Check(sb.x >= 0 && sb.y >= 0 && sb.x + sb.w <= TC_SCREEN_W && sb.y + sb.h <= TC_SCREEN_H, "the always-on stamina bar is inside the screen");
        Check(barOnControl == 0 && !UiRect_Overlaps(sb, inv) && !UiRect_Overlaps(sb, b) && !UiRect_Overlaps(sb, p), "and clear of every control, the INV and STATS buttons and the popup");
        Check(sb.w >= 150.0f && sb.h >= 8.0f, "and big enough to read (the old bars were 5 and 6 px high)");
        Check(sb.x + sb.w <= 520.0f, "and clear of the weapon readout (right-aligned at x 620, about 100 wide)");
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
