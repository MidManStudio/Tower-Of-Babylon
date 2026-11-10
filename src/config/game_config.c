// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "game_config.h and game_config.c"
// ============================================================================
#include "game_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../flashlight/flashlight.h"
#include "../projectiles/projectile_config.h"
#include "../projectiles/projectile_patterns.h"
#include "../weapons/weapons.h"

extern const MdixBaked g_bakedWeapons, g_bakedProjectiles, g_bakedFlashlight;     // config_baked.c, written by tools/mdix_bake.mjs

// The .mdix files write these enums out by value; if a C enum is ever renumbered this stops the build instead of
// quietly turning every pistol into something else.
typedef char enum_values_match_the_mdix_files[(WEAPON_KIND_GUN == 2 && FIRE_SEMI == 0 && FIRE_AUTO == 1 && FIRE_PUMP == 2
    && AMMO_PISTOL == 1 && AMMO_SHELLS == 2 && AMMO_RIFLE == 3 && SHOT_CONE == 0 && SHOT_FAN == 1
    && MOVE_STRAIGHT == 0 && MOVE_GUIDED == 2 && MOVE_TELEPORT == 3 && MOVE_WAVE == 4 && MOVE_CIRCULAR == 5
    && PIERCE_NONE == 0 && PIERCE_PIERCER == 1 && PIERCE_RANDOM == 2) ? 1 : -1];

#define MAX_PELLETS 32

// ---- reading ------------------------------------------------------------------------------------------------------
typedef struct { const MdixDoc *d; char err[160]; } Rd;

static void Fail(Rd *r, const char *path, const char *why)
{
    if (!r->err[0]) snprintf(r->err, sizeof r->err, "%s: %s", path, why);
}

static const char *Path(char *buf, int cap, const char *arr, int i, const char *key)
{
    snprintf(buf, (size_t)cap, "%s[%d].%s", arr, i, key);
    return buf;
}

static int RInt(Rd *r, const char *arr, int i, const char *key, int lo, int hi)
{
    char p[64]; int v = 0;
    Path(p, sizeof p, arr, i, key);
    if (!MdixDoc_Int(r->d, p, &v)) { Fail(r, p, "missing or not an int"); return lo; }
    if (v < lo || v > hi) { char w[48]; snprintf(w, sizeof w, "must be %d to %d", lo, hi); Fail(r, p, w); return lo; }
    return v;
}

static float RFloat(Rd *r, const char *arr, int i, const char *key, float lo, float hi)
{
    char p[64]; float v = 0.0f;
    Path(p, sizeof p, arr, i, key);
    if (!MdixDoc_Float(r->d, p, &v)) { Fail(r, p, "missing or not a number"); return lo; }
    if (!(v >= lo && v <= hi)) { char w[48]; snprintf(w, sizeof w, "must be %g to %g", (double)lo, (double)hi); Fail(r, p, w); return lo; }
    return v;
}

static void RStr(Rd *r, const char *arr, int i, const char *key, char *out, int cap)
{
    char p[64];
    Path(p, sizeof p, arr, i, key);
    out[0] = 0;
    if (!MdixDoc_Str(r->d, p, out, cap) || out[0] == 0) Fail(r, p, "missing, empty or too long");
}

// The same, for a value in a single table (flashlight.range).
static float KFloat(Rd *r, const char *obj, const char *key, float lo, float hi)
{
    char p[64]; float v = 0.0f;
    snprintf(p, sizeof p, "%s.%s", obj, key);
    if (!MdixDoc_Float(r->d, p, &v)) { Fail(r, p, "missing or not a number"); return lo; }
    if (!(v >= lo && v <= hi)) { char w[48]; snprintf(w, sizeof w, "must be %g to %g", (double)lo, (double)hi); Fail(r, p, w); return lo; }
    return v;
}

static int KInt(Rd *r, const char *obj, const char *key, int lo, int hi)
{
    char p[64]; int v = 0;
    snprintf(p, sizeof p, "%s.%s", obj, key);
    if (!MdixDoc_Int(r->d, p, &v)) { Fail(r, p, "missing or not an int"); return lo; }
    if (v < lo || v > hi) { char w[48]; snprintf(w, sizeof w, "must be %d to %d", lo, hi); Fail(r, p, w); return lo; }
    return v;
}

