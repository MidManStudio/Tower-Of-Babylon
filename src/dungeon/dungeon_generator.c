// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_generator.c"
// ============================================================================
#include "dungeon_generator.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../floorgen/fg_bitrow.h"
#include "../floorgen/fg_clock.h"
#include "../floorgen/fg_fixed.h"
#include "../floorgen/fg_probe.h"
#include "../floorgen/fg_rng.h"

// Integer only: no float, no double and no libm in this file (docs/floorgen.md). The one float, the display fraction in the
// report, carries the allow marker and is never read by the layout. tests/floorgen_integer_headless.c keeps it that way.

#define GEN_MAX_ATTEMPTS 8
#define MAX_PARTITIONS   56                       // on a 45 by 45 floor; scaled by the floor's area (ScaleByArea)
#define SEGMENT_MIN      5
#define SEGMENT_MAX      18
#define DOOR_WIDTH       3
#define DOOR_MIN_LENGTH  9

// A probe's open span is cut when it exceeds a target. The target is large near
// the stair doors and shrinks toward the middle of the floor, so the tower gets
// tighter the further you are from where you arrived. Distances are Q16.16 tiles.
#define SPAN_NEAR  FX_FROM_INT(17)
#define SPAN_FAR   FX_FROM_INT(9)
#define RAMP_TILES 30

#define PILLAR_OPEN_MEAN (FX_FROM_INT(11) / 2)   // 5.5 tiles
#define PILLAR_MIN_SPAN  FX_FROM_INT(8)
#define PILLAR_CHANCE_PM 700                      // per mille

// Pillar halls: open squares reserved before partitioning.
#define HALL_RADIUS      5
#define HALL_MIN_DIST_SQ (14 * 14)
#define POCKET_KEEP_MIN  20

#define ROUTE_MIN        52                       // on a 45 by 45 floor; scaled by the floor's height (RouteMinFor)
#define ROUTE_MAX        260                      // likewise, by width plus height (RouteMaxFor)
#define WALKABLE_MIN_PCT 55
#define WALKABLE_MAX_PCT 90

// Scratch arrays are sized for the biggest floor; a floor of w by h tiles uses the first w * h entries, indexed z * w + x.
#define TILES_MAX  (MAP_MAX * MAP_MAX)
#define MAX_HALLS  40                              // 4 halls on 45 by 45, scaled by area, rounded up

// Quantities that grow with the floor are written as (value on the 45 by 45 floor) * (this floor's area) / (that floor's area), in
// integers, so on 45 by 45 they are exactly the old numbers and every old seed still gives the same floor.
#define SOLO_HALL_AREA ((MAP_WIDTH - 2) * (MAP_HEIGHT - 2))   // 43 by 43, the open hall of the 45 by 45 floor
static int ScaleByArea(int valueSolo, int hallArea) { return valueSolo * hallArea / SOLO_HALL_AREA; }
// The shortest route to the stairs a floor may have. The mean route grows by about 1.3 tiles per tile of height (measured: 60 at
// 45, 90 at 64, 113 at 81, 138 at 100, 171 at 128), so the minimum, 87 percent of the mean at 45, grows by 7/6 of a tile.
static int RouteMinFor(int h)         { return ROUTE_MIN + (h - MAP_HEIGHT) * 7 / 6; }
static int RouteMaxFor(int w, int h)  { return ROUTE_MAX * (w + h) / (MAP_WIDTH + MAP_HEIGHT); }

static uint32_t Hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint32_t MixSeed(uint32_t a, uint32_t b) { return Hash32(a ^ Hash32(b + 0x9e3779b9U)); }

uint32_t DungeonFloorSeed(uint32_t towerSeed, int floorNum)
{
    return MixSeed(towerSeed, (uint32_t)floorNum * 0x9e3779b1U);
}

const char *GenStageName(int stage)
{
    static const char *const names[GEN_STAGE_COUNT] = { "shell", "protect", "halls", "partitions", "pillars", "pockets", "measure" };
    return stage >= 0 && stage < GEN_STAGE_COUNT ? names[stage] : "?";
}

