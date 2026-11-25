# Shooting in a networked game: the design and its foundations

## The principle
The server is the only one that decides what happened. A player's own screen is allowed to be ahead of the server for what only that player does (moving, and soon firing: the muzzle flash, the recoil, the ammo count, the first part of the bullet's flight), and is corrected by the server's answer; everything else on the screen (other players, monsters, what hit what) is what the server said, shown a little late and smoothly. **Movement already works this way** (`net.md`: the client predicts with `NetSim_Step`, the server answers every tick with the owner's state, the client re-runs from the answer, and a correction fades in as `errOffset`). This document is how shooting joins it, taken from the owner's Unity packages (`MidManStudio_Unity`: `com.midmanstudio.projectilesystem`, `com.midmanstudio.netcode`) and ported as design, not linked: the Rust `projectile_core` crate is built around Unity's memory layout and would be a second native library (Android, web) next to a C projectile system that is already tested.

## What the Unity design is, and where it lands here
| Unity package | Idea | Here |
|---|---|---|
| `ProjectileFireRequest` (config id, origin, direction, `ClientFireTick`, 16-bit `FireSeed`) | one small request; every peer regenerates the pellets from the seed | **`NetShot_Seed(playerId, tick, shotIndex)`** (`net_shot.h`): the seed is *computed* on both ends from who, which command tick and which shot, so nothing but the fire button travels; `Gunfire_Shoot`'s `rng` argument already makes the whole spread and the speeds from one number (tested: same seed, same pellets to the last bit) |
| `ClientFireTick` + `MID_LagCompensator.BeginRewind` | the server judges the shot where the targets were when the shooter looked | **`viewLag`** on every command (the client's tick minus the tick the others are drawn at) and **`NetHistory`** (`net_history.h`): the server's record of every player's pose per tick, `NetServer_RewoundPose` |
| `ServerProjectileAuthority` + Rust `tick_projectiles_3d` | projectiles live on the server | step 2 below: a `ProjectileWorld` instance on the server (the projectile pool is process-global today, and the host runs the server and its own client in one process) |
| `ClientPredictionManager` (cosmetic projectile at once, `HitConfirmed` makes it glide to the true hit) | the shooter sees the shot with no delay | step 2: the client makes its own projectiles from the same seed at once; the server's events correct them |
| `WireCompression` (octahedral direction, half-precision origin) | small packets | later: not needed while only the fire bit and the view lag travel |
| `NetworkRPCQueue`, network variables | typed events and replicated state | the `.mdix`-generated layer, after shooting has shown what it needs |

## The steps
1. **Foundations (done, `tower_netshots_foundation`)**: `NetCmd.viewLag` (protocol version 3), `NetClient_ViewLag`, `NetHistory`, the server's recording and `NetServer_RewoundPose`, `NetShot_Seed`, and proof on a real link that the rewound pose is the pose the shooter's screen showed. Nothing a player can see yet.
2. **A shot in a session**: a `ProjectileWorld` instance (pool and impacts as a struct; the single-player functions keep working on a default one), an `Arsenal` per player on the server fed by the command's fire/reload/swap bits, shots made from `NetShot_Seed`, the same shot made at once on the shooter's screen, spawn events to the others, the server's ammo in the owner's state.
3. **Hits**: the server judges each projectile's first ticks against the rewound poses of the other players, health and damage in the session, hit and death events, the owner's health in the owner's state.
4. **Monsters on the server**: spawned from the floor's seed, run only by the server, sent in snapshots and drawn interpolated like players; rewound for hits the same way.
5. **The typed layer** (messages with ids, replicated variables with dirty bits, RPCs), generated from `.mdix`, once 2 to 4 have shown which events and state are needed.

## `net_history.h` and `net_history.c`
**What it does:** a ring of the last 64 server ticks (`NET_HIST_FRAMES`, 2.1 s) holding each player's position and yaw as they were after that tick's step (`NetHistory_Begin`/`Set`), read back exactly (`PoseAt`) or blended between two ticks (`PoseAtF`, yaw the short way round). `NetHistory_RewindTick(nowTick, viewLag)` is `nowTick - viewLag`, never more than `NET_REWIND_MAX` (20 ticks, 0.67 s) back and never before tick 1. `NetServer_Tick` records every started player at the end of each tick, before the snapshots; `NetServer_RewoundPose(server, viewLag, id, &pos, &yaw)` answers for the tick being run.
**Why the pose after the step:** that is exactly what the snapshot of that tick says, which is what the client draws (`NetClient_RemoteView` blends snapshots), so "where the server says the target was at tick V" and "where the shooter saw it" are the same number.
**Limits are deliberate:** a claim of more lag than 20 ticks is judged at 20 (a cheat or a very bad link gets no longer rewind: asking for more never helps); a tick that has left the ring, or a player who was not there, is "not there", never someone else's pose. Only position and yaw are kept: standing height, a slide or a dive will matter for hit boxes (step 3).
**Tests** (`net_shot_headless.c`): the ring's edges (newest, oldest, replaced, not yet run, tick 0, absent players, ids out of range, a frame just begun, a pose set for a tick whose frame is gone), the blend (including a yaw across a full turn, both ways), the rewind clamp.

## `net_shot.h` and `net_shot.c`
**What it does:** `NetShot_Seed(playerId, tick, shotIndex)`: a 32-bit mix (multiply-xorshift) of the three, never 0 (the pellet generator needs a non-zero state). Pinned values are in the test so a different compiler or platform cannot silently disagree with the server. `Gunfire_Shoot(..., &seed)` uses it as its `rng`; the same seed throws the same pellets, directions and speeds, to the last bit (`weapons_headless.c`), and the next tick's seed throws different ones.
**Decision:** the seed is never sent. It costs 2 bytes in the Unity request; here both ends know who fired and at which command tick, so it costs nothing and cannot be chosen by a cheat.

## The view lag
Every command carries `viewLag` (one byte, protocol version 3): `NetClient_ViewLag(client, nowMs, tick)` = the command's tick minus the server tick the others are drawn at on that screen at that moment (the estimated server tick minus `NET_INTERP_TICKS`), rounded, 0 to 255, 0 before the first snapshot. On the server the command for tick T runs in server tick T, so the shot is judged against the poses at `T - viewLag`. On a link with no delay it is 6 (the client runs 3 ticks ahead of the server and draws others 3 behind). **Tests:** the arithmetic (exact values, rounding of 8.7 to 9 and 8.3 to 8, 0 not negative, 255 held, 0 before a snapshot) and, on a real loopback link with one player walking and one watching, over 180 commands: the pose the server rewinds the walker to is the pose the watcher's screen showed, **0.011 m at worst (0.005 m mean)**; judged against where the walker is now it would have been **0.72 m** off on average (a rewind is about 140 times closer here).
**Precision:** the view lag is a whole number of ticks, so the rewound pose is within half a tick of what was drawn: at a sprint (7.6 m/s) up to 0.13 m. A fractional lag can be added to the command later if that matters for hit boxes.

## Decisions to take with the owner before step 2
1. What does a player carry in a session? (Solo: nothing but fists until a gun is found; a session has no pickups yet.)
2. Do players hurt each other, and what happens at 0 health in a session (down, out, respawn at the start)?
3. ~~Do monsters come into sessions (step 4) or do sessions stay player-only for a while?~~ **Answered 2026-11-25: they come in, and they are the G.V.E.X.**, as Flecs entities on the server (`gvex_session.md` is the plan; step 4 below becomes its slices S1 to S4).
4. Is the shot's first part judged only at the muzzle ray (like hitscan, the first 0.25 m steps) or does every flying projectile test against rewound poses for its whole flight (more exact, more work)?
5. Do other players see the shooter's flash and sound (recommended: yes, as a spawn event), and the reload need not be seen (as decided).

## Not seen on a screen or two phones
Nothing in this step changes what is drawn; it only adds a byte to every command (Input packets are 48 bytes, from 44) and a record on the server. Protocol version 3: every device must run this build.
