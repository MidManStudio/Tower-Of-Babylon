# input

## Overview

Everything the player does with their hands comes through `src/input/`. Fingers, mouse and keyboard go in; one `InputFrame` of intentions comes out (move, look, sprint, jump, fire, use, swap, light, slide, reload), so `main.c` never asks raylib which key is down and never knows which platform it is on. The controls follow the Call of Duty Mobile arrangement:

- **Left half: a floating move stick.** It appears where the thumb lands, anywhere in the left half (kept far enough in that the whole ring is on screen), and stays put while the finger moves. Forward is up the screen.
- **Sprint is not a button.** An icon sits above the stick. Push the knob all the way out ahead and the icon turns yellow: you are sprinting. A ring around the icon fills as the knob nears the rim, so you can see how far to go. Sideways or backwards at the rim is not a sprint; once started it holds until the knob comes back under 80% or swings away from ahead (so it does not flicker at the edge). Orange means the push is there but the player is too tired.
- **Right side: a fire pad that also aims.** The finger on the pad fires, and moving that finger turns the camera, so one thumb shoots and aims. A finger on a free spot of the right half is the look finger. JUMP is a button; USE, SWAP, LIGHT, SLIDE and RELOAD appear only when they have something to act on (RELOAD: when the gun in hand has room in its magazine and rounds in the bag).
- **STATS** (top left) opens the stats popup (`docs/hud.md`); INV opens the inventory.

A finger keeps the job it had when it went down until it lifts: a look finger that slides over JUMP does not jump, a camera finger that slides onto the fire pad does not fire, a stick finger that wanders into the right half is still the stick.

Keyboard (web and desktop): W A S D, Shift sprint, Space jump, F fire, E use, Q swap, R reload, C or Left Ctrl slide, L light, I or Tab inventory, P stats. A mouse button held down counts as one more finger, so a PC browser plays like a phone. On the desktop build the mouse is locked to the window and turns the camera with no button held, and the left button fires.

## Which controls exist where (`docs/platforms.md`)

| Capability | Web | Android | Desktop |
|---|---|---|---|
| `TOWER_INPUT_TOUCH` (touch logic compiled, controls drawn) | yes | yes | no |
| `TOWER_INPUT_KEYBOARD` (keys read) | yes | no | yes |
| `TOWER_INPUT_POINTER_LOCK` (mouse look with no button) | no | no | yes |

The browser may be on a phone or a PC, so it decides by use: the touch controls are drawn until a key is pressed, are hidden while the keyboard is in use, and come back with the next real finger. A mouse never switches the scheme.

## Modules

### `ui_rect.h`
A rectangle and a circle in canvas pixels (the canvas is 800 x 450) with `Contains` and `Overlaps` (circle with circle, and circle with rectangle, for a layout made of round buttons). No raylib, so the touch logic, the HUD layout and their tests need only a C compiler.

### `touch_controls.h` and `touch_controls.c`
**What it does:** the touch controls as a state machine with no raylib in it. `Tc_Update(state, fingers, count, context, out)` runs once a frame. A finger that lands is given a job by where it lands and what the game says is available (a tap-only region, the stick, one of the buttons, the fire pad, the look finger, or nothing) and keeps it. The output: move vector (dead zone 0.08, then rescaled so the edge of the dead zone is 0), look movement in pixels, sprint, fire/jump/use/swap held, light, slide and reload as one-frame presses, and the numbers the HUD draws (stick anchor and knob, how close the knob is to a sprint, whether a camera finger is down). The layout is one `TcLayout` shared with the HUD.

