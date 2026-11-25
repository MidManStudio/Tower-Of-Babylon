# Two players and the G.V.E.X. in one session: the plan

**Decided (2026-11-25):** monsters come into sessions, and they are the G.V.E.X. (`design/10-gvex-dossier.md`); the game uses an ECS for them, **Flecs** (chosen earlier; mid-ecs cannot do this use yet, see mid-engine's `docs/research-ecs-from-a-c-game.md`). This file is the plan from what the code is today to "two players on two phones fighting the same creatures". Nothing in it is built yet except where it says so.

## Where things stand (checked in the code)

- The server runs **only the movement** of players (`NetSim_Step`, `net_move_sim.c`). It does not run `GameSim`; the snapshot is the owner's movement state plus the other players' position and yaw (`NetSnapshot`: 77 floats-and-bytes for the owner, 18 bytes per other player).
- `GameSim` has **one player**, and its monsters, projectiles, pickups, props and stair clock keep their state **at file scope** in their own modules: one simulation per process (`sim.md`, "Not verified" and "For multiplayer"). The host of a session runs the server and its own client in **one process**, so the server's monsters and the client's picture of them cannot both live in those globals.
- Nothing replicates a monster, a shot, a hit or a health value. Sessions have no combat at all; `net_shots.md` step 1 (view lag, pose history, the shot seed) is the only part of combat that exists.

## What an ECS buys here, in one sentence

Flecs worlds are **instances**: a server's world of creatures and a client's world of what the server said can live side by side in one process, which file-scope modules cannot do. A spike confirmed it on this machine: two `ecs_init()` worlds in one program, a system running in one and not touching the other.

**The spike (not in the repo):** the amalgamated `flecs.c` (4 MB) and `flecs.h` (1.2 MB) from the Flecs repository, built as a custom build with `FLECS_CUSTOM_BUILD` and only `FLECS_SYSTEM`, `FLECS_PIPELINE`, `FLECS_TIMER`, `FLECS_MODULE`, `FLECS_PARSER`, `FLECS_QUERY_DSL`, `FLECS_LOG` and `FLECS_OS_API_IMPL`, with `gcc -std=gnu99 -O2`: 10 seconds on one core, a 1.4 MB object. A first run without `FLECS_QUERY_DSL` aborted silently: a system declared with a signature string needs the query DSL addon.

## The path, in slices (each ends in something two phones can check)

**S1: the same ghouls on both screens.**
- Vendor a pinned custom build of Flecs under `third_party/flecs/` (see the decision below).
- A `gvex` module (C, in `src/gvex/`) owns a Flecs world for a session: components `Pos`, `Yaw`, `Health`, `Tier`, `Specimen`, `Variant`, `Senses`; the first system set is the Ghoul (`X0-01`): wake, sense (sight and hearing, the rules of `design/03`), chase the nearest player, step.
- The server world is spawned from the floor seed like the single-player enemies (same lattice rule, `enemies.md`), so the same seed gives the same ghouls.
- Snapshots carry them: one record per creature (id, designation byte, variant and state byte, position, yaw, health in a byte: about 16 to 19 bytes each by the player records' own sizes), so roughly 55 creatures fit in one datagram beside four players (an estimate to measure); more than that needs the nearest N per recipient. The protocol version goes to 5.
- Clients keep a **view world** of what the server said, interpolated like the other players (`NetClient_RemoteView`), and draw from it.
- Test: a headless run with two players and a few ghouls over the pretend network, checking that both views agree with the server's within the interpolation delay, with loss and reordering; and that two worlds in one process do not touch.

**S2: they hurt.** The server applies a ghoul's attack to the player's health (the same `DamagePlayer` rules as single player), the owner's state carries health, and death does something. **Needs a decision** (below).

**S3: shots hurt them.** The fire bit makes the server fire from an `Arsenal` kept per player (`sim.md`, "N players in `GameSim`"), the shot is judged against the creatures' rewound positions (the creature history is the same ring as `NetHistory`, for creatures), health drops, hit and kill events go to everyone, ammo goes in the owner's state, and the shooter's own screen shows the flash at once. This is steps 2 and 3 of `net_shots.md` done for creatures first.

**S4: the second and third creatures.** The Aracnid (the web as a slow and a snare, the light that blinds, the three variants) and the worm swarm with its Prime Specimen. They are where relationships earn their keep: `governs` (Prime to colony), `spawned_by` (MajourBuzzCunt to its brood), `divided_from` (Lumbricus Major). The swarm's 240 dots are not replicated one by one; what a client needs is its core, its worms' heads and its state, and the dots can be simulated from them on each screen (to be designed, and to be checked for determinism).

**S5: the typed layer.** `monsters.mdix` becomes the registry (designations, tiers, senses, numbers) and generates what S1 to S4 wrote by hand (`net_shots.md` step 5).

Single-player keeps its own enemy modules until the Flecs ones reach parity, then the old ones go; two implementations of the same creature are the thing to avoid for long.

## Decisions (mine, to be confirmed or changed)

1. **Vendor Flecs or fetch it.** Recommended: vendor the pinned, custom-built amalgamation under `third_party/flecs/` (about 5 MB of source in the repo), because the work happens on a phone over GitHub and a build that needs the network for a dependency is the build that fails at the wrong moment. Not decided; it changes the repository's size.
2. **Death in a session** (needed before S2): down and out of the match, back at the start of the floor after a delay (single player's rule), or spectating (`design/06-multiplayer.md`, "The audience"). The design already leans to the audience.
3. **Hit testing for S3** (`net_shots.md`, decision 4): hitscan against rewound poses first, projectiles judged for their whole flight later. Recommended because it is the smaller step and the shot seed already makes every pellet reproducible.
4. **Teams** (players hurt each other but not teammates, as you decided) are a relationship too; they are not needed for S1 to S3 if a session starts with one team.

## Not verified

- **Flecs on the other targets:** the web build (emscripten) is not needed for sessions, which are native only, but single-player will use Flecs once the old modules go and that has not been tried; the Android armv7 build with the NDK compiler has not been tried either, and neither has the cost on a phone.
- **The snapshot size estimate** and how many creatures one datagram holds.
- **The Rust driver's cost with a larger snapshot** on a phone's radio.
