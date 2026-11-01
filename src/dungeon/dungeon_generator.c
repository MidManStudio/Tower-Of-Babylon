// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "dungeon_generator.c"
// ============================================================================
#include "dungeon_generator.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dungeon_probes.h"

#define GEN_MAX_ATTEMPTS 8
#define MAX_PARTITIONS   56
#define SEGMENT_MIN      5
#define SEGMENT_MAX      18
#define DOOR_WIDTH       3
#define DOOR_MIN_LENGTH  9

// A probe's open span is cut when it exceeds a target. The target is large near
// the stair doors and shrinks toward the middle of the floor, so the tower gets
// tighter the further you are from where you arrived.
#define SPAN_NEAR  17.0f
#define SPAN_FAR   9.0f
#define RAMP_TILES 30.0f

#define PILLAR_OPEN_MEAN 5.5f
#define PILLAR_MIN_SPAN  8.0f
#define PILLAR_CHANCE    0.7f

// Pillar halls: open squares reserved before partitioning.
#define HALL_RADIUS      5
#define HALL_MIN_DIST    14.0f
#define POCKET_KEEP_MIN  20

#define ROUTE_MIN     52
#define ROUTE_MAX    260
#define WALKABLE_MIN 0.55f
#define WALKABLE_MAX 0.90f

#define TILE_COUNT (MAP_WIDTH * MAP_HEIGHT)

typedef struct { uint32_t s; } Rng;

static uint32_t Hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint32_t MixSeed(uint32_t a, uint32_t b) { return Hash32(a ^ Hash32(b + 0x9e3779b9U)); }

static void RngInit(Rng *r, uint32_t seed) { r->s = seed ? seed : 0x6d2b79f5U; }

static uint32_t RngNext(Rng *r)
{
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

static int RngRange(Rng *r, int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(RngNext(r) % (uint32_t)(hi - lo + 1));
}

static float RngFloat(Rng *r) { return (float)(RngNext(r) >> 8) * (1.0f / 16777216.0f); }

uint32_t DungeonFloorSeed(uint32_t towerSeed, int floorNum)
{
    return MixSeed(towerSeed, (uint32_t)floorNum * 0x9e3779b1U);
}

typedef struct {
    FloorMap  *f;
    Rng        rng;
    ProbeField pf;
    bool       prot[MAP_HEIGHT][MAP_WIDTH];  // tiles the generator must leave alone
    bool       hall[MAP_HEIGHT][MAP_WIDTH];  // kept open of partitions, filled with pillars
    int        hallX[4], hallZ[4], hallCount;
} Ctx;

static inline bool IsReserved(const FloorMap *f, int x, int z) { return f->grid[z][x].flags & CELL_RESERVED; }

static void SetWall(FloorMap *f, int x, int z, uint8_t variant)
{
    f->grid[z][x].type    = TILE_WALL;
    f->grid[z][x].variant = variant;
}

// The part of every floor that never changes.
static void StampShell(FloorMap *f)
{
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            MapCell *c = &f->grid[z][x];
            c->type = TILE_WALL;
            c->discovered = false;
            c->variant = WALL_SHELL;
            c->flags = 0;
        }

    for (int z = HALL_Z0; z <= HALL_Z1; ++z)
        for (int x = HALL_X0; x <= HALL_X1; ++x) f->grid[z][x].type = TILE_FLOOR;

    for (int z = UP_ALCOVE_Z0; z <= UP_ALCOVE_Z1; ++z) SetWall(f, STAIR_WALL_X, z, WALL_SHELL);
    for (int x = STAIR_WALL_X; x <= HALL_X1; ++x) SetWall(f, x, UP_ALCOVE_Z1, WALL_SHELL);
    for (int z = DOWN_ALCOVE_Z0; z <= DOWN_ALCOVE_Z1; ++z) SetWall(f, STAIR_WALL_X, z, WALL_SHELL);
    for (int x = STAIR_WALL_X; x <= HALL_X1; ++x) SetWall(f, x, DOWN_ALCOVE_Z0, WALL_SHELL);

    for (int z = UP_DOOR_Z0; z <= UP_DOOR_Z1; ++z) f->grid[z][STAIR_WALL_X].type = TILE_FLOOR;
    for (int z = DOWN_DOOR_Z0; z <= DOWN_DOOR_Z1; ++z) f->grid[z][STAIR_WALL_X].type = TILE_FLOOR;

    f->grid[UP_PAD_Z][UP_PAD_X].type = TILE_STAIRS_UP;
    f->grid[DOWN_PAD_Z][DOWN_PAD_X].type = TILE_STAIRS_DOWN;

    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            const bool border = x == 0 || z == 0 || x == MAP_WIDTH - 1 || z == MAP_HEIGHT - 1;
            const bool upAlcove = x >= STAIR_WALL_X && z >= UP_ALCOVE_Z0 && z <= UP_ALCOVE_Z1;
            const bool downAlcove = x >= STAIR_WALL_X && z >= DOWN_ALCOVE_Z0 && z <= DOWN_ALCOVE_Z1;
            if (border || upAlcove || downAlcove) f->grid[z][x].flags |= CELL_RESERVED;
        }

    f->startX = Tile_ToWorld(ARRIVE_BELOW_X);
    f->startZ = Tile_ToWorld(ARRIVE_BELOW_Z);
    f->stairsX = Tile_ToWorld(UP_PAD_X);
    f->stairsZ = Tile_ToWorld(UP_PAD_Z);
    f->elevatorX = f->elevatorZ = 0.0f;
    f->downX = Tile_ToWorld(DOWN_PAD_X);
    f->downZ = Tile_ToWorld(DOWN_PAD_Z);
    f->arriveAboveX = Tile_ToWorld(ARRIVE_ABOVE_X);
    f->arriveAboveZ = Tile_ToWorld(ARRIVE_ABOVE_Z);
}

