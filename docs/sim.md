# The fixed-step simulation (`main.c`, `src/input/input_latch.*`, `src/core/fixed_step.*`)

## Overview

The rules of the game run **30 times a second**, whatever the screen does. The screen is drawn at its own pace (60 or 144 frames a second); each frame the game asks `FixedStep_Advance` how many whole 1/30 s steps the elapsed time is worth, runs that many `SimTick`s, and draws the picture blended between the last two steps. This makes the game play the same at any frame rate, and it is the groundwork for multiplayer: a server stepping the same rules at the same rate agrees with the phone tick for tick (`design/06-multiplayer.md`). Before this the rules ran once per frame with a capped frame time.

## What runs where

**Every frame (`Frame()` in `main.c`):** reading input, UI taps (STATS, INV, the inventory screen, which change the inventory at once), **turning the camera** (`PlayerApplyLook` + `PlayerSyncCamera`: aim never waits for a step, a step later would feel like lag), the flashlight (its beam must follow the view, and its charge drains by time so the frame rate does not change it), sound, the screen effects that fade by the clock (the fade after a door, hit and kill markers, the heal flash), drawing.

**Every step (`SimTick()`):** the ammo pickups, door and pickup use, swapping, the light and slide buttons, `PlayerStep` (movement, stamina, sliding, falling, the camera's position), jumping, the doors' opening and the floor change, attacks (`Arsenal_Update`, shots, melee), projectiles, the monsters and their noise, the stair clock (`FloorGame_Tick`), blows on the player, death and respawn.

## Input between steps: `input_latch.h`

A press that comes and goes between two steps (a one-frame jump or fire tap at 144 fps against 30 steps) must not be lost. `SimCmd_Add` runs once per frame and keeps presses (jump, fire, use, swap, light, slide) until `SimCmd_Take` hands them to the next step and clears them; held things (the stick, sprint, the USE hold, fire held) are whatever they were last. A fire tap that is over before the step still fires once, as a press. Two steps in one frame: the first gets the presses, the second none.

## Drawing between steps

The picture is blended `alpha` (0 to 1, how far the next step has got) of the way from the start of the last step to its end:
- **The camera:** position blended from `g_prevEye` (set at the start of each step, and reset at every teleport so it never sweeps across the floor) to now; direction is always the latest, because aim is per frame. The picture is up to one step (33 ms) behind the rules for movement; extrapolating instead would put the camera into walls at sprint speed (0.25 m a step).
- **The monsters** (`Enemies_BeginTick`, `Enemies_SetRenderAlpha`): `prevPosition` per monster; for spiders the body and the hips blend, a planted foot stays on the floor (`prevPos`).
- **The shots** (`Projectiles_SetRenderLag`): a round is drawn that much of its flight behind where the step left it, never behind its muzzle.

## Decisions

- Aim per frame, rules per step: the standard split for a first-person game; the cost is that a tap waits up to a step (33 ms) for its effect.
- 30 Hz and at most 5 steps a frame (`SIM_MAX_STEPS_PER_FRAME`): a stall of more than 167 ms makes the game slow down for a moment instead of racing to catch up (the dropped steps are counted in `FixedStep.dropped`).
- The flashlight is deliberately *not* stepped: it is presentation (flicker, beam) with a charge that drains by time. It will need to be server state when the inventory is.

## Not verified

- Not run on a phone. Feel is the risk: input latency (a press waits up to one step), the blend (a step behind), the viewmodel (the arsenal's recoil and muzzle flash fade in steps of 1/30 s, so they tick at 30 Hz), doors animating in steps, the spiders' blend. Everything is compiled for web, Android and desktop, and each piece is tested, but main.c itself cannot be run here.
- `main.c`'s `SimTick` was moved from the old `Frame()` as it was, with the per-frame effects split out; there is no test that runs `main.c`. A replay of a recorded input through `SimTick` at two frame rates should be the next test, which needs `SimTick` to move out of `main.c` (see below).
- Inventory actions from the screen (drop, use, equip, load a battery) still act on the game at once, in the frame, not as commands in a step. Fine for one player; they become commands for the server.

## For multiplayer (the next stage)

The rules still live in `main.c` and in module globals (the enemies, the projectiles, the pickups): one simulation per process. To run on a server, `SimTick` and its state (player, arsenal, inventory, torch, doors) must move into a `src/sim` module with a `GameSim` struct and no raylib, and the commands must carry yaw and pitch (today the camera's yaw is read from the player, since it is turned in the frame). `SimCmd` is the shape of that command.

## Tests

`tests/sim_headless.c` (21 checks): the latch (every press kept, taken once, a fire tap kept as a press, a hold fires every step, a fresh command asks for nothing), the same 90 steps at 30, 60 and 144 frames a second (exactly the same place with the same input; one jump per jump press whatever the rate), look-per-frame plus step-per-tick equal to the old one-call update, the camera direction following at once while its position does not move, and the monsters' blend hooks. Five deliberate breakages (a press not latched, not cleared, a fire tap lost, look applied only in the step, BeginTick not remembering) were each caught; one needed a stronger test.
