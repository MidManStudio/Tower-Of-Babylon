// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "floorgen_rng_headless.c"
// ============================================================================
// The random numbers of the floor generator (src/floorgen/fg_rng.h): PCG32 as in mid-engine, SplitMix64, Lemire's range with the
// 32 bit threshold, and the stream keys. The vectors were computed twice, in C and in Python, from the algorithm in
// mid-math/src/ran_gen/pcg.rs. Each rule is also broken on purpose in a copy and the check has to catch it. Needs nothing but
// the header, and is inside a macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DFLOORGEN_RNG_HEADLESS_TEST -Isrc tests/floorgen_rng_headless.c -o floorgen_rng_headless
// Run:
//   ./floorgen_rng_headless
#ifdef FLOORGEN_RNG_HEADLESS_TEST

#include <stdio.h>

#include "floorgen/fg_rng.h"

static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

// ---- the vectors -----------------------------------------------------------------------------------------------------------------

static const uint32_t VEC_42_54[6]  = { 0x0c855c84u, 0x0bde36a5u, 0x49dd4da9u, 0x92dc7b03u, 0x044ceb1du, 0xb7c9a0b0u };
static const uint32_t VEC_BIG_1[4]  = { 0x93525db3u, 0x1f4d360eu, 0xd1332d05u, 0xa3a75e1cu };
static const uint32_t RANGE_0_100[8] = { 4, 4, 28, 57, 1, 71, 29, 94 };
static const uint32_t RANGE_3_19[8]  = { 14, 17, 15, 9, 15, 18, 4, 10 };

// The same check against any output function, so a broken copy can be run through it.
typedef uint32_t (*NextFn)(FgPcg32 *);
static void SeedWith(NextFn next, FgPcg32 *r, uint64_t seed, uint64_t seq)
{
    r->inc = (seq << 1) | 1u;
    r->state = seed;
    (void)next(r);
}
static int VectorsMatch(NextFn next)
{
    FgPcg32 r;
    SeedWith(next, &r, 42, 54);
    for (int i = 0; i < 6; ++i) { const uint32_t v = next(&r); if (v != VEC_42_54[i]) return 0; }
    SeedWith(next, &r, 0x123456789ABCDEF0ull, 1);
    for (int i = 0; i < 4; ++i) { const uint32_t v = next(&r); if (v != VEC_BIG_1[i]) return 0; }
    return 1;
}

// A copy of the output function with the rotation left out.
static uint32_t NextNoRotate(FgPcg32 *r)
{
    const uint64_t old = r->state;
    r->state = old * 6364136223846793005ull + r->inc;
    return (uint32_t)(((old >> 18) ^ old) >> 27);
}
// A copy with the wrong multiplier.
static uint32_t NextWrongMultiplier(FgPcg32 *r)
{
    const uint64_t old = r->state;
    r->state = old * 6364136223846793004ull + r->inc;
    const uint32_t xsh = (uint32_t)(((old >> 18) ^ old) >> 27);
    const uint32_t rot = (uint32_t)(old >> 59);
    return (xsh >> rot) | (xsh << ((32u - rot) & 31u));
}

// ---- the threshold ---------------------------------------------------------------------------------------------------------------

typedef uint32_t (*ThresholdFn)(uint32_t);
static int ThresholdCorrect(ThresholdFn fn)
{
    static const uint32_t ns[] = { 1, 2, 3, 5, 6, 7, 10, 45, 64, 81, 100, 128, 129, 1000, 65535, 65537, 1000000, 4000000007u };
    for (unsigned i = 0; i < sizeof ns / sizeof ns[0]; ++i)
        if (fn(ns[i]) != (uint32_t)((1ull << 32) % ns[i])) return 0;
    for (uint32_t n = 1; n <= 3000; ++n)
        if (fn(n) != (uint32_t)((1ull << 32) % n)) return 0;
    return 1;
}
// The threshold taken from a 64 bit negation (2^64 mod n): what a 64 bit `range` variable gives, and what mid-math's range_u32
// does. It agrees with the right one for powers of two and some other numbers, and not for 7, 45, 81, 100 or 129.
static uint32_t ThresholdFrom64(uint32_t n) { return (uint32_t)((0ull - (uint64_t)n) % (uint64_t)n); }

