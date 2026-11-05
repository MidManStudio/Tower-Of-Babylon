// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "enemy_manager.h"
// ============================================================================
#ifndef ENEMY_MANAGER_H
#define ENEMY_MANAGER_H

#include <stdbool.h>

#include "enemy_types.h"
#include "../dungeon/dungeon_types.h"

#define MAX_FLOOR_ENEMIES 24

// How close to a step or railing of the flight a monster's body may come. Its own radius (up to 0.7 m) would be wider than a
// 0.5 m tread, so a fat monster could never get onto the first step; the player's own clearance is the same 0.3 m.
#define ENEMY_STAIR_CLEARANCE 0.3f

void InitEnemiesSystem(void);

// Clears the pool and spawns the enemies of one floor. The layout is a
// function of the floor's seed, so the same floor always gets the same enemies.
// tier is normally the floor number and scales count and health.
void SpawnEnemiesForFloor(const FloorMap *floorMap, int tier);

// One more enemy at a place the game chooses (the unlock console calls them). Returns its slot, or -1 when every slot is
// in use. alerted: it starts out hunting, as if it had heard the player.
int Enemies_SpawnAt(Vector3 pos, int tier, bool alerted);

// Read-only look at one enemy slot (0 .. MAX_FLOOR_ENEMIES - 1), alive or not; NULL for a slot that does not exist. For the missions,
// the HUD and the tests; the enemies stay owned by the manager.
const Enemy *Enemies_At(int slot);

// Every monster walks this many times faster (1 = normal): the stair clock raises it when the top door opens. A new floor
// (SpawnEnemiesForFloor) puts it back to 1.
void Enemies_SetRage(float speedMultiplier);

void UpdateEnemiesSystem(float dt, Vector3 playerPos, const FloorMap *floorMap);

// How far the player can be heard right now, metres, set once a frame by the caller (standing about 3, walking
// about 8, sprinting about 20, plus gasping when exhausted, and a gunshot adds its own for a moment). A cylinder enemy
// within that distance notices the player even through a wall, and a spider hunts from farther away. Without a call it
// is 0 and enemies rely on sight alone, as before.
void Enemies_SetPlayerNoise(float metres);
float Enemies_PlayerNoise(void);
// Draws the enemies within 40 m of viewer, with their attack and hit animations.
void DrawEnemiesSystem(Vector3 viewer);
void UnloadEnemiesSystem(void);

// Damages the first enemy whose cylinder overlaps a sphere at pos. Returns true on a hit.
bool CheckAndHandleEnemyDamage(Vector3 pos, float radius, float damage);

// A melee blow from eye, aimed along the floor-plane direction (fx, fz) (unit). Hits every enemy
// within reach and inside the arc (arcDeg either side of straight ahead), cylinders and spiders
// alike, and pushes struck cylinders back by knockback metres. Returns how many were hit.
// A cylinder must be level with the eye (within its height, plus 0.3 m); a spider is judged in 3D.
int EnemiesMeleeHit(const FloorMap *floorMap, Vector3 eye, float fx, float fz, float reach, float arcDeg, float damage, float knockback);

// Running totals of hits and kills the player has landed (damage above zero only), for the
// hit marker: a change since the last frame means a hit, or a kill, happened.
unsigned EnemyHitsLanded(void);
unsigned EnemyKillsLanded(void);

int CountActiveEnemies(void);

// The nearest enemy that is awake (chasing or attacking) within maxDist of `from`: where it is and how far. For sound: the
// footsteps and the first growl of what is coming. False when none is.
bool Enemies_NearestAwake(Vector3 from, float maxDist, Vector3 *at, float *dist);

// Attacks on the player. An enemy in reach needs ENEMY_WINDUP_SECS of contact before its
// first blow, then strikes every ENEMY_COOLDOWN_SECS while the contact lasts. Leaving reach
// resets the wind-up.
#define ENEMY_HURT_SECS     0.15f   // flash and stagger after being hit
#define ENEMY_LUNGE_SECS    0.25f
#define ENEMY_WINDUP_SECS   0.35f
#define ENEMY_COOLDOWN_SECS 1.10f
#define CYLINDER_DAMAGE     10.0f

// Advances every attack timer by dt and reports one blow landing this frame, if any:
// its damage, and where it came from (for knockback). playerPos is the player's feet.
bool EnemiesAttackPlayer(float dt, Vector3 playerPos, float playerHeight, float *outDamage, Vector3 *outFrom);

#endif