static void BuildProtectMask(Ctx *c)
{
    const int r2 = OPEN_RADIUS_TILES * OPEN_RADIUS_TILES;
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            bool p = IsReserved(c->f, x, z);
            int dx = x - UP_APPROACH_X, dz = z - UP_APPROACH_Z;
            if (dx * dx + dz * dz <= r2) p = true;
            dx = x - DOWN_APPROACH_X; dz = z - DOWN_APPROACH_Z;
            if (dx * dx + dz * dz <= r2) p = true;
            c->prot[z][x] = p;
        }
}

static float DistToApproach(int x, int z)
{
    const float a = hypotf((float)(x - UP_APPROACH_X), (float)(z - UP_APPROACH_Z));
    const float b = hypotf((float)(x - DOWN_APPROACH_X), (float)(z - DOWN_APPROACH_Z));
    return a < b ? a : b;
}

// Number of free tiles from (x, z) in direction (dx, dz), not counting the start.
static int ScanFree(const FloorMap *f, int x, int z, int dx, int dz)
{
    int n = 0;
    while (!Tile_IsSolid(f, x + dx * (n + 1), z + dz * (n + 1))) ++n;
    return n;
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
        if (!Tile_InBounds(x, z) || c->prot[z][x] || c->hall[z][x] || c->f->grid[z][x].type != TILE_FLOOR) return false;
        for (int d = -MIN_CORRIDOR_TILES; d <= MIN_CORRIDOR_TILES; ++d) {
            if (d == 0) continue;
            if (Tile_IsSolid(c->f, vertical ? x + d : x, vertical ? z : z + d)) return false;
        }
    }
    return true;
}

