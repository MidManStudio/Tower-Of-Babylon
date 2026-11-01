// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "staircase.h and staircase.c"
// ============================================================================
#include "staircase.h"

#include <math.h>

// ---- Alcove interiors in world units ---------------------------------------
#define ALC_X0   ((float)STAIR_WALL_X * TILE_SIZE + TILE_SIZE * 0.5f)    // 73, inside face of the west wall
#define ALC_X1   ((float)(MAP_WIDTH - 1) * TILE_SIZE - TILE_SIZE * 0.5f) // 87, inside face of the east wall
#define UP_Z0    (TILE_SIZE * 0.5f)                                      // 1
#define UP_Z1    ((float)UP_ALCOVE_Z1 * TILE_SIZE - TILE_SIZE * 0.5f)    // 15

// ---- The flight ------------------------------------------------------------
// Two runs of 16 steps joined by a landing, 8 m of rise in all, in a switchback:
//   run A  climbs east  along z 6.5..9.5,  x 76..84, y 0 -> 4
//   landing                 x 84..87, z 6.5..15,     y 4
//   run B  climbs west  along z 12..15,    x 84..76, y 4 -> 8
//   top landing             x 73..76, z 12..15,      y 8, door in the west wall
// Every step is a solid block from the floor up, so the flight is one solid mass.
#define RUN_STEPS    16
#define STEP_TREAD   0.5f
#define RUN_X_LOW    76.0f
#define RUN_X_HIGH   (RUN_X_LOW + (float)RUN_STEPS * STEP_TREAD)   // 84
#define LANE_A_Z0    6.5f
#define LANE_A_Z1    9.5f
#define LANE_B_Z0    12.0f
#define LANE_B_Z1    15.0f
#define RUN_RISE     ((float)RUN_STEPS * STEP_RISE)                // 4
#define RAIL_HALF    0.08f
#define RAIL_HEIGHT  0.95f

// ---- Doors -----------------------------------------------------------------
#define DOOR_W        1.6f
#define DOOR_H        2.2f
#define DOOR_USE_DIST 2.4f

typedef struct {
    float wallX;    // x of the wall face the door sits on
    float nx;       // +1 when the room is on the +x side of the wall, -1 when on the -x side
    float cz;       // z of the door centre
    float baseY;    // height of the door sill
} DoorDef;

static const DoorDef k_doors[DOOR_COUNT] = {
    { ALC_X0, +1.0f, 13.5f, FLOOR_HEIGHT },
    { ALC_X1, -1.0f, 80.0f, 0.0f },
};

// ---- Collision boxes -------------------------------------------------------
enum { BOX_SOLID = 0, BOX_RAIL = 1 };

typedef struct {
    float x0, x1, z0, z1, y0, y1;
    unsigned char kind;
} SBox;

#define MAX_BOXES 48
static SBox g_boxes[MAX_BOXES];
static int  g_count = 0;
static bool g_built = false;

static void AddBox(float x0, float x1, float z0, float z1, float y0, float y1, unsigned char kind)
{
    if (g_count >= MAX_BOXES) return;
    g_boxes[g_count++] = (SBox){ x0, x1, z0, z1, y0, y1, kind };
}

