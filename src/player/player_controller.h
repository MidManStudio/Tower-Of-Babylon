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

// Omnidirectional movement: walking and sprinting work in every direction, and the stick's direction is split into
// forward/back and left/right. Each has its own top speed, as a share of the forward one, so a diagonal lands between the two.
// (Placeholder numbers: Black Ops 6 does not publish its own. They are the one place to tune this.)
#define PLAYER_STRAFE_MUL      0.85f   // left and right
#define PLAYER_BACKPEDAL_MUL   0.70f   // backwards

// Aiming down sights (the weapon's own field of view, time and move speed are in weapons.mdix). The base field of view, and how
// quickly ADS lets go: a share of the time it took to go in.
#define PLAYER_BASE_FOV        60.0f
#define PLAYER_ADS_OUT_SHARE   0.6f
#define PLAYER_ADS_DEFAULT_SECS 0.20f

// Leaning: automatic, never a button, in two cases. (1) At the corner of a wall: a sight line is cast straight ahead from the body and again
// from the eye at full lean to either side; if the wall ahead is close and one of the leaned lines gets PLAYER_LEAN_GAIN_ON metres further than
// the middle one, there is an opening that leaning shows, and the player leans toward it. Facing a flat wall, or a long way from it, nothing
// happens. (2) Beside a wall: a wall within PLAYER_LEAN_SIDE_NEAR of the body on one side, and none on the other, and the player leans away from
// it (a wall on the left: a lean to the right), facing any way. Between two close walls there is nowhere to lean to, so no lean. A corner
// opening wins when both are there, and it is the full lean: the beside-a-wall one is PLAYER_LEAN_SIDE_SCALE of it. The eye moves sideways and the view tilts, and a lean can never put the eye in a wall.
#define PLAYER_LEAN_NEAR_WALL  1.5f    // metres: the wall ahead is this close or closer for a lean to start
#define PLAYER_LEAN_NEAR_LET_GO 2.0f   // ...and it lets go when the wall is further than this (so the edge of the range does not flicker)
#define PLAYER_LEAN_SIGHT      8.0f    // metres: how far the sight lines look
#define PLAYER_LEAN_GAIN_ON    1.0f    // a lean starts when a leaned line sees this much further than the middle one
#define PLAYER_LEAN_GAIN_OFF   0.5f    // ...and lets go when it sees less than this
#define PLAYER_LEAN_DWELL      0.10f   // seconds the above must hold before the lean starts (walking past a corner is not a peek)
#define PLAYER_LEAN_SIDE_NEAR   0.80f  // metres from the body's centre: a wall this close beside the player (and none on the other side) starts a lean away from it
#define PLAYER_LEAN_SIDE_LET_GO 1.00f  // ...and it lets go when that wall is further than this (the body is 0.35 m across, a corridor tile 2 m)
#define PLAYER_LEAN_SIDE_REACH  1.40f  // metres: how far the side lines look
#define PLAYER_LEAN_SIDE_SCALE  0.80f  // a lean away from a wall beside the player goes this far (shift, tilt, dip and slowdown): 20% less than the corner lean
#define PLAYER_LEAN_SIDE_ROOM   0.20f  // the free side needs this much room for the eye for a side lean to start (half of it to hold)
#define PLAYER_LEAN_SHIFT      0.40f   // metres the eye moves sideways at full lean
#define PLAYER_LEAN_ROLL       0.1745f // radians the view tilts at full lean (10 degrees)
#define PLAYER_LEAN_DIP        0.06f   // metres the eye drops at full lean
#define PLAYER_LEAN_SECS       0.15f   // from upright to full lean
#define PLAYER_LEAN_MOVE_MUL   0.65f   // walking speed share at full lean
#define PLAYER_LEAN_CAM_CLEAR  0.40f   // the eye stays at least this far from any wall (the near plane is 0.25 and the corners of the view reach about 0.39)

#define PLAYER_GRAVITY         14.0f   // m/s squared

