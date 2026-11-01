// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/flashlight.md, section "flashlight.h and flashlight.c"
// ============================================================================
#ifndef FLASHLIGHT_H
#define FLASHLIGHT_H

#include <stdbool.h>

#include "raylib.h"

// The flashlight: how long its charge lasts, how it fails when the charge runs low, how batteries refill it, and the
// shape of the beam. No drawing and no inventory: the game owns the item and calls these. The numbers are in
// assets/config/flashlight.mdix (see config.md).

typedef struct {
    float    capacitySecs;    // most charge it holds, in seconds of light
    float    startSecs;       // what one found on the floor holds
    float    batterySecs;     // what one battery adds
    float    lowFraction;     // below this share of capacity the light starts to fail
    float    beamInnerDeg;    // half-angle of full strength
    float    beamOuterDeg;    // half-angle where it has faded to nothing
    float    range;           // metres to where it has faded to nothing
    float    strength;        // overall brightness of the beam
    float    lag;             // how fast the beam follows the camera, per second
    unsigned tint;            // 0xRRGGBB
} FlashlightConfig;

void Flashlight_SetConfig(const FlashlightConfig *config);
const FlashlightConfig *Flashlight_Config(void);

typedef struct {
    bool    on;               // switched on
    float   charge;           // seconds of light left
    float   level;            // 0 to 1: how bright it is this frame, after flicker
    float   clock;            // seconds since it was set up; drives the flicker
    Vector3 dir;              // the beam's direction (unit): follows the camera with a little lag
    bool    dirValid;
    bool    died;             // set on the frame the charge ran out; the caller clears it
} Flashlight;

void  Flashlight_Init(Flashlight *f);                    // off, empty, not yet pointing anywhere
void  Flashlight_Give(Flashlight *f, float chargeSecs);  // picked up with this much charge (clamped to the capacity)
bool  Flashlight_Toggle(Flashlight *f);                  // switches it; false (and stays off) when it has no charge. Returns whether it is now on
// Loads a battery. Refused (0) while the flashlight holds more than capacity - batterySecs / 2, so one is never half wasted.
// Otherwise adds up to batterySecs (clamped to the capacity), turns the light on if it had gone flat, and returns the seconds added.
float Flashlight_AddBattery(Flashlight *f);
float Flashlight_Fraction(const Flashlight *f);          // charge / capacity
bool  Flashlight_IsLow(const Flashlight *f);             // below lowFraction

// Once a frame: drains the charge while on, decides how bright the light is this frame (flickering when low), and swings
// the beam toward `forward` (the camera's direction, unit).
void  Flashlight_Update(Flashlight *f, float dt, Vector3 forward);

// How strongly the beam lights a point, from 0 to strength: the same arithmetic as the shader in highp_shader.c (this is
// what the test compares a real render against). `eye` is where the light is.
float Flashlight_BeamAt(const Flashlight *f, Vector3 eye, Vector3 point);

// A soft glow around the player that lights nearby surfaces without the shader: the fallback when the high precision
// shader is not available (or switched off for comparing). 0 to about 0.6, within 6 m.
float Flashlight_GlowAt(const Flashlight *f, Vector3 eye, Vector3 point);

#endif
