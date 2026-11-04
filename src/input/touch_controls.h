// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/input.md, section "touch_controls.h and touch_controls.c"
// ============================================================================
#ifndef TOUCH_CONTROLS_H
#define TOUCH_CONTROLS_H

#include <stdbool.h>

#include "ui_rect.h"

// The touch controls as a state machine, with no raylib in it: the fingers on the screen go in, what the player is doing
// comes out. The layout (the Call of Duty Mobile arrangement):
//   left half   a FLOATING move stick: it appears where the thumb lands, anywhere in the left half. Pushing it all the way
//               out, ahead, is a sprint (the sprint icon above the stick lights up).
//   right side  a FIRE pad that also aims: the finger that holds it fires, and moving that finger turns the camera, so you
//               can shoot and aim with one thumb. A finger anywhere else on the right half that is not on a button is the
//               LOOK finger. JUMP, USE, SWAP, LIGHT and SLIDE are buttons.
// A finger keeps the job it had when it went down until it lifts: a look finger that slides over JUMP does not jump, and a
// stick finger that wanders into the right half is still the stick.

#define TC_MAX_TOUCHES 8
#define TC_MAX_BLOCKED 10
#define TC_SCREEN_W    800.0f
#define TC_SCREEN_H    450.0f

typedef struct { int id; float x, y; } TcTouch;

typedef struct {
    float    stickRadius;                   // 50: how far the knob travels
    float    knobRadius;                    // 22 (drawing)
    float    deadzone;                      // 0.08 of the radius
    UiRect   stickZone;                     // where a stick may begin: the left half, below the STATS button
    float    stickMinX, stickMaxX, stickMinY, stickMaxY;   // where the stick's centre may sit, so the whole ring stays on screen
    float    sprintOn, sprintOff;           // push beyond 0.92 of the radius to start a sprint, drop under 0.80 to end it
    float    sprintConeOn, sprintConeOff;   // and it must be ahead: within about 70 degrees of straight up (45 once started)
    float    sprintIconGap;                 // the icon sits this many pixels above the top of the ring
    float    idleX, idleY;                  // where the faint "stick goes here" ring is drawn when no finger is down
    UiCircle firePad;
    UiRect   jump, use, swap, light, slide;
} TcLayout;
const TcLayout *Tc_Layout(void);

// What the game says about this frame; it decides which fingers do what.
typedef struct {
    bool   locked;            // a door is opening, or the player is dead: nothing the fingers do has any effect
    bool   walkOnly;          // the inventory is open: only the stick (and the sprint) work, the right half is for taps
    bool   useAvailable;      // there is a door or a pickup in reach: the USE button exists
    bool   swapAvailable;     // more than one weapon owned: the SWAP button exists
    bool   lightAvailable;    // a flashlight is carried: the LIGHT button exists
    bool   slideAvailable;    // the player is sprinting or sliding: the SLIDE button exists
    UiRect blocked[TC_MAX_BLOCKED];    // regions that only take taps (STATS, INV, the popup, the diagnostic rows): no finger there
    int    blockedCount;               // becomes a stick, a look finger, or a press
} TcContext;

typedef struct {
    float moveX, moveY;                     // -1..1; moveY is -1 for forward, like W
    float lookDX, lookDY;                   // canvas pixels moved by the look finger (and the fire finger) this frame
    bool  sprint;                           // the stick is out at the rim, ahead
    bool  fireHeld, jumpHeld, useHeld, swapHeld;
    bool  lightPressed, slidePressed;       // once, on the frame the finger lands
    // for drawing
    bool  stickActive;
    float anchorX, anchorY, knobX, knobY;
    float sprintFill;                       // 0..1: how close the knob is to starting a sprint
    bool  lookActive;
} TcOut;

typedef enum { TC_ROLE_NONE = 0, TC_ROLE_IGNORE, TC_ROLE_STICK, TC_ROLE_LOOK, TC_ROLE_FIRE, TC_ROLE_JUMP, TC_ROLE_USE, TC_ROLE_SWAP,
               TC_ROLE_LIGHT, TC_ROLE_SLIDE } TcRole;
typedef struct { bool used, fresh; int id, role; float lastX, lastY; } TcSlot;
typedef struct { TcSlot slot[TC_MAX_TOUCHES]; float anchorX, anchorY; bool sprinting, prevWalkOnly; } TcState;

void Tc_Init(TcState *s);
// Once per frame, with every finger that is down (an empty list when none is: that is what lifts them).
void Tc_Update(TcState *s, const TcTouch *touches, int count, const TcContext *ctx, TcOut *out);

#endif