typedef struct {
    FloorMap    *f;
    int          w, h;                            // the floor's size
    FloorShell   sh;                              // its stair shell
    int          hallArea;                        // open tiles of the empty hall, (w - 2) * (h - 2)
    int          probeCols, probeRows;            // the probe lattice of this size
    int          maxPartitions;
    FgPcg32      rng;
    FgBitGrid    solid;                           // the walls of f, kept in step with it by SetWall and SetOpen
    FgProbeField pf;
    bool         prot[MAP_MAX][MAP_MAX];          // tiles the generator must leave alone
    bool         hall[MAP_MAX][MAP_MAX];          // kept open of partitions, filled with pillars
    int          hallX[MAX_HALLS], hallZ[MAX_HALLS], hallCount;
    bool         trackProbes;                     // while set, SetWall keeps the probe field in step (the partition pass)
} Ctx;

static inline bool InB(const Ctx *c, int x, int z) { return x >= 0 && z >= 0 && x < c->w && z < c->h; }

static inline bool IsReserved(const FloorMap *f, int x, int z) { return f->grid[z][x].flags & CELL_RESERVED; }

// Used before there is a Ctx, to stamp the fixed shell.
static void StampWall(FloorMap *f, int x, int z, uint8_t variant)
{
    f->grid[z][x].type    = TILE_WALL;
    f->grid[z][x].variant = variant;
}

// The two ways a tile changes during generation. Both keep the bit grid in step with the floor.
static void SetWall(Ctx *c, int x, int z, uint8_t variant)
{
    StampWall(c->f, x, z, variant);
    FgBitGrid_Set(&c->solid, x, z, true);
    if (c->trackProbes) FgProbeField_TileChanged(&c->solid, &c->pf, x, z);
}

static void SetOpen(Ctx *c, int x, int z)
{
    c->f->grid[z][x].type    = TILE_FLOOR;
    c->f->grid[z][x].variant = 0;
    FgBitGrid_Set(&c->solid, x, z, false);
}

static void SyncBits(Ctx *c)
{
    FgBitGrid_Init(&c->solid, c->w, c->h);
    for (int z = 0; z < c->h; ++z)
        for (int x = 0; x < c->w; ++x)
            if (c->f->grid[z][x].type == TILE_WALL) FgBitGrid_Set(&c->solid, x, z, true);
}

// The part of every floor of this size that never changes. f->w and f->h are set before this is called.
static void StampShell(FloorMap *f)
{
    const int W = f->w, H = f->h;
    const FloorShell sh = FloorShell_Make(W, H);
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            MapCell *c = &f->grid[z][x];
            c->type = TILE_WALL;
            c->discovered = false;
            c->variant = WALL_SHELL;
            c->flags = 0;
        }

    for (int z = sh.hallZ0; z <= sh.hallZ1; ++z)
        for (int x = sh.hallX0; x <= sh.hallX1; ++x) f->grid[z][x].type = TILE_FLOOR;

    for (int z = sh.upAlcoveZ0; z <= sh.upAlcoveZ1; ++z) StampWall(f, sh.wallX, z, WALL_SHELL);
    for (int x = sh.wallX; x <= sh.hallX1; ++x) StampWall(f, x, sh.upAlcoveZ1, WALL_SHELL);
    for (int z = sh.downAlcoveZ0; z <= sh.downAlcoveZ1; ++z) StampWall(f, sh.wallX, z, WALL_SHELL);
    for (int x = sh.wallX; x <= sh.hallX1; ++x) StampWall(f, x, sh.downAlcoveZ0, WALL_SHELL);

    for (int z = sh.upDoorZ0; z <= sh.upDoorZ1; ++z) f->grid[z][sh.wallX].type = TILE_FLOOR;
    for (int z = sh.downDoorZ0; z <= sh.downDoorZ1; ++z) f->grid[z][sh.wallX].type = TILE_FLOOR;

    f->grid[sh.upPadZ][sh.upPadX].type = TILE_STAIRS_UP;
    f->grid[sh.downPadZ][sh.downPadX].type = TILE_STAIRS_DOWN;

    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            const bool border = x == 0 || z == 0 || x == W - 1 || z == H - 1;
            const bool upAlcove = x >= sh.wallX && z >= sh.upAlcoveZ0 && z <= sh.upAlcoveZ1;
            const bool downAlcove = x >= sh.wallX && z >= sh.downAlcoveZ0 && z <= sh.downAlcoveZ1;
            if (border || upAlcove || downAlcove) f->grid[z][x].flags |= CELL_RESERVED;
        }

    f->startX = Tile_ToWorld(sh.arriveBelowX);
    f->startZ = Tile_ToWorld(sh.arriveBelowZ);
    f->stairsX = Tile_ToWorld(sh.upPadX);
    f->stairsZ = Tile_ToWorld(sh.upPadZ);
    f->elevatorX = f->elevatorZ = 0;
    f->downX = Tile_ToWorld(sh.downPadX);
    f->downZ = Tile_ToWorld(sh.downPadZ);
    f->arriveAboveX = Tile_ToWorld(sh.arriveAboveX);
    f->arriveAboveZ = Tile_ToWorld(sh.arriveAboveZ);
}

