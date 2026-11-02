// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "audio_events.h and audio_events.c"
// ============================================================================
#ifndef AUDIO_EVENTS_H
#define AUDIO_EVENTS_H

#include <stdbool.h>

#include "raylib.h"

// What the game does to make sound, in one place: which sound goes with which happening, how the footsteps are spaced, when
// the heartbeat starts. main.c calls these with plain facts (a speed, a flag, a position) and never names a sound itself, so
// changing what the game sounds like is changing this file and sounds.mdix, not hunting through main.c.

void AudioEvents_Reset(void);                                                   // a new floor or a new life: forget the edges and the timers

// Called every frame. speed is the horizontal speed in m/s.
void AudioEvents_Movement(float dt, float speed, bool grounded, float velocityY, bool sprinting, bool sliding, bool exhausted, bool alive);

void AudioEvents_Shot(int weaponId);                                            // a gun fired (WeaponId)
void AudioEvents_DryFire(void);
void AudioEvents_Melee(bool hitSomething);                                      // a swing connected with the air or with an enemy
void AudioEvents_HitMarker(bool kill);                                          // the player's shot or blow landed on an enemy
void AudioEvents_Hurt(void);                                                    // the player took a blow
void AudioEvents_Health(float fraction, bool alive);                            // heartbeat when low, the death sound once
void AudioEvents_Pickup(void);
void AudioEvents_UseItem(void);
void AudioEvents_Battery(void);
void AudioEvents_Flashlight(bool on);                                           // the switch
void AudioEvents_Door(void);
void AudioEvents_Enemies(float dt, bool anyAwake, Vector3 at, float dist);      // from Enemies_NearestAwake: the first growl and the skittering
void AudioEvents_Ambience(bool on);

#endif