static bool PlacePartition(Ctx *c, int px, int pz, bool vertical)
{
    const int dx = vertical ? 1 : 0;
    const int dz = vertical ? 0 : 1;
    const int lo = ScanFree(c->f, px, pz, -dx, -dz);
    const int hi = ScanFree(c->f, px, pz, dx, dz);
    const int coord = vertical ? px : pz;
    const int cmin = coord - lo + MIN_CORRIDOR_TILES;
    const int cmax = coord + hi - MIN_CORRIDOR_TILES;
    if (cmax < cmin) return false;
    const int origin = vertical ? pz : px;

    for (int attempt = 0; attempt < 12; ++attempt) {
        int line;
        const float pick = RngFloat(&c->rng);
        if (pick < 0.2f)      line = cmin + RngRange(&c->rng, 0, 1);   // narrow lane against one wall
        else if (pick < 0.4f) line = cmax - RngRange(&c->rng, 0, 1);
        else                  line = RngRange(&c->rng, cmin, cmax);

        int a = origin, b = origin;
        if (!SegmentFits(c, vertical, line, a, b)) continue;

        const int target = RngRange(&c->rng, SEGMENT_MIN + 2, SEGMENT_MAX);
        bool growA = true, growB = true;
        while (b - a + 1 < target && (growA || growB)) {
            if (growB) { if (SegmentFits(c, vertical, line, b + 1, b + 1)) ++b; else growB = false; }
            if (b - a + 1 >= target) break;
            if (growA) { if (SegmentFits(c, vertical, line, a - 1, a - 1)) --a; else growA = false; }
        }
        const int len = b - a + 1;
        if (len < SEGMENT_MIN) continue;

        int gap0 = -1;
        if (len >= DOOR_MIN_LENGTH) gap0 = RngRange(&c->rng, a + 2, b - DOOR_WIDTH - 1);
        for (int i = a; i <= b; ++i) {
            if (gap0 >= 0 && i >= gap0 && i < gap0 + DOOR_WIDTH) continue;
            SetWall(c->f, vertical ? line : i, vertical ? i : line, WALL_PARTITION);
        }
        return true;
    }
    return false;
}

static int PartitionPass(Ctx *c)
{
    bool exhausted[PROBE_COUNT];
    memset(exhausted, 0, sizeof exhausted);
    int placed = 0;

    while (placed < MAX_PARTITIONS) {
        ProbeField_Analyze(c->f, &c->pf, false);

        int best = -1;
        float bestExcess = 0.0f;
        bool bestVertical = true;
        for (int i = 0; i < PROBE_COUNT; ++i) {
            const Probe *p = &c->pf.probes[i];
            if (!p->active || exhausted[i]) continue;
            float t = DistToApproach(p->tileX, p->tileZ) / RAMP_TILES;
            if (t > 1.0f) t = 1.0f;
            const float target = SPAN_NEAR + (SPAN_FAR - SPAN_NEAR) * t;
            const float exX = p->spanX - target;
            const float exZ = p->spanZ - target;
            const float ex = exX > exZ ? exX : exZ;
            if (ex > bestExcess) { bestExcess = ex; best = i; bestVertical = exX >= exZ; }
        }
        if (best < 0) break;

        const Probe *p = &c->pf.probes[best];
        bool ok = PlacePartition(c, p->tileX, p->tileZ, bestVertical);
        if (!ok) {
            float t = DistToApproach(p->tileX, p->tileZ) / RAMP_TILES;
            if (t > 1.0f) t = 1.0f;
            const float target = SPAN_NEAR + (SPAN_FAR - SPAN_NEAR) * t;
            const float other = bestVertical ? p->spanZ : p->spanX;
            if (other > target) ok = PlacePartition(c, p->tileX, p->tileZ, !bestVertical);
        }
        if (ok) ++placed; else exhausted[best] = true;
    }
    return placed;
}

static bool Open3x3(const FloorMap *f, int x, int z)
{
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (Tile_IsSolid(f, x + dx, z + dz)) return false;
    return true;
}