static void BuildProtectMask(Ctx *c)
{
    const int r2 = OPEN_RADIUS_TILES * OPEN_RADIUS_TILES;
    const FloorShell *sh = &c->sh;
    for (int z = 0; z < c->h; ++z)
        for (int x = 0; x < c->w; ++x) {
            bool p = IsReserved(c->f, x, z);
            int dx = x - sh->upApproachX, dz = z - sh->upApproachZ;
            if (dx * dx + dz * dz <= r2) p = true;
            dx = x - sh->downApproachX; dz = z - sh->downApproachZ;
            if (dx * dx + dz * dz <= r2) p = true;
            c->prot[z][x] = p;
        }
}

// Squared distance from a tile to the nearer of the two approach points.
static int DistToApproachSq(const Ctx *c, int x, int z)
{
    const int ax = x - c->sh.upApproachX, az = z - c->sh.upApproachZ;
    const int bx = x - c->sh.downApproachX, bz = z - c->sh.downApproachZ;
    const int a = ax * ax + az * az, b = bx * bx + bz * bz;
    return a < b ? a : b;
}

// The span a probe may have at this tile, Q16.16 tiles: SPAN_NEAR at the approach points, falling in a straight line to SPAN_FAR
// at RAMP_TILES away or more.
static Fx SpanTarget(const Ctx *c, int tileX, int tileZ)
{
    Fx t = FgDistFx2(DistToApproachSq(c, tileX, tileZ)) / RAMP_TILES;
    if (t > FX_ONE) t = FX_ONE;
    return SPAN_NEAR - (Fx)(((int64_t)(SPAN_NEAR - SPAN_FAR) * t) >> FX_SHIFT);
}

// Number of free tiles from (x, z) in direction (dx, dz), not counting the start.
static int ScanFree(const Ctx *c, int x, int z, int dx, int dz)
{
    if (dx > 0) return FgBitGrid_RunE(&c->solid, x, z);
    if (dx < 0) return FgBitGrid_RunW(&c->solid, x, z);
    if (dz > 0) return FgBitGrid_RunS(&c->solid, x, z);
    return FgBitGrid_RunN(&c->solid, x, z);
}

// Wall segment on column `line` (vertical, runs along z) or row `line`
// (horizontal, runs along x), tiles a..b. Every tile must be open floor outside
// the protected area, and the band MIN_CORRIDOR_TILES wide on both sides must
// be free of solid tiles so no corridor narrower than that is created.
static bool SegmentFits(const Ctx *c, bool vertical, int line, int a, int b)
{
    for (int i = a; i <= b; ++i) {
        const int x = vertical ? line : i;
        const int z = vertical ? i : line;
        if (!InB(c, x, z) || c->prot[z][x] || c->hall[z][x] || c->f->grid[z][x].type != TILE_FLOOR) return false;
        if (!FgBitGrid_SidesFree(&c->solid, vertical, line, i, MIN_CORRIDOR_TILES)) return false;
    }
    return true;
}

