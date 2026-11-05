// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "fixed_step.h and fixed_step.c"
// ============================================================================
#ifndef FIXED_STEP_H
#define FIXED_STEP_H

// The accumulator at the heart of MidManStudio_Unity's FixedStepLoop (packages/com.midmanstudio.patterns, Runtime/GameLoop), as
// plain C: frame time goes in, a whole number of fixed steps comes out. A game that runs its rules in fixed steps behaves the same at
// 30 and at 144 frames a second, and a server that steps the same rules at the same rate agrees with it tick for tick, which is
// what the multiplayer needs. A frame that took far too long (a stall, a tab in the background) cannot make the game spin through
// hundreds of steps to catch up: the steps beyond maxSteps are dropped, and counted.
typedef struct { float step, accumulator; int maxSteps; unsigned long dropped, total; } FixedStep;

void  FixedStep_Init(FixedStep *f, float stepSeconds, int maxStepsPerFrame);
int   FixedStep_Advance(FixedStep *f, float dt);       // how many steps to run now (0 .. maxSteps)
float FixedStep_Alpha(const FixedStep *f);             // 0..1: how far the next step has got, for drawing between steps

#endif
