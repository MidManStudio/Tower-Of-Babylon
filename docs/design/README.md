# Tower of Babylon: design documents

These are the planning documents for the game. They were written from what you described (the game loop, the mechanics list, the four characters, the monsters, the tower and its lore) and from what the engine can do today. Each one says which parts are **yours** (taken from your notes), which are **built** (in the game now) and which are **proposals** (my suggestions, marked so you can overrule them). Nothing here is binding: change a document when the design changes, the same way the technical docs follow the code.

| File | What it holds |
|---|---|
| `00-vision-and-opinion.md` | The pitch, the pillars, what is unique, what could sink it, and my honest read. Start here. |
| `01-game-loop.md` | The loop you drew, floor by floor and match by match, the stairs and collapse dynamic, points, the downed state, the endgame betrayal, and the holes I found in it. |
| `02-characters.md` | The four playable characters, their three abilities each, what each needs from the engine, and the questions to settle. |
| `03-monsters-and-senses.md` | The sight, hearing and smell model, the monster list mapped to it, and how it ties into the noise, stamina and flashlight systems that already exist. |
| `04-mechanics-and-systems.md` | Your mechanics list with its status (stamina and slide are done), a spec for each one still to do, status effects, consumables, and the inventory rework. |
| `05-world-and-lore.md` | The universe, the federation, the tower as a broadcast, the NPCs, how much lore the game itself should carry, and a content note. |
| `06-multiplayer.md` | How to build the multiplayer in stages, what must be decided by a server, AI contestants, and the spectator idea. |
| `07-roadmap.md` | Milestones from the solo slice to the betrayal finale, the first thing to build next, a risk register, and the list of decisions I need from you. |

## Where the game is today

Built and tested: a procedural tower of floors with stairs and doors, ceiling lights in patches so that some areas are black, enemies (the cylinder "strides" and the ceiling spiders) that hear you, a velocity-based player with jump, run, a stamina gauge, fatigue and the noise it makes, **slide**, a health system, four weapons (pipe, pistol, shotgun, rifle) with their own ammo and projectiles, an inventory with a touch screen, a **flashlight with batteries** that fails when low, and weapon, projectile and flashlight numbers in DixScript files. See `docs/*.md` for each.

Not built: floors with missions, the stairwell unlock, the countdown and the collapse; points and the shop; the safe floors; teams and multiplayer; characters and their abilities; bosses; most monsters; sight and smell; crouch, wall peeking, dive; the inventory rework; sound; an announcer.