static bool PlacePartition(Ctx *c, int px, int pz, bool vertical)
{
    const int dx = vertical ? 1 : 0;
    const int dz = vertical ? 0 : 1;
    const int lo = ScanFree(c, px, pz, -dx, -dz);
    const int hi = ScanFree(c, px, pz, dx, dz);
    const int coord = vertical ? px : pz;
    const int cmin = coord - lo + MIN_CORRIDOR_TILES;
    const int cmax = coord + hi - MIN_CORRIDOR_TILES;
    if (cmax < cmin) return false;
    const int origin = vertical ? pz : px;

    for (int attempt = 0; attempt < 12; ++attempt) {
        int line;
        const uint32_t pick = FgPcg32_Range(&c->rng, 0u, 1000u);
        if (pick < 200u)      line = cmin + FgRng_Int(&c->rng, 0, 1);   // narrow lane against one wall
        else if (pick < 400u) line = cmax - FgRng_Int(&c->rng, 0, 1);
        else                  line = FgRng_Int(&c->rng, cmin, cmax);

        int a = origin, b = origin;
        if (!SegmentFits(c, vertical, line, a, b)) continue;

        const int target = FgRng_Int(&c->rng, SEGMENT_MIN + 2, SEGMENT_MAX);
        bool growA = true, growB = true;
        while (b - a + 1 < target && (growA || growB)) {
            if (growB) { if (SegmentFits(c, vertical, line, b + 1, b + 1)) ++b; else growB = false; }
            if (b - a + 1 >= target) break;
            if (growA) { if (SegmentFits(c, vertical, line, a - 1, a - 1)) --a; else growA = false; }
        }
        const int len = b - a + 1;
        if (len < SEGMENT_MIN) continue;

        int gap0 = -1;
        if (len >= DOOR_MIN_LENGTH) gap0 = FgRng_Int(&c->rng, a + 2, b - DOOR_WIDTH - 1);
        for (int i = a; i <= b; ++i) {
            if (gap0 >= 0 && i >= gap0 && i < gap0 + DOOR_WIDTH) continue;
            SetWall(c, vertical ? line : i, vertical ? i : line, WALL_PARTITION);
        }
        return true;
    }
    return false;
}

static int PartitionPass(Ctx *c)
{
    bool exhausted[FG_PROBE_MAX];
    memset(exhausted, 0, sizeof exhausted);
    int placed = 0;

    // The probe field is analysed once and kept in step by SetWall (FgProbeField_TileChanged), and each probe's span target, which
    // depends on its position only, is worked out once: the same numbers as analysing and measuring everything on every round.
    FgProbeField_Analyze(&c->solid, &c->pf, false);
    Fx targets[FG_PROBE_MAX];
    for (int i = 0; i < c->pf.count; ++i) targets[i] = SpanTarget(c, c->pf.probes[i].tileX, c->pf.probes[i].tileZ);
    c->trackProbes = true;

    while (placed < c->maxPartitions) {
        int best = -1;
        Fx bestExcess = 0;
        bool bestVertical = true;
        for (int i = 0; i < c->pf.count; ++i) {
            const FgProbe *p = &c->pf.probes[i];
            if (!p->active || exhausted[i]) continue;
            const Fx target = targets[i];
            const Fx exX = p->spanX - target;
            const Fx exZ = p->spanZ - target;
            const Fx ex = exX > exZ ? exX : exZ;
            if (ex > bestExcess) { bestExcess = ex; best = i; bestVertical = exX >= exZ; }
        }
        if (best < 0) break;

        const FgProbe *p = &c->pf.probes[best];
        bool ok = PlacePartition(c, p->tileX, p->tileZ, bestVertical);
        if (!ok) {
            const Fx target = targets[best];
            const Fx other = bestVertical ? p->spanZ : p->spanX;
            if (other > target) ok = PlacePartition(c, p->tileX, p->tileZ, !bestVertical);
        }
        if (ok) ++placed; else exhausted[best] = true;
    }
    c->trackProbes = false;
    return placed;
}

static bool TryPillar(Ctx *c, int x, int z)
{
    if (!InB(c, x, z) || c->prot[z][x] || !FgBitGrid_Open3x3(&c->solid, x, z)) return false;
    SetWall(c, x, z, WALL_PILLAR);
    return true;
}

static void PickHalls(Ctx *c)
{
    memset(c->hall, 0, sizeof c->hall);
    c->hallCount = 0;
    const int wantSolo = FgRng_Int(&c->rng, 2, 4);                  // the draw is the same on every size
    int want = ScaleByArea(wantSolo, c->hallArea);
    if (want > MAX_HALLS) want = MAX_HALLS;
    const int triesMax = ScaleByArea(60, c->hallArea);
    for (int tries = 0; tries < triesMax && c->hallCount < want; ++tries) {
        const int cx = FG_PROBE_ORIGIN + FG_PROBE_STRIDE * FgRng_Int(&c->rng, 1, c->probeCols - 2);
        const int cz = FG_PROBE_ORIGIN + FG_PROBE_STRIDE * FgRng_Int(&c->rng, 1, c->probeRows - 2);
        if (DistToApproachSq(c, cx, cz) < HALL_MIN_DIST_SQ) continue;
        if (cx - HALL_RADIUS < c->sh.hallX0 || cx + HALL_RADIUS > c->sh.hallX1) continue;
        if (cz - HALL_RADIUS < c->sh.hallZ0 || cz + HALL_RADIUS > c->sh.hallZ1) continue;
        bool clash = false;
        for (int i = 0; i < c->hallCount; ++i)
            if (abs(cx - c->hallX[i]) < 2 * HALL_RADIUS + 2 && abs(cz - c->hallZ[i]) < 2 * HALL_RADIUS + 2) clash = true;
        for (int dz = -HALL_RADIUS; dz <= HALL_RADIUS && !clash; ++dz)
            for (int dx = -HALL_RADIUS; dx <= HALL_RADIUS; ++dx)
                if (c->prot[cz + dz][cx + dx]) { clash = true; break; }
        if (clash) continue;
        for (int dz = -HALL_RADIUS; dz <= HALL_RADIUS; ++dz)
            for (int dx = -HALL_RADIUS; dx <= HALL_RADIUS; ++dx) c->hall[cz + dz][cx + dx] = true;
        c->hallX[c->hallCount] = cx;
        c->hallZ[c->hallCount] = cz;
        ++c->hallCount;
    }
}

