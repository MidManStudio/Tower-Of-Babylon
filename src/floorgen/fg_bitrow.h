// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "fg_bitrow.h"
// ============================================================================
#ifndef FG_BITROW_H
#define FG_BITROW_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// A row of up to 128 tiles as one 128 bit mask (two 64 bit words), the shape of mid-math's BitMask128. Bit i is tile i of the row.
// FgBitGrid keeps the solid tiles of a floor twice, by row (bit = x) and by column (bit = z), so a free run in any of the four
// directions is one shift and one count-trailing-or-leading-zeros, and a band of rows is a few ORs and one AND.
// Out of bounds counts as solid, like Tile_IsSolid. Plain loops are the oracle in tests/floorgen_bits_headless.c.
#define FG_MAP_MAX 128

typedef struct { uint64_t lo, hi; } FgRow;

#if defined(__GNUC__) || defined(__clang__)
#define FG_CTZ64(x) ((int)__builtin_ctzll(x))
#define FG_CLZ64(x) ((int)__builtin_clzll(x))
#define FG_POP64(x) ((int)__builtin_popcountll(x))
#else
static inline int FgCtz64_(uint64_t x) { int n = 0; while ((x & 1u) == 0) { x >>= 1; ++n; } return n; }
static inline int FgClz64_(uint64_t x) { int n = 0; while ((x & (1ull << 63)) == 0) { x <<= 1; ++n; } return n; }
static inline int FgPop64_(uint64_t x) { int n = 0; while (x) { x &= x - 1u; ++n; } return n; }
#define FG_CTZ64(x) FgCtz64_(x)
#define FG_CLZ64(x) FgClz64_(x)
#define FG_POP64(x) FgPop64_(x)
#endif

static inline FgRow FgRow_Zero(void) { FgRow r = { 0, 0 }; return r; }
static inline FgRow FgRow_Or(FgRow a, FgRow b)     { FgRow r = { a.lo | b.lo, a.hi | b.hi }; return r; }
static inline FgRow FgRow_And(FgRow a, FgRow b)    { FgRow r = { a.lo & b.lo, a.hi & b.hi }; return r; }
static inline FgRow FgRow_AndNot(FgRow a, FgRow b) { FgRow r = { a.lo & ~b.lo, a.hi & ~b.hi }; return r; }   // a and not b
static inline bool  FgRow_Any(FgRow a)             { return (a.lo | a.hi) != 0; }

static inline bool FgRow_Test(FgRow a, int i) { return i < 64 ? ((a.lo >> i) & 1u) != 0 : ((a.hi >> (i - 64)) & 1u) != 0; }
static inline void FgRow_Set(FgRow *a, int i)   { if (i < 64) a->lo |= 1ull << i;  else a->hi |= 1ull << (i - 64); }
static inline void FgRow_Clear(FgRow *a, int i) { if (i < 64) a->lo &= ~(1ull << i); else a->hi &= ~(1ull << (i - 64)); }

static inline FgRow FgRow_Shl1(FgRow a) { FgRow r = { a.lo << 1, (a.hi << 1) | (a.lo >> 63) }; return r; }
static inline FgRow FgRow_Shr1(FgRow a) { FgRow r = { (a.lo >> 1) | (a.hi << 63), a.hi >> 1 }; return r; }

// Shifts by n (0 to 127; 128 or more gives zero). Bits move across the 64 bit word boundary.
static inline FgRow FgRow_Shr(FgRow a, int n)
{
    FgRow r;
    if (n <= 0) return a;
    if (n >= 128) return FgRow_Zero();
    if (n >= 64) { r.lo = a.hi >> (n - 64); r.hi = 0; return r; }
    r.lo = (a.lo >> n) | (a.hi << (64 - n));
    r.hi = a.hi >> n;
    return r;
}
static inline FgRow FgRow_Shl(FgRow a, int n)
{
    FgRow r;
    if (n <= 0) return a;
    if (n >= 128) return FgRow_Zero();
    if (n >= 64) { r.lo = 0; r.hi = a.lo << (n - 64); return r; }
    r.lo = a.lo << n;
    r.hi = (a.hi << n) | (a.lo >> (64 - n));
    return r;
}

