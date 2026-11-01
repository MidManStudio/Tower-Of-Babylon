# Multiplayer

## What the design needs

Up to four teams of four, one tower seed, a server's worth of authority over who is alive, who holds what and who opened the stairs, projectiles, monsters that are the same for everyone, a downed state, trading, sabotage, a spectator mode, and (in the PvPvE mode) betrayal. The survival mode needs the same for one team of up to four.

## What the platform allows

- The game runs in a browser as WebAssembly. A browser cannot open raw TCP or UDP sockets. It can use **WebSockets** (over TCP) and, with more work, **WebRTC data channels** (which can be unreliable and unordered, like UDP).
- A page served over plain HTTP can use `ws://` to `localhost` or to the host phone's address on the local network (a page served over HTTPS may only use `wss://`).
- Only the **host** needs Termux: the phone that runs the static file server and the game server. A second player only needs a browser and the host's address. Use a second device, not a second tab: Android pauses background tabs.
- WebSockets are TCP, so one lost packet delays everything behind it. That is acceptable for 4 to 8 players at 20 to 30 updates a second on a good local network, and gets worse with distance and more players. WebRTC data channels can be added later if it matters.

## The design that fits

**An authoritative server.** One server process (Rust is a good fit: `tokio` and `tokio-tungstenite`, a small program that can run in Termux) owns the truth: positions, health, inventories, monsters, projectiles, the stairs and the clock. Clients send their **inputs** (move, look, buttons) and show what the server tells them, predicting their own movement locally so it feels instant and correcting when the server disagrees. A client never says "I hit that monster" or "I have this item"; it says "I pressed fire", and the server decides. This is what keeps betrayal, sabotage and PvP honest, and it is not much more work than trusting the clients if it is done from the start.

**Seeded floors.** The dungeon generator is deterministic from a seed, so the server sends a floor's seed and every client builds the same floor. Monsters, pickups, missions and loot are spawned by the server and sent as entities.

**Share the code.** The movement, weapons, projectiles, inventory and flashlight rules are in plain C with no drawing (they are tested headlessly). The server can run that same code, either as a native C server or, in Rust, with a port of the rules. Running the same C on the server (a small `main` that links the simulation modules) avoids two versions of the truth drifting apart; Termux has a C compiler, and the simulation modules already compile without raylib's drawing. I recommend this over a rewrite in Rust, at least at first.

**Messages (a start):**
- client to server: input frames (sequence number, move, look, buttons) at 30 a second; actions (use, drop, equip, slide) as events.
- server to client: snapshots of nearby entities at 20 a second (position, velocity, state, health), events (shot fired, hit, monster alerted, item picked up, stairs unlocked, countdown started), and the floor seed on a floor change.

## Stages (each is a game you could stop at)

1. **Local multiplayer groundwork, no network.** A second "player" object in the single-player game driven by a scripted bot, to find every place the code assumes one player (there are many: `player` is a global in `main.c`).
2. **A dedicated simulation core.** A server program that runs the simulation without a window and a client that connects, sends inputs and draws what it is told. Two phones, cooperative survival on one seed. This is the stage that tells you whether the network design works.
3. **Teams and the stair clock.** Four players, two teams, the unlock mission, the countdown and the collapse; the first PvP.
4. **AI contestants.** A bot that plays by the same rules (finds missions, fights, flees, opens or waits for the stairs). Fills a lobby so a match can start with whoever is there. This is also how a single player gets the PvPvE game.
5. **Sabotage items and monster luring.** Server-side monster senses (`03-monsters-and-senses.md`) with noise and scent from any player.
6. **More teams, bosses, the betrayal finale.**

## Things to decide early because they are expensive to change

- **Who simulates monsters.** The server, always (otherwise two players see different monsters). Monster senses and pathfinding then run on the server; keep them cheap (the enemy update costs only a few microseconds a frame today, measured natively).
- **How many entities per client.** Send only what is within range of the player (interest management); 16 players and their projectiles to every client is the part that gets heavy.
- **Latency and projectiles.** A rifle round at 85 m/s crosses a corridor in a fraction of a second: either send a shot as an event the client animates and let the server decide the hit (best), or simulate projectiles on the clients and have the server confirm. The swept hit test in `projectiles.md` is already what a server needs.
- **Cheating.** Authoritative simulation means a cheating client can move and shoot but cannot invent items, kills or points. Say now that this is a priority, because PvP with betrayal and rewards attracts it.
- **Lobbies and matchmaking.** Not needed for friends testing on a local network; a real concern later, and the reason for the AI contestants.

## The audience (my suggestion, which fits your setting)

A player who is eliminated stays in the match as **the audience**. They watch the broadcast through the tower's cameras and drones (follow a player, free camera on the floor), hear the commentator, and can take small actions that affect the show: voting for who gets a gift (a battery, a medkit) or who gets a hazard (a trap on a floor, a monster pack), maybe with a small cost in "audience favour" that regenerates. This solves dead time, makes eliminated players part of the sabotage and betrayal without breaking balance (their influence is limited and visible), and uses the broadcast premise as a mechanic. It is also the cheapest way to get a spectator mode, since the cameras already exist in the fiction.
