# Characters

Every character is a condemned criminal chosen for the Tower, has three abilities (a passive, an activatable and a special), and a backstory. This document keeps the kit and the engine needs; the longer lore is in `05-world-and-lore.md`. Three things to settle for the whole roster are at the end.

## Roster at a glance

| Character | Role | Passive | Activatable | Special |
|---|---|---|---|---|
| **Sinless Steve** | Stealth saboteur | Faster, slide boost, quieter steps, senses things from further away | See no evil, hear no evil, do evil: unseen and unheard, cannot attack, can cause a ruckus | The sinless blade: ultra powerful melee with ranged strikes |
| **Brouugenxxxgerrrrr** | Area control | Toxic gas around him, tentacles (aim assist), a little passive healing | Chemical Warfare: a damaging toxic shroud that also hides him | Tentacle-launched globulus orbs: toxic splash, lure monsters, spawn larvae |
| **Totour Khan** | Brawler | More health and durability, stronger melee, higher jumps | Smash: a shockwave in a line, very loud | Four armed machine: a four armed minigun |
| **Elelelelele** | Assassin | Stealthy movement, electric slide, wall bounce | A very long slide that leaves a stunning electric trail | A silent laser gun that pierces |

Short call names for the screen and voice chat (a proposal; see `00-vision-and-opinion.md`): Steve, Brou, Khan, Lele.

## Sinless Steve

**Overview (yours):** the poster boy of the game. An assassin type; a man with vitiligo (or something like it); the only human among the characters.
**Backstory (yours):** a priest who started a cult after his vitiligo worsened and he took it as a sign from God that he should do more with his life. The cult set out to take down the oppressors on his side of the planet. What began as a faithful revolution became a massacre on both sides, with Steve at the front of it, hundreds of thousands of lives lost for a promise of ethereal salvation.
**Kit (yours):**
1. *Passive:* speed, a slide boost, steps that are harder to detect, and he detects further than other players.
2. *Activatable, "see no evil, hear no evil, do evil":* enemies cannot see or hear him. He cannot attack enemies directly, but can sabotage enemy players by causing a ruckus that drags monsters to them.
3. *Special, the sinless blade:* an ultra powerful melee weapon with ranged strikes.

**Engine needs and notes (mine):**
- Slide boost: the slide exists (`player.md`); a character multiplier on `PLAYER_SLIDE_SPEED_MUL` and a lower cost is a small change.
- Quieter steps: `PlayerNoise` already returns metres by state; a character factor multiplies it.
- Detects further: needs the sense system (`03-monsters-and-senses.md`). Decide what "detect" means: seeing monsters through walls as outlines, hearing them, a radar ping? It is the thing that makes him a scout.
- Unseen and unheard: monsters stop sensing him. **Does it also hide him from other players?** Your note says "enemies", which could mean monsters, rival players, or both. This changes the match a great deal (an invisible player in a hunting game is very strong). Needs your ruling.
- "Cannot directly attack while hidden, can cause a ruckus": a noise-making action (throw something, bang a door) that creates a noise source at a place; this is the "lure" primitive the other characters also use. Build it once as a general noise event.
- The blade with ranged strikes: a melee weapon row whose swing also fires a projectile. The weapon config format already allows a weapon to name a projectile; a melee-plus-projectile kind is a small extension.

## Brouugenxxxgerrrrr

**Overview (yours):** a member of the xxxgerrr race, an alien species known for bio-chemical knowledge; violent and feared; the bad guys of the setting. One of the yellow ones (green, brown and yellow; yellow is the worst).
**Backstory (yours):** the xxxgerrr like chemical warfare and fouling things with chemicals; loud and destructive; the niu declared them an invasive species to be wiped out, hunted and exterminated on sight. This one is a big one, one of the worst, who refuses to die: it poisoned planets, caused famines that killed entire species, killed millions of aliens and thousands of federation officers, until they blew up a planet it was busy ruining. It somehow survived, weakened, and was captured and locked up tight.
**Kit (yours):**
1. *Passive, proximity damage:* a toxic gas around him hurts anyone who gets close; tentacles give more aim assist; some passive healing.
2. *Activatable, Chemical Warfare:* a toxic shroud that does a lot of damage, and that also hides him ("you can't really see the thing").
3. *Special:* his tentacles launch globulus orbs which break on impact and splash a toxic liquid that damages you, makes you tantalising to monsters, and spawns tiny larvae that grow into annoying monsters if not killed early.

**Engine needs and notes (mine):**
- This kit needs a **status effect system** (`04-mechanics-and-systems.md`): poison damage over time, "scented" (monsters are drawn to you), and a visibility penalty for the shroud.
- Aura damage: a per-frame check of players within a radius of him, with the damage scaled by distance. Straightforward once there are other players to hurt (or monsters, which already exist to test on).
- Aim assist from tentacles: a slight pull of the aim toward the nearest valid target inside a cone; keep it small, it is a passive.
- The orb: a projectile config with a splash on impact (`projectiles.md` has no splash yet); the larvae are a monster type that grows over time (a timer and a size).
- Balance worry: an aura plus a damaging shroud plus passive healing is a very good tank for a stealth-and-sabotage game. The weakness to design in: he is loud and visible (the gas gives him away) and the shroud blinds *him* too.

