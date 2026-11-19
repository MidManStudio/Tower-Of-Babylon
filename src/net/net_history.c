// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net_shots.md, section "net_history.h and net_history.c"
// ============================================================================
#include "net_history.h"

#include <math.h>
#include <string.h>

void NetHistory_Init(NetHistory *h) { memset(h, 0, sizeof *h); }

static NetPoseFrame *Slot(NetHistory *h, uint32_t tick) { return &h->f[tick % NET_HIST_FRAMES]; }
static const NetPoseFrame *SlotC(const NetHistory *h, uint32_t tick)
{
    const NetPoseFrame *f = &h->f[tick % NET_HIST_FRAMES];
    return (tick != 0 && f->tick == tick) ? f : NULL;
}

void NetHistory_Begin(NetHistory *h, uint32_t tick)
{
    NetPoseFrame *f = Slot(h, tick);
    memset(f, 0, sizeof *f);
    f->tick = tick;
}

void NetHistory_Set(NetHistory *h, uint32_t tick, int id, Vector3 pos, float yaw)
{
    if (id < 0 || id >= NET_MAX_PLAYERS || tick == 0) return;
    NetPoseFrame *f = Slot(h, tick);
    if (f->tick != tick) return;                                // the frame was never begun (or has been replaced)
    f->present[id] = 1; f->pos[id] = pos; f->yaw[id] = yaw;
}

bool NetHistory_PoseAt(const NetHistory *h, uint32_t tick, int id, Vector3 *pos, float *yaw)
{
    if (id < 0 || id >= NET_MAX_PLAYERS) return false;
    const NetPoseFrame *f = SlotC(h, tick);
    if (!f || !f->present[id]) return false;
    if (pos) *pos = f->pos[id];
    if (yaw) *yaw = f->yaw[id];
    return true;
}

static float LerpAng(float a, float b, float u)
{
    float d = fmodf(b - a, 6.28318531f);
    if (d > 3.14159265f) d -= 6.28318531f;
    if (d < -3.14159265f) d += 6.28318531f;
    return a + d * u;
}

bool NetHistory_PoseAtF(const NetHistory *h, double tick, int id, Vector3 *pos, float *yaw)
{
    if (tick < 1.0) return false;
    const uint32_t t0 = (uint32_t)floor(tick);
    const float u = (float)(tick - (double)t0);
    Vector3 a, b; float ya, yb;
    if (!NetHistory_PoseAt(h, t0, id, &a, &ya)) return false;
    if (u < 1e-6f) { if (pos) *pos = a; if (yaw) *yaw = ya; return true; }
    if (!NetHistory_PoseAt(h, t0 + 1, id, &b, &yb)) return false;
    if (pos) *pos = (Vector3){ a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u };
    if (yaw) *yaw = LerpAng(ya, yb, u);
    return true;
}

uint32_t NetHistory_RewindTick(uint32_t nowTick, uint8_t viewLag)
{
    uint32_t lag = viewLag > NET_REWIND_MAX ? NET_REWIND_MAX : viewLag;
    if (lag >= nowTick) return 1;
    return nowTick - lag;
}