// Pillars go on a regular lattice. Reserved halls get the full lattice. Rooms the
// probes found to be wide open get a looser, partial one.
static int PillarPass(Ctx *c)
{
    int placed = 0;
    for (int h = 0; h < c->hallCount; ++h)
        for (int oz = -FG_PROBE_STRIDE; oz <= FG_PROBE_STRIDE; oz += FG_PROBE_STRIDE)
            for (int ox = -FG_PROBE_STRIDE; ox <= FG_PROBE_STRIDE; ox += FG_PROBE_STRIDE)
                if (TryPillar(c, c->hallX[h] + ox, c->hallZ[h] + oz)) ++placed;

    FgProbeField_Analyze(&c->solid, &c->pf, true);
    for (int i = 0; i < c->pf.count; ++i) {
        const FgProbe *p = &c->pf.probes[i];
        if (!p->active || p->meanReach < PILLAR_OPEN_MEAN) continue;
        if (p->spanX < PILLAR_MIN_SPAN || p->spanZ < PILLAR_MIN_SPAN) continue;
        for (int oz = -FG_PROBE_STRIDE / 2; oz <= FG_PROBE_STRIDE / 2; oz += FG_PROBE_STRIDE)
            for (int ox = -FG_PROBE_STRIDE / 2; ox <= FG_PROBE_STRIDE / 2; ox += FG_PROBE_STRIDE) {
                if (!FgRng_Chance(&c->rng, PILLAR_CHANCE_PM)) continue;
                if (TryPillar(c, p->tileX + ox, p->tileZ + oz)) ++placed;
            }
    }
    return placed;
}

static int g_queue[TILES_MAX];
static int g_dist[TILES_MAX];

// Breadth-first flood over walkable tiles. Fills g_dist (-1 = unreached) and
// returns how many tiles were reached.
static int Flood(const FloorMap *f, int sx, int sz)
{
    const int W = f->w, n = f->w * f->h;
    for (int i = 0; i < n; ++i) g_dist[i] = -1;
    if (Tile_IsSolid(f, sx, sz)) return 0;
    int head = 0, tail = 0;
    g_dist[sz * W + sx] = 0;
    g_queue[tail++] = sz * W + sx;
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dzs[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        const int cur = g_queue[head++];
        const int cx = cur % W, cz = cur / W;
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + dxs[k], nz = cz + dzs[k];
            if (Tile_IsSolid(f, nx, nz)) continue;
            const int ni = nz * W + nx;
            if (g_dist[ni] >= 0) continue;
            g_dist[ni] = g_dist[cur] + 1;
            g_queue[tail++] = ni;
        }
    }
    return tail;
}

static int g_cost[TILES_MAX];
static int g_prev[TILES_MAX];
static unsigned char g_done[TILES_MAX];
static unsigned char g_reach[TILES_MAX];  // 1 = reachable from the arrival point
static unsigned char g_pocket[TILES_MAX]; // 1 = tile of the pocket being handled
static int g_pocketList[TILES_MAX];

// A binary heap of tiles ordered by (cost, index), with decrease-key. The order is total, so it pops tiles in exactly the order the
// old scan chose them (cheapest first, the lowest index among equals) and every old floor comes out the same; it costs n log n
// where the scan cost n squared, which is what made 128 by 128 floors unaffordable (docs/design/08, section 13.4).
static int g_heap[TILES_MAX];
static int g_heapPos[TILES_MAX];          // where a tile sits in g_heap, -1 when it is not in it
static int g_heapN;

