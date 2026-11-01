# Monsters and senses

## The idea (yours)

The monsters of the tower are procedural, with a handful of common ones to be named later. Each one finds you with one or more of three senses: it **sees** you, **hears** you, or **smells** you. Each sense has a range. Hearing and smell reach furthest. Smell is uncommon and needs something poured on you, which is an item an enemy player can use against you.

This one rule carries a lot of the game: it is the horror (what is hunting me, and how do I make it stop?), the stealth (be quiet, stay dark), and the sabotage (make a ruckus near *them*, pour scent on *them*).

## The model (my proposal)

Every monster has three numbers and a few rules. A monster notices you when any of its senses reaches you.

| Sense | Reaches you when | What changes the reach |
|---|---|---|
| **Sight** | you are inside its view cone, within its sight range, and there is a clear line | the light on you (a dark corner is hard to see into, a lit hall is easy), a lit flashlight (it marks you), crouching, a shroud or invisibility |
| **Hearing** | what you are doing makes noise that reaches it, within the noise's radius | your action (walking 8 m, sprinting 20, sliding 14, a shot 28 to 40 m, a shout, a smash), and crouching or a character's quiet steps shrink it. Walls may or may not stop it (see below) |
| **Smell** | you are carrying a scent, within its smell range | a scent item or a toxic splash that wears off over time; no light or noise changes it |

### What exists in the code today

- **Hearing as a radius.** `PlayerNoise` (`player.md`) already gives the noise a player makes in metres: standing about 3, walking about 8, sprinting about 20, plus 3 when low on stamina and 6 when exhausted, and now 14 while sliding. A gunshot is heard out to the gun's `noise_metres` for its `noise_secs` (`weapons.md`): pistol 28 m, shotgun 40 m, rifle 34 m. The cylinder enemies hear through walls; the spiders hunt by noise out to twelve tiles plus a tile per metre of noise.
- **Sight is not there.** Today no monster uses vision: they are alerted by noise or by being close. That is the next gap.
- **Smell is not there.**
- **Light is there** (`lighting.md`): `Lighting_AtPoint` returns the light at a point, and the flashlight (`flashlight.md`) lights surfaces. Nothing yet makes a monster care about either.

### Tying the flashlight into it (the most important consequence)

Right now the flashlight is pure benefit. Once sight depends on light it becomes a **trade-off**, which is what a horror flashlight should be:

- A monster that hunts by sight sees you from further when you stand in light, and further still when your flashlight is on (proposal: the flashlight adds a long sight range for sighted monsters, as if you were standing in a lit hall).
- Dark patches become places to hide from sighted monsters, which is exactly what the dead-light patches are for.
- The flashlight does nothing against hearing and smell monsters, so the choice of tool depends on what is hunting you.
- A failing flashlight (it flickers) is both a warning to the player and a flickering beacon to sighted monsters.

### Walls and sound

Hearing through walls is cheap and makes monsters feel omniscient; stopping sound at walls makes sneaking possible and makes corners matter. A good compromise (proposal): sound passes through walls at a reduced radius (for example a third), and travels further down an open corridor. The current cylinder enemies hear through walls at full range; a gentle change to this is worth testing early because it changes how the whole game feels.

## The monster list

Your descriptions, mapped to the senses and to the engine. "Built" means something like it already exists in `docs/enemies.md`.

| Monster | Senses | Notes from you | Engine status | Proposal |
|---|---|---|---|---|
| **The strides** | sight, hearing | the lowest tier; mindless ghouls that wander and attack on sight; slow but relentless, so do not get cornered | **built** as the cylinder enemies (they hunt by noise; no sight yet) | add sight; keep slow, persistent; the staple of floors 1 to 5 |
| **The arachnids** | sight (wider range), hearing | spiders; spin webs that slow you; walk on walls | **built** (spiders on the ceiling, hunting by noise); no webs, no sight | webs as a slow zone (a status effect); a sight range wider than the strides' |
| **The gliders** | hearing, smell | big flying mosquitoes; easy to sneak past since they do not see | not built | flying path-finding is new; start with a simple swooping approach on a noise or scent |
| **The brain-tentacle things** | hearing, as vibrations | big heads with tentacles that walk on them; dangerous and fast; you can sneak past if you are quiet | not built | vibration = ground footsteps only (a gunshot or a shout does not carry; running does): a distinct rule that rewards crouching |
| **The amalgamated** | varies | combine with other monsters or their own kind, growing bigger and more random, with more ways to find you | not built | the procedural centrepiece: the merged monster has the union of the senses and the average of the stats, scaled up |
| **The ether things** | hearing | follow the shadows and teleport; very hard to kill or get away from | not built | teleport between dark patches near the target; they are the answer to "dark is safe", so they make the flashlight and lit rooms the safe places and flip the logic on those floors |
| **The beasts** | sight, hearing and smell | all three; as hard as rocks; conventional weapons do little; can one-shot you | not built | the boss-adjacent tier; make them rare and terrifying rather than common |

### Procedural variation (yours: "the monsters are procedural")

A monster is a **body** (size, speed, health, limbs) plus a **sense kit** plus a **behaviour** (wander, ambush, swarm, flee when hurt). Define each of these in a `monsters.mdix`, like the weapons, and generate monsters by combining them (amalgamation is then just combining two rows). This is the cheapest way to get variety, and it makes the senses data you can tune.

## Floors and tiers (proposal)

| Floors | Mostly | Notes |
|---|---|---|
| 1 to 5 | strides, arachnids | learn to be quiet; sight is short; noise and light matter |
| 6 to 9 | + gliders, brain-tentacle things | hearing becomes the main threat; scent items appear in shops |
| 10 | first boss | see below |
| 11 to 19 | + ether things, amalgamated | the dark stops being safe; monsters combine |
| 20 and on | + beasts | |

Health and speed already scale with the floor number in the current enemy code (`enemies.md`): health 40 + 20 per tier, chase speed 3.6 + 0.3 per tier up to 4.5 for cylinders. Keep scaling in the monster data, not scattered through the code.

## Bosses (every ten floors, yours)

A boss floor is a different kind of floor: one large arena, the stairs unlock by defeating or escaping the boss, and the other teams are in the arena with you. A boss is a monster with all three senses, a scripted pattern, and a story reason to exist. Plan three or four, then reuse with variation. Bosses can also be where team detachment becomes allowed.

## Open questions

1. Should sound stop at walls, or pass through at a reduced radius? (I would test the compromise above.)
2. Does a monster remember where it last sensed you? How long?
3. Do monsters fight each other (a strong one eating a weak one)? It would make the amalgamated monsters feel natural.
4. What is the first scent item, and how does it wear off?