static void Build(void)
{
    if (g_built) return;
    g_built = true;
    g_count = 0;

    for (int j = 0; j < RUN_STEPS; ++j) {                      // run A
        const float x0 = RUN_X_LOW + (float)j * STEP_TREAD;
        AddBox(x0, x0 + STEP_TREAD, LANE_A_Z0, LANE_A_Z1, 0.0f, (float)(j + 1) * STEP_RISE, BOX_SOLID);
    }
    AddBox(RUN_X_HIGH, ALC_X1, LANE_A_Z0, LANE_B_Z1, 0.0f, RUN_RISE, BOX_SOLID);   // landing
    for (int j = 0; j < RUN_STEPS; ++j) {                      // run B
        const float x1 = RUN_X_HIGH - (float)j * STEP_TREAD;
        AddBox(x1 - STEP_TREAD, x1, LANE_B_Z0, LANE_B_Z1, 0.0f, RUN_RISE + (float)(j + 1) * STEP_RISE, BOX_SOLID);
    }
    AddBox(ALC_X0, RUN_X_LOW, LANE_B_Z0, LANE_B_Z1, 0.0f, 2.0f * RUN_RISE, BOX_SOLID);   // top landing

    // Railings. They block, they are never stood on.
    AddBox(RUN_X_LOW, RUN_X_HIGH, LANE_A_Z0 - RAIL_HALF, LANE_A_Z0 + RAIL_HALF, STEP_RISE, RUN_RISE + 1.0f, BOX_RAIL);
    AddBox(RUN_X_LOW, RUN_X_HIGH, LANE_A_Z1 - RAIL_HALF, LANE_A_Z1 + RAIL_HALF, STEP_RISE, RUN_RISE + 1.0f, BOX_RAIL);
    AddBox(RUN_X_HIGH, ALC_X1, LANE_A_Z0 - RAIL_HALF, LANE_A_Z0 + RAIL_HALF, RUN_RISE, RUN_RISE + 1.0f, BOX_RAIL);
    AddBox(RUN_X_HIGH - RAIL_HALF, RUN_X_HIGH + RAIL_HALF, LANE_A_Z1, LANE_B_Z0, RUN_RISE, RUN_RISE + 1.0f, BOX_RAIL);
    AddBox(ALC_X0, RUN_X_HIGH, LANE_B_Z0 - RAIL_HALF, LANE_B_Z0 + RAIL_HALF, RUN_RISE + STEP_RISE, 2.0f * RUN_RISE + 1.0f, BOX_RAIL);
}

float Staircase_GroundHeight(float x, float z, float y)
{
    Build();
    float ground = 0.0f;
    const float reach = y + STEP_UP_MAX + 0.001f;
    for (int i = 0; i < g_count; ++i) {
        const SBox *b = &g_boxes[i];
        if (b->kind != BOX_SOLID) continue;
        if (x < b->x0 || x > b->x1 || z < b->z0 || z > b->z1) continue;
        if (b->y1 <= reach && b->y1 > ground) ground = b->y1;
    }
    return ground;
}

bool Staircase_Blocked(float x, float z, float radius, float y, float height)
{
    Build();
    const float reach = y + STEP_UP_MAX + 0.001f;
    for (int i = 0; i < g_count; ++i) {
        const SBox *b = &g_boxes[i];
        if (x < b->x0 - radius || x > b->x1 + radius || z < b->z0 - radius || z > b->z1 + radius) continue;
        if (b->y1 > reach && b->y0 < y + height - 0.05f) return true;
    }
    return false;
}

// ---- Doors -----------------------------------------------------------------
bool Staircase_DoorInRange(DoorId id, Vector3 p)
{
    const DoorDef *d = &k_doors[id];
    if (fabsf(p.z - d->cz) > DOOR_W * 0.5f + 0.9f) return false;
    if (fabsf(p.x - d->wallX) >= DOOR_USE_DIST) return false;
    return p.y > d->baseY - 0.6f && p.y < d->baseY + 1.2f;
}

bool Staircase_DoorLocked(DoorId id, const FloorMap *floor)
{
    return id == DOOR_DOWN && floor->floorNumber <= 1;
}

void Staircase_SpawnBelow(Vector3 *pos, float *yaw)
{
    const DoorDef *d = &k_doors[DOOR_DOWN];
    *pos = (Vector3){ d->wallX + d->nx * (DOOR_USE_DIST + 0.4f), d->baseY, d->cz };
    *yaw = -PI / 2.0f;     // west, away from the door and into the hall
}

void Staircase_SpawnAbove(Vector3 *pos, float *yaw)
{
    const DoorDef *d = &k_doors[DOOR_UP];
    *pos = (Vector3){ d->wallX + d->nx * (DOOR_USE_DIST + 0.2f), d->baseY, d->cz };
    *yaw = PI / 2.0f;      // east, along the flight that leads down
}

