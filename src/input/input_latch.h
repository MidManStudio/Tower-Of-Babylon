// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/sim.md, section "input_latch.h and input_latch.c"
// ============================================================================
#ifndef INPUT_LATCH_H
#define INPUT_LATCH_H

#include <stdbool.h>

#include "input.h"

// What the player wants at a simulation tick. The screen is drawn at its own pace (60, 144 frames a second) and the rules run
// 30 times a second, so a press that comes and goes between two ticks must not be lost: a press is kept ("latched") until the
// next tick takes it, and a held button or stick is whatever it was last.
typedef struct {
    Vector2 move;                      // the stick or keys, as last seen
    bool    sprint, useHeld;           // held, as last seen
    bool    jump, fire, use, swap, light, slide, reload, dive;     // pressed at any time since the last tick (fire: held at any time)
    bool    fireHeld;                  // held at the last frame
} SimCmd;

void   SimCmd_Init(SimCmd *c);
void   SimCmd_Add(SimCmd *c, const InputFrame *in);   // once per frame, after Input_Update
SimCmd SimCmd_Take(SimCmd *c);                        // the command for one tick; the latched presses are cleared, the held states stay

#endif