// ---- ranges and chances ----------------------------------------------------------------------------------------------------------

static int RangeInside(void)
{
    FgPcg32 r;
    FgPcg32_Seed(&r, 99, 7);
    static const uint32_t his[] = { 1, 2, 3, 7, 10, 45, 128, 1000 };
    for (unsigned h = 0; h < sizeof his / sizeof his[0]; ++h)
        for (int i = 0; i < 20000; ++i) {
            const uint32_t lo = 5, hi = 5 + his[h];
            const uint32_t v = FgPcg32_Range(&r, lo, hi);
            if (v < lo || v >= hi) return 0;
        }
    return 1;
}

static int Uniform(uint32_t n, int draws, int tolerancePermille)
{
    FgPcg32 r;
    FgPcg32_Seed(&r, 1234567, 11);
    static int count[64];
    for (uint32_t i = 0; i < n; ++i) count[i] = 0;
    for (int i = 0; i < draws; ++i) ++count[FgPcg32_Range(&r, 0, n)];
    const long expect = draws / (long)n;
    for (uint32_t i = 0; i < n; ++i) {
        const long d = count[i] > expect ? count[i] - expect : expect - count[i];
        if (d * 1000 > expect * tolerancePermille) return 0;
    }
    return 1;
}

// ---- streams ---------------------------------------------------------------------------------------------------------------------

typedef void (*StreamFn)(FgPcg32 *, uint64_t, uint32_t, uint32_t, uint32_t);

// Every stream of a floor must start differently: 3 stages by 4 indices by 4 attempts, compared on their first four numbers.
static int StreamsDistinct(StreamFn fn)
{
    uint32_t first[48][4];
    int n = 0;
    const uint64_t seed = FgFloorSeed64(0x12345678u);
    for (uint32_t stage = 0x10; stage <= 0x30; stage += 0x10)
        for (uint32_t index = 0; index < 4; ++index)
            for (uint32_t attempt = 0; attempt < 4; ++attempt) {
                FgPcg32 r;
                fn(&r, seed, stage, index, attempt);
                for (int k = 0; k < 4; ++k) first[n][k] = FgPcg32_Next(&r);
                ++n;
            }
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b)
            if (first[a][0] == first[b][0] && first[a][1] == first[b][1] && first[a][2] == first[b][2] && first[a][3] == first[b][3]) return 0;
    return 1;
}
// A copy that forgets the attempt number.
static void StreamIgnoresAttempt(FgPcg32 *r, uint64_t floorSeed64, uint32_t stage, uint32_t index, uint32_t attempt)
{
    (void)attempt;
    FgStream(r, floorSeed64, stage, index, 0);
}
// A copy that forgets the stage.
static void StreamIgnoresStage(FgPcg32 *r, uint64_t floorSeed64, uint32_t stage, uint32_t index, uint32_t attempt)
{
    (void)stage;
    FgStream(r, floorSeed64, 0x10, index, attempt);
}

