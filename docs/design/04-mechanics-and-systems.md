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
| [x] | Magazines and reloading | **done** (`weapons.md`, "Magazines and reloading"): a magazine per gun, the R key and a RELOAD button, an empty gun reloads itself, the shotgun loads one shell at a time and can be interrupted |
| [x] | Melee durability | **done** (`weapons.md`, "Durability"): the pipe breaks after 30 swings that connect and is gone |
| [ ] | Crouching | to do |
| [x] | Omnidirectional movement | **done** (`player.md`, "Omnidirectional movement"): walking, sprinting and sliding in every direction, left, right and backwards slower (placeholder 0.85 and 0.70 of forward) |
| [x] | Aiming down sights | **done** (`player.md`, "Aiming down sights"): holding the fire pad with a gun aims (a settings option comes later); per gun field of view, time, move share and spread share in `weapons.mdix`; the gun and hands move to the middle of the view. Not yet in the netcode's `NetCmd` |
| [x] | Wall peeking | **done** (`player.md`, "Leaning"): automatic, with a gun in hand, at the corner of a wall toward the opening (no buttons); the eye is kept out of walls. Not yet in the netcode's `NetCmd` |
| [x] | Dive | **done** (`player.md`, "Diving"): run, jump, dive; answers below; the slide is now forward only |
| [ ] | Consumables | **partly**: medkits and batteries exist and can be used; the "taking" mechanic below is not built |
| [ ] | Inventory rework | to do (the current inventory is a simple 12 slot grid; `inventory.md`) |

## Stamina, as you described it

Your description: a gauge that depletes when running and jumping; when it gets too low the player gets sluggish and breathes heavily, which makes more noise and attracts more monsters. Built that way: see `player.md`. The numbers are 200 stamina (doubled from 100 when you asked for twice as much), 17 a second to sprint (about twelve seconds), 12 to jump, regeneration of 15 a second after a one second pause, speed and acceleration falling smoothly to 45% at zero, and noise of walking 8 m, sprinting 20 m, plus 3 m when low and 6 m when exhausted. What is *not* in your description but is now true: a slide costs 18 stamina.

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

## Wall peeking (done, then changed)

**Yours:** Call of Duty style wall peeking when holding a gun. Asked later: when is it available? **Yours:** near walls and corners only.
**Changed after the first test on a phone (**Yours:** "lean is supposed to be automatic, not manual: when you go close to a wall you lean by yourself, not at a wall, at the corner of a wall, toward the direction of the opening"):** the first build had held LEAN buttons and Z and X near any wall. It is now automatic, at corners only, toward the opening; see `player.md`, "Leaning" for how a corner is found (sight lines: the wall ahead is close and a leaned eye sees at least 1 m further than the middle). The proposal below is what the first build came from; the lean of 0.4 m, 10 degrees and 0.15 s stand, and the speed while leaning is now 0.65 of walking.
**Proposal:**
- Two small lean buttons that appear only when holding a gun and standing near a wall corner (a probe from the existing dungeon probes decides this), or a lean on an edge-swipe; a key (`Q` and `E`).
- A lean moves the camera sideways by 0.4 m (the lean) and rolls it by 10 degrees, easing in and out in 0.15 s, with the weapon following. The body stays put, so the lean cannot go through a wall (the camera is clamped by a short probe the way the body is by the collision).
- While leaning: movement slowed to a crawl; firing allowed; noise unchanged; the player is partly visible to sight monsters only on the side that leans (a nice detail, expensive; defer).
- Tests: the lean offset is clamped by walls, the weapon aim follows the leaned camera, the lean releases when the weapon is changed to hands.

## Dive (done)

**Yours:** if you are running and tap jump, a dive button appears that lets you dive forward, left, right or even back depending on the direction the stick is pushed at the time of the jump; after completion the player is in a roll, and then back in a crouch.
**Your answers and what was built:** you said the dive is read at the tap, that you can aim down the sights during it, that the dive should give a short damage-free window only if Call of Duty does (the sources found say nothing of the sort, so no window: `PLAYER_DIVE_GRACE_SECS` is 0), that online comes later, and that a slide is forward only and left, right and back are for the dive. The rest took the defaults below. Built in `player.md`, "Diving".
**Proposal:**
- A DIVE button appears while sprinting and in the air from a jump (so "run, jump, dive" is the sequence), at the same screen position as SLIDE when that is not showing, or beside it.
- The dive is a committed arc: about 3 m in the stick's direction (relative to the camera), with a fixed airtime of 0.5 s, a lower camera, and a forward pitch of the view; it costs 25 stamina (more than a slide, because it is a stronger dodge) and cannot be steered.
- Landing: a roll of 0.4 s in the same direction at reduced speed, with the camera rolling through it, then the player is crouched (and can stand). The sprint and the slide are blocked during the roll.
- Invulnerability: your notes do not say. A brief window (0.25 s in the middle of the dive) is the usual reason a dive feels like a dodge; the alternative is only moving out of the way. Decide this.
- The dive and the slide share the controller state structure, so build the dive as another state of the movement state machine (see below) rather than another pile of flags.
- Tests: distance and duration, direction from the stick, stamina, refusals, the roll into the crouch.

**The questions, with what happened to each:**
1. **Invulnerability.** *Answered: only if Call of Duty has it; the sources say nothing of it, so none. The constant is there to turn on.*
2. **When is the direction read?** *Answered: at the press (the DIVE tap), relative to the camera, a centred stick meaning forward.*
3. **How long does the DIVE button stay?** *Not answered; default taken: until the dive starts or the player lands.*
4. **Crouch first?** *Not answered; taken: the roll ends standing for now, and the crouch joins later (building crouch first was not asked for).*
5. **Firing and aiming during the dive and the roll.** *Answered for ADS: yes during the dive. Built: fire and ADS work throughout the dive and the roll (nothing blocks them).*
6. **Numbers:** 3 m, 0.5 s in the air, a 0.4 s roll, 25 stamina, refused when exhausted. *Taken as the starting point.*
7. **Netcode:** *Answered: later.* A dive needs a button bit and a state in `NetCmd`/`NetSim_Step` (the other chat's code); the same goes for ADS and lean (`player.md`, "Netcode overlap").

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
