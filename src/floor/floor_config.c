// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floor_run.md, section "floor_config.h and floor_config.c"
// ============================================================================
#include "floor_config.h"

#include <stdio.h>

static FloorRunConfig g_cfg;
static bool g_have = false;

const FloorRunConfig *FloorConfig_Get(void)
{
    if (!g_have) { FloorRun_DefaultConfig(&g_cfg); g_have = true; }
    return &g_cfg;
}

typedef struct { const MdixDoc *doc; char *err; int cap; bool ok; } Reader;

static float F(Reader *r, const char *key, float lo, float hi)
{
    char path[64]; float v = 0.0f;
    snprintf(path, sizeof path, "stair_clock.%s", key);
    if (!MdixDoc_Float(r->doc, path, &v)) { if (r->ok) snprintf(r->err, (size_t)r->cap, "%s: missing, or not a number", path); r->ok = false; return lo; }
    if (!(v >= lo && v <= hi)) { if (r->ok) snprintf(r->err, (size_t)r->cap, "%s: %g is outside %g to %g", path, (double)v, (double)lo, (double)hi); r->ok = false; return lo; }
    return v;
}

static int I(Reader *r, const char *key, int lo, int hi)
{
    char path[64]; int v = 0;
    snprintf(path, sizeof path, "stair_clock.%s", key);
    if (!MdixDoc_Int(r->doc, path, &v)) { if (r->ok) snprintf(r->err, (size_t)r->cap, "%s: missing, or not a whole number", path); r->ok = false; return lo; }
    if (v < lo || v > hi) { if (r->ok) snprintf(r->err, (size_t)r->cap, "%s: %d is outside %d to %d", path, v, lo, hi); r->ok = false; return lo; }
    return v;
}

bool FloorConfig_LoadFrom(const MdixDoc *doc, char *err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    if (!doc) { if (err && errCap > 0) snprintf(err, (size_t)errCap, "no document"); return false; }
    char scratch[160] = { 0 };
    Reader r = { doc, err && errCap > 0 ? err : scratch, err && errCap > 0 ? errCap : (int)sizeof scratch, true };
    FloorRunConfig c;
    c.holdSecs        = F(&r, "console_hold_secs", 1.0f, 60.0f);
    c.drainPerSec     = F(&r, "console_drain_per_sec", 0.0f, 10.0f);
    c.reach           = F(&r, "console_reach", 0.5f, 10.0f);
    c.monstersOnStart = I(&r, "monsters_on_start", 0, 12);
    c.monsterGapSecs  = F(&r, "monster_gap_secs", 0.1f, 30.0f);
    c.countdownSecs   = F(&r, "countdown_secs", 3.0f, 300.0f);
    c.collapseSecs    = F(&r, "collapse_secs", 3.0f, 300.0f);
    c.collapseEase    = I(&r, "collapse_ease", 0, 3);
    c.rageSpeed       = F(&r, "rage_speed", 1.0f, 3.0f);
    if (!r.ok) return false;
    g_cfg = c; g_have = true;
    return true;
}