// Count trailing zeros, count leading zeros of the 128 bit value, population count. Ctz and Clz need a non-zero row.
static inline int FgRow_Ctz(FgRow a) { return a.lo ? FG_CTZ64(a.lo) : 64 + FG_CTZ64(a.hi); }
static inline int FgRow_Clz(FgRow a) { return a.hi ? FG_CLZ64(a.hi) : 64 + FG_CLZ64(a.lo); }
static inline int FgRow_Pop(FgRow a) { return FG_POP64(a.lo) + FG_POP64(a.hi); }

// Bits [0, n) and bits [a, b] (a <= b, both inside 0..127).
static inline FgRow FgRow_MaskBelow(int n)
{
    FgRow r;
    if (n <= 0) return FgRow_Zero();
    if (n >= 128) { r.lo = ~0ull; r.hi = ~0ull; return r; }
    if (n >= 64) { r.lo = ~0ull; r.hi = (n == 64) ? 0ull : ((1ull << (n - 64)) - 1u); return r; }
    r.lo = (1ull << n) - 1u; r.hi = 0; return r;
}
static inline FgRow FgRow_MaskRange(int a, int b) { return FgRow_AndNot(FgRow_MaskBelow(b + 1), FgRow_MaskBelow(a)); }

typedef struct {
    int    w, h;                      // the floor's size in tiles, at most FG_MAP_MAX each
    FgRow  rows[FG_MAP_MAX];          // rows[z]: bit x set when tile (x, z) is solid
    FgRow  cols[FG_MAP_MAX];          // cols[x]: bit z set when tile (x, z) is solid
} FgBitGrid;

static inline void FgBitGrid_Init(FgBitGrid *g, int w, int h) { memset(g, 0, sizeof *g); g->w = w; g->h = h; }

// Inside the floor only; the caller never sets a tile that is outside.
static inline void FgBitGrid_Set(FgBitGrid *g, int x, int z, bool solid)
{
    if (solid) { FgRow_Set(&g->rows[z], x); FgRow_Set(&g->cols[x], z); }
    else       { FgRow_Clear(&g->rows[z], x); FgRow_Clear(&g->cols[x], z); }
}

static inline bool FgBitGrid_Test(const FgBitGrid *g, int x, int z)
{
    return x < 0 || z < 0 || x >= g->w || z >= g->h || FgRow_Test(g->rows[z], x);
}

// Free tiles from (x, z) to the next solid tile (or the edge), not counting (x, z) itself: east, west, south (+z), north (-z).
// Every query has a _Fast version (shifts and bit scans) and a _Plain version (one tile at a time) that is the oracle. The name
// without a suffix is the fast one, or the plain one when the whole program is built with -DFG_PLAIN_SCANS: a floor made either
// way must be identical, which tests/floorgen_hash_headless.c checks against golden hashes taken from the plain build.
static inline int FgBitGrid_RunE_Fast(const FgBitGrid *g, int x, int z)
{
    const FgRow t = FgRow_Shr(g->rows[z], x + 1);
    return FgRow_Any(t) ? FgRow_Ctz(t) : g->w - 1 - x;
}
static inline int FgBitGrid_RunW_Fast(const FgBitGrid *g, int x, int z)
{
    const FgRow t = FgRow_And(g->rows[z], FgRow_MaskBelow(x));
    return FgRow_Any(t) ? x - (127 - FgRow_Clz(t)) - 1 : x;
}
static inline int FgBitGrid_RunS_Fast(const FgBitGrid *g, int x, int z)
{
    const FgRow t = FgRow_Shr(g->cols[x], z + 1);
    return FgRow_Any(t) ? FgRow_Ctz(t) : g->h - 1 - z;
}
static inline int FgBitGrid_RunN_Fast(const FgBitGrid *g, int x, int z)
{
    const FgRow t = FgRow_And(g->cols[x], FgRow_MaskBelow(z));
    return FgRow_Any(t) ? z - (127 - FgRow_Clz(t)) - 1 : z;
}

