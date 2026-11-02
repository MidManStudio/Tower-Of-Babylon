// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "audio.h and audio.c"
// ============================================================================
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

#include "raylib.h"
#include "sound_bank.h"

// Plays the sounds of the sound bank through raylib. The synthesiser renders every sound once, a few per frame, into raylib
// sounds; playing one picks a take, a free voice, a little pitch variation and (when it has a place) a pan and distance
// volume. A peak limiter sits on the final mix. Nothing here blocks, allocates while playing, or needs a sound file.
//
//   Audio_Init       once, before GameConfig_Init: hands sounds.mdix to the config layer. Opens no sound card.
//   Audio_Update     every frame: opens the sound card (on the web, only after the first touch or key, as browsers demand),
//                    renders the next sounds within a small time budget, runs delayed sounds and fades loops.
//   Audio_Shutdown   when the game ends.
// Until a sound has been rendered, playing it returns false and nothing is heard; the game never waits for audio.

void Audio_Init(void);
void Audio_Update(float dt, Vector3 listener, float listenerFx, float listenerFz, bool userGesture);
void Audio_Shutdown(void);

bool Audio_Play(int id, float volume);                                          // flat, as if inside your head
bool Audio_PlayEx(int id, float volume, float pitch, float pan);                // pitch 1 = as rendered, pan -1 left .. 1 right
bool Audio_PlayAt(int id, Vector3 where, float volume);                         // in the room: pan and distance from the listener
void Audio_PlayIn(int id, float seconds, float volume);                         // the same as Audio_Play, a moment later

// A looping sound (SoundDef.loop): fades in to volume, fades out when switched off. pitch changes its speed (a heartbeat).
void Audio_LoopSet(int id, bool on, float volume, float pitch);

void Audio_SetMaster(float v);                                                  // 0..1
void Audio_SetCategoryVolume(int category, float v);
void Audio_SetCategoryMuted(int category, bool muted);

typedef struct {
    bool  deviceReady;      // the sound card is open
    bool  deviceFailed;     // opening it failed; nothing will play
    int   built, total;     // sounds rendered so far, of those in the bank
    int   voicesActive;     // sounds sounding right now
    float limiterGain;      // 1 = untouched; lower while the limiter is holding a loud moment down
    bool  bankLoaded;       // sounds.mdix was read (baked copy or the file)
    unsigned revision;      // how many times the bank has been loaded
} AudioStatus;
const AudioStatus *Audio_Status(void);

#endif
