// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "spider.h"
// ============================================================================
#ifndef SPIDER_H
#define SPIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"
#include "../dungeon/dungeon_types.h"
#include "../shared/health.h"

#define SPIDER_LEGS 8
#define MAX_SPIDERS 4

typedef struct {
    Vector3 foot, footNormal;           // current foot tip and the surface under it
    Vector3 from, fromNormal;           // swing start
    Vector3 to, toNormal;               // swing end
    float   t, dur, liftH;
    bool    swinging;
    bool    grounded;                   // false while swinging or when no ground was found
    bool    probing;                    // front leg reaching for the next surface
    bool    probeDone;
    float   phase, prevPhase, phaseOffset;
    bool    due;                        // phase wrapped and the foot has not been re-placed yet
    int     row;                        // 0 front .. 3 rear
    int     side;                       // -1 left, +1 right
    Vector3 root, knee, ankle;          // joints, refreshed every update for drawing
} SpiderLeg;

typedef struct {
    bool     active;
    uint32_t id;

    // Anchor: a point on the current surface, its normal and the direction of travel.
    Vector3 anchor, normal, heading;
    float   speed;

    // Surface switch in progress.
    bool    transActive, transConvex;
    Vector3 transPoint, transNormal, transHeading;
    float   transTimer;

    // Body, following the legs.
    Vector3 pos, vel, up, fwd;

    float     gaitPhase;
    SpiderLeg legs[SPIDER_LEGS];

    HealthComponent health;
    Color    tint;

    // Counters for tests and the HUD.
    float attackTimer;                  // counts down while touching the player; a blow lands at 0
    float rear;                         // 0..1: rearing up before a bite (animation only)
    float lunge;                        // 1 at the moment of a bite, fades to 0 (animation only)
    float hurtTimer;                    // seconds left of the flash and stagger after being hit

    int switches;
    int failsafes;
    int lostSurface;
} Spider;

// ---- Locomotion (no AI) ----------------------------------------------------
// Places a spider on a surface. normal and heading need not be exact.
void Spider_Init(Spider *s, const FloorMap *f, Vector3 anchor, Vector3 normal, Vector3 heading);

// Advances one spider. want is the direction it would like to travel, in world
// space. It is projected onto the current surface. wantSpeed is in m/s.
void Spider_Step(Spider *s, const FloorMap *f, float dt, Vector3 want, float wantSpeed);

// Head, thorax and abdomen spheres, used for pushing out of terrain, hit tests and drawing.
void Spider_BodySpheres(const Spider *s, Vector3 centers[3], float radii[3]);

// ---- Enemy system ----------------------------------------------------------
void Spiders_Init(void);
void Spiders_SpawnForFloor(const FloorMap *f, int tier);
void Spiders_Update(float dt, Vector3 playerPos, const FloorMap *f);
// Sets how far the player can be heard, metres (see Enemies_SetPlayerNoise). A spider hunts from a base of 12 tiles of
// path, plus a tile for every metre of noise, up to the whole map (22 m, a hunt of 34 tiles, until it is called).
void Spiders_SetNoise(float metres);
// Draws the spiders within 40 m of viewer. Rearing, lunging and hit flashes are drawn here.
void Spiders_Draw(Vector3 viewer);
void Spiders_Unload(void);
#define SPIDER_DAMAGE        15.0f
#define SPIDER_WINDUP_SECS   0.45f   // the rear-up the player can see coming
#define SPIDER_COOLDOWN_SECS 1.20f
#define SPIDER_REACH         0.60f   // beyond touching: a bite lunges this far
#define SPIDER_LUNGE_SECS    0.25f   // how long the lunge shows
#define SPIDER_HURT_SECS     0.18f   // flash and stagger after a hit

// Advances the spiders' attack timers and reports one blow landing this frame. A spider hits
// when any of its body spheres is within SPIDER_REACH of the player's body, a vertical
// capsule standing at playerPos (the feet) that is playerHeight tall.
bool Spiders_AttackPlayer(float dt, Vector3 playerPos, float playerHeight, float *outDamage, Vector3 *outFrom);
// A melee blow from eye, aimed along the floor-plane direction (fx, fz) (unit). Hits each spider
// that has a body sphere within reach (3D) and inside the arc. Returns how many were hit.
// A struck spider is staggered for 0.35 s.
int Spiders_MeleeHit(Vector3 eye, float fx, float fz, float reach, float arcDeg, float damage);
bool Spiders_CheckDamage(Vector3 pos, float radius, float damage);
int  Spiders_CountActive(void);
const Spider *Spiders_Get(int index);

#endif
