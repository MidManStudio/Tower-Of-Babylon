# Multiplayer netcode (`src/net/`)

## Overview

This is the first multiplayer step the owner chose: **a game protocol, a server loop and a client, tested over a pretend network with delay, loss and reordering**, all in the sandbox with no real network. It runs the game's own player movement on a server and on up to four clients and keeps them in step. **The running game does not use it yet** (`main.c` is untouched; there is no menu, no server program and no real transport), so nothing about single-player can have changed.

What exists:

- a **wire format** (`net_wire`, `net_proto`): bytes in and out of a buffer with every access checked, and five messages (Hello, Welcome, Disconnect, Input, Snapshot);
- a **transport seam** (`net_transport.h`): the only thing the server and client know about the network;
- a **pretend network** (`net_loopback`): delay, jitter, loss, doubling, reordering, blackouts, with a fixed random generator so a run is the same every time;
- the **movement simulation** both ends share (`net_move_sim`): the real `PlayerStep`, jump and slide, from a rounded command;
- an **authoritative server** (`net_server`) and a **predicting client** (`net_client`).

### The architecture decision, and what it leaves open

The protocol, the server loop and the client logic are **C**, written against the small transport interface; the network itself (native QUIC, the browser's WebTransport) stays in the Rust `mid-net` crates and plugs in behind that interface later, through a C interface that has yet to be added to `mid_net.h` (today it has only the codec: encode and decode of two packet kinds, no connect, send or poll). Reasons: the game is C and the client's prediction needs the C movement code, so the client logic has to be C anyway; the server then shares the very same files; and everything but the final transport can be verified in this sandbox, whose Rust is held to 1.75. If the server is ever better written in Rust (the earlier plan was a Rust binary calling the C simulation), the seam is the same: the transport trait on one side, the C simulation on the other, and what changes is who owns the loop. The CI results the owner sent (mid-net 77 tests on rustc 1.99, the wasm one green) mean the Rust transports build, so the missing piece is the C interface to them, not the transports.

## The protocol (`net_proto.h`)

Two channels, as a QUIC connection gives: **datagrams** that may be lost, doubled and reordered (Input, Snapshot), and a **reliable ordered** channel (Hello, Welcome, Disconnect). A packet starts `'T' 'B'` and a kind byte.

| Message | Direction | Channel | Size | What it carries |
|---|---|---|---|---|
| Hello | client to server | reliable | 17 B | protocol version, the chosen character id |
| Welcome | server to client | reliable | 27 B | version, tick rate, the player's id, the floor's seed and number, the server's tick, the spawn |
| Input | client to server | datagram | 40 B | the tick, the newest snapshot seen, and **four commands** (that tick and the three before it) |
| Snapshot | server to client | datagram | 71 to 125 B | the tick, whether the player has started, how early the newest command came, the owner's full movement state, up to three other players (rounded) |
| Disconnect | either | reliable | 4 B | a reason (normal, version, full, timeout, bad) |

A **command** (`NetCmd`, 7 bytes) is one tick of the player's wishes already rounded to what the wire carries (stick to a 127th, yaw to a 65536th of a turn, pitch to a 32767th of 89 degrees, eight buttons): **both ends run the game from the rounded command**, so the client's prediction and the server's simulation start from the same numbers. The buttons already include fire, use, swap and light, which this slice does not use yet.

The **owner's state** is sent as full floats (15 of them) and not rounded, because a rounded state could not be run on from; other players are told rounded millimetres.

**Decoding is strict**: wrong magic, an unknown kind, a short packet, a byte too many, a count over its limit, a NaN or infinity, an unknown flag bit, a character id that is not lowercase letters, digits and `_`, a player id out of range: all refused. Tested by cutting every message at every length, spoiling bytes, 300000 random and spoilt packets (what is accepted must re-encode to exactly the same bytes), and the whole test under AddressSanitizer and UndefinedBehaviorSanitizer with no finding.

**Bandwidth** at 30 snapshots a second: about 1.2 KB/s up and 2.1 to 3.8 KB/s down per client (full snapshots, no deltas yet).

## How it works

### The server (`net_server.c`)
It owns the floor (made from the seed, as every client makes it) and every player. **It never takes a position from a client**, only commands. Each tick (30 Hz, driven by the caller): for each player it takes the command that arrived for this tick and steps the player with `NetSim_Step`; then it sends every client a snapshot.

- A command carries the tick it is for. One **already past** is late (refused, counted); one **more than 20 ticks ahead** is refused (a flooded or forged clock); a **second command for a tick** is ignored (the first wins, so repeating a command does not move the player twice).
- A command that **did not arrive in time** is replaced by the last one for 5 ticks (one-off presses removed), then by standing still (the aim kept), then the client is dropped after 5 seconds of silence.
- The player is **not run until its first command arrives** (it starts at that command's tick), so the client and the server begin from the same state.
- It reports back **how early** the newest command came (`margin`, in ticks, negative when late). That is what the client tunes its clock by.
- **Limits:** at most 64 packets per client per poll (a flood waits, the tick is never held up), a stranger's packets counted and thrown away.

### The client (`net_client.c`)
- **Prediction.** It runs its own player the moment the player acts, with the same `NetSim_Step`, keeps the last 64 ticks of commands and resulting states, and sends each command at once, repeated in the next three packets, so a lost packet is nearly always covered by the next (measured: 0.2% of commands missing at their tick at 20% loss).
- **Reconciliation.** A snapshot for tick T carries the server's state after tick T. The client compares it with its own record of tick T. Within 2 cm (and the same stamina, speed and slide state): nothing to do. Otherwise it **puts the server's state back and runs its own commands since again** to the present, so a correction is a change of the present and not a rubber band back in time. The jump that makes is **not shown**: the drawn position is the predicted one plus a difference that fades away (85% kept each tick), unless it is over two metres (a teleport, a respawn), which is shown as it is.
- **The clock.** Its tick count runs ahead of the server's (at first by the measured round trip plus 3 ticks) so a command is there before the server needs it. If the worst margin of the last 20 snapshots is below 1 it runs 5% faster, above 4 5% slower, until it is back in range (tested: a route that gets 8 times slower makes 48 commands late once, then none; when it is fast again the lead falls from 8 ticks to 6).
- **Other players** are drawn **100 ms in the past**, between the two snapshots that surround that time; with no newer snapshot they are held where they were last seen.

## Modules

### `net_wire.h` and `net_wire.c`
`NetW` and `NetR`: little-endian whatever the machine, every access checked. A writer that runs out of room, or a reader asked for more than is left, sets an error and goes on harmlessly; nothing is read or written outside the buffer. `NetR_Done` is true only when nothing went wrong and every byte was read.

### `net_proto.h` and `net_proto.c`
The messages above, `NetProto_Encode` and `NetProto_Decode`, and the command rounding (`NetCmd_Make`, `NetCmd_Move` which holds the length to 1 so a forged (127, 127) is no faster walk, `NetCmd_Yaw`, `NetCmd_Pitch`). The tick rate (30), the datagram limit (1200 bytes), the player limit (4) and the redundancy (4) are here.

### `net_transport.h`
`NetTransport`: send and receive of datagrams and of reliable messages, as function pointers. Nothing else of the network is known.

### `net_loopback.h` and `net_loopback.c`
A pretend link between two ends with time that only moves when the test says so. Per direction: latency, jitter (which reorders), loss, doubling. The reliable channel never loses and keeps order (a late message holds up the ones behind). A blackout loses datagrams and holds reliable messages until it ends. Datagrams over the limit are refused.

### `net_move_sim.h` and `net_move_sim.c`
`NetSim_Step` is the movement of `SimTick` in `main.c` (slide, sprint, `PlayerStep`, jump, in that order) from a `NetCmd`; `NetSim_Capture` and `NetSim_Apply` take and put back the movement state; `NetSim_Error` says how far apart two are (position, plus anything that would make the sprint differ a second later). **When `SimTick` moves out of `main.c`, this is what it replaces.**

### `net_server.h` and `net_server.c`, `net_client.h` and `net_client.c`
As above.

## Tests: `tests/net_headless.c`

96 checks, each with its reason in its name: the wire; the protocol (round trips to the same bytes, every refusal, command rounding, the fuzz); the pretend network (latency, 20% loss gives about a fifth lost, doubling, reordering, reliable in order, blackouts, same seed same network); the handshake (ids, the same floor on both ends, versions both ways, a full server); **prediction with no delay** (no correction after the start, the client's record of every tick within a ten-thousandth of a metre of the server's, and proof that the script really jumps, sprints and slides on the server); **a bad network** (100 ms round trip, 5% loss: no command missing at its tick, no correction); **a very bad one** (200 ms, 20% loss, 5% doubled: 0.2% missing, no restart from scratch); **reconciliation** (a shove the client could not know about is corrected and the client agrees again; the drawn player glides while the predicted one jumps; a 6 m shove is not glided; a stamina-only difference is corrected); **commands not to be trusted** (before Hello, from the far future, ten copies, late, forged full speed, a flood, junk); **other players** (within 2 cm of the truth at the time shown under 5% loss and jitter, no jumps, held in a half-second blackout, accurate again after it); **leaving and going quiet** (goodbye, a freed slot, a client that goes silent stops where it is and is dropped after 5 s, a client that hears nothing closes); **the clock lead** (adapts both ways).

**Mutation tests (35).** Each rule was broken on purpose and the tests had to fail: every wire check; every decode refusal; no loss, no order, no blackout, no size limit in the pretend network; every server rule (version, late, far future, doubled, flood limit, Hello first, repeat for ever, timeout, margin sign, run before the first command); and on the client never correcting, correcting without replaying, no smoothing, glide over a huge jump, no clock adjustment, no repeated commands, no interpolation, no 100 ms delay, a stamina difference not noticed, and the server ignoring jump or sprint. **Four survived at first and were fixed in the tests:** the server ignoring jumps or sprint (both ends share the movement code, so breaking it on both keeps them in agreement; the tests now prove the script jumps, sprints and slides), and a stamina difference (a big one also changes the speed and so shows up as a position error; a 5-point one isolates the rule).

## Not verified

- **Cross-machine determinism.** Prediction matched to a ten-thousandth of a metre because client and server ran the same binary on one machine. A phone (ARM) and a server (x86 or ARM) may differ in the last bits of a float (fused multiply-add, the maths library). The 2 cm tolerance and the reconciliation are built for that, but how often they fire between real devices is unknown until a phone talks to a server.
- **No real network, no real time.** Everything ran on a made-up clock in steps of a millisecond. Frame pacing on a phone (a frame that takes 100 ms, the app in the background) is not tested; the client runs at most 5 ticks in one update and drops the rest.
- **The transports** (native QUIC, the browser's WebTransport) are not connected; see "what comes next".
- Nothing here has been run in the game or on a screen.

## Known limits (what this slice is not)

- **Movement only.** Shots, hits, enemies, the swarm, doors, stairs, the stair clock, pickups, the inventory, the flashlight and health are all still `main.c`'s. They need `SimTick` and its state out of `main.c` into a simulation that has no window, with the monsters, projectiles and pickups (module-global today, so one simulation per process) as parts of it. That is the next step and the owner's earlier choice.
- **Props.** The player's movement collides with the floor's props (`CheckObstacleCollision`, a module-global list). The tests have no props. A real server and client must build the same props from the floor seed; if they do not, prediction will be wrong near them.
- One floor per server; changing floors is not handled. Players do not collide with one another and all start at the same place. No health or death in the netcode yet.
- Snapshots are whole every tick: no deltas, no interest management, 4 players at most.
- The client's aim is trusted as sent (fine for co-op, not for a competitive mode).
- A returning network after a gap shows the other player jumping to where they now are (they did move); there is no catch-up glide and no extrapolation.
- Only the physical movement state is compared when reconciling; the camera's eye height and head bob are not.
- No encryption or sign-in: the transport (QUIC) will provide the former; the latter is for the lobby.

## What comes next

1. **Move `SimTick` and its state out of `main.c`** into a simulation with no window (commands in, events out), with a replay test at two frame rates; this makes the whole game runnable by the server and replaces `net_move_sim`.
2. **The C interface to the Rust transports** (connect, send, poll in `mid_net.h`, a `NetTransport` over it) and a **server program**; native QUIC first, built by CI.
3. **The client in the game**: a menu to host or join; `NetClient_Update` in the frame loop, the other players drawn.
4. **The browser path**: WebTransport needs a secure page (`localhost` is one, a LAN address is not) and a certificate the browser accepts (a pinned self-signed one must be valid 14 days at most); a WebSocket dev transport is the alternative. The owner has not chosen.

## Fixes and Problems

- **The player controller needs the props function.** Linking it for the first time here failed on `CheckObstacleCollision`; the test supplies "no props" as the movement tests do.
- **A test measured the wrong thing.** The reconciliation test shoved the player 1.7 m, which became 2.4 m once the commands since were run again, over the client's two-metre "do not glide" limit, so it saw no glide. Now a 1 m shove tests the glide and a 6 m one tests the snap.
- **The half-second blackout test's window included the moment the network came back**, so it saw the (correct) jump to where the other player now was. The window ends before that, and a second check says they are accurate again afterwards.
- **The slide never happened in the first script** (a wall stopped the sprint, and a slide needs 2 m/s); found only because a test counted slides. The script now tries more than once.
- A statically allocated pool of link ends was replaced by two ends inside each link before it could bite a test that makes many links.
