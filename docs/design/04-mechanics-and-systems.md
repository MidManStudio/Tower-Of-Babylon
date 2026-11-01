# Mechanics and systems

Your mechanics list, with where each one stands, then a specification for what is left. Checkboxes follow your list. Status words: **done** (built, tested, in the game), **partly** (some of it is there), **to do**.

## The list

| | Mechanic | Status |
|---|---|---|
| [x] | Velocity based movement | **done** (`player.md`) |
| [x] | Jump | **done** |
| [x] | Run | **done**: a tap toggle on the phone, Shift on a keyboard |
| [x] | Stamina gauge | **done**, and your list's checkbox is out of date: running and jumping drain it, regenerate after a pause, and as it falls the player gets slower and starts making more noise, with a worse state when exhausted (`player.md`, "Stamina and fatigue") |
| [x] | Slide | **done** in this update (`player.md`, "Sliding") |
| [ ] | Crouching | to do |
| [ ] | Wall peeking | to do |
| [ ] | Dive | to do |
| [ ] | Consumables | **partly**: medkits and batteries exist and can be used; the "taking" mechanic below is not built |
| [ ] | Inventory rework | to do (the current inventory is a simple 12 slot grid; `inventory.md`) |

## Stamina, as you described it

Your description: a gauge that depletes when running and jumping; when it gets too low the player gets sluggish and breathes heavily, which makes more noise and attracts more monsters. Built that way: see `player.md`. The numbers are 100 stamina, 17 a second to sprint (about six seconds), 12 to jump, regeneration of 15 a second after a one second pause, speed and acceleration falling smoothly to 45% at zero, and noise of walking 8 m, sprinting 20 m, plus 3 m when low and 6 m when exhausted. What is *not* in your description but is now true: a slide costs 18 stamina.

## Slide, as you described it

Your description: the player runs and can click a slide button that appears, which makes him slide a bit faster than running, effective for dodging, and it uses stamina. Built that way: the button appears while running at (570, 150), the slide starts about a quarter faster than a sprint and runs down over 0.85 s covering five metres, costs 18 stamina, and has a 14 m noise. Character perks (Steve's slide boost, Elelelelele's long electric slide) are not built; see `02-characters.md`.

## Crouching (to do)

