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
//   left half   a FLOATING move stick: it appears where the thumb lands, anywhere in the left half. It is quick: full walking
//               speed at half the ring (stickFullAt). Pushing it all the way out, in ANY direction, is a sprint (the sprint icon
//               above the stick lights up); how fast each direction goes is the player controller's business, not the stick's.
//   right side  a FIXED FIRE pad that also aims: the finger that holds it fires, and moving that finger turns the camera by
//               exactly how far it moves (TC_AIM_DRAG, the default: the original feel), so you can shoot and aim with one thumb.
//               (TC_AIM_JOYSTICK is the other way, kept for a settings screen: the pad as a stick, turning faster the further out it
//               is, with its knob drawn following the finger.) A finger anywhere else on the right half that is not on a button is
//               the LOOK finger. JUMP, USE (PICKUP), SWAP, LIGHT, SLIDE and RELOAD are round buttons: JUMP and PICKUP above the pad,
//               SLIDE beside it, RELOAD under SLIDE at the bottom edge (a placeholder spot, like the rest of the layout). DIVE sits
//               where SLIDE does: the two are never there at once (a slide starts from a forward sprint on the ground, a dive from
//               the air after a sprinting jump). Leaning has no buttons: it is automatic (player_controller.c).
// A finger keeps the job it had when it went down until it lifts: a look finger that slides over JUMP does not jump, and a
// stick finger that wanders into the right half is still the stick.

#define TC_MAX_TOUCHES 8
#define TC_MAX_BLOCKED 10
#define TC_SCREEN_W    800.0f
#define TC_SCREEN_H    450.0f

typedef struct { int id; float x, y; } TcTouch;

// Every control is a circle, as in the layout sketch (a placeholder: the HUD is to become customisable, and this table is the one
// place that will have to give way to saved positions).
typedef struct {
    float    stickRadius;                   // 87: the ring, and how far the knob travels
    float    stickBaseRadius;               // 56: the inner disc drawn in the middle of the ring (drawing)
    float    knobRadius;                    // 30 (drawing)
    float    deadzone;                      // 0.04 of the radius: the knob has to leave this before anything moves
    float    stickFullAt;                   // 0.50 of the radius: the knob distance that is already full speed (the rest of the ring is a safe margin and the sprint's run-up)
    UiRect   stickZone;                     // where a stick may begin: the left half, below the STATS button
    float    stickMinX, stickMaxX, stickMinY, stickMaxY;   // where the stick's centre may sit, so the whole ring stays on screen
    float    sprintOn, sprintOff;           // push beyond 0.92 of the radius to start a sprint (any direction), drop under 0.80 to end it
    float    sprintIconGap, sprintIconRadius;   // the icon sits this many pixels above the top of the ring; its size
    float    idleX, idleY;                  // where the faint "stick goes here" ring is drawn when no finger is down
    UiCircle firePad;                       // the outer ring: where a finger counts as being on the pad
    float    fireInnerRadius;               // 42: the button disc inside it (drawing): it is the aim knob, and follows the finger
    float    lookGain;                      // 2.0: canvas pixels the camera turns per pixel the LOOK finger moves
    float    fireLookGain;                  // 1.0: the same for the fire finger in TC_AIM_DRAG (the original feel: the camera follows the finger one to one)
    float    aimDeadzone, aimFullAt;        // TC_AIM_JOYSTICK: 0.10 of the pad's radius is dead (a plain tap does not drift), 0.85 is full turn rate
    float    aimCurve;                      // 1.6: the response between them is t^1.6, so the first half of the throw is fine aim and the end is a fast turn
    float    aimRatePx;                     // 720: canvas pixels per second the camera turns at full deflection (about 3.6 rad/s, 200 degrees a second)
    UiCircle jump, use, swap, light, slide, reload;
    UiCircle dive;                          // the same place as slide: tapped in the air after a sprinting jump
} TcLayout;
const TcLayout *Tc_Layout(void);

// How the fire pad aims. A setting that does not exist yet (the settings screen is to come): until it does the game passes
// TC_DEFAULT_AIM_MODE every frame. Drag is 0, so a context that says nothing gets the original feel.
typedef enum { TC_AIM_DRAG = 0, TC_AIM_JOYSTICK } TcAimMode;
#define TC_DEFAULT_AIM_MODE TC_AIM_DRAG

// What the game says about this frame; it decides which fingers do what.
typedef struct {
    bool   locked;            // a door is opening, or the player is dead: nothing the fingers do has any effect
    bool   walkOnly;          // the inventory is open: only the stick (and the sprint) work, the right half is for taps
    bool   useAvailable;      // there is a door or a pickup in reach: the USE button exists
    bool   swapAvailable;     // more than one weapon owned: the SWAP button exists
    bool   lightAvailable;    // a flashlight is carried: the LIGHT button exists
    bool   slideAvailable;    // the player is sprinting or sliding: the SLIDE button exists
    bool   reloadAvailable;   // the gun in hand has room in its magazine and rounds in the bag: the RELOAD button exists
    bool   diveAvailable;     // airborne after a sprinting jump: the DIVE button exists (in SLIDE's place)
    UiRect blocked[TC_MAX_BLOCKED];    // regions that only take taps (STATS, INV, the popup, the diagnostic rows): no finger there
    int    blockedCount;               // becomes a stick, a look finger, or a press
    float  dt;                 // seconds since the last frame: the joystick aim turns the camera at a RATE, so it needs the time (0 turns nothing)
    TcAimMode aimMode;         // how the fire pad aims (0 = TC_AIM_DRAG)
} TcContext;

typedef struct {
    float moveX, moveY;                     // -1..1; moveY is -1 for forward, like W
    float lookDX, lookDY;                   // canvas pixels the camera should turn this frame: the look finger's movement (times lookGain), plus the fire finger's
    float aimX, aimY;                       // TC_AIM_JOYSTICK: the pad's deflection after the dead zone and curve, -1..1 (0 in TC_AIM_DRAG)
    bool  sprint;                           // the stick is out at the rim, in any direction
    bool  fireHeld, jumpHeld, useHeld, swapHeld;
    bool  lightPressed, slidePressed, reloadPressed, divePressed;   // once, on the frame the finger lands
    // for drawing
    bool  stickActive;
    float anchorX, anchorY, knobX, knobY;
    float sprintFill;                       // 0..1: how close the knob is to starting a sprint
    bool  aimActive;                        // TC_AIM_JOYSTICK only: a finger is on the fire pad and its knob follows it (in TC_AIM_DRAG the disc stays put)
    float aimKnobX, aimKnobY;               // where the pad's knob is drawn: under the finger, held inside the ring
    bool  lookActive;
} TcOut;

typedef enum { TC_ROLE_NONE = 0, TC_ROLE_IGNORE, TC_ROLE_STICK, TC_ROLE_LOOK, TC_ROLE_FIRE, TC_ROLE_JUMP, TC_ROLE_USE, TC_ROLE_SWAP,
               TC_ROLE_LIGHT, TC_ROLE_SLIDE, TC_ROLE_RELOAD, TC_ROLE_DIVE } TcRole;
typedef struct { bool used, fresh; int id, role; float lastX, lastY; } TcSlot;
typedef struct { TcSlot slot[TC_MAX_TOUCHES]; float anchorX, anchorY; bool sprinting, prevWalkOnly; } TcState;

void Tc_Init(TcState *s);
// Once per frame, with every finger that is down (an empty list when none is: that is what lifts them).
void Tc_Update(TcState *s, const TcTouch *touches, int count, const TcContext *ctx, TcOut *out);

#endif