// ---- building the tables ---------------------------------------------------------------------------------------------
static bool BuildProjectiles(const MdixDoc *d, ProjectileConfig *out, int *count, char *err, int cap)
{
    Rd r = { d, "" };
    const int n = MdixDoc_ArrayLen(d, "projectiles");
    if (n < 1 || n > MAX_PROJECTILE_CONFIGS) { snprintf(err, (size_t)cap, "projectiles: %d configs, expected 1 to %d", n, MAX_PROJECTILE_CONFIGS); return false; }
    for (int i = 0; i < n; ++i) {
        ProjectileConfig *c = &out[i];
        memset(c, 0, sizeof *c);
        RStr(&r, "projectiles", i, "id", c->id, (int)sizeof c->id);
        c->motion       = RInt(&r, "projectiles", i, "motion", 0, 5);
        c->speedMin     = RFloat(&r, "projectiles", i, "speed_min", 0.1f, 1000.0f);
        c->speedMax     = RFloat(&r, "projectiles", i, "speed_max", c->speedMin, 1000.0f);
        c->lifetime     = RFloat(&r, "projectiles", i, "lifetime", 0.05f, 60.0f);
        c->maxRange     = RFloat(&r, "projectiles", i, "max_range", 0.1f, 1000.0f);
        c->gravity      = RFloat(&r, "projectiles", i, "gravity", 0.0f, 10.0f);
        c->radius       = RFloat(&r, "projectiles", i, "radius", 0.0f, 5.0f);
        c->falloffStart = RFloat(&r, "projectiles", i, "falloff_start", 0.0f, 1000.0f);
        c->falloffEnd   = RFloat(&r, "projectiles", i, "falloff_end", c->falloffStart, 1000.0f);
        c->falloffMin   = RFloat(&r, "projectiles", i, "falloff_min", 0.0f, 1.0f);
        c->piercing     = RInt(&r, "projectiles", i, "piercing", 0, 2);
        c->maxHits      = RInt(&r, "projectiles", i, "max_hits", 1, 16);
        c->trailLength  = RFloat(&r, "projectiles", i, "trail_length", 0.0f, 20.0f);
        c->trailWidth   = RFloat(&r, "projectiles", i, "trail_width", 0.001f, 2.0f);
        c->colorCore    = (unsigned)RInt(&r, "projectiles", i, "color_core", 0, 0xFFFFFF);
        c->colorGlow    = (unsigned)RInt(&r, "projectiles", i, "color_glow", 0, 0xFFFFFF);
        for (int k = 0; k < i; ++k) if (strcmp(out[k].id, c->id) == 0) Fail(&r, "projectiles", "an id is used twice");
        if (r.err[0]) { snprintf(err, (size_t)cap, "%s", r.err); return false; }
    }
    *count = n;
    return true;
}

