// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "fg_fixed.h"
// ============================================================================
#ifndef FG_FIXED_H
#define FG_FIXED_H

#include <stdint.h>

// Signed Q16.16 fixed point, for everything that decides a layout (no float, no libm: see docs/floorgen.md).
// Never shift a negative number right: that is implementation defined. Values used with FX_TO_INT are not negative.
typedef int32_t Fx;

#define FX_SHIFT 16
#define FX_ONE   65536
#define FX_HALF  32768

#define FX_FROM_INT(n) ((Fx)((int32_t)(n) * FX_ONE))
#define FX_TO_INT(v)   ((int)((v) >> FX_SHIFT))

// Exact floor of the square root, bit by bit: no float, no table, the same on every platform.
static inline uint32_t FgIsqrt64(uint64_t n)
{
    uint64_t res = 0, bit = 1ull << 62;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return (uint32_t)res;
}

// Euclidean distance in tiles for a squared tile distance d2 (0 or more), as Q16.16, rounded down.
static inline Fx FgDistFx2(int d2)
{
    return (Fx)FgIsqrt64(((uint64_t)(uint32_t)d2) << 32);
}

#endif
