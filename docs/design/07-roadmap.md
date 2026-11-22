# Roadmap

Sizes are relative (S a few small changes, M one delivery like the last ones, L several), not calendar time. Every milestone ends with something you can play and could stop at.

## Milestones

### M0. The engine and a place to stand (done)

A procedural tower with stairs; patchy ceiling lighting; enemies that hunt by noise (strides, spiders); a velocity based player with jump, run, stamina, fatigue and noise; **slide**; health and hit feedback; four weapons with their own ammunition and projectiles; an inventory with a touch screen; a **flashlight with batteries** that fails when low; weapon, projectile and flashlight numbers in DixScript files; a fix for the half precision glitch on the phone; headless tests for all of it.

### M1. The solo floor loop (L) *build this next*

The game you could ship as "Survival mode", and the first time the stair clock is real.

1. **The unlock mission** (M) *first cut built (`docs/floor_run.md`)*: a console somewhere on each floor that must be worked for a time (hold USE, 8 to 12 seconds, loud, spawns monsters when started). The first mission type, and the one that is always there.
2. **The countdown and the collapse** (M) *first cut built*: unlocking starts a countdown (30 to 45 s); at zero the door opens and the floor collapses in a wave from the far edge; anyone still on it dies. Falling-floor visuals can be simple (quads dropping and fading).
3. **Sight for monsters** (M): view cone, range, line of sight, light and flashlight affecting it (`03-monsters-and-senses.md`). Sound through walls at a reduced radius.
4. **Crouch** (S): `04-mechanics-and-systems.md`.
5. **The angry floor** (S): a rising meter that makes monsters spawn faster and, past a limit, forces the stairs (the standoff fix, `01-game-loop.md`).
6. **Score** (S): points from the mission and from kills, floors climbed, a results screen.

*Done when:* a ten floor run is tense for reasons other than difficulty numbers, and you want to do another.

*Done, to be tried on a phone:* the rules run on a fixed 30 Hz step with the picture blended between steps (`docs/sim.md`), and at most two weapons can be equipped (the bag holds more; `docs/weapons.md`).
*Next, in the owner's order:* the character selection screen at launch (information and the selection UI only, from `02-characters.md`; no abilities), then the first multiplayer step: the game protocol and a server loop over a loopback transport with fake lag (see the handover for what `mid-net` has and lacks).

