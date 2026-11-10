# The stair clock (`src/core/`, `src/floor/`)

## Overview

The core loop of the show (`design/01-game-loop.md`): there is a console somewhere on every floor, and holding USE at it unlocks the stairs. That starts a 30 second countdown during which the top door stays shut. At zero the door opens and the floor collapses in a wave from the far edge toward the door; whoever is still on fallen floor dies. This is milestone M1 items 1 and 2 of `design/07-roadmap.md`, solo only: the same rules are what the server will run later, which is why they are plain C with no raylib, stepped in fixed ticks.

The numbers are in `assets/config/floor.mdix` (`config.md`): hold time 10 s, countdown 30 s, collapse 40 s, three monsters called, monsters 1.35 times faster once the door opens. The 30 second countdown is the number you asked for.

**What came from the Unity utilities.** Before writing this, `MidManStudio_Unity` was searched for existing logic (the rule: check it before building a feature). Ported to C: `CountdownTimer` and `ValueInterpolationTimer` (one way; `Runtime/Timers/Timer.cs`) and the accumulator of `FixedStepLoop` (`com.midmanstudio.patterns`, `Runtime/GameLoop`). Looked at and not needed yet: `TickDispatcher`/`TickDelay` (delayed callbacks by tick: the console's staggered monster calls are one counter and a timer here; worth porting when there are many delayed actions), `EventQueue` (the clock's own 16 slot event ring does the one job it needs; port it when a second system needs one), `StateMachine` (three phases are an enum), `NetworkTimer` (the same idea as `FixedStep`), and the netcode package (for the multiplayer stage).

## How a floor goes

1. The floor starts: the console is placed, the top door is locked (red lamp, "LOCKED"), a toast says to find it. The console is a dark cabinet with an amber screen and a pillar of light so it can be found in the dark.
2. The player stands within 2.4 m of it and holds USE (the USE button reads HOLD UNLOCK). A bar at the top of the screen fills over 10 s. The first moment it is worked it calls three monsters, 1.5 s apart, on open ground 7 to 11 m away, already hunting (once per floor, even if the player walks away). Letting go drains the progress at 0.15 of the whole per second, so a short break to fight is not a restart.
3. Full: STAIRS UNLOCKED, a countdown at the top of the screen from 0:30. The last ten seconds tick, higher each second.
4. Zero: the top door unlocks, the monsters speed up, the floor starts to go. The front is a circle around the top door that shrinks from the farthest floor to nothing over 40 s: floor outside it is not drawn and anyone standing on it dies ("THE FLOOR GAVE WAY"). The nearer the door, the longer the player has; the door's own floor goes last, at the very end.
5. Going up through the door starts the next floor with a new clock. Coming down into a floor already climbed has no clock and an open door. Dying restarts the clock on the same floor.

## Modules

### `timers.h` and `timers.c`
**What it does:** `CountdownTimer` (`Countdown_Init/Start/Stop/Tick/Progress`; `Tick` says so once, on the tick that reaches zero, then the timer stops at 0) and `Tween` (`Tween_Start/Tick/Value`: from, to, duration, ease linear / in / out / in-out; lands exactly on `to`). No clock of their own: the caller passes the time that passed.

**Decisions:** ping-pong and custom curves are left out: Unity's own `timers.md` says its factory never turns ping-pong on, and nothing here needs either. A timer of zero seconds finishes on its first tick and its progress is 0 (no division by zero).

### `fixed_step.h` and `fixed_step.c`
**What it does:** `FixedStep_Advance(f, dt)` turns a frame's time into a whole number of fixed steps (here 30 a second, at most 5 a frame), keeps the remainder, and drops and counts what cannot be caught up after a stall. `FixedStep_Alpha` is the blend factor for drawing between steps (not used yet).

**Why it is here:** the clock's rules take the same number of steps at 30, 60 and 144 frames a second, and a server stepping the same rules at the same rate will agree with the phone tick for tick. That is the groundwork for multiplayer, and the first piece of the game that runs on it. The rest of the game still steps by frame time; moving it onto fixed steps is a multiplayer-stage job (`design/06-multiplayer.md`).

### `floor_run.h` and `floor_run.c`
**What it does:** the rules. `FloorRun_Begin(cfg, console, door, maxRadius)`, `FloorRun_Step(dt, reachable, useHeld)` (one tick), `FloorRun_PopEvent` (oldest first from a ring of 16; when it is full the oldest is dropped), `FloorRun_DoorOpen`, `FloorRun_Gone(x, z)` (has the floor there fallen), `FloorRun_CountdownLeft`. Phases: exploring, countdown, collapsing. Events: console start, console abort, monster call, unlocked, a count (last ten seconds), door open, collapse begin. Nothing in it spawns, plays or kills: it posts events and the game does those.

**Decisions:** progress drains rather than resets when USE is let go (a number in the config; 0 keeps it). The monsters are called once per floor, on the first moment of work. The wave is a circle around the door, not a path: simple, visible, and it means "nearer the door, more time". Its pace is a config number (0 steady, 1 speeds up, 2 slows, 3 slow at both ends).

### `floor_config.h` and `floor_config.c`
**What it does:** reads `assets/config/floor.mdix` (`FloorConfig_LoadFrom`, `FloorConfig_Get`) the same way the other config files are read: every value must be there, of the right type and in range (`countdown_secs` 3 to 300, and so on) or nothing is installed and the message names the value. Until a document loads, the built-in defaults, which equal the file, are used.

