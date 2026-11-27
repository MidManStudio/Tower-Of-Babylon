// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/gvex_session.md, section "gvex_world.h and gvex_world.c"
// ============================================================================
#include "gvex_world.h"

#include "../platform/platform.h"

#if TOWER_GVEX

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../enemies/enemy_manager.h"
#include "../enemies/enemy_plan.h"
#include "../enemies/enemy_types.h"
#include "../../third_party/flecs/tob_flecs.h"

#define GVEX_ALERT_SECS     3.0f    // keeps chasing this long after it last saw or heard a player (the cylinder enemies' ENEMY_ALERT_SECS)
#define GVEX_SIGHT_RANGE   14.0f    // the cylinder enemies' aggroRange
#define GVEX_ATTACK_RANGE   1.8f
#define GVEX_ATTACK_HOLD    1.35f   // once it is attacking it keeps at it until the player is this many reaches away (2.43 m), so a player wobbling across the 1.8 m line does not make it flicker between attacking and chasing

// Placeholders, not the dossier's: it names the three grades and gives no numbers. Medium is the single-player cylinder's chase speed for the floor's
// tier; slow and runner scale it. A runner at the top tier (4.5 m/s * 1.25) is quicker than a walking player (4.5) and slower than a sprinting one (7.6).
static const float kGradeSpeed[GVEX_GRADES] = { 0.75f, 1.0f, 1.25f };
float Gvex_GradeSpeedFactor(int grade) { return (grade >= 0 && grade < GVEX_GRADES) ? kGradeSpeed[grade] : 1.0f; }

const char *Gvex_Designation(uint8_t kind) { return kind == GVEX_KIND_GHOUL ? "G.V.E.X.-X0-01" : "?"; }
const char *Gvex_Name(uint8_t kind) { return kind == GVEX_KIND_GHOUL ? "Ghouls" : "?"; }

// ---- components: plain data, no pointers (a creature is a row of these) ----
typedef struct { float x, y, z; } GvPos;
typedef struct { float v; } GvYaw;
typedef struct { float radius, height; } GvBody;
typedef struct { float hp, maxHp; } GvHealth;
typedef struct { uint16_t id; uint8_t kind, variant; } GvIdent;
typedef struct { float sight, alert; } GvSenses;                  // alert: seconds of chasing left
typedef struct { float speed, attackRange; } GvMove;
typedef struct { uint8_t v; } GvState;
typedef struct { float x, z, dist; uint8_t has; } GvTarget;        // who it is after this tick

struct GvexWorld {
    ecs_world_t *w;
    const FloorMap *map;
    ecs_entity_t cPos, cYaw, cBody, cHealth, cIdent, cSenses, cMove, cState, cTarget;
    ecs_query_t *qRecords;
    ecs_entity_t byId[GVEX_MAX_CREATURES];
    GvexTarget targets[GVEX_MAX_TARGETS];
    int nTargets;
};

static ecs_entity_t Define(ecs_world_t *w, const char *name, size_t size, size_t align)
{
    ecs_entity_t e = ecs_entity(w, { .name = name });
    return ecs_component(w, { .entity = e, .type.size = (ecs_size_t)size, .type.alignment = (ecs_size_t)align });
}
#define DEFINE(w, T) Define((w), #T, sizeof(T), _Alignof(T))
#define SET(g, e, comp, T, ...) do { T v_ = __VA_ARGS__; ecs_set_id((g)->w, (e), (g)->comp, sizeof(T), &v_); } while (0)

// ---- the systems (run in the order they are declared) ----

// Who it senses, how long it stays after them, and what it is doing: the cylinder enemies' rules (enemy_manager.c) over every player at once.
static void GhoulSense(ecs_iter_t *it)
{
    const GvexWorld *g = (const GvexWorld *)it->ctx;
    const GvPos *pos = ecs_field(it, GvPos, 0);
    const GvBody *body = ecs_field(it, GvBody, 1);
    GvSenses *sn = ecs_field(it, GvSenses, 2);
    GvState *st = ecs_field(it, GvState, 3);
    GvTarget *tg = ecs_field(it, GvTarget, 4);
    const GvMove *mv = ecs_field(it, GvMove, 5);
    (void)body;
    for (int i = 0; i < it->count; ++i) {
        int nearest = -1, nearestSensed = -1;
        float dNearest = 1e30f, dSensed = 1e30f;
        for (int t = 0; t < g->nTargets; ++t) {
            const GvexTarget *p = &g->targets[t];
            if (!p->present) continue;
            const float dx = p->x - pos[i].x, dz = p->z - pos[i].z, d = sqrtf(dx * dx + dz * dz);
            if (d < dNearest) { dNearest = d; nearest = t; }
            const bool sees = d <= sn[i].sight && EnemyWalk_LineOfSight(g->map, (Vector3){ pos[i].x, pos[i].y, pos[i].z }, (Vector3){ p->x, p->y, p->z });
            const bool hears = d <= p->noise;                                   // noise carries through walls
            if ((sees || hears) && d < dSensed) { dSensed = d; nearestSensed = t; }
        }
        if (nearest < 0) { tg[i].has = 0; sn[i].alert = 0.0f; st[i].v = GVEX_STATE_IDLE; continue; }
        if (nearestSensed >= 0) sn[i].alert = GVEX_ALERT_SECS;
        else if (sn[i].alert > 0.0f) sn[i].alert -= it->delta_time;
        const int pick = nearestSensed >= 0 ? nearestSensed : nearest;
        const float d = nearestSensed >= 0 ? dSensed : dNearest;
        tg[i].has = 1; tg[i].x = g->targets[pick].x; tg[i].z = g->targets[pick].z; tg[i].dist = d;
        const bool attacking = st[i].v == GVEX_STATE_ATTACK;                    // the state it had last tick: a creature that is attacking holds on to it across a wider band than it started in
        if (d <= mv[i].attackRange || (attacking && d <= mv[i].attackRange * GVEX_ATTACK_HOLD)) st[i].v = GVEX_STATE_ATTACK;
        else if (sn[i].alert > 0.0f)     st[i].v = GVEX_STATE_CHASE;
        else                             st[i].v = GVEX_STATE_IDLE;
    }
}