static bool BuildWeapons(const MdixDoc *d, const ProjectileConfig *proj, int projCount, WeaponDef *out, char *err, int cap)
{
    Rd r = { d, "" };
    const int n = MdixDoc_ArrayLen(d, "weapons");
    if (n != WEAPON_COUNT) { snprintf(err, (size_t)cap, "weapons: %d rows, the game has %d weapons", n, (int)WEAPON_COUNT); return false; }
    bool seen[WEAPON_COUNT] = { false };
    memset(out, 0, sizeof(WeaponDef) * WEAPON_COUNT);

    for (int i = 0; i < n; ++i) {
        char id[24];
        RStr(&r, "weapons", i, "id", id, (int)sizeof id);
        int w = -1;
        for (int k = 0; k < WEAPON_COUNT; ++k) if (strcmp(id, Weapon_IdString((WeaponId)k)) == 0) w = k;
        if (w < 0) { char p[64]; Fail(&r, Path(p, sizeof p, "weapons", i, "id"), "not a weapon the game knows"); snprintf(err, (size_t)cap, "%s", r.err); return false; }
        if (seen[w]) { Fail(&r, "weapons", "a weapon is listed twice"); snprintf(err, (size_t)cap, "%s", r.err); return false; }
        seen[w] = true;

        WeaponDef *x = &out[w];
        x->id = (WeaponId)w;
        RStr(&r, "weapons", i, "name", x->name, (int)sizeof x->name);
        x->kind     = (WeaponKind)RInt(&r, "weapons", i, "kind", 0, 2);
        x->damage   = RFloat(&r, "weapons", i, "damage", 0.0f, 10000.0f);
        x->cooldown = RFloat(&r, "weapons", i, "cooldown", 0.01f, 60.0f);
        x->duration = RFloat(&r, "weapons", i, "duration", 0.01f, 60.0f);
        if (x->kind == WEAPON_KIND_GUN) {
            x->fireMode    = (FireMode)RInt(&r, "weapons", i, "fire_mode", 0, 2);
            x->ammo        = (AmmoType)RInt(&r, "weapons", i, "ammo", 1, AMMO_TYPE_COUNT - 1);
            x->ammoPerShot = RInt(&r, "weapons", i, "ammo_per_shot", 1, 50);
            char pj[24];
            RStr(&r, "weapons", i, "projectile", pj, (int)sizeof pj);
            x->projectile = -1;
            for (int k = 0; k < projCount; ++k) if (strcmp(proj[k].id, pj) == 0) x->projectile = k;
            if (x->projectile < 0 && !r.err[0]) { char p[64]; Fail(&r, Path(p, sizeof p, "weapons", i, "projectile"), "no projectile config has this id"); }
            x->pellets      = RInt(&r, "weapons", i, "pellets", 1, MAX_PELLETS);
            x->pattern      = RInt(&r, "weapons", i, "pattern", 0, 1);
            x->spreadDeg    = RFloat(&r, "weapons", i, "spread_deg", 0.0f, 45.0f);
            x->spreadMaxDeg = RFloat(&r, "weapons", i, "spread_max_deg", x->spreadDeg, 45.0f);
            x->heatPerShot  = RFloat(&r, "weapons", i, "heat_per_shot", 0.0f, 1.0f);
            x->heatRecover  = RFloat(&r, "weapons", i, "heat_recover", 0.0f, 100.0f);
            x->kick         = RFloat(&r, "weapons", i, "kick", 0.0f, 10.0f);
            x->noiseMetres  = RFloat(&r, "weapons", i, "noise_metres", 0.0f, 500.0f);
            x->noiseSecs    = RFloat(&r, "weapons", i, "noise_secs", 0.0f, 60.0f);
            x->pickupAmmo   = RInt(&r, "weapons", i, "pickup_ammo", 0, AMMO_MAX);
            x->ammoBox      = RInt(&r, "weapons", i, "ammo_box", 1, AMMO_MAX);
            x->magSize        = RInt(&r, "weapons", i, "mag_size", 0, AMMO_MAX);
            x->reloadSecs     = RFloat(&r, "weapons", i, "reload_secs", 0.05f, 60.0f);
            x->reloadPerRound = RInt(&r, "weapons", i, "reload_per_round", 0, 1);
        } else {
            x->durability = RInt(&r, "weapons", i, "durability", 0, 100000);
            x->windup    = RFloat(&r, "weapons", i, "windup", 0.0f, 60.0f);
            x->reach     = RFloat(&r, "weapons", i, "reach", 0.1f, 50.0f);
            x->arcDeg    = RFloat(&r, "weapons", i, "arc_deg", 0.0f, 180.0f);
            x->knockback = RFloat(&r, "weapons", i, "knockback", 0.0f, 50.0f);
            x->projectile = -1;
        }
        if (r.err[0]) { snprintf(err, (size_t)cap, "%s", r.err); return false; }
    }
    for (int k = 0; k < WEAPON_COUNT; ++k)
        if (!seen[k]) { snprintf(err, (size_t)cap, "weapons: no row for \"%s\"", Weapon_IdString((WeaponId)k)); return false; }
    return true;
}