static bool TryPillar(Ctx *c, int x, int z)
{
    if (!Tile_InBounds(x, z) || c->prot[z][x] || !Open3x3(c->f, x, z)) return false;
    SetWall(c->f, x, z, WALL_PILLAR);
    return true;
}

static void PickHalls(Ctx *c)
{
    memset(c->hall, 0, sizeof c->hall);
    c->hallCount = 0;
    const int want = RngRange(&c->rng, 2, 4);
    for (int tries = 0; tries < 60 && c->hallCount < want; ++tries) {
        const int cx = PROBE_ORIGIN + PROBE_STRIDE * RngRange(&c->rng, 1, PROBE_COLS - 2);
        const int cz = PROBE_ORIGIN + PROBE_STRIDE * RngRange(&c->rng, 1, PROBE_ROWS - 2);
        if (DistToApproach(cx, cz) < HALL_MIN_DIST) continue;
        if (cx - HALL_RADIUS < HALL_X0 || cx + HALL_RADIUS > HALL_X1) continue;
        if (cz - HALL_RADIUS < HALL_Z0 || cz + HALL_RADIUS > HALL_Z1) continue;
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
        for (int oz = -PROBE_STRIDE; oz <= PROBE_STRIDE; oz += PROBE_STRIDE)
            for (int ox = -PROBE_STRIDE; ox <= PROBE_STRIDE; ox += PROBE_STRIDE)
                if (TryPillar(c, c->hallX[h] + ox, c->hallZ[h] + oz)) ++placed;

    ProbeField_Analyze(c->f, &c->pf, true);
    for (int i = 0; i < PROBE_COUNT; ++i) {
        const Probe *p = &c->pf.probes[i];
        if (!p->active || p->meanReach < PILLAR_OPEN_MEAN) continue;
        if (p->spanX < PILLAR_MIN_SPAN || p->spanZ < PILLAR_MIN_SPAN) continue;
        for (int oz = -PROBE_STRIDE / 2; oz <= PROBE_STRIDE / 2; oz += PROBE_STRIDE)
            for (int ox = -PROBE_STRIDE / 2; ox <= PROBE_STRIDE / 2; ox += PROBE_STRIDE) {
                if (RngFloat(&c->rng) > PILLAR_CHANCE) continue;
                if (TryPillar(c, p->tileX + ox, p->tileZ + oz)) ++placed;
            }
    }
    return placed;
}

static int g_queue[TILE_COUNT];
static int g_dist[TILE_COUNT];

// Breadth-first flood over walkable tiles. Fills g_dist (-1 = unreached) and
// returns how many tiles were reached.
static int Flood(const FloorMap *f, int sx, int sz)
{
    for (int i = 0; i < TILE_COUNT; ++i) g_dist[i] = -1;
    if (Tile_IsSolid(f, sx, sz)) return 0;
    int head = 0, tail = 0;
    g_dist[sz * MAP_WIDTH + sx] = 0;
    g_queue[tail++] = sz * MAP_WIDTH + sx;
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dzs[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        const int cur = g_queue[head++];
        const int cx = cur % MAP_WIDTH, cz = cur / MAP_WIDTH;
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + dxs[k], nz = cz + dzs[k];
            if (Tile_IsSolid(f, nx, nz)) continue;
            const int ni = nz * MAP_WIDTH + nx;
            if (g_dist[ni] >= 0) continue;
            g_dist[ni] = g_dist[cur] + 1;
            g_queue[tail++] = ni;
        }
    }
    return tail;
}

static int g_cost[TILE_COUNT];
static int g_prev[TILE_COUNT];
static unsigned char g_done[TILE_COUNT];
static unsigned char g_reach[TILE_COUNT];  // 1 = reachable from the arrival point
static unsigned char g_pocket[TILE_COUNT]; // 1 = tile of the pocket being handled
static int g_pocketList[TILE_COUNT];

