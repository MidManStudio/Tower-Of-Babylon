# Vision, and what I think of it

## The pitch, in your terms

Condemned death-row criminals from across a galaxy are put into a broadcast gladiator show, the Tower of Babylon: an endless, ever-changing tower full of horrors, run by the niu galactic federation and watched everywhere (cameras and drones on every floor, a commentator on a mic insulting everyone). Teams of up to four climb, floor after floor, completing missions, surviving monsters, and sabotaging each other. Opening a floor's stairs starts a countdown, and when it ends the floor collapses, so whoever is left behind dies. The last team standing is disbanded (only one person can be freed), and its members take each other out. The survivor is pardoned, "hopefully the niu federation will keep its word... probably." There is also a survival mode with no enemy teams, solo or co-op, to see how many floors you can climb.

One sentence version (mine): *a battle royale played as a horror tower climb, where the stairs are the weapon, the monsters can be pointed at other players, and everyone you trust has to die at the end.*

## Pillars

These are what I would test every feature against. They are my reading of your notes; change them if I read you wrong.

1. **The stairs decide everything.** One team opening the stairs starts a clock for everyone. That is the engine of the whole match: it forces movement, creates the "let someone else do it and swoop in" play you described, culls the slow, and makes every floor end.
2. **You are a signal.** Monsters find you by sight, sound and smell, so what you do (run, shoot, slide, light your flashlight, get splashed with something) decides who comes for you. This is the horror, and it is also the sabotage: you can point the monsters at someone else.
3. **Sabotage over shooting.** Fighting other teams is allowed but the interesting play is indirect: luring, trapping, stealing the stairs, spoiling a mission. Each character brings a different way to do it.
4. **Trust is a resource.** Teams are real for as long as it suits you, you can leave your team once you are deep enough, and in the end the team is gone. The design should make "when do I betray them" a question from the first floor.
5. **It is a show.** The cameras, the commentator, the audience: the game explains itself through the broadcast, and the people who die become the audience.

## What is genuinely unusual

I looked at where each piece sits among games players know, so you do not have to take this on faith.

- **The collapsing-floor stair countdown as a shared clock** is the most ownable idea here. Extraction shooters have extraction, battle royales have a closing zone, but a clock that *one team starts and every team must beat*, which you can choose to start or let someone else start, is a different shape. It turns a mission into a decision.
- **Monsters as a weapon you aim at people** (making someone "tantalising to monsters", pouring scent on them, causing a ruckus near them). Co-op horror like Lethal Company has monsters as a threat; here they are also an instrument. It also makes your stealth and noise systems matter, and you have already built them.
- **The betrayal finale and the leave-your-team floor.** Social-deduction games have betrayal, team shooters have teams; a match that starts as a co-op climb and ends as a duel between the people who climbed together is rare, and the "plan your betrayal from the beginning" framing is a good one.
- **The broadcast framing** (announcer, drones, an audience that the dead join) is a way to solve problems rather than just a coat of paint; see below.
- **Four very different characters** with kits built around the same three systems (stealth, area control, brute force, speed) is conventional, but your versions have personality: the invisible chaos-maker, the walking toxic cloud, the four-armed warlord with a minigun, the electric slide assassin.

Taken apart, most pieces have precedents. The combination, built on the stair clock, is what is new. I would call it **distinctive, not unprecedented**, and that is a good place to be: it is recognisable ("it's like X, but...") and still has a hook of its own.

## Is it interesting enough?

On paper, yes: the loop has a clear shape (arrive, work, decide, race, collapse, repeat), a reason to look at the other players, a horror layer that already works in the engine, and an ending that people will talk about. The part that excites me most is the moment you described without quite saying it: *you have unlocked the stairs, the clock is running, and three other teams are converging on you.*

Where it is **thin** is everything *between* those moments: what is it like to spend four minutes on a floor? The missions, the monsters' behaviour, the movement and the shooting have to be satisfying by themselves, in a single-player build, before any of the multiplayer matters. That is also where you are already working (movement, weapons, lighting, flashlight, slide), which is the right order.

## What could sink it

Listed by how much I would worry, most first.

1. **Scope.** Sixteen players, procedural floors, a roster of characters with three abilities each, a monster zoo, bosses every ten floors, a shop, a safe zone, a commentator, all multiplayer, built by one person on a phone. Each item is reasonable; together they are a studio's worth. The answer is stages (see `07-roadmap.md`): a solo slice that is fun, then co-op, then two teams, then more. Every stage must be a game you could stop at.
2. **Multiplayer population.** A PvP game needs players at the same time. If four teams of four are needed to start a match, most matches will never start. The fix is **AI contestants** (teams filled with bots that use the same rules) from the first multiplayer build; it is also how a solo player gets the PvPvE game, and how you test.
3. **Netcode.** Browser WebAssembly can only use WebSockets (TCP). That is fine for a turn-based or slow game and workable for 4 to 8 players at 20 to 30 updates a second with a server in charge; sixteen players with projectiles is much harder. See `06-multiplayer.md`.
4. **Stalemate.** If everyone is afraid to start the stair clock, nobody does. There has to be pressure that makes waiting costly (the floor getting angrier, the show's producers forcing it open). See `01-game-loop.md`.
5. **Dead time.** A player eliminated on floor 4 of a match that lasts an hour is bored for fifty minutes. The audience idea (the dead watch the broadcast and can affect it) fixes this and fits the setting; see `06-multiplayer.md`.
6. **Phone performance.** The target device is a Galaxy A13 in a browser. Every system you add (more monsters, more players, bigger floors) spends the same small budget. Keep the diagnostics overlay and look at it after every change.
7. **Content and ratings.** One character's background (see `02-characters.md` and `05-world-and-lore.md`) is likely to cause trouble with app stores and age ratings, and is a lot to carry for a character who is otherwise a playful jester-assassin. I flag it plainly in those documents and leave the decision with you.
8. **Names.** "Brouugenxxxgerrrrr" and "Elelelelele" are hard to read, type and say, which matters in a game where teammates shout at each other. Keep the full names for the lore and give each a short call name for the screen and the voice chat (for example "Brou" and "Lele").

## What I would do with this

- Make the solo survival game the first thing that is fun, using the single-player tower you have. Then add the stair clock to it, because the clock is the heart of the multiplayer loop and it can be tested alone (you against the collapse).
- Make sight, hearing and smell real, and tie the flashlight and noise to them (`03-monsters-and-senses.md`). That gives the horror its rules and gives sabotage something to act on.
- Build characters in the order that exercises the most new systems for the least work (`07-roadmap.md`).
- Do not start on the shop, bosses or lore delivery until the loop of a floor is fun with no rewards at all.