// Diving: from a sprint that goes left, right or back (the slide is for forward, so the dive button takes its place) straight from the ground,
// and from the air after a sprinting jump ("run, jump, dive": the way forward, where the slide comes first), in any direction (forward, left,
// right, back) as the stick was
// pushed when the DIVE button was tapped. A fixed arc: PLAYER_DIVE_DIST metres in PLAYER_DIVE_SECS whatever height it was asked at,
// then a roll of PLAYER_ROLL_SECS that runs down from PLAYER_ROLL_SPEED. Placeholder numbers, like the slide's.
#define PLAYER_DIVE_DIST       3.0f    // metres along the floor, in the air
#define PLAYER_DIVE_SECS       0.5f    // the airtime: the arc is made to land after exactly this long on level ground
#define PLAYER_ROLL_SECS       0.4f
#define PLAYER_ROLL_SPEED      3.5f    // m/s at the landing, running down linearly to PLAYER_ROLL_END_SPEED
#define PLAYER_ROLL_END_SPEED  1.0f
#define PLAYER_DIVE_DROP       0.23f   // metres the camera drops during a dive, the roll and lying (0.63 standing -> 0.40, see PLAYER_CAM_MIN_HEIGHT)
#define PLAYER_CAM_MIN_HEIGHT  0.40f   // the camera never goes lower than this above the floor under it: the near plane is 0.25 and the corners of the view reach about 0.39,
                                       // so a lower camera sees under the floor (the first dive's 0.08 m did)
#define PLAYER_LIE_PUSH        0.25f   // lying: the stick must move this far (of 1) from where it was when the roll ended, or be let go and pushed, to get up
#define PLAYER_DIVE_NOISE      16.0f   // how far a dive is heard
#define PLAYER_DIVE_GRACE_SECS 0.0f    // no damage for this long after a dive starts: 0, because Call of Duty gives none (set to 0.5 to cover the airtime)
#define STAMINA_DIVE_COST      25.0f   // a dive needs at least this much in hand, and a player who is not exhausted

#define PLAYER_STAMINA_MAX     200.0f  // twice the old 100. Drain and costs below are unchanged, so everything the bar pays for lasts twice as long
#define STAMINA_SCALE          (PLAYER_STAMINA_MAX / 100.0f)   // the thresholds below were set for a bar of 100: they grow with it so the bar keeps its shape
#define STAMINA_SPRINT_DRAIN   17.0f   // per second while sprinting: a full bar is about twelve seconds
#define STAMINA_REGEN          15.0f   // per second once resting (unchanged: a bigger tank takes longer to fill)
#define STAMINA_REGEN_DELAY    1.0f    // seconds after the last effort before it starts coming back
#define STAMINA_JUMP_COST      12.0f
#define STAMINA_SPRINT_MIN     (15.0f * STAMINA_SCALE)   // a sprint can only be started with more than this; one that is going carries on to zero
#define STAMINA_FRESH          (60.0f * STAMINA_SCALE)   // at or above this stamina there is no fatigue; below it the player slows smoothly, the lower the slower
#define STAMINA_EXHAUST_RECOVER (30.0f * STAMINA_SCALE)  // an exhausted player stays slow until stamina is back to this
#define STAMINA_LOW            (25.0f * STAMINA_SCALE)   // below this the player breathes hard (the view bobs, enemies hear it)
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
#define PLAYER_SLIDE_MIN_FORWARD 0.70f // a slide only goes forward: the sprint must be within 45 degrees of straight ahead (left, right and back are for the dive)
#define PLAYER_SLIDE_STEER     1.4f    // radians a second the stick can turn the slide
#define PLAYER_SLIDE_DROP      0.25f   // metres the camera drops (held at PLAYER_CAM_MIN_HEIGHT above the floor, so the real drop is 0.23)
#define PLAYER_SLIDE_NOISE     14.0f   // how far a slide is heard: loud, but less than a sprint's 20

// Stim shot (placeholder numbers): for PLAYER_STIM_SECS seconds stamina gets STAMINA_STIM_RATE a second on top of whatever else is going
// on (regeneration, the sprint's drain, the regeneration pause), so a sprint keeps filling it (net 53 a second), and exhaustion is over in
// a second; PLAYER_STIM_HEAL health comes back, spread evenly over the time. The time is long enough to fill an empty bar while sprinting.
#define PLAYER_STIM_SECS       4.0f
#define STAMINA_STIM_RATE      70.0f
#define PLAYER_STIM_HEAL       15.0f

