// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud.h and hud.c"
// ============================================================================
#ifndef HUD_H
#define HUD_H

#include <stdbool.h>

#include "raylib.h"
#include "../input/touch_controls.h"
#include "hud_layout.h"

// Everything drawn over the 3D view that is not a screen effect: the touch controls, the STATS button and popup, the pickup
// prompt, the toast, the diagnostics. main.c fills one HudView a frame with plain values and calls Hud_Draw; the HUD asks the
// game for nothing and keeps no pointer into it.
//
// The stats (health, stamina, weapon and ammo, flashlight charge, the floor and its generator numbers, enemies, FPS) are in a
// popup behind the STATS button, so the screen stays clear to play on. Set HUD_VITALS_ALWAYS_ON to 1 to keep a health and a
// stamina bar and the weapon with its ammo on the screen as well.
#define HUD_VITALS_ALWAYS_ON 0

typedef struct {
    int   depthBits, msaaSamples;       // what the browser or the phone gave (-1 = unknown)
    float subMs[4];                     // the 3D stage's parts: light, world, entities, hands (ms)
} HudDebug;

typedef struct {
    bool  alive, sprinting, sliding, exhausted;
    float hp, maxHp, stamina01, exhaustRecover01;
    float stim01;                       // a stim shot is working: the share of its time that remains (1 just used .. 0 over; 0 = none). The HUD shows the stamina bar and this while it runs
    bool  tooTiredToSlide, tooTiredToDive;
    const char *weaponName; bool weaponIsGun, weaponIsMelee; int ammo; bool weaponDry; int weaponsOwned;   // ammo: the reserve in the bag (the whole count for a gun with no magazine)
    int   mag, magSize;                 // the gun in hand: rounds loaded, and what the magazine holds (magSize 0: no magazine)
    bool  reloading; float reloadFrac;  // a reload is running; how far through the current step (0..1)
    bool  reloadShown, magEmpty;        // the RELOAD button is drawn (a reload can start, or one is running); the magazine is empty
    bool  weaponWears; float weaponCondition; int durabilityLeft;   // a melee weapon that wears out: 1 new .. 0 breaking; swings left
    bool  hasTorch, torchOn, torchLow, torchFlat; float torchFraction, torchSecs, torchLevel;
    int   floorNumber, enemies;
    const char *floorInfo;              // the generator's numbers for this floor, one line
    bool  useShown, useLocked; const char *useLabel;
    bool  slideShown;
    bool  diveShown;                    // the DIVE button is drawn, in SLIDE's place (in the air after a sprinting jump)
    bool  invOpen;
    const char *characterName;          // the chosen character's short name, or NULL when none is chosen (shown beside STATS in the popup)
    int   stairPhase;                   // the stair clock: 0 none on this floor, 1 find the console, 2 countdown, 3 collapsing
    float consoleProgress, stairCountdown, collapseLeft01; bool consoleHeld;
    HudDebug debug;
} HudView;

void Hud_Init(void);
void Hud_Toast(const char *text);
void Hud_Update(float dt);

// A tap (a finger that just went down). True when the HUD used it: the STATS button, the popup's X and switch, a diagnostics row.
bool Hud_HandleTap(Vector2 where, bool inventoryOpen);

void Hud_SetStatsOpen(bool open);
void Hud_ToggleStats(void);
bool Hud_StatsOpen(void);
bool Hud_DiagnosticsOn(void);

// The regions that only take taps right now, to hand to the input system so a finger there does not become the stick or the camera.
int Hud_BlockedRects(UiRect *out, int cap);

void Hud_Draw(const HudView *v);

// internal: the three parts
void HudControls_Draw(const HudView *v);
void HudStats_Draw(const HudView *v);
void HudRun_Draw(const HudView *v);
void HudDebug_Draw(const HudView *v);
bool HudDebug_HandleTap(Vector2 where);

#endif