// Cheapest route from the pocket to the reachable region. Walking open ground
// costs 1, going through a wall costs 6, reserved walls are off limits. The
// route is opened with a 3x3 brush.
static bool CarveToReachable(Ctx *c)
{
    FloorMap *f = c->f;
    for (int i = 0; i < TILE_COUNT; ++i) {
        g_cost[i] = g_pocket[i] ? 0 : INT_MAX;
        g_prev[i] = -1;
        g_done[i] = 0;
    }
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dzs[4] = { 0, 0, 1, -1 };

    int goal = -1;
    for (int iter = 0; iter < TILE_COUNT; ++iter) {
        int u = -1, best = INT_MAX;
        for (int i = 0; i < TILE_COUNT; ++i)
            if (!g_done[i] && g_cost[i] < best) { best = g_cost[i]; u = i; }
        if (u < 0) break;
        if (g_reach[u]) { goal = u; break; }
        g_done[u] = 1;
        const int ux = u % MAP_WIDTH, uz = u / MAP_WIDTH;
        for (int k = 0; k < 4; ++k) {
            const int nx = ux + dxs[k], nz = uz + dzs[k];
            if (!Tile_InBounds(nx, nz)) continue;
            const bool solid = Tile_IsSolid(f, nx, nz);
            if (solid && IsReserved(f, nx, nz)) continue;
            const int ni = nz * MAP_WIDTH + nx;
            const int step = solid ? 6 : 1;
            if (g_cost[u] + step < g_cost[ni]) { g_cost[ni] = g_cost[u] + step; g_prev[ni] = u; }
        }
    }
    if (goal < 0) return false;

    for (int cur = goal; cur >= 0; cur = g_prev[cur]) {
        const int cx = cur % MAP_WIDTH, cz = cur / MAP_WIDTH;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cx + dx, z = cz + dz;
                if (!Tile_InBounds(x, z) || IsReserved(f, x, z)) continue;
                f->grid[z][x].type = TILE_FLOOR;
                f->grid[z][x].variant = 0;
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
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dzs[4] = { 0, 0, 1, -1 };

    for (int pass = 0; pass < 40; ++pass) {
        Flood(f, ARRIVE_BELOW_X, ARRIVE_BELOW_Z);
        for (int i = 0; i < TILE_COUNT; ++i) g_reach[i] = g_dist[i] >= 0;

        int start = -1;
        for (int i = 0; i < TILE_COUNT && start < 0; ++i)
            if (!g_reach[i] && !Tile_IsSolid(f, i % MAP_WIDTH, i / MAP_WIDTH)) start = i;
        if (start < 0) return;

        memset(g_pocket, 0, sizeof g_pocket);
        int head = 0, tail = 0;
        bool hasReserved = false;
        g_pocket[start] = 1;
        g_pocketList[tail++] = start;
        while (head < tail) {
            const int cur = g_pocketList[head++];
            const int cx = cur % MAP_WIDTH, cz = cur / MAP_WIDTH;
            if (IsReserved(f, cx, cz)) hasReserved = true;
            for (int k = 0; k < 4; ++k) {
                const int nx = cx + dxs[k], nz = cz + dzs[k];
                if (Tile_IsSolid(f, nx, nz)) continue;
                const int ni = nz * MAP_WIDTH + nx;
                if (g_pocket[ni] || g_reach[ni]) continue;
                g_pocket[ni] = 1;
                g_pocketList[tail++] = ni;
            }
        }

        if (tail >= POCKET_KEEP_MIN || hasReserved) {
            if (CarveToReachable(c)) continue;
        }
        for (int i = 0; i < tail; ++i) {
            const int x = g_pocketList[i] % MAP_WIDTH, z = g_pocketList[i] / MAP_WIDTH;
            if (!IsReserved(f, x, z)) SetWall(f, x, z, WALL_PARTITION);
        }
    }
}

typedef struct {
    bool  reservedIntact;
    bool  upPadReached;
    bool  arriveAboveReached;
    int   pathLength;
    int   pockets;
    float walkable;
} Metrics;

static void Measure(const FloorMap *f, Metrics *m)
{
    static FloorMap canon;
    StampShell(&canon);

    m->reservedIntact = true;
    for (int z = 0; z < MAP_HEIGHT && m->reservedIntact; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            if (!IsReserved(&canon, x, z)) continue;
            const MapCell *a = &f->grid[z][x];
            const MapCell *b = &canon.grid[z][x];
            if (!(a->flags & CELL_RESERVED) || a->type != b->type || a->variant != b->variant) {
                m->reservedIntact = false;
                break;
            }
        }

    const int reached = Flood(f, ARRIVE_BELOW_X, ARRIVE_BELOW_Z);
    m->upPadReached = g_dist[UP_PAD_Z * MAP_WIDTH + UP_PAD_X] >= 0;
    m->arriveAboveReached = g_dist[ARRIVE_ABOVE_Z * MAP_WIDTH + ARRIVE_ABOVE_X] >= 0;
    m->pathLength = m->upPadReached ? g_dist[UP_PAD_Z * MAP_WIDTH + UP_PAD_X] : -1;

    int open = 0;
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x)
            if (!Tile_IsSolid(f, x, z)) ++open;
    m->pockets = open - reached;
    m->walkable = (float)open / (float)((HALL_X1 - HALL_X0 + 1) * (HALL_Z1 - HALL_Z0 + 1));
}