## Totour Khan

**Overview (yours):** a four-armed warlord who wants to conquer the galaxy; only war and pillage on his mind; only the strong survive and all belongs to the strongest of the strong.
**Backstory (yours):** a conqueror who wanted the whole galaxy to bow; fought countless wars; killed uncountable living things; destroyed several niu outposts and planets; enslaved many planets to fuel his empire; tried to take on the niu home base and was utterly beaten and captured.
**Kit (yours):**
1. *Passive:* more health and durability, more powerful melee, jumps higher.
2. *Activatable, smash:* a shockwave that travels in a straight line, damaging whatever is in its path; very loud, so it draws monsters.
3. *Special, four armed machine:* a four-armed minigun.

**Engine needs and notes (mine):**
- The passive is all numbers (health, melee damage multiplier, jump speed): the cheapest character to build and a good first one for testing the character-kit plumbing.
- The shockwave: a line-shaped area attack (a swept box that moves forward); a loud noise event on use.
- The minigun: a weapon row with `fire_mode = AUTO`, a very short cooldown, large magazine, a spin-up, and a four-arm viewmodel. The weapon config (`weapons.mdix`) already describes everything but the spin-up and the heat/overheat; add those.
- He is a natural target for the monster-lure sabotage because he is loud; that is a good asymmetry.

## Elelelelele

**Overview (yours):** the ninja and assassin. From an alien race called the exmuin who lived in an ocean world. A sea-creature-looking alien in what looks like a jester costume.
**Backstory (yours, summarised):** her homeworld was peaceful, ruled by strict rules, one with nature, until corporations protected by the federation arrived for its resources. She hated the peace and hated the corporations, and used the corporations' arrival as an opportunity: taking resources and funds from her own people to go around assassinating corporate leaders, profiting, and living freely. She is not a Robin Hood figure; she is a criminal. Her crimes include the worst kinds (the in-fiction record includes sexual violence, which this document deliberately leaves out in detail). Her killings left evidence that pointed at her own people, which led to a war in which they suffered most. She was caught when she went after a federation leader.
**Kit (yours):**
1. *Passive:* stealthy movement; an electric slide that (your words) "causes more stamina" (see the question below); wall bounce, so she can jump on walls and leap off them.
2. *Activatable:* run forward into a super long slide that leaves an electric trail that stuns for longer than her passive.
3. *Special:* a laser gun that pierces and is stealthy: it does not attract enemies.

**Engine needs and notes (mine):**
- The electric slide builds on the slide that now exists. A longer slide (a larger duration and start speed) that drops a trail: a series of small hazard volumes along the path that stun whoever touches them. Needs the status effect system (stun).
- Wall bounce: a jump off a wall surface when airborne and touching it, keeping some of the speed; needs wall-contact detection in the controller (the collision code already knows which axis was blocked).
- The laser: a projectile config with `piercing = PIERCER`, a high `max_hits`, no tracer or a faint one, and no noise (the weapon's `noise_metres` is already a number: set it to 0). The config format already supports this today.
- **Question:** "an electric slide that causes more stamina": does it *cost* more stamina, or *give* more (regenerate while sliding)? I read it as costs more; tell me if not.

**Content note (mine, plainly):** the sexual violence in her record is the part of the whole cast most likely to cause friction with app stores and age-rating questionnaires, and it is a heavy thing to attach to a character you also want players to enjoy as a playful jester-assassin. You said yourself the age rating might change it. My suggestion: keep her a criminal of the worst kind in the fiction without putting that specific crime in any text the game, the store page or the character select shows (the federation's file can say "violent crimes" and the rest is implied), or choose a different atrocity for her. It is your story; this is a practical flag, not a refusal, and I have kept the detail out of the planning documents on purpose.

## Settled across the roster

1. **Duplicates.** Can two teams pick the same character? Can a team? (Proposal: duplicates across teams yes, within a team no, at first.)
2. **Is an ability a cooldown, a resource or a charge?** (Proposal: the passive is always on, the activatable is a cooldown with a duration, the special is earned: charged by points or kills, so it comes once per few floors.)
3. **A shared definition of "enemy".** Whenever a kit says enemies, say whether it means monsters, other teams, or both. Steve's invisibility and Brou's shroud both depend on it.
4. **Where character numbers live.** In a `characters.mdix` next to `weapons.mdix`, one row per character with the numbers (health, speed multipliers, noise factors, slide perks, ability cooldowns). It uses the same machinery as `config.md`.
5. **Order to build them** (`07-roadmap.md`): Khan first (the kit is mostly numbers), then Lele (slide and the status system), then Steve (needs the sense system), then Brou (needs most of the rest).
