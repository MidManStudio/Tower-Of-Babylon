# Player: health, hit effects, screen effects and input

## Overview

The player has 100 health. Enemies that touch them take health away, push them back, flash the screen red and shake the camera. As health falls a dark ring closes in from the edges of the screen. At zero health the screen fades out and the player comes back at the start of the same floor with full health and a fresh set of enemies. This file also covers the touch input in `main.c` and the frame graph overlay, because both were changed together while chasing the camera glitching.

## Modules

### `player_types.h` and `player_controller.c`

`PlayerController` gained `health` (the shared `HealthComponent`, whose `invulnerabilityTimer` is the grace period), `hitFlash` and `shake` (both 1 right after a hit, fading to 0), `shakeClock` and `knock` (knockback velocity on the floor plane).

- `DamagePlayer(player, amount, from)`: does nothing and returns false while the grace period runs or the player is dead. Otherwise it takes the health, starts a `PLAYER_HIT_GRACE_SECS` (0.9 s) grace period, sets the flash and shake to 1, and starts the knockback straight away from `from` at `PLAYER_KNOCKBACK_SPEED` (7.5 m/s). A blow from exactly the player's own position pushes backwards from where they are looking.
- Knockback decays at `PLAYER_KNOCK_DECAY` (6 per second), which moves the player about 1.2 m. It goes through the same wall, stair and obstacle tests as walking, one axis at a time, so a push into a wall stops 0.45 m short of it and slides along it.
- The flash fades in about 0.33 s and the shake in 0.25 s. The shake is added to the camera's yaw and pitch only (up to 0.035 and 0.025 rad); the player's real aim does not move.
- `HealPlayer(player, amount)` adds health up to the maximum. It returns false and does nothing when the player is dead, already at full health, or the amount is not positive. A medkit uses it (see `inventory.md`).
- `ResetPlayerHealth` gives full health and clears the effects. `TeleportPlayer` also stops any knockback.

### `screen_fx.h` and `screen_fx.c` (new, in `src/ui/`)

- **Vignette.** One 256 by 256 radial gradient texture, clear in the middle and opaque black at the rim, made at start-up with `GenImageGradientRadial`. `ScreenFx_VignetteStrength(hp fraction)` is 0 at 75% health and above and rises on a smoothstep curve to 1 at no health. A hit adds up to 0.3 for a moment, so the edges darken on every hit.
- **Drawing.** The texture is drawn scaled around the screen centre, 3.4 times the screen size at strength 0 (only a faint edge shows) down to 1.0 times at strength 1 (a small clear hole in the middle), with opacity 0.35 to 1. Below 30% health the scale pulses by 3% at about 1 beat per second.
- **Red flash.** A full-screen red rectangle at up to 40% opacity, scaled by `hitFlash`.
- **Health bar.** Bottom centre, green to red, with the number.
- **Crosshair and hit marker.** A small dot marks the centre of the screen, where shots go. When a shot lands, four short white ticks flash around it for 0.25 s; when it kills, larger red ticks flash for 0.5 s. `GameSim_Step` compares the hit and kill totals (the monsters' plus the swarm's) with the last step's and posts `SIMEV_HIT` / `SIMEV_KILL`; `main.c` sets the markers from those events.
- **Frame graph.** `FrameGraph_Push(frameMs, lookPx)` once a frame and `FrameGraph_Draw` in the top-right corner. The top strip is time per frame (bars green up to 18.5 ms, yellow up to 34 ms, red above, with guide lines at 16.7 and 33.3 ms); the bottom strip is how far the look input moved that frame. Text under it gives average and worst frame time and the worst look step over the last 120 frames, then how long the game's own code took in the worst frame, split into input, simulation (`sim`), enemies (`ai`), 3D drawing (`3d`), 2D drawing (`ui`) and `EndDrawing` (`end`). It is a diagnostic. Tapping the FPS counter (top left) turns it, the depth and MSAA readout and the light counts on and off; a finger that goes down on that counter does not start the move stick. It is on at start so the next screenshot carries the numbers. `main.c` also shows "depth N bit  msaa M" under the FPS counter, which is what the browser actually gave the page (see `dungeon.md`, "Phone glitches"); tapping that line switches between the high precision shader and raylib's default, to compare the two (see `lighting.md`).
- `ScreenFx_DrawStaminaBar(x, y, w, h, fraction, exhausted, recoverFraction, time)`: the thin bar under the health bar. Blue when well, amber below a quarter, pulsing red while exhausted with a tick at the recovery level.

