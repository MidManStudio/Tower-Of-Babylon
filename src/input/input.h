// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/input.md, section "input.h and input.c"
// ============================================================================
#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

#include "raylib.h"
#include "touch_controls.h"

// The one place the game reads the player. Fingers, mouse and keyboard go in; one InputFrame of intentions comes out, so
// main.c never asks raylib which key is down and never knows which platform it is on. What each platform can do is the
// capability list in src/platform/platform.h (TOWER_INPUT_TOUCH, _KEYBOARD, _POINTER_LOCK); code for a device a platform does
// not have is not compiled there.
//
// Which controls are live is decided by the context the game passes in each frame (the same TcContext the touch logic
// uses): a locked game (a door opening, the player dead) gives an empty frame, an open inventory lets only walking through,
// a button with nothing to act on does not exist, and the regions that only take taps keep fingers off the stick and the camera.

typedef TcContext InputContext;

typedef enum { INPUT_SCHEME_TOUCH = 0, INPUT_SCHEME_KEYBOARD } InputScheme;

typedef struct {
    Vector2 move;                     // x right, y -1 forward (as W is)
    Vector2 look;                     // canvas pixels the camera should turn this frame
    bool    sprint;                   // wanted: the stick pushed out ahead, or Shift
    bool    jump;                     // try to jump this frame
    bool    fire;                     // held
    bool    use, swap, light, slide;  // once per press
    bool    inventoryToggle;          // the I or Tab key (the INV button is a tap: Input_NewTap)
    bool    statsToggle;              // the P key (the STATS button is a tap)
    bool    gesture;                  // a finger, a click or a key this frame: a browser opens the sound card only after one
    InputScheme scheme;               // what the player used last; the touch controls are drawn only for TOUCH
} InputFrame;

void Input_Init(void);
void Input_Update(const InputContext *ctx, InputFrame *out);           // once per frame

// A finger that went down this frame (or a mouse click when no finger is down): where. Call once per frame.
bool Input_NewTap(Vector2 *where);

// For drawing the touch controls.
const TcOut *Input_Touch(void);
InputScheme  Input_Scheme(void);

#endif
