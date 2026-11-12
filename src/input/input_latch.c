// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/sim.md, section "input_latch.h and input_latch.c"
// ============================================================================
#include "input_latch.h"

#include <string.h>

void SimCmd_Init(SimCmd *c) { memset(c, 0, sizeof *c); }

void SimCmd_Add(SimCmd *c, const InputFrame *in)
{
    c->move = in->move;
    c->sprint = in->sprint;
    c->useHeld = in->useHeld;
    c->lean = in->lean;
    c->fireHeld = in->fire;
    c->jump  = c->jump  || in->jump;
    c->fire  = c->fire  || in->fire;
    c->use   = c->use   || in->use;
    c->swap  = c->swap  || in->swap;
    c->light = c->light || in->light;
    c->slide = c->slide || in->slide;
    c->reload = c->reload || in->reload;
}

SimCmd SimCmd_Take(SimCmd *c)
{
    SimCmd out = *c;
    out.fire = c->fire || c->fireHeld;                 // a tap that was over before the tick still fires once
    c->jump = c->fire = c->use = c->swap = c->light = c->slide = c->reload = false;
    return out;
}
