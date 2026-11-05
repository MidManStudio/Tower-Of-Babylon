// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "timers.h and timers.c"
// ============================================================================
#include "timers.h"

void Countdown_Init(CountdownTimer *t, float seconds) { t->initial = seconds; t->time = seconds; t->running = false; }
void Countdown_Start(CountdownTimer *t) { t->time = t->initial; t->running = true; }
void Countdown_Stop(CountdownTimer *t) { t->running = false; }

bool Countdown_Tick(CountdownTimer *t, float dt)
{
    if (!t->running) return false;
    t->time -= dt;
    if (t->time > 0.0f) return false;
    t->time = 0.0f;
    t->running = false;
    return true;
}

float Countdown_Progress(const CountdownTimer *t) { return t->initial > 0.0f ? t->time / t->initial : 0.0f; }

void Tween_Start(Tween *t, float from, float to, float duration, int ease)
{
    t->from = from; t->to = to; t->duration = duration > 0.0001f ? duration : 0.0001f;
    t->elapsed = 0.0f; t->ease = ease; t->running = true;
}

bool Tween_Tick(Tween *t, float dt)
{
    if (!t->running) return false;
    t->elapsed += dt;
    if (t->elapsed < t->duration) return false;
    t->elapsed = t->duration;
    t->running = false;
    return true;
}

float Tween_Value(const Tween *t)
{
    const float u = t->elapsed / t->duration;
    float k = u;
    switch (t->ease) {
        case TWEEN_EASE_IN:     k = u * u; break;
        case TWEEN_EASE_OUT:    k = 1.0f - (1.0f - u) * (1.0f - u); break;
        case TWEEN_EASE_IN_OUT: k = u * u * (3.0f - 2.0f * u); break;
        default: break;
    }
    return t->from + (t->to - t->from) * k;
}
