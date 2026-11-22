// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "fg_rng.h"
// ============================================================================
#ifndef FG_RNG_H
#define FG_RNG_H

#include <stdbool.h>
#include <stdint.h>

// The generator version. It goes up whenever the output for any seed changes, and it is mixed into every stream and into
// the floor hash, so a client and a server with different versions cannot agree on a floor by accident.
#define FG_GEN_VERSION 1

// PCG32 as in mid-engine (mid-math/src/ran_gen/pcg.rs): 64 bit state, odd increment from the stream number, xorshift and rotate
// output. NOT the seeding of the PCG reference code: one step is discarded after the seed is added, as in mid-math.
typedef struct { uint64_t state, inc; } FgPcg32;

static inline uint32_t FgPcg32_Next(FgPcg32 *r)
{
    const uint64_t old = r->state;
    r->state = old * 6364136223846793005ull + r->inc;
    const uint32_t xsh = (uint32_t)(((old >> 18) ^ old) >> 27);
    const uint32_t rot = (uint32_t)(old >> 59);
    return (xsh >> rot) | (xsh << ((32u - rot) & 31u));
}

static inline void FgPcg32_Seed(FgPcg32 *r, uint64_t seed, uint64_t seq)
{
    r->inc = (seq << 1) | 1u;
    r->state = seed;
    (void)FgPcg32_Next(r);
}

// 2^32 mod n, the number of low products Lemire's method must throw away to be exactly unbiased (n is not 0).
static inline uint32_t FgPcg32_Threshold(uint32_t n) { return (0u - n) % n; }

// A number in [lo, hi). Lemire's method with the 32 bit threshold. hi <= lo gives lo.
static inline uint32_t FgPcg32_Range(FgPcg32 *r, uint32_t lo, uint32_t hi)
{
    if (hi <= lo) return lo;
    const uint32_t n = hi - lo;
    uint64_t m = (uint64_t)FgPcg32_Next(r) * n;
    if ((uint32_t)m < n) {
        const uint32_t t = FgPcg32_Threshold(n);
        while ((uint32_t)m < t) m = (uint64_t)FgPcg32_Next(r) * n;
    }
    return lo + (uint32_t)(m >> 32);
}

// A number in [lo, hi], both included. hi <= lo gives lo.
static inline int FgRng_Int(FgPcg32 *r, int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)FgPcg32_Range(r, 0u, (uint32_t)(hi - lo) + 1u);
}

// True with a chance of perMille in 1000.
static inline bool FgRng_Chance(FgPcg32 *r, uint32_t perMille) { return FgPcg32_Range(r, 0u, 1000u) < perMille; }

// SplitMix64 (the usual constants): widens a 32 bit seed and decorrelates stream keys.
static inline uint64_t FgSplitMix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Stage numbers of the streams. A stage gets its own stream (and so does each zone or socket inside it), so adding a stage or
// a socket kind never moves anything an earlier stage already made. Only FG_STREAM_ZONE is used so far.
enum {
    FG_STREAM_RECIPE   = 0x01,
    FG_STREAM_SKELETON = 0x02,
    FG_STREAM_GRAPH    = 0x03,
    FG_STREAM_SOCKETS  = 0x04,
    FG_STREAM_ZONE     = 0x10,   // index = (zone << 4) | sub-stage
    FG_STREAM_LEVELS   = 0x20,
    FG_STREAM_COVER    = 0x30,
    FG_STREAM_DRESS    = 0x40
};

// Sub-stages of the one zone there is so far (the ported generator).
enum { FG_ZONE_HALLS = 0, FG_ZONE_PARTITIONS = 1, FG_ZONE_PILLARS = 2 };

// The 64 bit seed of a floor: its 32 bit seed (on the wire) and the generator version, mixed.
static inline uint64_t FgFloorSeed64(uint32_t floorSeed) { return FgSplitMix64(((uint64_t)FG_GEN_VERSION << 32) | floorSeed); }

// One independent stream: key = stage, index (24 bits) and attempt (8 bits). State and increment are both derived with SplitMix64,
// so two streams of one floor do not share a state and do not have related increments.
static inline void FgStream(FgPcg32 *r, uint64_t floorSeed64, uint32_t stage, uint32_t index, uint32_t attempt)
{
    const uint64_t key = ((uint64_t)stage << 32) | ((uint64_t)(index & 0xFFFFFFu) << 8) | (uint64_t)(attempt & 0xFFu);
    FgPcg32_Seed(r, FgSplitMix64(floorSeed64 ^ FgSplitMix64(key)), FgSplitMix64(key + 0x9E3779B97F4A7C15ull));
}

#endif