static inline bool HeapLess(int a, int b) { return g_cost[a] < g_cost[b] || (g_cost[a] == g_cost[b] && a < b); }

static void HeapUp(int i)
{
    const int node = g_heap[i];
    while (i > 0) {
        const int parent = (i - 1) / 2;
        if (!HeapLess(node, g_heap[parent])) break;
        g_heap[i] = g_heap[parent];
        g_heapPos[g_heap[i]] = i;
        i = parent;
    }
    g_heap[i] = node;
    g_heapPos[node] = i;
}

static void HeapDown(int i)
{
    const int node = g_heap[i];
    for (;;) {
        const int l = 2 * i + 1;
        if (l >= g_heapN) break;
        const int r = l + 1;
        const int m = (r < g_heapN && HeapLess(g_heap[r], g_heap[l])) ? r : l;
        if (!HeapLess(g_heap[m], node)) break;
        g_heap[i] = g_heap[m];
        g_heapPos[g_heap[i]] = i;
        i = m;
    }
    g_heap[i] = node;
    g_heapPos[node] = i;
}

// Adds a tile, or moves it up after its cost fell.
static void HeapPushOrLower(int node)
{
    if (g_heapPos[node] < 0) { g_heap[g_heapN] = node; g_heapPos[node] = g_heapN; ++g_heapN; }
    HeapUp(g_heapPos[node]);
}

static int HeapPop(void)
{
    const int top = g_heap[0];
    g_heapPos[top] = -1;
    --g_heapN;
    if (g_heapN > 0) { g_heap[0] = g_heap[g_heapN]; g_heapPos[g_heap[0]] = 0; HeapDown(0); }
    return top;
}

// Cheapest route from the pocket to the reachable region. Walking open ground
// costs 1, going through a wall costs 6, reserved walls are off limits. The
// route is opened with a 3x3 brush.
static bool CarveToReachable(Ctx *c)
{
    FloorMap *f = c->f;
    const int W = c->w, n = c->w * c->h;
    g_heapN = 0;
    for (int i = 0; i < n; ++i) {
        g_cost[i] = g_pocket[i] ? 0 : INT_MAX;
        g_prev[i] = -1;
        g_done[i] = 0;
        g_heapPos[i] = -1;
    }
    for (int i = 0; i < n; ++i)
        if (g_pocket[i]) HeapPushOrLower(i);
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dzs[4] = { 0, 0, 1, -1 };

    int goal = -1;
    while (g_heapN > 0) {
        const int u = HeapPop();
        if (g_reach[u]) { goal = u; break; }
        g_done[u] = 1;
        const int ux = u % W, uz = u / W;
        for (int k = 0; k < 4; ++k) {
            const int nx = ux + dxs[k], nz = uz + dzs[k];
            if (!InB(c, nx, nz)) continue;
            const bool solid = Tile_IsSolid(f, nx, nz);
            if (solid && IsReserved(f, nx, nz)) continue;
            const int ni = nz * W + nx;
            const int step = solid ? 6 : 1;
            if (g_cost[u] + step < g_cost[ni]) { g_cost[ni] = g_cost[u] + step; g_prev[ni] = u; HeapPushOrLower(ni); }
        }
    }
    if (goal < 0) return false;

    for (int cur = goal; cur >= 0; cur = g_prev[cur]) {
        const int cx = cur % W, cz = cur / W;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cx + dx, z = cz + dz;
                if (!InB(c, x, z) || IsReserved(f, x, z)) continue;
                SetOpen(c, x, z);
            }
    }
    return true;
}

