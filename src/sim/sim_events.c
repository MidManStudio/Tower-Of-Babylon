// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/sim.md, section "sim_events.h and sim_events.c"
// ============================================================================
#include "sim_events.h"

#include <string.h>

static SimEventQueue *g_bound = 0;

void SimEvents_Clear(SimEventQueue *q) { q->head = q->count = 0; q->dropped = 0; }

static SimEvent *Slot(SimEventQueue *q)
{
    if (q->count >= SIM_EVENT_MAX) { ++q->dropped; return 0; }
    SimEvent *e = &q->ev[(q->head + q->count) % SIM_EVENT_MAX];
    ++q->count;
    memset(e, 0, sizeof *e);
    return e;
}

void SimEvents_Push(SimEventQueue *q, SimEventKind kind, int arg)
{
    SimEvent *e = Slot(q);
    if (!e) return;
    e->kind = (unsigned char)kind;
    e->arg = arg;
}

void SimEvents_PushSound(SimEventQueue *q, SimSound sound, int arg)
{
    SimEvent *e = Slot(q);
    if (!e) return;
    e->kind = SIMEV_SOUND;
    e->sound = (unsigned char)sound;
    e->arg = arg;
}

void SimEvents_PushToast(SimEventQueue *q, const char *text)
{
    SimEvent *e = Slot(q);
    if (!e) return;
    e->kind = SIMEV_TOAST;
    if (!text) text = "";
    size_t n = strlen(text);
    if (n > SIM_EVENT_TEXT - 1) n = SIM_EVENT_TEXT - 1;
    memcpy(e->text, text, n);
    e->text[n] = '\0';
}

bool SimEvents_Pop(SimEventQueue *q, SimEvent *out)
{
    if (q->count <= 0) return false;
    *out = q->ev[q->head];
    q->head = (q->head + 1) % SIM_EVENT_MAX;
    --q->count;
    return true;
}

void SimOut_Bind(SimEventQueue *q) { g_bound = q; }
void SimOut_Toast(const char *text) { if (g_bound) SimEvents_PushToast(g_bound, text); }
void SimOut_Sound(SimSound sound, int arg) { if (g_bound) SimEvents_PushSound(g_bound, sound, arg); }