### Wiring (`GameSim_Step` in `src/sim/game_sim.c`; the drawing in `main.c`)

- Each step, after enemies update, `EnemiesAttackPlayer` is asked for a blow and `DamagePlayer` applies it.
- At zero health movement, looking, jumping and shooting stop, any door in progress is cancelled, the screen fades to black over 1.2 s and "YOU DIED" appears. After 2.6 s `RespawnPlayer` restores health, puts the player in front of the down door of the current floor, respawns that floor's enemies and fades back in. The tower is not regenerated.
- Draw order: 3D view (floor, lights, stairs, enemies, pickups, projectiles, then the hands and the slash trail after a depth clear), health effects, the green flash after a medkit, door fades, death fade, door button, joystick and buttons, text, health bar, frame graph, the INV button or the inventory screen.
- The inventory screen and its button take touches before the look and button code does: while it is open, new fingers on the right half only tap the screen, look, jump and attack are off, and the move stick still works. See `inventory.md`.

### Look input (touch)

The old code decided what a touch was by where it was on the screen, on every frame. That caused three bugs that all show up as a glitchy camera while turning:

1. The JUMP and SHOOT buttons sit where the right thumb rests. A look swipe that crossed a button stopped turning for that frame, pressed the button, and then restarted its movement from scratch.
2. A second finger anywhere on the right half became a second camera finger. The code subtracted positions of two different fingers from each other, giving a huge jump every frame.
3. Raw swipe speed went straight into the camera, so one noisy frame was one violent turn.

Now each finger is followed by its id (`GetTouchPointId`). The finger that started on the left is the joystick and the one that started on the right, not on a button, is the camera, for as long as they stay down. A look finger can cross the buttons freely. Further fingers on the right can press buttons but cannot steal the camera. The look step is capped at `MAX_LOOK_PX` (90 canvas pixels) per frame. A finger that lifts gives up its job. The mouse path is unchanged.

`SetConfigFlags(FLAG_MSAA_4X_HINT)` is called before `InitWindow`. It smooths polygon edges and the thin corner lines, which crawl visibly while the view turns. A phone browser may silently refuse it; nothing breaks if it does.

### Movement, sprinting and stamina (`player_controller.c`)

The player has a velocity on the floor plane (`moveVel`, m/s) that speeds up and slows down instead of jumping to full speed.

