// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "timers.h and timers.c"
// ============================================================================
#ifndef TIMERS_H
#define TIMERS_H

#include <stdbool.h>

// Two timers ported from MidManStudio_Unity (packages/com.midmanstudio.utilities, Runtime/Timers/Timer.cs): CountdownTimer and
// ValueInterpolationTimer. Plain C, no raylib, no clock of their own: the caller passes the time that passed, so the same code
// runs in the game, in a test and on a server.

// ---- CountdownTimer ---------------------------------------------------------------------------------------
typedef struct { float initial, time; bool running; } CountdownTimer;

void  Countdown_Init(CountdownTimer *t, float seconds);        // set up, not running
void  Countdown_Start(CountdownTimer *t);                      // from the full time, also when already running
void  Countdown_Stop(CountdownTimer *t);
bool  Countdown_Tick(CountdownTimer *t, float dt);             // true once, on the tick that reaches zero (the timer then stops)
float Countdown_Progress(const CountdownTimer *t);             // 1 just started .. 0 done (time / initial), as Unity's Progress

// ---- Tween (ValueInterpolationTimer, one way) -------------------------------------------------------------
// Unity's ping-pong and custom curve are left out: its own timers.md says the factory never turns ping-pong on, and nothing in the
// game needs either.
typedef enum { TWEEN_LINEAR = 0, TWEEN_EASE_IN, TWEEN_EASE_OUT, TWEEN_EASE_IN_OUT } TweenEase;
typedef struct { float from, to, duration, elapsed; int ease; bool running; } Tween;

void  Tween_Start(Tween *t, float from, float to, float duration, int ease);
bool  Tween_Tick(Tween *t, float dt);                          // true once, on the tick that reaches the end
float Tween_Value(const Tween *t);                             // from .. to, shaped by the ease; stays at "to" after the end

#endif
