// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/player.md, section "player_types.h and player_controller.c"
// ============================================================================
#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include "player_types.h"
#include "../dungeon/dungeon_types.h"

#define PLAYER_MAX_HP        100.0f
#define PLAYER_HIT_GRACE_SECS 0.9f    // no further damage for this long after a hit
#define PLAYER_KNOCKBACK_SPEED 7.5f   // m/s at the moment of a hit, decaying
#define PLAYER_KNOCK_DECAY     6.0f   // per second; the push covers about 1.2 m

// Movement and stamina. Speeds are metres per second on the floor, accelerations metres per second squared.
#define PLAYER_WALK_SPEED      4.5f
#define PLAYER_SPRINT_SPEED    7.6f
#define PLAYER_ACCEL           30.0f   // speeding up
#define PLAYER_DECEL           40.0f   // slowing down when the stick is let go
#define PLAYER_AIR_CONTROL     0.35f   // share of the acceleration available in the air
#define PLAYER_JUMP_SPEED      6.0f

#define PLAYER_STAMINA_MAX     100.0f
#define STAMINA_SPRINT_DRAIN   17.0f   // per second while sprinting: a full bar is about six seconds
#define STAMINA_REGEN          15.0f   // per second once resting
#define STAMINA_REGEN_DELAY    1.0f    // seconds after the last effort before it starts coming back
#define STAMINA_JUMP_COST      12.0f
#define STAMINA_SPRINT_MIN     15.0f   // a sprint can only be started with more than this; one that is going carries on to zero
#define STAMINA_FRESH          60.0f   // at or above this stamina there is no fatigue; below it the player slows smoothly, the lower the slower
#define STAMINA_EXHAUST_RECOVER 30.0f  // an exhausted player stays slow until stamina is back to this
#define FATIGUE_MIN_MUL        0.45f   // speed and acceleration at zero stamina, as a share of fresh (eased in from STAMINA_FRESH)
#define EXHAUSTED_EXTRA_MUL    0.90f   // exhausted: a little slower again on top
#define EXHAUSTED_REGEN_MUL    0.70f
#define SPRINT_FOV_KICK        6.0f    // degrees added to the field of view at full sprint

// Sliding. Only from a sprint, on the ground. It starts a bit faster than a sprint and runs down over PLAYER_SLIDE_SECS, so
// it is a burst for getting out of the way, not a way to travel; it costs stamina at the start and nothing more.
#define PLAYER_SLIDE_SPEED_MUL 1.25f   // start speed, as a share of the sprint speed
#define PLAYER_SLIDE_END_SPEED 3.5f    // m/s the slide has run down to at the end
#define PLAYER_SLIDE_SECS      0.85f
#define PLAYER_SLIDE_COOLDOWN  0.45f   // after it ends, before another may start
#define STAMINA_SLIDE_COST     18.0f   // about a second of sprinting; a slide needs at least this much in hand
#define PLAYER_SLIDE_STEER     1.4f    // radians a second the stick can turn the slide
#define PLAYER_SLIDE_DROP      0.25f   // metres the camera drops
#define PLAYER_SLIDE_NOISE     14.0f   // how far a slide is heard: loud, but less than a sprint's 20

void InitPlayerController(PlayerController *player, float startX, float startZ);
// The game runs the rules on fixed steps but turns the camera every frame, so the aim never waits for a step:
//   PlayerApplyLook(&p, lookDelta)    every frame: turns yaw and pitch (lookDelta in canvas pixels)
//   PlayerSyncCamera(&p)              every frame after it: the camera's direction follows (its position stays as the last step left it)
//   PlayerStep(&p, move, dt, map)     every step: movement, stamina, sliding, falling, the camera's position
// moveDelta is the stick or keys: x to the right, y negative forward, length 0 to 1. Set player->wantSprint first.
void PlayerApplyLook(PlayerController *player, Vector2 lookDelta);
void PlayerSyncCamera(PlayerController *player);
void PlayerStep(PlayerController *player, Vector2 moveDelta, float dt, const FloorMap *map);
// All three in one call (look, then a step), for the tests and anything that has no frame/step split.
void UpdatePlayerController(PlayerController *player, Vector2 moveDelta, Vector2 lookDelta, float dt, const FloorMap *map);
// Jumps if standing on something. Costs stamina; when there is too little, or the player is exhausted, it is a weak hop.
// Returns true if the player left the ground.
bool PlayerTryJump(PlayerController *player);
// Starts a slide: needs a sprint in progress, the ground under the feet, STAMINA_SLIDE_COST in hand, no exhaustion and no slide
// still cooling down. Returns whether it started (and then the cost is taken).
bool PlayerTrySlide(PlayerController *player);
// How fast the player is moving on the floor plane right now, m/s.
// The fatigue factor, 1 when fresh down to FATIGUE_MIN_MUL at zero stamina: what speed and acceleration are multiplied by.
float PlayerFatigue(const PlayerController *player);
// How far the player can be heard, metres: standing about 3, walking up to about 8, sprinting about 20, with gasping
// when tired or exhausted added. Enemies use it (see enemy_manager.h).
float PlayerNoise(const PlayerController *player);
float PlayerSpeed(const PlayerController *player);
// Puts the player at pos facing yaw, with no vertical speed and the camera snapped to the new height.
void TeleportPlayer(PlayerController *player, Vector3 pos, float yaw);
// Hurts the player. from is where the blow came from: the knockback pushes away from it.
// Returns false, and does nothing, while the player is in the grace period or already dead.
bool DamagePlayer(PlayerController *player, float amount, Vector3 from);
// Gives health back, up to the maximum. False, and does nothing, when dead or already at full health.
bool HealPlayer(PlayerController *player, float amount);
// Full health, no effects. Used when the player respawns.
void ResetPlayerHealth(PlayerController *player);
void DrawPlayerDebug(const PlayerController *player);

#endif