- **Speeds.** Walking `PLAYER_WALK_SPEED` 4.5 m/s, sprinting `PLAYER_SPRINT_SPEED` 7.6 m/s. A partial push of the stick walks proportionally slower (half a push, half the speed) and a diagonal is never faster than straight. Left, right and backwards are slower than forward: see "Omnidirectional movement" below.
- **Acceleration.** `PLAYER_ACCEL` 30 m/s^2 speeding up (walking speed in about 0.15 s), `PLAYER_DECEL` 40 stopping (a fifth of a second to stand still), a third as much in the air (`PLAYER_AIR_CONTROL`), so a jump keeps its direction. Collision is as before, an axis at a time, but a wall now also takes the speed along that axis away, so the player slides along it and does not stay "pushing".
- **Stamina.** 0 to 200 (`PLAYER_STAMINA_MAX`, doubled from 100; every threshold below is written as a share of the bar through `STAMINA_SCALE`, so the shape of the curves did not change). Sprinting needs the sprint wanted (`wantSprint`, set by `main.c` each frame), the stick pushed out in any direction (omni sprint: see below), not aiming down the sights and not leaning, the player on the ground and not exhausted, and more than `STAMINA_SPRINT_MIN` (30, 15% of the bar) to start. It costs 17 a second, so a full bar is about twelve seconds (it was six), and a sprint that has started carries on to zero. A jump costs 12 and a slide 18, unchanged, so each is now half as large a bite out of the bar. Stamina starts coming back after a second without effort, at 15 a second, also unchanged: **a bar twice the size with the same regeneration takes about twice as long to refill**, which is the balance consequence of the doubling (`STAMINA_REGEN` is the knob if it feels too slow).
- **Tired is gradual.** Speed and acceleration are multiplied by a fatigue factor of 1 at 120 stamina (60% of the bar) or more, falling smoothly (a smoothstep, no sudden change at 120) to 0.45 at zero (`STAMINA_FRESH`, `FATIGUE_MIN_MUL`, `PlayerFatigue`). Walking is 4.5 m/s fresh, about 3.3 at 60 stamina and 2.0 at zero. A sprint is slowed by the same factor, so a sprint gets slower as the bar empties, not only when it is gone. The player is also slow to get going in proportion.
- **Exhausted.** When stamina reaches zero the player is exhausted: the factor is a further 10% lower, a sprint cannot start, a jump is a weak hop, the view heaves, and stamina comes back at 70% of the usual rate. It ends at 60 stamina (`STAMINA_EXHAUST_RECOVER`, 30% of the bar), which after the one second delay is about 6.7 seconds of rest, measured (the factor is then 0.73, and keeps rising as stamina does).
- **View.** The field of view widens by up to 6 degrees as a sprint builds (`SPRINT_FOV_KICK`) and eases back; the head bobs by a few centimetres with distance walked (more when sprinting, none standing still); the first person hands bob with real speed (`PlayerSpeed`, up to 1.5 times the walking amount).
- `PlayerTryJump` replaces the jump `main.c` did by hand: a full jump costs stamina, a weak hop (70% of the speed) costs none and is all there is when exhausted or short.
- **Controls (`main.c`).** A RUN button (450, 225, 100 by 70, left of the USE button) toggles running with a tap, so the right thumb can go back to looking. It turns itself off when the stick is let go for 0.4 s, when exhausted, during a door sequence or when dead. It shows RUN (grey), RUNNING (green) while it is actually sprinting, and TIRED (amber) when exhausted. On a keyboard hold Shift. A stamina bar sits under the health bar: blue, amber below a quarter, and a pulsing red while exhausted with a white tick at the level where it ends.

### Sliding (`player_controller.c`)

A burst for getting out of the way. Tap SLIDE (or `C` or Left Ctrl) while running. The button appears only while the player is sprinting (and while sliding), at (570, 150) beside the LIGHT button; it is orange when there is too little stamina, and a tap then says "TOO TIRED TO SLIDE".

