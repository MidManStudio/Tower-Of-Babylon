// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "sound_bank.h and sound_bank.c"
// ============================================================================
#ifndef SOUND_BANK_H
#define SOUND_BANK_H

#include <stdbool.h>

#include "../config/mdix_doc.h"
#include "audio_core.h"
#include "synth.h"

// Every sound the game can make, in the order of the rows in assets/config/sounds.mdix (tools/mdix_bake.mjs checks the
// order, so a sound cannot be given the wrong recipe). To add one: add its id here, to SOUND_NAMES in sound_bank.c and to
// SOUND_IDS in tools/mdix_bake.mjs, then write its row in sounds.mdix.
typedef enum {
    SND_FOOTSTEP = 0, SND_JUMP, SND_LAND, SND_SLIDE, SND_PIPE_SWING, SND_PIPE_HIT, SND_PISTOL, SND_SHOTGUN, SND_SHOTGUN_PUMP,
    SND_RIFLE, SND_DRY_FIRE, SND_PICKUP, SND_USE_ITEM, SND_FLASH_CLICK, SND_BATTERY, SND_HURT, SND_DEATH, SND_HIT_CONFIRM,
    SND_KILL_CONFIRM, SND_SKITTER, SND_ALERT, SND_DOOR, SND_HEARTBEAT, SND_AMBIENT, SND_UI_CLICK,
    SND_COUNT
} SoundId;

typedef struct {
    char      id[24];
    int       category;        // AudioCategory
    float     volume;          // 0..2, before the category and master volumes
    float     pitchJitter;     // each play is detuned by up to this fraction either way (0.05 = 5%)
    int       voices;          // how many copies may sound at once
    int       variants;        // how many different takes are rendered (different noise), played in turn, never the same twice
    bool      loop;            // a looping sound (heartbeat, ambience): started and stopped, not fired
    float     falloffStart;    // metres of full volume; both 0 = a flat 2D sound that does not depend on where it is
    float     falloffEnd;      // metres where it falls silent
    SynRecipe recipe;
} SoundDef;

// Reads assets/config/sounds.mdix (a document from the baked table or from the wasm). Installs the whole bank only when
// every sound and layer checks out; otherwise nothing changes and err says which value is wrong.
bool SoundBank_LoadFrom(const MdixDoc *doc, char *err, int errCap);

bool            SoundBank_Loaded(void);
unsigned        SoundBank_Revision(void);       // goes up by one on every successful load, so the player knows to render again
const SoundDef *SoundBank_Get(int id);          // NULL when out of range or nothing is loaded
const char     *SoundBank_Name(int id);         // "footstep", ...

#endif