**Yours:** crouching for when dead silence is required.
**Proposal:**
- A CROUCH toggle button (tap to crouch, tap to stand), a key (`Ctrl` or `X`). Stand up is refused when there is a low ceiling above (there are none yet; keep the check for later).
- Speed 2.2 m/s; noise while moving 3 m (against walking 8), standing still 1 m; no sprint, no slide, no jump out of a crouch except a small hop that stands you up.
- The camera drops about 0.35 m, blended like the slide's. The collision radius is unchanged; the visible body is lower.
- Sight monsters see a crouching player from a shorter range (the factor is a number in the monster's sense data).
- Slide ends in a crouch if the crouch button is held (a natural tie with the dive's "roll, then crouch").
- Tests: noise values, speed, camera blend, refusals, interplay with slide and sprint.

## Wall peeking (to do)

**Yours:** Call of Duty style wall peeking when holding a gun.
**Proposal:**
- Two small lean buttons that appear only when holding a gun and standing near a wall corner (a probe from the existing dungeon probes decides this), or a lean on an edge-swipe; a key (`Q` and `E`).
- A lean moves the camera sideways by 0.4 m (the lean) and rolls it by 10 degrees, easing in and out in 0.15 s, with the weapon following. The body stays put, so the lean cannot go through a wall (the camera is clamped by a short probe the way the body is by the collision).
- While leaning: movement slowed to a crawl; firing allowed; noise unchanged; the player is partly visible to sight monsters only on the side that leans (a nice detail, expensive; defer).
- Tests: the lean offset is clamped by walls, the weapon aim follows the leaned camera, the lean releases when the weapon is changed to hands.

## Dive (to do)

**Yours:** if you are running and tap jump, a dive button appears that lets you dive forward, left, right or even back depending on the direction the stick is pushed at the time of the jump; after completion the player is in a roll, and then back in a crouch.
**Proposal:**
- A DIVE button appears while sprinting and in the air from a jump (so "run, jump, dive" is the sequence), at the same screen position as SLIDE when that is not showing, or beside it.
- The dive is a committed arc: about 3 m in the stick's direction (relative to the camera), with a fixed airtime of 0.5 s, a lower camera, and a forward pitch of the view; it costs 25 stamina (more than a slide, because it is a stronger dodge) and cannot be steered.
- Landing: a roll of 0.4 s in the same direction at reduced speed, with the camera rolling through it, then the player is crouched (and can stand). The sprint and the slide are blocked during the roll.
- Invulnerability: your notes do not say. A brief window (0.25 s in the middle of the dive) is the usual reason a dive feels like a dodge; the alternative is only moving out of the way. Decide this.
- The dive and the slide share the controller state structure, so build the dive as another state of the movement state machine (see below) rather than another pile of flags.
- Tests: distance and duration, direction from the stick, stamina, refusals, the roll into the crouch.

### A movement state machine

Sprint, slide, crouch, dive and the roll are all modes the player can be in, with rules for entering and leaving each. The slide was added with its own flag (`sliding`) and its own timers, which is fine for one; the second and third mode will be easier with a small explicit state (`MOVE_STAND`, `MOVE_SPRINT`, `MOVE_SLIDE`, `MOVE_CROUCH`, `MOVE_DIVE`, `MOVE_ROLL`) and a table of which transition is allowed. Do that refactor when crouch is added, not before.

## Consumables and "taking" them (partly)

**Yours:** the consumables are in the items, which are dealt with separately, but the mechanics for taking them are needed; you will provide references.
**Built:** an item can be used from the inventory screen and takes effect at once (a medkit heals 40; a battery loads the flashlight).
**Proposal for the rest:**
- Each consumable has a **use time** in its row (0 for a battery, 2.0 s for a medkit, 0.8 s for a stimulant), a **channel**: a progress ring on the screen while it runs, the player slowed to walking speed, interrupted by damage or by pressing another action, and not consumed if interrupted.
- Healing a downed teammate is the same mechanic with a target and a longer use time (`01-game-loop.md`).
- A quick-use slot: one or two consumables assigned to buttons on the HUD so they can be used without opening the inventory. This matters in a fight.
- Items go in an `items.mdix` (same machinery as the weapons), with the use time, effect, weight, stack size, price and description.

## Status effects (needed by the characters)

The kits in `02-characters.md` all lean on temporary effects: poison (Brou's gas, the toxic orbs), stun (Lele's trail), "scented" (monsters are drawn to you), a slow (webs), a visibility change (a shroud, invisibility), bleeding out (downed). They are best built as one system:

- A status is a type, a duration, a strength and a source, stored on a player or a monster in a short list; each frame the list is ticked and its effects applied through small hooks (damage per second, a speed multiplier, a noise multiplier, a visibility multiplier, a lure strength).
- Stacking rule per type (refresh, add, or strongest wins), and a clear display for the player (an icon and a timer).
- Define the types in a `statuses.mdix` so the numbers can be tuned.

## The inventory rework (to do)

**Yours:** the inventory will be reworked properly because there need to be rules and arrangements: what you can carry, and what each item weighs, which will affect the character, and the number of weapons you can have on hand against what you keep in the inventory.

**Today:** twelve slots in a 4 by 3 grid, items that stack, a USE/EQUIP/DROP panel, and the world keeps running while it is open (`inventory.md`).

**Proposal:**
1. **Hands.** Three weapon positions on the character: a *primary* (a long gun), a *sidearm* (the pistol) and a *melee* (the pipe or equivalent). Swapping between them is fast (the SWAP button cycles). Anything beyond that lives in the bag and takes a moment to bring out (a "draw" time).
2. **Weight.** Every item has a weight (kg) in `items.mdix`. The bag has a **comfortable limit** per character (Khan's is higher). Below the limit nothing changes. Above it, the player is slower and gets tired faster, in proportion to the excess; far above it the player cannot pick up more. This is a number in the stamina and speed formulas, not a new system.
3. **Capacity.** Keep slots (so the screen stays a grid) *and* weight, or replace slots with weight alone. I would keep the slots: a grid is easy to read on a phone and gives a natural limit on how many different things you carry.
4. **Dropping and swapping.** Dropping is already there; add "swap with what is on the floor".
5. **Looting other players.** A dead or downed player's items drop as a pile others can take. The inventory screen needs to handle taking from a pile.
6. **Sample weights** (proposals): pipe 2.5, pistol 1.1, shotgun 3.4, rifle 3.6, a box of ammunition 0.3 to 0.8, medkit 0.6, battery 0.15, flashlight 0.4.

When to do it: after the loop is fun and before the shop (the shop sells things you need to be able to carry).

## Smaller things on the list in your notes, and some I noticed

- Aim assist for Brou's tentacles, wall bounce for Lele, a jump boost for Khan are all character perks, not base mechanics; they hang off the same state machine and the character table.
- **Sound** does not exist at all: no footsteps, shots, monsters or music. For a game whose rules are about sound, this is an important gap. A plan for generated sounds in the web build is in the notes in `07-roadmap.md`.