// ---- Drawing ---------------------------------------------------------------
#define SHAFT_TOP (FLOOR_HEIGHT + 3.5f)

static void Slab(float x0, float x1, float y0, float y1, float z0, float z1, Color c)
{
    DrawCube((Vector3){ 0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * (z0 + z1) }, x1 - x0, y1 - y0, z1 - z0, c);
}

static void DrawShaft(void)
{
    // The alcove walls are WALL_HEIGHT tall. Carry them up past the top landing
    // and close the top, so the climb ends in a small dark room.
    const Color wall = (Color){ 52, 52, 60, 255 };
    const float xa = ALC_X0 - TILE_SIZE, xb = ALC_X1 + TILE_SIZE;
    const float za = UP_Z0 - TILE_SIZE, zb = UP_Z1 + TILE_SIZE;
    Slab(xa, xb, WALL_HEIGHT, SHAFT_TOP, za, UP_Z0, wall);          // north
    Slab(xa, xb, WALL_HEIGHT, SHAFT_TOP, UP_Z1, zb, wall);          // south
    Slab(ALC_X1, xb, WALL_HEIGHT, SHAFT_TOP, UP_Z0, UP_Z1, wall);   // east
    Slab(xa, ALC_X0, WALL_HEIGHT, SHAFT_TOP, UP_Z0, UP_Z1, wall);   // west, closes the doorway above head height
    Slab(xa, xb, SHAFT_TOP, SHAFT_TOP + 0.2f, za, zb, (Color){ 20, 20, 25, 255 });   // cap
}

static void DrawFlight(void)
{
    const Color body = (Color){ 50, 50, 58, 255 };
    const Color treadA = (Color){ 98, 92, 94, 255 };
    const Color treadB = (Color){ 80, 76, 80, 255 };

    Build();
    int solid = 0;
    for (int i = 0; i < g_count; ++i) {
        const SBox *b = &g_boxes[i];
        if (b->kind != BOX_SOLID) continue;
        Slab(b->x0, b->x1, 0.0f, b->y1, b->z0, b->z1, body);
        Slab(b->x0, b->x1, b->y1, b->y1 + 0.03f, b->z0, b->z1, (solid & 1) ? treadB : treadA);
        ++solid;
    }
}

static void DrawFlightRail(bool runA, float z)
{
    const Color rail = (Color){ 120, 30, 30, 255 };
    const float xs = runA ? RUN_X_LOW + STEP_TREAD * 0.5f : RUN_X_HIGH - STEP_TREAD * 0.5f;
    const float xe = runA ? RUN_X_HIGH - STEP_TREAD * 0.5f : RUN_X_LOW + STEP_TREAD * 0.5f;
    const float y0 = (runA ? 0.0f : RUN_RISE) + STEP_RISE;
    const float y1 = (runA ? 0.0f : RUN_RISE) + RUN_RISE;

    DrawCylinderEx((Vector3){ xs, y0 + RAIL_HEIGHT, z }, (Vector3){ xe, y1 + RAIL_HEIGHT, z }, 0.04f, 0.04f, 6, rail);
    const int posts[5] = { 0, 4, 8, 12, 15 };
    for (int k = 0; k < 5; ++k) {
        const float t = (float)posts[k] / (float)(RUN_STEPS - 1);
        const float x = xs + (xe - xs) * t;
        const float top = y0 + (y1 - y0) * t;
        DrawCube((Vector3){ x, top + RAIL_HEIGHT * 0.5f, z }, 0.07f, RAIL_HEIGHT, 0.07f, rail);
    }
}

static void DrawLevelRail(float x0, float z0, float x1, float z1, float y)
{
    const Color rail = (Color){ 120, 30, 30, 255 };
    DrawCylinderEx((Vector3){ x0, y + RAIL_HEIGHT, z0 }, (Vector3){ x1, y + RAIL_HEIGHT, z1 }, 0.04f, 0.04f, 6, rail);
    DrawCube((Vector3){ x0, y + RAIL_HEIGHT * 0.5f, z0 }, 0.07f, RAIL_HEIGHT, 0.07f, rail);
    DrawCube((Vector3){ x1, y + RAIL_HEIGHT * 0.5f, z1 }, 0.07f, RAIL_HEIGHT, 0.07f, rail);
}

