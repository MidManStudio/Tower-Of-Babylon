// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "fg_clock.h"
// ============================================================================
#ifndef FG_CLOCK_H
#define FG_CLOCK_H

#include <stdint.h>
#include <time.h>

// Microseconds from clock(): plain C99, so it builds the same for the tests, the server, Android and the web. It wraps after about
// 71 minutes, so only differences are meaningful (unsigned subtraction handles the wrap). In the browser the resolution is whatever
// the browser's timer gives. Used only for the generation report, never for a decision.
static inline uint32_t FgClockMicros(void)
{
    return (uint32_t)(((uint64_t)clock() * 1000000ull) / (uint64_t)CLOCKS_PER_SEC);
}

#endif