// Walk at its target, a wall at a time so it slides along walls instead of sticking to them; face it while chasing or attacking.
static void GhoulMove(ecs_iter_t *it)
{
    const GvexWorld *g = (const GvexWorld *)it->ctx;
    GvPos *pos = ecs_field(it, GvPos, 0);
    GvYaw *yaw = ecs_field(it, GvYaw, 1);
    const GvBody *body = ecs_field(it, GvBody, 2);
    const GvMove *mv = ecs_field(it, GvMove, 3);
    const GvState *st = ecs_field(it, GvState, 4);
    const GvTarget *tg = ecs_field(it, GvTarget, 5);
    for (int i = 0; i < it->count; ++i) {
        if (!tg[i].has || st[i].v == GVEX_STATE_IDLE) continue;
        float mx = tg[i].x - pos[i].x, mz = tg[i].z - pos[i].z;
        const float ml = sqrtf(mx * mx + mz * mz);
        if (ml < 0.01f) continue;
        mx /= ml; mz /= ml;
        yaw[i].v = atan2f(mx, mz);
        // It walks while chasing, and also while attacking a player who is in the hold band (past its reach, not yet far enough to give up the attack):
        // it closes up to its reach and no further, so it neither stands swinging at the air nor walks into the player.
        const bool closing = st[i].v == GVEX_STATE_ATTACK && tg[i].dist > mv[i].attackRange;
        if (st[i].v != GVEX_STATE_CHASE && !closing) continue;
        float step = mv[i].speed * it->delta_time;
        if (closing) { const float room = tg[i].dist - mv[i].attackRange * 0.98f; if (step > room) step = room; }
        const float nx = pos[i].x + mx * step;
        if (!EnemyWalk_Blocked(g->map, nx, pos[i].z, body[i].radius)) pos[i].x = nx;
        const float nz = pos[i].z + mz * step;
        if (!EnemyWalk_Blocked(g->map, pos[i].x, nz, body[i].radius)) pos[i].z = nz;
    }
}

// ---- the world ----

// A system runs in the pipeline's update phase, in the order the systems are declared.
static ecs_entity_t SystemEntity(ecs_world_t *w, const char *name)
{
    ecs_entity_t e = ecs_entity(w, { .name = name });
    ecs_add_id(w, e, ecs_dependson(EcsOnUpdate));
    return e;
}

static bool SetUp(GvexWorld *g)
{
    ecs_world_t *w = g->w;
    g->cPos = DEFINE(w, GvPos);       g->cYaw = DEFINE(w, GvYaw);       g->cBody = DEFINE(w, GvBody);
    g->cHealth = DEFINE(w, GvHealth); g->cIdent = DEFINE(w, GvIdent);   g->cSenses = DEFINE(w, GvSenses);
    g->cMove = DEFINE(w, GvMove);     g->cState = DEFINE(w, GvState);   g->cTarget = DEFINE(w, GvTarget);

    ecs_system(w, { .entity = SystemEntity(w, "GhoulSense"),
                    .query.expr = "GvPos, GvBody, GvSenses, GvState, GvTarget, GvMove", .callback = GhoulSense, .ctx = g });
    ecs_system(w, { .entity = SystemEntity(w, "GhoulMove"),
                    .query.expr = "GvPos, GvYaw, GvBody, GvMove, GvState, GvTarget", .callback = GhoulMove, .ctx = g });
    g->qRecords = ecs_query(w, { .expr = "GvIdent, GvPos, GvYaw, GvState, GvHealth" });
    return g->qRecords != NULL;
}

