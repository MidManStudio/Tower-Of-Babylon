// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "audio_events.h and audio_events.c"
// ============================================================================
#include "audio_events.h"

#include "../weapons/weapons.h"
#include "audio.h"
#include "audio_core.h"

#define WALK_STRIDE      1.70f     // metres per footstep walking (4.5 m/s: 2.6 steps a second)
#define SPRINT_STRIDE    2.30f     // sprinting (7.6 m/s: 3.3 steps a second)
#define LOW_HEALTH       0.40f     // below this fraction the heartbeat starts
#define SKITTER_RANGE    22.0f
#define SKITTER_SECS     0.30f
#define PUMP_DELAY_SECS  0.55f     // the shotgun's pump comes after the blast

static Footsteps g_steps;
static bool  g_wasGrounded = true, g_wasSliding = false, g_wasAlive = true, g_hadEnemy = false;
static float g_prevVelY = 0.0f, g_skitter = 0.0f;

void AudioEvents_Reset(void)
{
    g_steps = (Footsteps){ 0 };
    g_wasGrounded = true; g_wasSliding = false; g_hadEnemy = false;
    g_prevVelY = 0.0f; g_skitter = 0.0f;
    Audio_LoopSet(SND_HEARTBEAT, false, 1.0f, 1.0f);
}

void AudioEvents_Movement(float dt, float speed, bool grounded, float velocityY, bool sprinting, bool sliding, bool exhausted, bool alive)
{
    if (!alive) { g_wasGrounded = grounded; g_prevVelY = velocityY; g_wasSliding = sliding; return; }

    if (sliding && !g_wasSliding) Audio_Play(SND_SLIDE, 1.0f);
    if (!grounded && g_wasGrounded && velocityY > 1.0f) Audio_Play(SND_JUMP, 1.0f);              // left the ground going up: a jump (a fall is silent)
    if (grounded && !g_wasGrounded && g_prevVelY < -3.0f) {                                      // came down fast enough to feel it
        const float hard = (-g_prevVelY - 3.0f) / 9.0f;
        Audio_Play(SND_LAND, 0.55f + (hard > 0.45f ? 0.45f : hard));
    }
    g_wasGrounded = grounded; g_prevVelY = velocityY; g_wasSliding = sliding;

    if (grounded && !sliding && speed > 0.6f) {
        if (Footsteps_Advance(&g_steps, speed * dt, sprinting ? SPRINT_STRIDE : WALK_STRIDE)) {
            float vol = sprinting ? 1.0f : 0.65f;
            if (exhausted) vol *= 0.8f;                                                           // dragging feet: lower but heavy (pitch below)
            Audio_PlayEx(SND_FOOTSTEP, vol, g_steps.foot ? 0.92f : 1.0f, g_steps.foot ? 0.12f : -0.12f);
        }
    } else if (speed < 0.3f) {
        g_steps.dist = 0.0f;                                                                      // standing still: the next step starts fresh
    }
}

static int GunSound(int weaponId)
{
    switch (weaponId) {
        case WEAPON_PISTOL:  return SND_PISTOL;
        case WEAPON_SHOTGUN: return SND_SHOTGUN;
        case WEAPON_RIFLE:   return SND_RIFLE;
        default:             return -1;
    }
}

void AudioEvents_Shot(int weaponId)
{
    const int snd = GunSound(weaponId);
    if (snd < 0) return;
    Audio_Play(snd, 1.0f);
    if (weaponId == WEAPON_SHOTGUN) Audio_PlayIn(SND_SHOTGUN_PUMP, PUMP_DELAY_SECS, 1.0f);
}

void AudioEvents_DryFire(void)  { Audio_Play(SND_DRY_FIRE, 1.0f); }
void AudioEvents_Melee(bool hit) { Audio_Play(SND_PIPE_SWING, 1.0f); if (hit) Audio_PlayIn(SND_PIPE_HIT, 0.06f, 1.0f); }
void AudioEvents_HitMarker(bool kill) { Audio_Play(kill ? SND_KILL_CONFIRM : SND_HIT_CONFIRM, 1.0f); }
void AudioEvents_Hurt(void)     { Audio_Play(SND_HURT, 1.0f); }
void AudioEvents_Pickup(void)   { Audio_Play(SND_PICKUP, 1.0f); }
void AudioEvents_UseItem(void)  { Audio_Play(SND_USE_ITEM, 1.0f); }
void AudioEvents_Battery(void)  { Audio_Play(SND_BATTERY, 1.0f); }
void AudioEvents_Flashlight(bool on) { Audio_PlayEx(SND_FLASH_CLICK, 1.0f, on ? 1.12f : 0.88f, 0.0f); }
void AudioEvents_Door(void)     { Audio_Play(SND_DOOR, 1.0f); }

void AudioEvents_Health(float fraction, bool alive)
{
    if (!alive) {
        if (g_wasAlive) Audio_Play(SND_DEATH, 1.0f);
        g_wasAlive = false;
        Audio_LoopSet(SND_HEARTBEAT, false, 1.0f, 1.0f);
        return;
    }
    g_wasAlive = true;
    if (fraction < LOW_HEALTH) {
        const float need = (LOW_HEALTH - fraction) / LOW_HEALTH;                                   // 0 just under the line, 1 nearly dead
        Audio_LoopSet(SND_HEARTBEAT, true, 0.5f + 0.5f * need, 1.0f + 0.6f * need);                // louder and faster as it gets worse
    } else {
        Audio_LoopSet(SND_HEARTBEAT, false, 1.0f, 1.0f);
    }
}

void AudioEvents_Enemies(float dt, bool anyAwake, Vector3 at, float dist)
{
    if (anyAwake && !g_hadEnemy) { Audio_PlayAt(SND_ALERT, at, 1.0f); g_skitter = 0.0f; }          // the first one to notice you
    g_hadEnemy = anyAwake;
    if (!anyAwake) return;
    g_skitter -= dt;
    if (g_skitter <= 0.0f && dist < SKITTER_RANGE) {
        Audio_PlayAt(SND_SKITTER, at, 1.0f);
        g_skitter = SKITTER_SECS * (0.8f + 0.4f * dist / SKITTER_RANGE);                           // faster when it is close
    }
}

void AudioEvents_Ambience(bool on) { Audio_LoopSet(SND_AMBIENT, on, 1.0f, 1.0f); }