// Every open tile the arrival point cannot reach belongs to a pocket. A pocket
// that is large, or that holds a reserved tile such as an alcove, is joined to
// the rest with a doorway. Small leftovers are filled in.
static void ConnectPockets(Ctx *c)
{
    FloorMap *f = c->f;
    const int W = c->w, n = c->w * c->h;
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dzs[4] = { 0, 0, 1, -1 };

    const int passMax = ScaleByArea(40, c->hallArea);
    for (int pass = 0; pass < passMax; ++pass) {
        Flood(f, c->sh.arriveBelowX, c->sh.arriveBelowZ);
        for (int i = 0; i < n; ++i) g_reach[i] = g_dist[i] >= 0;

        int start = -1;
        for (int i = 0; i < n && start < 0; ++i)
            if (!g_reach[i] && !Tile_IsSolid(f, i % W, i / W)) start = i;
        if (start < 0) return;

        memset(g_pocket, 0, sizeof g_pocket);
        int head = 0, tail = 0;
        bool hasReserved = false;
        g_pocket[start] = 1;
        g_pocketList[tail++] = start;
        while (head < tail) {
            const int cur = g_pocketList[head++];
            const int cx = cur % W, cz = cur / W;
            if (IsReserved(f, cx, cz)) hasReserved = true;
            for (int k = 0; k < 4; ++k) {
                const int nx = cx + dxs[k], nz = cz + dzs[k];
                if (Tile_IsSolid(f, nx, nz)) continue;
                const int ni = nz * W + nx;
                if (g_pocket[ni] || g_reach[ni]) continue;
                g_pocket[ni] = 1;
                g_pocketList[tail++] = ni;
            }
        }

        if (tail >= POCKET_KEEP_MIN || hasReserved) {
            if (CarveToReachable(c)) continue;
        }
        for (int i = 0; i < tail; ++i) {
            const int x = g_pocketList[i] % W, z = g_pocketList[i] / W;
            if (!IsReserved(f, x, z)) SetWall(c, x, z, WALL_PARTITION);
        }
    }
}

typedef struct {
    bool  reservedIntact;
    bool  upPadReached;
    bool  arriveAboveReached;
    int   pathLength;
    int   pockets;
    int   openTiles;
    int   routeMax;                    // the longest route to the stairs this floor's size allows
} Metrics;

static void Measure(const FloorMap *f, Metrics *m)
{
    static FloorMap canon;
    const int W = f->w, H = f->h;
    const FloorShell sh = FloorShell_Make(W, H);
    canon.w = W;
    canon.h = H;
    StampShell(&canon);

    m->reservedIntact = true;
    for (int z = 0; z < H && m->reservedIntact; ++z)
        for (int x = 0; x < W; ++x) {
            if (!IsReserved(&canon, x, z)) continue;
            const MapCell *a = &f->grid[z][x];
            const MapCell *b = &canon.grid[z][x];
            if (!(a->flags & CELL_RESERVED) || a->type != b->type || a->variant != b->variant) {
                m->reservedIntact = false;
                break;
            }
        }

    const int reached = Flood(f, sh.arriveBelowX, sh.arriveBelowZ);
    m->upPadReached = g_dist[sh.upPadZ * W + sh.upPadX] >= 0;
    m->arriveAboveReached = g_dist[sh.arriveAboveZ * W + sh.arriveAboveX] >= 0;
    m->pathLength = m->upPadReached ? g_dist[sh.upPadZ * W + sh.upPadX] : -1;
    m->routeMax = RouteMaxFor(W, H);

    int open = 0;
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x)
            if (!Tile_IsSolid(f, x, z)) ++open;
    m->pockets = open - reached;
    m->openTiles = open;
}

// The rules every floor must satisfy, including the fallback layout. NULL when they hold.
static const char *MetricsError(const Metrics *m)
{
    if (!m->reservedIntact)             return "fixed shell or stair alcoves were changed";
    if (!m->upPadReached)               return "up pad not reachable from arrival";
    if (!m->arriveAboveReached)         return "arrival from above not reachable";
    if (m->pockets != 0)                return "walkable pockets cut off from arrival";
    if (m->pathLength > m->routeMax)    return "route to the stairs is too long";
    return NULL;
}

bool ValidateFloorMap(const FloorMap *f, char *why, size_t whySize)
{
    Metrics m;
    Measure(f, &m);
    const char *err = MetricsError(&m);
    if (err && why && whySize) snprintf(why, whySize, "%s", err);
    return err == NULL;
}

// Quality targets. A generated layout that misses them is thrown away and
// rebuilt from the next sub-stream. Whole-number comparisons: open tiles against the hall area.
static bool MeetsTargets(const FloorMap *f)
{
    const int hallArea = (f->w - 2) * (f->h - 2);
    return f->pathLength >= RouteMinFor(f->h)
        && f->openTiles * 100 >= WALKABLE_MIN_PCT * hallArea
        && f->openTiles * 100 <= WALKABLE_MAX_PCT * hallArea;
}

static void StageEnd(FloorMap *f, GenStage s, uint32_t t0) { f->stageMicros[s] += FgClockMicros() - t0; }