static void SpawnGhoul(GvexWorld *g, int index, const EnemySpawn *sp, int tier)
{
    ecs_entity_t e = ecs_new(g->w);
    float height, radius; Color tint;
    EnemyLook(sp->hash, &height, &radius, &tint);
    const int grade = (int)((sp->hash >> 24) % GVEX_GRADES);
    const float base = ENEMY_CHASE_SPEED_BASE + ENEMY_CHASE_SPEED_STEP * (float)(tier % 4);     // the cylinder enemies' speed for this tier
    const float hp = 40.0f + 20.0f * (float)tier;
    SET(g, e, cPos, GvPos, { sp->x, 0.0f, sp->z });
    SET(g, e, cYaw, GvYaw, { 0.0f });
    SET(g, e, cBody, GvBody, { radius, height });
    SET(g, e, cHealth, GvHealth, { hp, hp });
    SET(g, e, cIdent, GvIdent, { (uint16_t)index, (uint8_t)GVEX_KIND_GHOUL, (uint8_t)grade });
    SET(g, e, cSenses, GvSenses, { GVEX_SIGHT_RANGE, 0.0f });
    SET(g, e, cMove, GvMove, { base * Gvex_GradeSpeedFactor(grade), GVEX_ATTACK_RANGE });
    SET(g, e, cState, GvState, { GVEX_STATE_IDLE });
    SET(g, e, cTarget, GvTarget, { 0.0f, 0.0f, 0.0f, 0 });
    g->byId[index] = e;
}

GvexWorld *GvexWorld_Create(const FloorMap *map, int tier)
{
    if (!map) return NULL;
    if (tier < 1) tier = 1;
    GvexWorld *g = (GvexWorld *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->map = map;
    g->w = ecs_init();
    if (!g->w || !SetUp(g)) { GvexWorld_Destroy(g); return NULL; }
    EnemySpawn plan[GVEX_MAX_CREATURES];
    const int n = EnemyPlan_Spawns(map, tier, plan, MAX_FLOOR_ENEMIES);         // the solo game's cap too: 24
    for (int i = 0; i < n; ++i) SpawnGhoul(g, i, &plan[i], tier);
    return g;
}

void GvexWorld_Destroy(GvexWorld *g)
{
    if (!g) return;
    if (g->w) {
        if (g->qRecords) ecs_query_fini(g->qRecords);
        ecs_fini(g->w);
    }
    free(g);
}

void GvexWorld_Step(GvexWorld *g, const GvexTarget *targets, int nTargets, float dt)
{
    if (!g) return;
    g->nTargets = (targets && nTargets > 0) ? (nTargets > GVEX_MAX_TARGETS ? GVEX_MAX_TARGETS : nTargets) : 0;
    if (g->nTargets) memcpy(g->targets, targets, (size_t)g->nTargets * sizeof g->targets[0]);
    ecs_progress(g->w, dt);
}

int GvexWorld_Records(GvexWorld *g, GvexRecord *out, int max)
{
    if (!g || !out || max < 1) return 0;
    int n = 0;
    ecs_iter_t it = ecs_query_iter(g->w, g->qRecords);
    while (ecs_query_next(&it)) {
        const GvIdent *id = ecs_field(&it, GvIdent, 0);
        const GvPos *pos = ecs_field(&it, GvPos, 1);
        const GvYaw *yaw = ecs_field(&it, GvYaw, 2);
        const GvState *st = ecs_field(&it, GvState, 3);
        const GvHealth *hp = ecs_field(&it, GvHealth, 4);
        for (int i = 0; i < it.count && n < max; ++i, ++n) {
            memset(&out[n], 0, sizeof out[n]);                          // the padding too: a caller may compare records byte for byte
            out[n] = (GvexRecord){ id[i].id, id[i].kind, id[i].variant, st[i].v, pos[i].x, pos[i].y, pos[i].z, yaw[i].v, hp[i].hp, hp[i].maxHp };
        }
        if (n >= max) { ecs_iter_fini(&it); break; }
    }
    for (int i = 1; i < n; ++i) {                                  // by id: the order of the tables is not the order of spawning
        const GvexRecord r = out[i]; int j = i - 1;
        while (j >= 0 && out[j].id > r.id) { out[j + 1] = out[j]; --j; }
        out[j + 1] = r;
    }
    return n;
}

int GvexWorld_Count(const GvexWorld *g)
{
    if (!g) return 0;
    int n = 0;
    for (int i = 0; i < GVEX_MAX_CREATURES; ++i) if (g->byId[i] && ecs_is_alive(g->w, g->byId[i])) ++n;
    return n;
}

bool GvexWorld_Damage(GvexWorld *g, int id, float amount)
{
    if (!g || id < 0 || id >= GVEX_MAX_CREATURES || !g->byId[id] || !ecs_is_alive(g->w, g->byId[id])) return false;
    GvHealth *hp = (GvHealth *)ecs_get_mut_id(g->w, g->byId[id], g->cHealth);
    if (!hp) return false;
    hp->hp -= amount;
    if (hp->hp > 0.0f) return false;
    ecs_delete(g->w, g->byId[id]);
    return true;
}

#else
typedef int GvexWorldDisabled;          // an empty translation unit is not allowed in ISO C
const char *Gvex_Designation(uint8_t kind) { (void)kind; return "?"; }
const char *Gvex_Name(uint8_t kind) { (void)kind; return "?"; }
#endif