static bool BuildFlashlight(const MdixDoc *d, FlashlightConfig *out, char *err, int cap)
{
    Rd r = { d, "" };
    memset(out, 0, sizeof *out);
    out->capacitySecs = KFloat(&r, "flashlight", "capacity_secs", 1.0f, 36000.0f);
    out->startSecs    = KFloat(&r, "flashlight", "start_secs", 0.0f, out->capacitySecs);
    out->batterySecs  = KFloat(&r, "flashlight", "battery_secs", 1.0f, out->capacitySecs);
    out->lowFraction  = KFloat(&r, "flashlight", "low_fraction", 0.0f, 1.0f);
    out->beamInnerDeg = KFloat(&r, "flashlight", "beam_inner_deg", 0.5f, 79.0f);
    out->beamOuterDeg = KFloat(&r, "flashlight", "beam_outer_deg", out->beamInnerDeg + 0.5f, 80.0f);
    out->range        = KFloat(&r, "flashlight", "range", 0.5f, 100.0f);
    out->strength     = KFloat(&r, "flashlight", "strength", 0.0f, 4.0f);
    out->lag          = KFloat(&r, "flashlight", "lag", 0.1f, 200.0f);
    out->tint         = (unsigned)KInt(&r, "flashlight", "tint", 0, 0xFFFFFF);
    if (r.err[0]) { snprintf(err, (size_t)cap, "%s", r.err); return false; }
    return true;
}

bool GameConfig_LoadFrom(const MdixDoc *weapons, const MdixDoc *projectiles, const MdixDoc *flashlight, char *err, int errCap)
{
    char local[160];
    if (!err) { err = local; errCap = (int)sizeof local; }
    err[0] = 0;
    ProjectileConfig pc[MAX_PROJECTILE_CONFIGS];
    int pn = 0;
    WeaponDef wd[WEAPON_COUNT];
    FlashlightConfig fc;
    if (!weapons || !projectiles || !flashlight) { snprintf(err, (size_t)errCap, "no document"); return false; }
    if (!BuildProjectiles(projectiles, pc, &pn, err, errCap)) return false;
    if (!BuildWeapons(weapons, pc, pn, wd, err, errCap)) return false;
    if (!BuildFlashlight(flashlight, &fc, err, errCap)) return false;
    ProjectileConfig_SetAll(pc, pn);       // nothing is installed unless all three built
    Weapons_SetDefs(wd);
    Flashlight_SetConfig(&fc);
    return true;
}

// ---- where the numbers come from ----------------------------------------------------------------------------------
static ConfigStatus g_status;
static bool g_waiting = false;

const ConfigStatus *GameConfig_Status(void)
{
    g_status.wasm = Mdix_WasmState();
    return &g_status;
}

static void Note(const char *what, const char *detail)
{
    snprintf(g_status.note, sizeof g_status.note, "%s%s%s", what, detail[0] ? ": " : "", detail);
}

// ---- extra files (see game_config.h) ------------------------------------------------------------------------------
typedef struct { const char *path; const MdixBaked *baked; ConfigBuildFn build; } ExtraFile;
static ExtraFile g_extra[GAME_CONFIG_MAX_EXTRAS];
static int  g_extraCount = 0;
static bool g_baseLoaded = false;          // GameConfig_LoadBaked has run, so a later registration installs its baked copy at once

static void LoadExtraBaked(const ExtraFile *x)
{
    char err[160];
    MdixDoc *d = MdixDoc_FromBaked(x->baked);
    if (!d || !x->build(d, err, (int)sizeof err)) Note("baked config is invalid, run tools/mdix_bake.mjs", d ? err : "no document");
    MdixDoc_Free(d);
}

bool GameConfig_RegisterExtra(const char *path, const MdixBaked *baked, ConfigBuildFn build)
{
    if (!path || !baked || !build || g_extraCount >= GAME_CONFIG_MAX_EXTRAS) return false;
    g_extra[g_extraCount] = (ExtraFile){ path, baked, build };
    if (g_baseLoaded) LoadExtraBaked(&g_extra[g_extraCount]);
    ++g_extraCount;
    return true;
}