- **Start.** `PlayerTrySlide` needs a sprint in progress, the ground under the feet, at least `STAMINA_SLIDE_COST` (18, about a second of sprinting) in hand, no exhaustion, no slide cooling down, and a speed of at least 2 m/s. It takes the 18 stamina at once and nothing more: while the slide runs there is no sprint drain and no regeneration.
- **Speed.** It starts at `PLAYER_SLIDE_SPEED_MUL` (1.25) times the sprint speed, 9.5 m/s fresh, times the fatigue factor (a tired player's slide is slower), and runs down along `end + (start - end) * (1 - t)^1.5` to `PLAYER_SLIDE_END_SPEED` (3.5 m/s) over `PLAYER_SLIDE_SECS` (0.85 s). That covers 5.0 m, which is what the test measures: a burst, not a way to travel. The slide sets the velocity directly instead of accelerating toward a wanted one, and goes through the same sliding collision as walking.
- **Steering.** The slide goes the way the player was going. Holding the stick turns it by at most `PLAYER_SLIDE_STEER` (1.4 rad/s).
- **View.** The camera drops `PLAYER_SLIDE_DROP` (25 cm), eased at 12 a second, the head bob is smoothed away, and the field of view keeps its sprint kick.
- **Noise.** A slide is heard at `PLAYER_SLIDE_NOISE` (14 m): louder than walking, quieter than a sprint's 20.
- **End.** After 0.85 s, when a wall has slowed it below 1.5 m/s, or when the feet leave the floor. Jumping out of a slide is allowed and ends it, keeping the speed. After it ends there is a 0.45 s cooldown (`PLAYER_SLIDE_COOLDOWN`); the player carries on sprinting if the run is still held.
- Tests: `movement_headless.c`, "Sliding" (13 checks plus the distance printed: 5.02 m against the curve's 5.02 m).
- Not done: no hands or weapon pose for a slide, no invulnerability while sliding (it dodges by getting out of the way, not by a grace period), enemy hit boxes do not shrink, and the characters' slide perks (`design/02-characters.md`) are not there. The slide is not a dive (`design/04-mechanics-and-systems.md`).

### Omnidirectional movement (`player_controller.c`)

Walking, sprinting and sliding work in every direction. The stick's direction is split into forward/back and left/right, and each has its own top speed as a share of the forward one: `PLAYER_STRAFE_MUL` 0.85 for left and right, `PLAYER_BACKPEDAL_MUL` 0.70 backwards (the user asked for left, right and back to be slower; the numbers are placeholders, Black Ops 6 publishes none). The two axes are scaled separately and the length of what comes out is the speed, so the multiplier is an ellipse: continuous all the way round (`PlayerDirectionMul(forward, right)` returns it for a unit direction; the test walks the compass in one degree steps and finds no jump bigger than 0.01), 0.93 on the forward diagonal, 0.78 on the backward one. Sprint speed is the same multiplier on 7.6 m/s (sideways 6.5, backwards 5.3), walking the same on 4.5 (3.8 and 3.2). A sprint costs the same stamina in any direction. A slide goes the way the sprint was going and starts at its speed times the same multiplier, so a sideways slide is shorter than a forward one. The stick no longer needs a forward cone to sprint (`input.md`).

This is inside `PlayerStep` and `PlayerTrySlide`, which the netcode's `NetSim_Step` also runs, so a client's prediction and the server agree as long as both are built from this source (the wire format did not change). Anything that rebuilds the server must rebuild the clients with it.

### Aiming down sights (ADS) (`player_controller.c`, `game_sim.c`, `viewmodel.c`)

Holding the fire pad (or F, or the left mouse button) with a gun in hand aims down the sights, as Call of Duty Mobile's fire button does; there will be a setting for it later (`docs/design/04-mechanics-and-systems.md`), so for now it is always so. The rules (`GameSim_Step`) set `player.wantAim` when a gun is in hand, fire is held (a tap does not aim: `SimCmd.fireHeld`), the gun is not reloading and has been raised at least 0.6 of the way, and nothing is locking the player (a door, death, the inventory). Fists and the pipe have no sights.

- **The weapon's own numbers** are in `weapons.mdix`: `ads_fov` (pistol 52, shotgun 54, rifle 46 degrees; the normal view is `PLAYER_BASE_FOV` 60), `ads_secs` (0.14, 0.22, 0.20 s from the hip to fully aimed), `ads_move` (the share of the walking speed left: 0.80, 0.65, 0.60) and `ads_spread` (the share of the spread left: 0.50, 0.75, 0.50). All placeholders to feel out. They are copied into the player only while it is at the hip, so swapping guns while aimed cannot jump the view.
- **The blend.** `aimBlend` runs 0 to 1 at `1 / adsSecs` a second and back out at 1 / (0.6 `adsSecs`) (`PLAYER_ADS_OUT_SHARE`); everything uses its smoothstep, `PlayerAimAmount`. A dead player does not aim.
- **What it changes.** The field of view goes from 60 to the gun's (`PlayerViewFov`, the sprint kick still added); the look sensitivity is multiplied by the ratio of the tangents of the half fields of view (0.72 for the rifle), so the crosshair crosses the world at the same speed zoomed or not; the top speed is multiplied by `adsMoveMul`; a sprint ends at once and cannot start while `wantAim` is set (so holding fire while running walks you); a shot's spread is `GameSim_SpreadDeg`, the gun's spread times `ads_spread` by the blend. The hands: the gun and both hands slide to the middle of the view (0.13 across for the pistol, 0.15 and 0.034 up for the long guns, `ADS_SLIDE_*` in `viewmodel.c`, `Viewmodel_BuildAimed`), the sway calms to 30%, and the hip crosshair fades to a quarter.
- **Between steps.** The blend moves in 30 Hz steps, so `main.c` blends the previous and current values by the frame's alpha (`GameSim.prevAim`) for the field of view and the pose.
- **Not done:** the first person hands are drawn with the same projection as the world, so zooming also magnifies the gun a little (a separate viewmodel field of view would fix it); no scope overlays, no per-weapon sight models, no ADS-in-slide special handling (ADS while sliding works, and a slide cannot start while fire is held, because holding fire ends the sprint it needs).

### Leaning (`player_controller.c`, `game_sim.c`)

Lean left or right while a gun is in hand, near a wall or a corner only (the user's answer: "when you are near walls, corners only"; the original note said "when holding a gun"). Held, not toggled: two buttons (`input.md`) or Z and X. There are no lean buttons anywhere else.

- **Availability.** `player.leanNearWall` is true when `NearestWallDist` (a wall tile, corners included, because a corner is a wall tile's corner) says a wall is within `PLAYER_LEAN_NEAR_WALL` (1 m) of the body; a body is never closer than 0.45. The rules add the gun and the player on foot (`GameSim_LeanButtonsShown`); the controller adds the ground and no slide. Anywhere else a held lean does nothing and an existing lean eases out (even with the button held), as it does when the gun is put away.
- **The lean.** `leanBlend` moves from -1 (left) to +1 (right) in `PLAYER_LEAN_SECS` (0.15 s). The eye moves sideways by up to `PLAYER_LEAN_SHIFT` (0.4 m) and down `PLAYER_LEAN_DIP` (6 cm), and the view rolls `PLAYER_LEAN_ROLL` (10 degrees): the camera's `up` vector tilts toward the lean (`PlayerViewUp`). The body does not move, so the player stays where the collision puts it.
- **It cannot go through a wall.** Each step the controller walks along both sides in 5 cm steps (`LeanRoom`) and stores the room to the left and right before the eye would come within `PLAYER_LEAN_CAM_CLEAR` (0.40 m: the near plane is 0.25 and the corners of the view reach about 0.39) of any wall; the camera moves by the lean times the smaller of the shift and the room. Leaning toward a wall you stand 0.6 m from moves 0.2 m.
- **While leaning** the top speed is `PLAYER_LEAN_MOVE_MUL` (0.4: a crawl), and a sprint ends and cannot start. Shots leave the leaned camera, as the aim comes from it. No leaning in the air or in a slide.
- **Not done:** the lean is not seen by monsters (the design doc defers "partly visible on the leaning side"), has no hands pose of its own (the hands are part of the view and tilt with it), and no sound.

### Netcode overlap (for the other chat)

`NetCmd` carries yaw, pitch, the stick and three button bits (sprint, slide, jump). The omni movement is inside the shared `PlayerStep`, so prediction and the server already agree. **ADS and lean are not in the wire yet.** `NetSim_Step` leaves `wantAim` and `wantLean` at 0, so online a player's ADS and lean are neither slowed nor seen by the server, and a leaned camera fires from an eye position the server does not know about. When the netcode takes these over it needs two things: a lean field (2 bits) and an ADS bit in `NetCmd` and in `NetSim_Step` (set `wantAim`, `adsFov`/`adsSecs`/`adsMoveMul`, `wantLean` from them), and the server's shot origin from the leaned eye. Nothing in `src/net/` was touched.

### `movement_headless.c`

Links the real controller on an open floor: one frame after a push the speed is small and it takes about a third of a second to reach walking speed; stopping takes a moment; diagonal, half-push and backward walking; the sprint speed, its drain per second, the field of view widening and narrowing; sprinting sideways and backwards at their slower speeds, no sprint standing or with little stamina; a full bar lasts 10.5 to 13 s and then the player is exhausted, slow and unable to sprint though sprint is held; an exhausted player is slower to pick up speed; rest does nothing for a second and exhaustion ends after 5.5 to 8 s at the recovery level; regeneration waits, then returns at the set rate and stops at the top; a tired player is a little slower; jumps cost stamina and are weak when exhausted or short, not in the air, not when dead, and keep their speed in the air; running into a wall stops at the clearance with no speed into it and diagonal pushes slide; head bob when walking and none standing, heaving when exhausted; teleport stops the player; a new player has full stamina. Omni movement: the direction multiplier (1 ahead, the strafe share to the side, the backpedal share behind, no jump going round the compass), every direction's sprint speed and stamina cost, a slide in any direction starting slower sideways and backwards. ADS: no ADS unless wanted, the blend's timing in and out, the field of view, the walking speed, the look sensitivity ratio, the sprint ending and not starting, a weapon with no zoom set changing nothing, a dead player not aiming. Leaning: nothing in the open, available near a wall, the timing, the eye's sideways shift and dip and the tilt (a unit `up` square to the view), the body staying put, the clamp against a wall at 0.6 m and 0.46 m (the eye never within 0.4 m), walking away from a wall ending it, the lean's speed, no sprint while leaning, none in the air, a teleport upright, a corner counting. Build line in the file header. Whole file sits inside `MOVEMENT_HEADLESS_TEST`.

### `health_headless.c`

Links the real player controller, staircase and screen effect code with stubbed draw calls. It checks damage, the grace period, knockback (direction, distance, stopping at a wall, a blow from the player's own position), death and reset, the vignette curve and the frame graph statistics. Build line in the file header. Whole file sits inside `HEALTH_HEADLESS_TEST`.

## Fixes and Problems

- A fresh enemy or spider started with its attack timer at zero, so the first one to reach the player hit instantly. They now start in wind-up. Found by the attack test.
- Omni movement, ADS and lean were added together (round 2). A test caught the left hand sliding the wrong way when the gun moved to the middle of the view (the left hand is the right one mirrored, so its x runs the other way). The first draft of the lean used a clearance of 0.28 m from walls; the near plane is 0.25 and the corners of the view reach 0.39 m, so it is 0.40.
- Not verified on a device: the ADS and lean numbers (field of view, times, speeds, the 10 degree tilt), whether the tilted camera is comfortable, the hands' ADS offsets (never rendered), and the 0.85 and 0.70 direction shares. They are placeholders in `player_controller.h` and `weapons.mdix`.
- Not verified: how the vignette, flash and shake look on the phone, and whether the phone's browser grants MSAA. Everything above is covered only by the headless tests.

### What to read off the frame graph

- A steady row of green bars at or under the 16.7 ms line: pacing is fine, so any remaining glitch is in what is drawn or in input.
- A tall bar with small numbers in the "our code" line: the frame was held up outside the game (browser, GPU, Android). A tall bar with one big number there: that stage is the cost.
- Bars jumping between green and yellow or red while turning: frames are late. Tell me the numbers.
- Tall blue look bars next to short ones while turning slowly: input spikes. The cap limits the damage but the cause is still worth knowing.
- Check Android battery saver. At low battery it can hold a phone below 60 FPS whatever the game does.

## Weapons and the HUD

What the player holds, the SWAP and USE buttons and the pickup messages are described in `docs/weapons.md`. The only change to this file's systems is that the old attack button no longer always fires projectiles: it punches, swings or fires, depending on the weapon in hand.

## Tuning

All in one place so they are easy to change (hit marker timing is `HIT_MARKER_SECS` and `KILL_MARKER_SECS` in `main.c`): `PLAYER_MAX_HP`, `PLAYER_HIT_GRACE_SECS`, `PLAYER_KNOCKBACK_SPEED`, `PLAYER_KNOCK_DECAY` in `player_controller.h`; `ENEMY_WINDUP_SECS`, `ENEMY_COOLDOWN_SECS`, `CYLINDER_DAMAGE` in `enemy_manager.h`; `SPIDER_DAMAGE`, `SPIDER_WINDUP_SECS`, `SPIDER_COOLDOWN_SECS`, `SPIDER_REACH` in `spider.h`; the vignette constants at the top of `screen_fx.c`; `MAX_LOOK_PX`, `DEATH_FADE_SECS`, `DEATH_TOTAL_SECS` in `main.c`.

## Known limits

- No healing and no pickups yet, so health only goes down until the player dies.
- Enemy attacks have no wind-up animation, only the 0.35 s delay, so the player gets no visual warning.
- Dying does not cost anything and does not reset the tower.
- The vignette is one stretched texture, so it is an ellipse that follows the screen's shape, not a circle.

### The cost of running

Running is meant to buy a lead and then take it back. Two things do that.

- **Speed.** Cylinder enemies chase at 3.6 to 4.5 m/s (`ENEMY_CHASE_SPEED_BASE` and `_STEP` in `enemy_types.h`; a tier adds 0.3, so the fastest equals a walking player) and spiders at 4.4 (`SPIDER_CHASE_SPEED`). They used to be 2.2 to 2.8, slower than a walk, so there was no reason to do anything but walk away. A fresh sprint is 7.6 m/s, 1.7 times the fastest enemy, so it opens a lead; the stamina is gone in about twelve seconds and the player is then slower than every enemy. `movement_headless.c` holds sprint on a treadmill (the player is put back each step so no corridor ends before the stamina does) with the fastest cylinder enemy 8 m behind: the lead peaks at about 30 m (it was 19) and the player is caught about 27 seconds after the start (it was 15), as the stamina empties and the exhausted player drops below the enemy's speed. A player who stops running in time, and walks, is never caught by the slower tiers and only slowly by the fastest.
- **Noise.** `PlayerNoise` is how far the player can be heard: about 3 m standing, up to about 8 m walking, 20 m sprinting, plus 3 m when low on stamina and 6 m when exhausted. `main.c` passes it to the enemies every frame (`Enemies_SetPlayerNoise`), and a gunshot makes the player audible at 28 m for 1.5 s. A cylinder enemy within that distance notices the player **through walls** and starts chasing; a spider hunts out to 12 tiles of path plus a tile per metre of noise (up to 120; a silent player is noticed at 12 tiles, a sprinting one at 32). So sprinting wakes things up from far away, and gasping at the end of a sprint does too.

### Known limits of movement

- Stamina is spent only by sprinting and jumping; melee swings and shots cost none.
- There is no crouch, no sliding and no footstep sound; the breathing is only a view heave.
- The RUN toggle ends when the stick is released for 0.4 s, so a run does not carry across a pause in walking.
- Running now costs something against the enemies (see "The cost of running" above).

### `PlayerApplyLook`, `PlayerSyncCamera`, `PlayerStep`
`UpdatePlayerController` is split so the game can turn the camera every frame and move the body every fixed step (`sim.md`): `PlayerApplyLook` (yaw and pitch), `PlayerSyncCamera` (the camera follows; its position is as the last step left it), `PlayerStep` (everything else). `UpdatePlayerController` is the three in one call and gives exactly the same result (tested); the movement tests still use it.