int main(void)
{
    printf("PCG32 against the vectors\n");
    {
        FgPcg32 r;
        FgPcg32_Seed(&r, 42, 54);
        int ok = 1;
        for (int i = 0; i < 6; ++i) { const uint32_t v = FgPcg32_Next(&r); if (v != VEC_42_54[i]) ok = 0; }
        Check(ok, "seed 42, stream 54 gives the six vectors");
        FgPcg32_Seed(&r, 0x123456789ABCDEF0ull, 1);
        ok = 1;
        for (int i = 0; i < 4; ++i) { const uint32_t v = FgPcg32_Next(&r); if (v != VEC_BIG_1[i]) ok = 0; }
        Check(ok, "a seed that uses all 64 bits gives its four vectors");
        FgPcg32_Seed(&r, 42, 54);
        ok = 1;
        for (int i = 0; i < 8; ++i) { const uint32_t v = FgPcg32_Range(&r, 0, 100); if (v != RANGE_0_100[i]) ok = 0; }
        Check(ok, "range(0, 100) gives the eight vectors");
        FgPcg32_Seed(&r, 7, 9);
        ok = 1;
        for (int i = 0; i < 8; ++i) { const uint32_t v = FgPcg32_Range(&r, 3, 19); if (v != RANGE_3_19[i]) ok = 0; }
        Check(ok, "range(3, 19) gives the eight vectors");
        // A property of PCG32 as mid-math seeds it (one step after the seed is added): two stream numbers of one seed share their FIRST
        // output, because the first output comes from a state that differs only in its low bits and the output function reads the high
        // ones. The second output differs. FgStream avoids this by deriving the state and the increment separately.
        int sameFirst = 1, diffSecond = 1;
        FgPcg32 mix;
        FgPcg32_Seed(&mix, 777, 1);
        for (int i = 0; i < 1000; ++i) {
            const uint32_t hi = FgPcg32_Next(&mix), lo = FgPcg32_Next(&mix);
            const uint64_t seed = ((uint64_t)hi << 32) | lo;
            FgPcg32 a, b;
            FgPcg32_Seed(&a, seed, 1);
            FgPcg32_Seed(&b, seed, 2);
            const uint32_t a0 = FgPcg32_Next(&a), b0 = FgPcg32_Next(&b);
            const uint32_t a1 = FgPcg32_Next(&a), b1 = FgPcg32_Next(&b);
            if (a0 != b0) sameFirst = 0;
            if (a1 == b1) diffSecond = 0;
        }
        Check(sameFirst && diffSecond, "raw PCG32, one seed, stream numbers 1 and 2: the first outputs are equal and the second differ (why FgStream mixes the key)");
        int fgDiffer = 1;
        for (uint32_t i = 0; i < 2000; ++i) {
            const uint64_t seed = FgFloorSeed64(i);
            FgPcg32 p, q;
            FgStream(&p, seed, FG_STREAM_ZONE, 1, 0);
            FgStream(&q, seed, FG_STREAM_ZONE, 2, 0);
            const uint32_t p0 = FgPcg32_Next(&p), q0 = FgPcg32_Next(&q);
            if (p0 == q0) fgDiffer = 0;
        }
        Check(fgDiffer, "FgStream: streams with neighbouring indices start differently on 2000 floors");
    }

    printf("\nSplitMix64\n");
    Check(FgSplitMix64(0) == 0xe220a8397b1dcdafull, "SplitMix64(0)");
    Check(FgSplitMix64(1) == 0x910a2dec89025cc1ull, "SplitMix64(1)");
    Check(FgSplitMix64(0xDEADBEEFull) == 0x4adfb90f68c9eb9bull, "SplitMix64(0xDEADBEEF)");

    printf("\nThe 32 bit threshold of the range\n");
    Check(ThresholdCorrect(FgPcg32_Threshold), "the threshold is 2^32 mod n for every n tried");
    Check(ThresholdFrom64(7) == 2 && FgPcg32_Threshold(7) == 4, "for n = 7 the 64 bit form gives 2 where the right threshold is 4");
    Check(ThresholdFrom64(45) != FgPcg32_Threshold(45) && ThresholdFrom64(81) != FgPcg32_Threshold(81) &&
          ThresholdFrom64(100) != FgPcg32_Threshold(100) && ThresholdFrom64(129) != FgPcg32_Threshold(129),
          "the 64 bit form is also wrong for 45, 81, 100 and 129 (the finding about mid-math's range_u32)");
    Check(ThresholdFrom64(64) == FgPcg32_Threshold(64) && ThresholdFrom64(128) == FgPcg32_Threshold(128), "the two forms agree for powers of two");

    printf("\nRanges and chances\n");
    Check(RangeInside(), "range(lo, hi) stays inside [lo, hi) over 160,000 draws");
    {
        FgPcg32 r;
        FgPcg32_Seed(&r, 3, 3);
        Check(FgPcg32_Range(&r, 9, 9) == 9 && FgPcg32_Range(&r, 9, 4) == 9, "an empty or backward range gives lo and does not divide by zero");
        int seen[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < 2000; ++i) { const int v = FgRng_Int(&r, 3, 6); if (v >= 3 && v <= 6) seen[v - 3] = 1; else seen[0] = -99; }
        Check(seen[0] == 1 && seen[1] == 1 && seen[2] == 1 && seen[3] == 1, "FgRng_Int(3, 6) reaches both ends and nothing outside");
        Check(FgRng_Int(&r, 8, 8) == 8, "FgRng_Int with lo equal to hi gives it");
        int never = 1, always = 1, hits = 0;
        for (int i = 0; i < 100000; ++i) {
            if (FgRng_Chance(&r, 0)) never = 0;
            if (!FgRng_Chance(&r, 1000)) always = 0;
            if (FgRng_Chance(&r, 300)) ++hits;
        }
        Check(never && always, "a chance of 0 never happens and a chance of 1000 always does");
        Check(hits > 29000 && hits < 31000, "a chance of 300 in 1000 happens 29 to 31 percent of the time");
    }
    Check(Uniform(10, 1000000, 20), "range(0, 10): every number within 2 percent of its share over a million draws");
    Check(Uniform(7, 700000, 20), "range(0, 7), where numbers are thrown away to stay unbiased: every number within 2 percent");

    printf("\nStreams\n");
    {
        const uint64_t seed = FgFloorSeed64(0x12345678u);
        Check(seed == 0x5657179ed0274a01ull, "the 64 bit seed of floor seed 0x12345678 (the generator version is in it)");
        Check(FgFloorSeed64(0x12345678u) != FgSplitMix64(0x12345678u), "the generator version changes the seed: it is not SplitMix64 of the bare number");
        FgPcg32 r;
        FgStream(&r, seed, FG_STREAM_ZONE, 1, 3);
        const uint32_t a0 = FgPcg32_Next(&r), a1 = FgPcg32_Next(&r), a2 = FgPcg32_Next(&r);
        Check(a0 == 0x07e71de9u && a1 == 0x3796ab1fu && a2 == 0x51e4de36u, "stream (zone, index 1, attempt 3) starts with its vectors");
        FgStream(&r, seed, FG_STREAM_ZONE, 0, 0);
        const uint32_t b0 = FgPcg32_Next(&r), b1 = FgPcg32_Next(&r), b2 = FgPcg32_Next(&r);
        Check(b0 == 0x88cc360bu && b1 == 0x2752bc51u && b2 == 0xa40aadf9u, "stream (zone, index 0, attempt 0) starts with its vectors");
        FgStream(&r, seed, FG_STREAM_LEVELS, 5, 255);
        const uint32_t c0 = FgPcg32_Next(&r), c1 = FgPcg32_Next(&r), c2 = FgPcg32_Next(&r);
        Check(c0 == 0xbd9415ceu && c1 == 0x7bc54265u && c2 == 0xc8f481d9u, "stream (levels, index 5, attempt 255) starts with its vectors");
        FgPcg32 x, y;
        FgStream(&x, seed, FG_STREAM_ZONE, 2, 1);
        FgStream(&y, seed, FG_STREAM_ZONE, 2, 1);
        int same = 1;
        for (int i = 0; i < 100; ++i) { const uint32_t p = FgPcg32_Next(&x), q = FgPcg32_Next(&y); if (p != q) same = 0; }
        Check(same, "the same key gives the same stream");
        FgStream(&y, FgFloorSeed64(0x12345679u), FG_STREAM_ZONE, 2, 1);
        FgStream(&x, seed, FG_STREAM_ZONE, 2, 1);
        Check(FgPcg32_Next(&x) != FgPcg32_Next(&y), "a different floor seed gives a different stream");
        Check(StreamsDistinct(FgStream), "48 streams of one floor (3 stages, 4 indices, 4 attempts) all start differently");
    }

    printf("\nBroken on purpose: each rule has to be caught\n");
    Check(VectorsMatch(FgPcg32_Next), "(the real output function passes the vector check)");
    Check(!VectorsMatch(NextNoRotate), "an output function without the rotation fails the vectors");
    Check(!VectorsMatch(NextWrongMultiplier), "an output function with the wrong multiplier fails the vectors");
    Check(!ThresholdCorrect(ThresholdFrom64), "the 64 bit threshold fails the threshold check");
    Check(!StreamsDistinct(StreamIgnoresAttempt), "streams that ignore the attempt number are caught");
    Check(!StreamsDistinct(StreamIgnoresStage), "streams that ignore the stage are caught");

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