void GameConfig_LoadBaked(void)
{
    MdixDoc *w = MdixDoc_FromBaked(&g_bakedWeapons), *p = MdixDoc_FromBaked(&g_bakedProjectiles), *fl = MdixDoc_FromBaked(&g_bakedFlashlight);
    char err[160];
    g_status.source = CONFIG_FROM_BAKED;
    g_status.note[0] = 0;
    if (!GameConfig_LoadFrom(w, p, fl, err, (int)sizeof err)) Note("baked config is invalid, run tools/mdix_bake.mjs", err);
    MdixDoc_Free(w);
    MdixDoc_Free(p);
    MdixDoc_Free(fl);
    for (int i = 0; i < g_extraCount; ++i) LoadExtraBaked(&g_extra[i]);
    g_baseLoaded = true;
}

void GameConfig_Init(void)
{
    GameConfig_LoadBaked();
    Mdix_WasmStart();
    g_waiting = Mdix_WasmState() == MDIX_WASM_LOADING;
    if (!g_waiting && Mdix_WasmState() == MDIX_WASM_FAILED) Note("wasm not used", Mdix_WasmError());
}

static char *ReadText(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n >= 0 ? (char *)malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) { buf[n] = 0; *len = (int)n; }
    return buf;
}

// Reads the two .mdix files through the wasm and installs them. Everything is freed on every path.
static void LoadFiles(void)
{
    int wl = 0, pl = 0, fl = 0;
    char *wt = ReadText("assets/config/weapons.mdix", &wl);
    char *pt = ReadText("assets/config/projectiles.mdix", &pl);
    char *ft = ReadText("assets/config/flashlight.mdix", &fl);
    MdixDoc *wd = NULL, *pd = NULL, *fd = NULL;
    char err[160] = "";
    if (!wt || !pt || !ft) {
        Note("config files not read, baked values kept", "assets/config/ is missing from the build");
    } else if (!(wd = MdixDoc_Parse(wt, err, (int)sizeof err))) {
        Note("weapons.mdix, baked values kept", err);
    } else if (!(pd = MdixDoc_Parse(pt, err, (int)sizeof err))) {
        Note("projectiles.mdix, baked values kept", err);
    } else if (!(fd = MdixDoc_Parse(ft, err, (int)sizeof err))) {
        Note("flashlight.mdix, baked values kept", err);
    } else if (!GameConfig_LoadFrom(wd, pd, fd, err, (int)sizeof err)) {
        Note("config files invalid, baked values kept", err);
    } else {
        g_status.source = CONFIG_FROM_FILES;
        g_status.note[0] = 0;
        g_status.bakedStale = Mdix_Hash(wt, wl) != g_bakedWeapons.sourceHash || Mdix_Hash(pt, pl) != g_bakedProjectiles.sourceHash
                           || Mdix_Hash(ft, fl) != g_bakedFlashlight.sourceHash;
    }
    MdixDoc_Free(wd);
    MdixDoc_Free(pd);
    MdixDoc_Free(fd);
    free(wt);
    free(pt);
    free(ft);

    // The extra files, each on its own: one that fails keeps its baked values and says so; the rest carry on.
    for (int i = 0; i < g_extraCount; ++i) {
        const ExtraFile *x = &g_extra[i];
        int xl = 0;
        char *xt = ReadText(x->path, &xl);
        char xerr[160] = "";
        MdixDoc *xd = xt ? MdixDoc_Parse(xt, xerr, (int)sizeof xerr) : NULL;
        if (!xt) Note("config file not read, baked values kept", x->path);
        else if (!xd) Note(x->path, xerr);
        else if (!x->build(xd, xerr, (int)sizeof xerr)) Note(x->path, xerr);
        else if (Mdix_Hash(xt, xl) != x->baked->sourceHash) g_status.bakedStale = true;
        MdixDoc_Free(xd);
        free(xt);
    }
}

void GameConfig_Poll(void)
{
    if (!g_waiting) return;
    const MdixWasmState s = Mdix_WasmState();
    if (s == MDIX_WASM_LOADING) return;
    g_waiting = false;
    if (s == MDIX_WASM_READY) LoadFiles();
    else Note("wasm not used", Mdix_WasmError());
}