### `floor_game.h` and `floor_game.c`
**What it does:** the clock inside the game. `FloorGame_Register` (hands floor.mdix to the config layer; before `GameConfig_Init`), `FloorGame_NewFloor(map, floor, cleared)` (places the console, works out how far the floor reaches from the door, locks the door), `FloorGame_NearConsole`, `FloorGame_ConsolePosition`, `FloorGame_Frame` (feeds the frame's time to a `FixedStep`, ticks the rules, does what the events ask: sounds, toasts, `Enemies_SpawnAt` for a monster call, `Enemies_SetRage` when the door opens, `Staircase_SetUpDoorLocked`, the collapse wave, and says whether the player is on fallen floor), `FloorGame_CollapseWave`, `FloorGame_Draw`, and the numbers the HUD shows.

**What it says to the outside.** The clock calls no audio, HUD or renderer function (since the simulation moved out of `main.c`, `sim.md`). Its sounds and messages go through `SimOut_Sound` and `SimOut_Toast` (`src/sim/sim_events.h`), which post to whatever queue the simulation has bound for the step, and do nothing with none bound. The collapse wave (centre and radius, a negative radius meaning none) is read back with `FloorGame_CollapseWave(&cx, &cz, &radius)`; `main.c` gives it to `DrawFloorMapCollapse` once a frame.

**Where the console goes:** on open ground (a metre clear all round) away from both alcoves, at least 22 tiles from the foot of the up stairs (relaxed to 12, then to anywhere, if a floor has no such place) and 8 from where the player arrives, picked from the floor's seed so a floor always has it in the same place. The player has to leave the stairs, work it, and get back.

## Tests

- `tests/floor_run_headless.c` (56 checks): the timers, the tween shapes, the fixed step (including 144 fps against 30 fps and a stall), the whole unlock, every drain case, the three monster calls, the countdown (door opens at 30 s, each of the last ten announced once), the collapse (monotone, halfway at half time, all gone at the end, nothing gone before), the 16 slot ring, and the config file (the shipped file says 30 s; bad values, a fractional monster count and a missing value are refused and named).
- `tests/floor_game_headless.c` (28 checks): the console's place over 60 floors, the door lock through the whole sequence, the monsters' places, the ticks of the last ten seconds, the kill at the far edge and the safety at the door, frame rate independence (the unlock takes 10 s at 30, 60 and 144 fps; a 2 s stall does not skip ahead), floors with no clock, a new life.
- Nine deliberate breakages (door stays locked, never kills, one tick per frame, countdown never starts, hold without reach, no drain, the wave outward, no stall cap, door open at unlock) were each caught.

## Not verified

- Nothing was seen on a screen or played. The console's look, the HUD clock's size and place, the pace of the collapse (40 s from the farthest corner), whether 10 s of holding feels right, whether three monsters are too few or too many: all guesses, all numbers in `floor.mdix`.
- The collapse is the floor tiles not being drawn plus a kill. There is no debris, no falling animation and no rumble beyond one sound; the player standing on the ceiling-less alcove sees the flight hanging over nothing.
- The sounds are the existing ones at other pitches (a click that rises for the countdown, the door's thud low for the unlock and the collapse). Nobody has listened.
- The wave is a circle around the door, so a player 120 m away by path but 60 m by line can die before they could have got there. It is fairer on an open floor than in a maze. A wave that follows the walking distance would be fairer; it needs the path field the spiders use.
- Multiplayer: this runs one clock per client. A server owning it, a shared countdown and a team owning an unlock are the next stage.
- Monsters rage (speed only). "More vicious" beyond speed, and the monsters rushing the stairs, are not built.

## Fixes and Problems

### The first doorway waypoint never let go
Found by the test that sends a monster up the flight: the monster walked to the waypoint in front of the alcove's doorway and stood on it forever, because the rule "outside the alcove, go to the doorway" stayed true once it got there. It is now "go to the doorway until in front of it and in line with it, then in through it" (`dungeon.md`, `stair_collision.c`). Not clock code, but found while building it.

### Test mistakes worth remembering
The first sequence test ran a clock that had already almost finished (the checks run one after another on the same floor, so time adds up) and one "farthest corner" point was farther than the radius the test had given. Both were the tests, not the game; the sequence is now split into floors that each start from zero.

### `FloorGame_Tick`
The game now runs its rules on fixed 30 Hz steps (`sim.md`), so `GameSim_Step` (`src/sim/game_sim.c`) calls `FloorGame_Tick` once per step (always `RUN_TICK_SECS`, `in->dt` unused). `FloorGame_Frame`, which keeps its own accumulator, is still there for callers with no steps and for the tests of frame-rate independence.

- The clock used to call `AudioEvents_ConsoleStart/Unlocked/CountTick/Collapse`, `Hud_Toast` and `DrawFloorMapCollapse` itself, which tied it to the audio, the HUD and the renderer and made it impossible to link in a program with no window. They are now the sinks and the getter above; `tests/floor_game_headless.c` catches the sinks with stand-ins for `SimOut_Sound` and `SimOut_Toast` and reads the wave with `FloorGame_CollapseWave` (its 28 checks are unchanged), and `tests/game_sim_headless.c` checks the whole path through the simulation.
