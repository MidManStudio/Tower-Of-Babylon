// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/swarm.md, section "swarm_config.h and swarm_config.c"
// ============================================================================
#include "swarm_config.h"

#include <stdio.h>

static SwarmConfig g_cfg;
static bool g_have = false;

void SwarmConfig_Defaults(SwarmConfig *c)
{
    c->worms = 24; c->dotsPerWorm = 10; c->iterations = 3;
    c->firstFloor = 1; c->everyNFloors = 1; c->showCore = 1;
    c->dotRadius = 0.09f; c->linkLength = 0.16f; c->crawlSpeed = 3.0f; c->speedSpread = 0.25f;
    c->weave = 0.35f; c->gravity = 14.0f; c->damping = 3.0f; c->squirm = 0.6f;
    c->senseRange = 30.0f; c->spawnMinDist = 26.0f;
    c->coreHealth = 150.0f; c->coreHalf = 0.45f; c->deathSecs = 4.0f;
    c->biteDamage = 1.5f; c->biteWindup = 0.3f; c->biteInterval = 0.5f; c->biteCap = 18.0f; c->biteReach = 0.3f;
}

const SwarmConfig *SwarmConfig_Get(void)
{
    if (!g_have) { SwarmConfig_Defaults(&g_cfg); g_have = true; }
    return &g_cfg;
}

typedef struct { const MdixDoc *doc; char *err; int cap; bool ok; } Reader;

static float F(Reader *r, const char *key, float lo, float hi)
{
    char path[64]; float v = 0.0f;
    snprintf(path, sizeof path, "swarm.%s", key);
    if (!MdixDoc_Float(r->doc, path, &v)) { if (r->ok) snprintf(r->err, (size_t)r->cap, "%s: missing, or not a number", path); r->ok = false; return lo; }
    if (!(v >= lo && v <= hi)) { if (r->ok) snprintf(r->err, (size_t)r->cap, "%s: %g is outside %g to %g", path, (double)v, (double)lo, (double)hi); r->ok = false; return lo; }
    return v;
}

static int I(Reader *r, const char *key, int lo, int hi)
{
    char path[64]; int v = 0;
    snprintf(path, sizeof path, "swarm.%s", key);
    if (!MdixDoc_Int(r->doc, path, &v)) { if (r->ok) snprintf(r->err, (size_t)r->cap, "%s: missing, or not a whole number", path); r->ok = false; return lo; }
    if (v < lo || v > hi) { if (r->ok) snprintf(r->err, (size_t)r->cap, "%s: %d is outside %d to %d", path, v, lo, hi); r->ok = false; return lo; }
    return v;
}

bool SwarmConfig_LoadFrom(const MdixDoc *doc, char *err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    if (!doc) { if (err && errCap > 0) snprintf(err, (size_t)errCap, "no document"); return false; }
    char scratch[160] = { 0 };
    Reader r = { doc, err && errCap > 0 ? err : scratch, err && errCap > 0 ? errCap : (int)sizeof scratch, true };
    SwarmConfig c;
    c.worms        = I(&r, "worms", 1, SWARM_MAX_WORMS);
    c.dotsPerWorm  = I(&r, "dots_per_worm", 3, 16);
    c.iterations   = I(&r, "iterations", 1, 8);
    c.firstFloor   = I(&r, "first_floor", 1, 1000);
    c.everyNFloors = I(&r, "every_n_floors", 1, 100);
    c.showCore     = I(&r, "show_core", 0, 1);
    c.dotRadius    = F(&r, "dot_radius", 0.03f, 0.3f);
    c.linkLength   = F(&r, "link_length", 0.03f, 0.5f);
    c.crawlSpeed   = F(&r, "crawl_speed", 0.2f, 8.0f);
    c.speedSpread  = F(&r, "speed_spread", 0.0f, 0.9f);
    c.weave        = F(&r, "weave", 0.0f, 1.2f);
    c.gravity      = F(&r, "gravity", 1.0f, 40.0f);
    c.damping      = F(&r, "damping", 0.0f, 20.0f);
    c.squirm       = F(&r, "squirm", 0.0f, 4.0f);
    c.senseRange   = F(&r, "sense_range", 4.0f, 120.0f);
    c.spawnMinDist = F(&r, "spawn_min_dist", 0.0f, 80.0f);
    c.coreHealth   = F(&r, "core_health", 1.0f, 10000.0f);
    c.coreHalf     = F(&r, "core_half", 0.2f, 1.5f);
    c.deathSecs    = F(&r, "death_secs", 0.5f, 60.0f);
    c.biteDamage   = F(&r, "bite_damage", 0.0f, 50.0f);
    c.biteWindup   = F(&r, "bite_windup", 0.0f, 5.0f);
    c.biteInterval = F(&r, "bite_interval", 0.1f, 10.0f);
    c.biteCap      = F(&r, "bite_cap", 0.0f, 100.0f);
    c.biteReach    = F(&r, "bite_reach", 0.0f, 2.0f);
    if (!r.ok) return false;
    if (c.worms * c.dotsPerWorm > SWARM_MAX_DOTS) {
        if (err && errCap > 0) snprintf(err, (size_t)errCap, "swarm.worms x swarm.dots_per_worm = %d: at most %d dots in all", c.worms * c.dotsPerWorm, SWARM_MAX_DOTS);
        return false;
    }
    g_cfg = c; g_have = true;
    return true;
}