// Box on the room side of a door's wall, offset from the wall face by off0..off1.
static void DoorBox(const DoorDef *d, float off0, float off1, float zc, float zw, float y0, float y1, Color c)
{
    const float xc = d->wallX + d->nx * 0.5f * (off0 + off1);
    DrawCube((Vector3){ xc, 0.5f * (y0 + y1), zc }, off1 - off0, y1 - y0, zw, c);
}

static Color Mix(Color a, Color b, float t)
{
    return (Color){ (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
                    (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
                    (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t), 255 };
}

static void DrawDoor(DoorId id, float open, bool locked)
{
    const DoorDef *d = &k_doors[id];
    const float y0 = d->baseY, y1 = d->baseY + DOOR_H;
    const Color frame = (Color){ 36, 36, 42, 255 };

    // Doorway: dark when shut, a bright room beyond when open.
    DoorBox(d, 0.0f, 0.10f, d->cz, DOOR_W, y0, y1, Mix((Color){ 14, 14, 18, 255 }, (Color){ 232, 216, 170, 255 }, open));

    DoorBox(d, 0.0f, 0.18f, d->cz - (DOOR_W * 0.5f + 0.1f), 0.2f, y0, y1 + 0.2f, frame);
    DoorBox(d, 0.0f, 0.18f, d->cz + (DOOR_W * 0.5f + 0.1f), 0.2f, y0, y1 + 0.2f, frame);
    DoorBox(d, 0.0f, 0.18f, d->cz, DOOR_W + 0.4f, y1, y1 + 0.2f, frame);

    // The slab slides along the wall toward -z, in front of the frame.
    const float slideZ = d->cz - open * (DOOR_W + 0.15f);
    DoorBox(d, 0.20f, 0.28f, slideZ, DOOR_W, y0, y1, (Color){ 62, 66, 76, 255 });
    DoorBox(d, 0.28f, 0.30f, slideZ, DOOR_W, y0 + 1.0f, y0 + 1.2f, (Color){ 170, 140, 30, 255 });

    // Lamp: red when locked, amber when ready, green once open.
    Color lamp = locked ? (Color){ 210, 30, 30, 255 } : (Color){ 230, 170, 40, 255 };
    if (open > 0.5f) lamp = (Color){ 60, 220, 90, 255 };
    DoorBox(d, 0.02f, 0.12f, d->cz, 0.3f, y1 + 0.3f, y1 + 0.55f, lamp);
}

#define STAIRS_DRAW_RANGE 34.0f

void DrawStairs(const FloorMap *floor, const float doorOpen[DOOR_COUNT], Vector3 viewer)
{
    // The flight, shaft and rails are a few thousand vertices. Skip them when they are far away.
    if (hypotf(viewer.x - 80.0f, viewer.z - 8.0f) < STAIRS_DRAW_RANGE) {
        DrawShaft();
        DrawFlight();

        DrawFlightRail(true, LANE_A_Z0);
        DrawFlightRail(true, LANE_A_Z1);
        DrawFlightRail(false, LANE_B_Z0);
        DrawLevelRail(RUN_X_HIGH, LANE_A_Z0, ALC_X1, LANE_A_Z0, RUN_RISE);
        DrawLevelRail(RUN_X_HIGH, LANE_A_Z1, RUN_X_HIGH, LANE_B_Z0, RUN_RISE);
        DrawLevelRail(ALC_X0, LANE_B_Z0, RUN_X_LOW, LANE_B_Z0, 2.0f * RUN_RISE);
    }

    for (int i = 0; i < DOOR_COUNT; ++i) {
        const DoorDef *d = &k_doors[i];
        if (hypotf(viewer.x - d->wallX, viewer.z - d->cz) > STAIRS_DRAW_RANGE) continue;
        DrawDoor((DoorId)i, doorOpen[i], Staircase_DoorLocked((DoorId)i, floor));
    }
}