static inline int FgBitGrid_RunE_Plain(const FgBitGrid *g, int x, int z) { int n = 0; while (!FgBitGrid_Test(g, x + 1 + n, z)) ++n; return n; }
static inline int FgBitGrid_RunW_Plain(const FgBitGrid *g, int x, int z) { int n = 0; while (!FgBitGrid_Test(g, x - 1 - n, z)) ++n; return n; }
static inline int FgBitGrid_RunS_Plain(const FgBitGrid *g, int x, int z) { int n = 0; while (!FgBitGrid_Test(g, x, z + 1 + n)) ++n; return n; }
static inline int FgBitGrid_RunN_Plain(const FgBitGrid *g, int x, int z) { int n = 0; while (!FgBitGrid_Test(g, x, z - 1 - n)) ++n; return n; }

// All nine tiles of the 3 by 3 block centred on (x, z) are free (and inside the floor).
static inline bool FgBitGrid_Open3x3_Fast(const FgBitGrid *g, int x, int z)
{
    if (x < 1 || z < 1 || x > g->w - 2 || z > g->h - 2) return false;
    const FgRow occ = FgRow_Or(FgRow_Or(g->rows[z - 1], g->rows[z]), g->rows[z + 1]);
    return !FgRow_Any(FgRow_And(occ, FgRow_MaskRange(x - 1, x + 1)));
}
static inline bool FgBitGrid_Open3x3_Plain(const FgBitGrid *g, int x, int z)
{
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (FgBitGrid_Test(g, x + dx, z + dz)) return false;
    return true;
}

// For one tile of a segment: the tiles at distance 1 to half on both sides, across the segment, are all free and inside.
// A vertical segment runs along z at x = line, so its sides are in the row of tile i; a horizontal one runs along x at z = line,
// so its sides are in the column of tile i.
static inline bool FgBitGrid_SidesFree_Fast(const FgBitGrid *g, bool vertical, int line, int i, int half)
{
    const int lim = vertical ? g->w : g->h;
    if (line - half < 0 || line + half >= lim) return false;
    FgRow m = FgRow_MaskRange(line - half, line + half);
    FgRow_Clear(&m, line);
    return !FgRow_Any(FgRow_And(vertical ? g->rows[i] : g->cols[i], m));
}
static inline bool FgBitGrid_SidesFree_Plain(const FgBitGrid *g, bool vertical, int line, int i, int half)
{
    for (int d = -half; d <= half; ++d) {
        if (d == 0) continue;
        if (FgBitGrid_Test(g, vertical ? line + d : i, vertical ? i : line + d)) return false;
    }
    return true;
}

#ifdef FG_PLAIN_SCANS
#define FgBitGrid_RunE       FgBitGrid_RunE_Plain
#define FgBitGrid_RunW       FgBitGrid_RunW_Plain
#define FgBitGrid_RunS       FgBitGrid_RunS_Plain
#define FgBitGrid_RunN       FgBitGrid_RunN_Plain
#define FgBitGrid_Open3x3    FgBitGrid_Open3x3_Plain
#define FgBitGrid_SidesFree  FgBitGrid_SidesFree_Plain
#else
#define FgBitGrid_RunE       FgBitGrid_RunE_Fast
#define FgBitGrid_RunW       FgBitGrid_RunW_Fast
#define FgBitGrid_RunS       FgBitGrid_RunS_Fast
#define FgBitGrid_RunN       FgBitGrid_RunN_Fast
#define FgBitGrid_Open3x3    FgBitGrid_Open3x3_Fast
#define FgBitGrid_SidesFree  FgBitGrid_SidesFree_Fast
#endif

static inline int FgBitGrid_CountSolid(const FgBitGrid *g)
{
    int n = 0;
    for (int z = 0; z < g->h; ++z) n += FgRow_Pop(g->rows[z]);
    return n;
}

#endif