// Rules every floor must satisfy, including the fallback layout.
bool ValidateFloorMap(const FloorMap *f, char *why, size_t whySize)
{
    Metrics m;
    Measure(f, &m);
    const char *err = NULL;
    if (!m.reservedIntact)            err = "fixed shell or stair alcoves were changed";
    else if (!m.upPadReached)         err = "up pad not reachable from arrival";
    else if (!m.arriveAboveReached)   err = "arrival from above not reachable";
    else if (m.pockets != 0)          err = "walkable pockets cut off from arrival";
    else if (m.pathLength > ROUTE_MAX) err = "route to the stairs is too long";
    if (err && why && whySize) snprintf(why, whySize, "%s", err);
    return err == NULL;
}

// Quality targets. A generated layout that misses them is thrown away and
// rebuilt from the next sub-seed.
static bool MeetsTargets(const FloorMap *f)
{
    return f->pathLength >= ROUTE_MIN && f->walkableFraction >= WALKABLE_MIN && f->walkableFraction <= WALKABLE_MAX;
}

static void BuildAttempt(FloorMap *f, uint32_t seed, bool fallback)
{
    static Ctx c;
    c.f = f;
    RngInit(&c.rng, seed);

    StampShell(f);
    BuildProtectMask(&c);
    PickHalls(&c);

    f->partitions = fallback ? 0 : PartitionPass(&c);
    f->pillars = PillarPass(&c);
    ConnectPockets(&c);

    Metrics m;
    Measure(f, &m);
    f->pathLength = m.pathLength;
    f->walkableFraction = m.walkable;
}

void GenerateFloorMap(FloorMap *outFloor, int floorNum, uint32_t baseSeed)
{
    const uint32_t floorSeed = DungeonFloorSeed(baseSeed, floorNum);
    outFloor->floorNumber = floorNum;
    outFloor->seed = floorSeed;

    for (int attempt = 0; attempt < GEN_MAX_ATTEMPTS; ++attempt) {
        BuildAttempt(outFloor, MixSeed(floorSeed, (uint32_t)attempt), false);
        if (ValidateFloorMap(outFloor, NULL, 0) && MeetsTargets(outFloor)) {
            outFloor->attempts = attempt + 1;
            outFloor->usedFallback = false;
            return;
        }
    }
    // Pillar hall with no partitions. It always passes, so the player is never stuck.
    BuildAttempt(outFloor, floorSeed, true);
    outFloor->attempts = GEN_MAX_ATTEMPTS;
    outFloor->usedFallback = true;
}