*Later, noted from the owner:* the HUD is to become **customisable** (positions and sizes chosen by the player). The layout today (one `TcLayout` table in `src/input/touch_controls.c` and `hud_layout.c`, measured off the owner's sketch) is a placeholder, not a design to protect; saved positions will replace the table.

### M2. Character kits (L, in parts)

A `characters.mdix`, a character select, and the kits in this order, each one exercising a new piece:
1. **Khan** (S): all numbers plus the smash (a line area attack) and the minigun (a weapon row).
2. **A status effect system** (M): needed by everyone after Khan.
3. **Elelelelele** (M): the long electric slide with a stun trail, wall bounce, the piercing silent laser.
4. **Steve** (M): needs M1's senses; invisibility, the noise-making action, the blade.
5. **Brou** (M): the aura, the shroud, the orbs and larvae; needs the rest.

*Done when:* each character feels different on the same floor.

### M3. Multiplayer foundation (L)

An authoritative server running the shared simulation; two phones, co-op survival on one seed (`06-multiplayer.md`, stages 1 and 2). No teams, no PvP yet.

*Done when:* two phones on one network can climb ten floors together and nothing diverges.

### M4. Teams and the stair clock for real (L)

Four players, two teams, the unlock mission as a contested objective, the shared countdown, the downed state and healing, trading and the first sabotage items, **AI contestants** to fill the empty places.

*Done when:* a match with one human and seven bots is worth playing, and the "let them open it, then swoop in" move happens by itself.

### M5. The tower proper (L)

More monsters (gliders, brain-tentacle things, ether things, amalgamated, beasts), bosses on floors 10 and 20, the safe floor with the shop and the sentinels, the inventory rework, consumables with use times, the audience (spectator) mode, more teams.

### M6. The finale and the show (M)

The betrayal ending, the commentator and the broadcast presentation, sound throughout, the freedom ending.

### F. Floor generation v2 (L, in six rounds; runs beside M3 and M4)

The floors become bigger (64 tiles for two teams, never more than 128), get a start room for each team, low walls to take cover behind, stacked levels (uncommon, up to four), themes (floral, fungal and mechanical on top of the office), a safe zone, and room for the missions and special rooms that are still to be designed. The plan is `08-floor-generation.md`; what it rests on is `09-floor-generation-research.md`. The rounds, each one playable on the phone: **R0** the foundation (an integer-only generator, a floor hash, accessors and a benchmark harness; nothing changes on screen), **R1** bigger floors and a start room for each team, **R2** cover, **R3** sockets, templates and the safe zone, **R4** stacked zones, **R5** themes and props. R1 gives M4 its start rooms; R3 gives M5 the places for the safe zone and the boss arena. Nothing under `src/net/` is touched by it; the multiplayer side is section 12.5 of the plan. **R0 is partly built (2026-11-21):** the integer-only generator, PCG32 streams, bit-row scans, the floor hash and five test suites are in; the accessor layer, the benchmark harness, the HUD line and a WebAssembly check remain (`floorgen.md`).

### Always running

Sound (see below), phone performance (check the diagnostics overlay after each change), tests, and the docs.

## Sound

**Status:** the first set of sounds exists (`docs/audio.md`): footsteps, the guns, melee, hit ticks, hurt, heartbeat, a monster's growl and skitter, the flashlight, pickups, doors, ambience, all synthesised, none heard yet. What follows is the plan it was built from. Still to do: sound through walls, the commentator, music, mixing by ear.

The plan: the game's rules are about noise. Notes from earlier work: the web build keeps `ASYNCIFY=1`; browsers will not play audio until the first tap, so start the audio system on the first touch; prefer **generated** sounds (raylib's `LoadSoundFromWave` on a waveform you synthesise: clicks, thumps, a shot, a breath) over files, which keeps the download small and lets the same code make the sounds in a test. Start with the three that carry meaning: footsteps by surface and speed (so the player *hears* their own noise level), the gunshots (each gun's own), and a monster alert sound. Do this alongside M1; it makes the noise system something the player can feel.

## Next three things to do

1. **The unlock mission and the countdown with the collapse** (M1, items 1 and 2). It is the loop's centre and the least known quantity: if it is not fun alone, nothing built on it will be.
2. **Sight for monsters** (item 3), which gives the flashlight its trade-off and gives crouch something to do.
3. ~~Footstep and gunshot sounds~~ (done, untuned: see `audio.md`).

## Risk register

| Risk | Likelihood | Impact | What reduces it |
|---|---|---|---|
| Scope: the whole design is a studio's worth | high | project stops | milestones that are each playable; build the solo slice first; cut characters before cutting quality |
| Multiplayer cannot hold 16 players over WebSockets on a phone host | medium | the main mode shrinks | start at 2 to 4, measure, keep teams of 4 and 2 to 4 teams as the target, AI contestants to fill |
| The stair clock is not fun alone | medium | the loop's centre fails | build it in M1, before any multiplayer, and iterate on it |
| Nobody to play with (a PvP game with no population) | high after launch | the mode is empty | AI contestants from M4; co-op and survival as complete modes |
| Phone performance degrades as systems are added | medium | unplayable on the target | diagnostics overlay after each change; budgets per system; the beam shader and the dark-quad rule already cost a few dozen quads; measure |
| App store or rating problems from the content | medium | cannot publish | rate honestly; keep the specific crime in one character's record out of player-facing text (`05-world-and-lore.md`) |
| The shader fails on some phone GPU | low to medium | glitches return or the beam is missing | the beam shader is a separate program with a glow fallback; the plain highp shader is untouched; test on the phone first |
| The eliminated are bored for most of a match | high | players leave | the audience mode (`06-multiplayer.md`) |
| One developer, one phone | certain | slow, and every change is tested by hand | the headless tests; deliveries as tarballs; offline renders; keep the diagnostics line useful |
| A standoff at the stairs | medium | matches stall | the angry floor and the forced opening |
| Floors differ between the server and the clients (each builds the layout from the seed on its own platform) | low after R0 | a desync from the first frame | integer-only generation, one random stream per stage, golden hashes natively and in WASM, the hash in the handshake (`08-floor-generation.md`, section 12) |
| Bigger and stacked floors cost the phone more than it has | medium | unplayable | R0 measures first; the lighting window and the bounded searches in R1; level culling in R4 (`08-floor-generation.md`, section 13) |

## Decisions I need from you

None of these block the next step (M1) except where noted. I will default as written if you do not say.

1. **Steve's invisibility:** does it hide him from other players as well as from monsters? *(default: monsters only; the ruckus power is what hurts players)* Blocks M2, step 4.
2. **Elelelelele's "electric slide that causes more stamina":** costs more, or gives more? *(default: costs more)* Blocks M2, step 3.
3. **The floor where a player may leave their team.** *(default: the first boss floor)* Blocks M4.
4. **What the stair unlock gives** the team that does it, or the team that swoops in. *(default: bonus points and a better chance of a rare item)* Blocks M1 item 1's reward.
5. **The crime in Elelelelele's record** as it appears to players. *(default: kept out of anything player-facing)* Does not block anything.
6. **Short call names** for the two with hard names. *(default: Brou, Lele)* Does not block anything.
7. **Can a team have two of the same character?** *(default: no; yes across teams)* Blocks M4.
8. **Dive invulnerability.** *(default: a short window in the middle)* Blocks the dive only.
9. **Who runs the server** in your first tests: a phone with Termux, or a computer? *(default: a phone)* Blocks M3.
10. **Targets:** browser only for now, or a native Android build later? *(default: browser only)* Affects networking and sound choices later.
11. **Floor size for three or four teams.** *(default: 81 tiles; the cap is 128)* Blocks only the size table of F, round R1.
12. **Holes in a deck:** rails or open edges, and does a 6 m drop hurt? *(default: open edges, no fall damage, a loud landing)* Blocks F, round R4.
13. **The elevator:** where it goes and what it does when it is active. *(default: a socket beside the stairs, behaviour left out)* Blocks only the elevator, in F round R3.
14. **A safe floor:** a whole floor, or a safe zone on a normal floor? *(default: a zone on a normal floor)* Blocks F, round R3. The rest of the open questions of F are in `08-floor-generation.md`, section 17.