// One attempt. Every random choice comes from a stream of its own (the zone's halls, partitions and pillars), keyed by the floor's
// seed and the attempt number, so the stages do not disturb each other.
static void BuildAttempt(FloorMap *f, uint32_t floorSeed, uint32_t attempt, bool fallback, Metrics *m)
{
    static Ctx c;
    const uint64_t seed64 = FgFloorSeed64(floorSeed);
    c.f = f;
    c.w = f->w;
    c.h = f->h;
    c.sh = FloorShell_Make(c.w, c.h);
    c.hallArea = (c.w - 2) * (c.h - 2);
    c.probeCols = ((c.w - 1 - FG_PROBE_ORIGIN) / FG_PROBE_STRIDE) + 1;
    c.probeRows = ((c.h - 1 - FG_PROBE_ORIGIN) / FG_PROBE_STRIDE) + 1;
    c.maxPartitions = ScaleByArea(MAX_PARTITIONS, c.hallArea);
    uint32_t t = FgClockMicros();

    StampShell(f);
    SyncBits(&c);
    StageEnd(f, GEN_STAGE_SHELL, t);

    t = FgClockMicros();
    BuildProtectMask(&c);
    StageEnd(f, GEN_STAGE_PROTECT, t);

    t = FgClockMicros();
    FgStream(&c.rng, seed64, FG_STREAM_ZONE, FG_ZONE_HALLS, attempt);
    PickHalls(&c);
    StageEnd(f, GEN_STAGE_HALLS, t);

    t = FgClockMicros();
    FgStream(&c.rng, seed64, FG_STREAM_ZONE, FG_ZONE_PARTITIONS, attempt);
    f->partitions = fallback ? 0 : PartitionPass(&c);
    StageEnd(f, GEN_STAGE_PARTITIONS, t);

    t = FgClockMicros();
    FgStream(&c.rng, seed64, FG_STREAM_ZONE, FG_ZONE_PILLARS, attempt);
    f->pillars = PillarPass(&c);
    StageEnd(f, GEN_STAGE_PILLARS, t);

    t = FgClockMicros();
    ConnectPockets(&c);
    StageEnd(f, GEN_STAGE_POCKETS, t);

    t = FgClockMicros();
    Measure(f, m);
    f->pathLength = m->pathLength;
    f->openTiles = m->openTiles;
    f->walkableFraction = (float)m->openTiles / (float)c.hallArea;   // FLOAT-OK: display only, never read by the layout
    StageEnd(f, GEN_STAGE_MEASURE, t);
}

void GenerateFloorMapSized(FloorMap *outFloor, int floorNum, uint32_t baseSeed, int w, int h)
{
    const uint32_t started = FgClockMicros();
    outFloor->w = w < FLOOR_TILES_MIN ? FLOOR_TILES_MIN : (w > FLOOR_TILES_MAX ? FLOOR_TILES_MAX : w);
    outFloor->h = h < FLOOR_TILES_MIN ? FLOOR_TILES_MIN : (h > FLOOR_TILES_MAX ? FLOOR_TILES_MAX : h);
    const uint32_t floorSeed = DungeonFloorSeed(baseSeed, floorNum);
    outFloor->floorNumber = floorNum;
    outFloor->seed = floorSeed;
    memset(outFloor->stageMicros, 0, sizeof outFloor->stageMicros);

    Metrics m;
    for (int attempt = 0; attempt < GEN_MAX_ATTEMPTS; ++attempt) {
        BuildAttempt(outFloor, floorSeed, (uint32_t)attempt, false, &m);
        if (MetricsError(&m) == NULL && MeetsTargets(outFloor)) {
            outFloor->attempts = attempt + 1;
            outFloor->usedFallback = false;
            outFloor->genMicros = FgClockMicros() - started;
            return;
        }
    }
    // Pillar hall with no partitions. It always passes, so the player is never stuck.
    BuildAttempt(outFloor, floorSeed, GEN_MAX_ATTEMPTS, true, &m);
    outFloor->attempts = GEN_MAX_ATTEMPTS;
    outFloor->usedFallback = true;
    outFloor->genMicros = FgClockMicros() - started;
}

// The 45 by 45 solo floor. src/net and the older tests build floors through this call.
void GenerateFloorMap(FloorMap *outFloor, int floorNum, uint32_t baseSeed)
{
    GenerateFloorMapSized(outFloor, floorNum, baseSeed, MAP_WIDTH, MAP_HEIGHT);
}
