// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "fixed_step.h and fixed_step.c"
// ============================================================================
#include "fixed_step.h"

void FixedStep_Init(FixedStep *f, float stepSeconds, int maxStepsPerFrame)
{
    f->step = stepSeconds > 0.0001f ? stepSeconds : 0.0001f;
    f->maxSteps = maxStepsPerFrame > 0 ? maxStepsPerFrame : 1;
    f->accumulator = 0.0f; f->dropped = 0; f->total = 0;
}

int FixedStep_Advance(FixedStep *f, float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    f->accumulator += dt;
    int steps = (int)(f->accumulator / f->step);
    if (steps > f->maxSteps) {
        f->dropped += (unsigned long)(steps - f->maxSteps);
        f->accumulator -= (float)(steps - f->maxSteps) * f->step;       // forget the time that cannot be caught up
        steps = f->maxSteps;
    }
    f->accumulator -= (float)steps * f->step;
    if (f->accumulator < 0.0f) f->accumulator = 0.0f;
    f->total += (unsigned long)steps;
    return steps;
}

float FixedStep_Alpha(const FixedStep *f) { return f->accumulator / f->step; }