**The layout (a placeholder, from the owner's sketch).** The HUD is to become customisable later, so these positions are not a design to protect: they are the one table `g_layout` in `touch_controls.c`, measured off a hand-drawn sketch (1920 x 1080, scaled by 5/12 to the canvas). Every control is a circle: the move stick's ring r 87 with an inner disc r 56 (idle at 128, 326), the sprint icon r 22 above it, PICKUP (the USE button) r 46 at 639, 172, JUMP r 37 at 760, 175 (above the aim joystick, out at the right edge), the fire pad r 71 at 724, 315 with a button disc r 42 and a bullet in it, SLIDE r 37 at 606, 357, RELOAD r 30 at 668, 408 (not in the sketch: the one free spot big enough, under SLIDE, 13 px clear of it and 7 px clear of the fire pad; the layout test checks it). The sketch has no SWAP, LIGHT or INV button, so they went to free spots: SWAP (r 30) at 632, 258 between PICKUP and the pad, LIGHT (r 32) at 700, 88 above them, INV (`inventory_ui.c`) moved to the top right corner, mirroring STATS. A small stray dot in the sketch near the top middle was ignored. The stick is bigger than before (ring 87, not 50), so the thumb travels farther for full speed; sprint still starts at 0.92 of the ring.

**The context** is how the game gates the controls, every frame: `locked` (a door is opening or the player is dead: everything zero, but the stick is still drawn and carries on when it unlocks), `walkOnly` (the inventory is open: only the stick works, the right half is for taps, and a camera finger that is down lets go at once and stays let go until it lifts), `useAvailable`, `swapAvailable`, `lightAvailable`, `slideAvailable`, `reloadAvailable` (a button that is not available is not there: a finger on its spot is the camera), and `blocked` (regions that only take taps: STATS, INV, the open popup, the diagnostic rows; a finger there does not become the stick or the camera, the tap is handled by the HUD).

**Decisions:** the fire finger aims because that is how one thumb fires and aims at once, and because the old split (a fire button, then lift and reach for the camera) is what makes a shooter hard on a phone. The stick's anchor does not follow the finger (that is the Call of Duty Mobile behaviour; a following stick drifts under the thumb). Sprint needs the rim *and* a direction within about 70 degrees of straight ahead, with a looser 45 degree cone to stay in it, because running backwards at 7.6 m/s through the rim is never what the thumb meant.

### `input.h` and `input.c`
**What it does:** the one place raylib is asked about the player. Collects the fingers (and the mouse as a finger), runs the touch logic, reads the keyboard and the locked mouse where the platform has them (each behind its capability, so the code for a device a platform lacks is not compiled there), applies the same `locked` and `walkOnly` rules to the keyboard and mouse, turns USE and SWAP into once-per-press (and reports `useHeld` too: the unlock console is worked by holding USE, so the keyboard's E is now read as held, not as a press), and reports `gesture` (a finger, click or key this frame: a browser opens the sound card only after one). `Input_NewTap` returns a finger that went down this frame, for the buttons that are taps (STATS, INV, the inventory screen).

**Menus:** `Input_Menu` (a step left or right, a confirm, whether anything touched the device) is what the character selection screen reads (`characters.md`); its keyboard reads are behind `TOWER_INPUT_KEYBOARD` like the rest, and it does not run the touch logic, so a finger on a menu never becomes a stick.

**Decisions:** it takes no game types, only the `InputContext` (the same struct the touch logic uses), so it can be tested and reused. The inventory and stats toggles from the keyboard are returned as flags; the buttons for them are taps handled where the screens are.

### `tests/input_headless.c`
**What it covers (82 checks):** the floating stick (appears under the thumb, stays put, forward is -y, full speed past the ring, dead zone, clamped near the edges, no stick above the zone or after 400 px), sprint (the ring fills, starts at 0.92 ahead, holds to 0.80, not sideways or backwards, ends when the knob swings round or the finger lifts), roles (second left finger ignored, the camera finger turns by exactly how far it moves, does not press JUMP in passing, a second camera finger is ignored), the fire pad (fires, aims, keeps firing off the pad, a camera finger sliding on does not fire, an occupied pad ignores a second finger), the buttons (only when available, light, slide and reload once per tap; no reload button when there is nothing to load or only walking is allowed), tap-only regions, `walkOnly` and `locked`, twelve fingers at once, and the layout (nothing outside the screen; no two controls overlapping, tested as circles and rectangles, not bounding squares; thumb-sized targets; the popup clear of every control; and the layout matching the sketch within 2 px, a block to delete when the HUD becomes customisable). Every pixel offset in the tests is a fraction of the stick radius, so retuning the layout does not break them. Five deliberate breakages of the logic were each caught, and so was moving JUMP onto the pad.

## Not verified

- Nothing here has been run on a phone. The logic is tested without a screen; the drawing, the thumb reach on a real 6.6 inch screen, the sizes of the targets and how the fire pad feels while aiming (the camera sensitivity while firing is the look sensitivity) need a person with the device. Positions and sizes are in `TcLayout` and `hud_layout.c`, one place each.
- The desktop path (`TOWER_INPUT_POINTER_LOCK`, the keyboard-only scheme) compiles and is checked by `tests/platform_check.sh`, but no desktop build exists to run it.
- A browser on a phone with a hardware keyboard attached would flip to the keyboard scheme on a key press and hide the touch controls until the next touch. Intended; not seen.

## Fixes and Problems

- The layout was redrawn from the owner's sketch (round buttons, a larger stick, JUMP above the aim joystick, PICKUP, SLIDE and the pad placed as drawn). The tests were rewritten in fractions of the stick radius and the layout checks now use real circle geometry. A round button has no corners: a finger on the corner of its bounding square is the camera.
- Replaces the touch code that was written inline in `main.c` (a fixed stick, a RUN toggle button with an idle timer, a shoot rectangle that fired but did not aim). The old behaviour that was kept: a finger's job is fixed when it lands, walking works with the inventory open, a locked game ignores everything.