void InitPlayerController(PlayerController *player, float startX, float startZ);
// The game runs the rules on fixed steps but turns the camera every frame, so the aim never waits for a step:
//   PlayerApplyLook(&p, lookDelta)    every frame: turns yaw and pitch (lookDelta in canvas pixels)
//   PlayerSyncCamera(&p)              every frame after it: the camera's direction follows (its position stays as the last step left it)
//   PlayerStep(&p, move, dt, map)     every step: movement, stamina, sliding, falling, the camera's position
// moveDelta is the stick or keys: x to the right, y negative forward, length 0 to 1. Set player->wantSprint first.
// (Aiming down sights makes it turn less per pixel, by the ratio of the two fields of view, so a pixel moves the crosshair across the
// same stretch of world however zoomed in.)
void PlayerApplyLook(PlayerController *player, Vector2 lookDelta);
void PlayerSyncCamera(PlayerController *player);
void PlayerStep(PlayerController *player, Vector2 moveDelta, float dt, const FloorMap *map);
// All three in one call (look, then a step), for the tests and anything that has no frame/step split.
void UpdatePlayerController(PlayerController *player, Vector2 moveDelta, Vector2 lookDelta, float dt, const FloorMap *map);
// Jumps if standing on something. Costs stamina; when there is too little, or the player is exhausted, it is a weak hop.
// Returns true if the player left the ground.
bool PlayerTryJump(PlayerController *player);
// Starts a slide: needs a FORWARD sprint in progress (see PlayerSprintingForward), the ground under the feet, STAMINA_SLIDE_COST in hand,
// no exhaustion and no slide still cooling down. Returns whether it started (and then the cost is taken).
bool PlayerTrySlide(PlayerController *player);
// A sprint that is going within 45 degrees of straight ahead (the velocity's direction against where the player faces): the only kind a slide can start from.
bool PlayerSprintingForward(const PlayerController *player);
// Whether a sprint is going left, right or back (not within 45 degrees of ahead, which is the slide's): the one the dive can start from on the ground.
bool PlayerSprintingSideways(const PlayerController *player);
// Whether a dive can be asked for now: alive, not already diving, rolling or lying, and either on the ground in a sideways or backward sprint, or in
// the air from a jump that began from a sprint (or a slide).
// This is when the DIVE button exists, whether or not there is the stamina for it.
bool PlayerDiveAvailable(const PlayerController *player);
// Starts a dive: PlayerDiveAvailable, no exhaustion and STAMINA_DIVE_COST in hand (taken). The direction is read now: moveDelta is the stick
// as for PlayerStep (x right, y negative forward); with the stick centred it is straight ahead. Returns whether it started.
bool PlayerTryDive(PlayerController *player, Vector2 moveDelta);
// Starts a stim shot: false (and nothing happens) when dead or when one is still running. The caller takes the item.
bool PlayerTryStim(PlayerController *player);
// Top speed in a direction, as a share of the forward speed: forward and right are the unit direction in the player's own frame
// (forward 1, right 0 is straight ahead). 1 ahead, PLAYER_STRAFE_MUL to the side, PLAYER_BACKPEDAL_MUL backwards, smoothly between.
float PlayerDirectionMul(float forward, float right);
// ADS amount, 0 hip to 1 fully aimed, eased (smoothstep) from aimBlend, for everything that looks or counts by how far in the player is.
float PlayerAimAmount(const PlayerController *player);
float PlayerAimEase(float aimBlend);        // the same easing of any aimBlend (an interpolated one, for drawing between steps)
// The field of view for a given aimBlend (the camera's own, or an interpolated one for drawing between steps), sprint kick included.
float PlayerViewFov(const PlayerController *player, float aimBlend);
// The camera's up vector for a view direction and a leanBlend: straight up when upright, tilted toward the lean.
Vector3 PlayerViewUp(Vector3 forward, float leanBlend);
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

