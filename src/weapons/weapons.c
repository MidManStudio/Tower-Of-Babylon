// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "weapons.h and weapons.c"
// ============================================================================
#include "weapons.h"

#include <math.h>

static WeaponDef g_defs[WEAPON_COUNT];

static const char *k_ids[WEAPON_COUNT] = { "hands", "pipe", "pistol", "shotgun", "rifle" };

void Weapons_SetDefs(const WeaponDef defs[WEAPON_COUNT])
{
    for (int i = 0; i < WEAPON_COUNT; ++i) g_defs[i] = defs[i];
}

const WeaponDef *Weapon_Def(WeaponId id)
{
    return &g_defs[(id >= 0 && id < WEAPON_COUNT) ? id : WEAPON_HANDS];
}

const char *Weapon_IdString(WeaponId id) { return (id >= 0 && id < WEAPON_COUNT) ? k_ids[id] : ""; }

const char *Ammo_Name(AmmoType t)
{
    switch (t) {
        case AMMO_PISTOL: return "PISTOL AMMO";
        case AMMO_SHELLS: return "SHELLS";
        case AMMO_RIFLE:  return "RIFLE AMMO";
        default:          return "";
    }
}

int Ammo_BoxAmount(AmmoType t)
{
    for (int i = 0; i < WEAPON_COUNT; ++i)
        if (g_defs[i].kind == WEAPON_KIND_GUN && g_defs[i].ammo == t) return g_defs[i].ammoBox;
    return 0;
}

AimFrame Aim_FromCamera(const Camera3D *cam)
{
    AimFrame a;
    a.eye = cam->position;
    float fx = cam->target.x - cam->position.x, fy = cam->target.y - cam->position.y, fz = cam->target.z - cam->position.z;
    const float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl > 1e-6f) { fx /= fl; fy /= fl; fz /= fl; } else { fx = 0.0f; fy = 0.0f; fz = 1.0f; }
    a.forward = (Vector3){ fx, fy, fz };
    // right = forward x up, with world up; falls back when looking straight up or down
    float rx = fy * 0.0f - fz * 1.0f, ry = fz * 0.0f - fx * 0.0f, rz = fx * 1.0f - fy * 0.0f;   // (fx,fy,fz) x (0,1,0)
    float rl = sqrtf(rx * rx + ry * ry + rz * rz);
    if (rl < 1e-4f) { rx = 1.0f; ry = 0.0f; rz = 0.0f; rl = 1.0f; }
    a.right = (Vector3){ rx / rl, ry / rl, rz / rl };
    // up = right x forward
    a.up = (Vector3){ a.right.y * fz - a.right.z * fy, a.right.z * fx - a.right.x * fz, a.right.x * fy - a.right.y * fx };
    return a;
}

// Metres ahead of, to the right of and below the eye, matching where the barrel ends in viewmodel.c.
static const float k_muzzle[WEAPON_COUNT][3] = {
    { 0.55f, 0.17f, 0.11f },    // hands (unused)
    { 0.55f, 0.17f, 0.11f },    // pipe (unused)
    { 0.55f, 0.17f, 0.11f },    // pistol
    { 1.00f, 0.15f, 0.08f },    // shotgun
    { 1.10f, 0.15f, 0.08f },    // rifle
};

Vector3 Aim_MuzzleFor(const AimFrame *aim, WeaponId id)
{
    const float *m = k_muzzle[(id >= 0 && id < WEAPON_COUNT) ? id : WEAPON_PISTOL];
    return (Vector3){ aim->eye.x + aim->forward.x * m[0] + aim->right.x * m[1] - aim->up.x * m[2],
                      aim->eye.y + aim->forward.y * m[0] + aim->right.y * m[1] - aim->up.y * m[2],
                      aim->eye.z + aim->forward.z * m[0] + aim->right.z * m[1] - aim->up.z * m[2] };
}

Vector3 Aim_Muzzle(const AimFrame *aim) { return Aim_MuzzleFor(aim, WEAPON_PISTOL); }

void Arsenal_Init(Arsenal *a)
{
    *a = (Arsenal){ 0 };
    a->owned[WEAPON_HANDS] = true;
    a->current = WEAPON_HANDS;
    a->attackT = -1.0f;
    a->equipT = 1.0f;
}

static void Equip(Arsenal *a, WeaponId id)
{
    a->current = id;
    a->equipT = 0.0f;
    a->attackT = -1.0f;
    a->cooldown = 0.2f;
    a->heat = 0.0f;
    a->pressBuf = 0.0f;
    a->reloading = false;                 // switching away (or putting the gun down) stops a reload; the rounds already in stay in
    a->reloadT = 0.0f;
    a->reloadPending = false;
}

bool Arsenal_IsEquipped(const Arsenal *a, WeaponId id) { return id >= 0 && id < WEAPON_COUNT && a->owned[id]; }

int Arsenal_EquippedCount(const Arsenal *a)
{
    int n = 0;
    for (int i = WEAPON_HANDS + 1; i < WEAPON_COUNT; ++i) n += a->owned[i] ? 1 : 0;
    return n;
}

bool Arsenal_Give(Arsenal *a, WeaponId id)
{
    if (id <= WEAPON_HANDS || id >= WEAPON_COUNT || a->owned[id]) return false;
    if (Arsenal_EquippedCount(a) >= ARSENAL_MAX_EQUIPPED) return false;
    a->owned[id] = true;
    Equip(a, id);
    return true;
}

bool Arsenal_Replace(Arsenal *a, WeaponId in, WeaponId out)
{
    if (in <= WEAPON_HANDS || in >= WEAPON_COUNT || a->owned[in]) return false;
    if (out <= WEAPON_HANDS || out >= WEAPON_COUNT || !a->owned[out]) return false;
    a->owned[out] = false;
    a->owned[in] = true;
    Equip(a, in);
    return true;
}

bool Arsenal_Equip(Arsenal *a, WeaponId id)
{
    if (id < 0 || id >= WEAPON_COUNT || !a->owned[id] || a->current == id) return false;
    Equip(a, id);
    return true;
}

bool Arsenal_Remove(Arsenal *a, WeaponId id)
{
    if (id <= WEAPON_HANDS || id >= WEAPON_COUNT || !a->owned[id]) return false;
    a->owned[id] = false;
    if (a->current == id) Equip(a, WEAPON_HANDS);
    return true;
}

void Arsenal_AddAmmo(Arsenal *a, int n)
{
    a->ammo += n;
    if (a->ammo > AMMO_MAX) a->ammo = AMMO_MAX;
}

int Arsenal_OwnedCount(const Arsenal *a)
{
    int n = 0;
    for (int i = 0; i < WEAPON_COUNT; ++i) n += a->owned[i] ? 1 : 0;
    return n;
}

void Arsenal_Cycle(Arsenal *a)
{
    for (int step = 1; step < WEAPON_COUNT; ++step) {
        const WeaponId next = (WeaponId)(((int)a->current + step) % WEAPON_COUNT);
        if (a->owned[next]) { Equip(a, next); return; }
    }
}

float Arsenal_SpreadDeg(const Arsenal *a)
{
    const WeaponDef *d = Weapon_Def(a->current);
    return d->spreadDeg + (d->spreadMaxDeg - d->spreadDeg) * a->heat;
}

// ---- magazines and reloading -------------------------------------------------------------------------------------------
static bool HasMagazine(const WeaponDef *d) { return d->kind == WEAPON_KIND_GUN && d->magSize > 0; }

bool Arsenal_CanReload(const Arsenal *a)
{
    const WeaponDef *d = Weapon_Def(a->current);
    return HasMagazine(d) && !a->reloading && a->equipT >= 0.6f && a->mag[a->current] < d->magSize && a->ammo > 0;
}

bool Arsenal_StartReload(Arsenal *a)
{
    if (!Arsenal_CanReload(a)) return false;
    a->reloading = true;
    a->reloadT = 0.0f;
    a->reloadPending = true;
    return true;
}

int Arsenal_Loaded(const Arsenal *a)
{
    return HasMagazine(Weapon_Def(a->current)) ? a->mag[a->current] : a->ammo;
}

float Arsenal_ReloadFraction(const Arsenal *a)
{
    if (!a->reloading) return 0.0f;
    const float s = Weapon_Def(a->current)->reloadSecs;
    if (s <= 0.0f) return 1.0f;
    const float f = a->reloadT / s;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

int Arsenal_Fill(Arsenal *a, WeaponId id, int rounds)
{
    if (id < 0 || id >= WEAPON_COUNT || rounds <= 0 || !HasMagazine(Weapon_Def(id))) return 0;
    const int room = Weapon_Def(id)->magSize - a->mag[id];
    const int put = room < rounds ? room : rounds;
    if (put <= 0) return 0;
    a->mag[id] += put;
    return put;
}

int Arsenal_TakeMag(Arsenal *a, WeaponId id)
{
    if (id < 0 || id >= WEAPON_COUNT) return 0;
    const int n = a->mag[id];
    a->mag[id] = 0;
    if (a->reloading && a->current == id) { a->reloading = false; a->reloadT = 0.0f; a->reloadPending = false; }
    return n;
}

// ---- melee durability --------------------------------------------------------------------------------------------------
bool Arsenal_Wear(Arsenal *a, WeaponId id, int hits)
{
    if (id <= WEAPON_HANDS || id >= WEAPON_COUNT || hits <= 0) return false;
    const WeaponDef *d = Weapon_Def(id);
    if (d->kind != WEAPON_KIND_MELEE || d->durability <= 0) return false;
    a->wear[id] += hits;
    if (a->wear[id] < d->durability) return false;
    a->wear[id] = 0;                       // it broke: the next one is new
    return true;
}

float Arsenal_Condition(const Arsenal *a, WeaponId id)
{
    if (id < 0 || id >= WEAPON_COUNT) return 1.0f;
    const WeaponDef *d = Weapon_Def(id);
    if (d->kind != WEAPON_KIND_MELEE || d->durability <= 0) return 1.0f;
    const float f = 1.0f - (float)a->wear[id] / (float)d->durability;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

#define PRESS_BUFFER_SECS 0.15f   // a tap just before the gun is ready still counts

void Arsenal_Update(Arsenal *a, float dt, bool attackHeld, ArsenalEvents *out)
{
    ArsenalEvents ev = { .def = Weapon_Def(a->current) };
    const WeaponDef *def = ev.def;
    const bool isGun = def->kind == WEAPON_KIND_GUN;
    const bool mags = HasMagazine(def);

    if (a->reloadPending) { a->reloadPending = false; ev.reloadStart = true; }

    const bool pressed = attackHeld && !a->heldPrev;
    a->heldPrev = attackHeld;
    if (pressed) a->pressBuf = PRESS_BUFFER_SECS; else if (a->pressBuf > 0.0f) a->pressBuf -= dt;

    a->cooldown -= dt;  if (a->cooldown < 0.0f) a->cooldown = 0.0f;
    a->equipT += dt * 4.0f;  if (a->equipT > 1.0f) a->equipT = 1.0f;
    a->recoil -= dt * 8.0f;  if (a->recoil < 0.0f) a->recoil = 0.0f;
    a->flash  -= dt * 18.0f; if (a->flash < 0.0f) a->flash = 0.0f;
    a->dry    -= dt * 5.0f;  if (a->dry < 0.0f) a->dry = 0.0f;
    a->heat   -= dt * def->heatRecover;  if (a->heat < 0.0f) a->heat = 0.0f;
    {
        const float step = dt * 5.0f;                                          // the reload pose eases in and out in about a fifth of a second
        if (a->reloading) { a->reloadBlend += step; if (a->reloadBlend > 1.0f) a->reloadBlend = 1.0f; }
        else              { a->reloadBlend -= step; if (a->reloadBlend < 0.0f) a->reloadBlend = 0.0f; }
    }

    if (a->attackT >= 0.0f) {
        a->attackT += dt;
        if (!a->impactDone && a->attackT >= def->windup) {
            a->impactDone = true;
            if (def->kind != WEAPON_KIND_GUN) ev.impact = true;
        }
        if (a->attackT >= def->duration) a->attackT = -1.0f;
    }

    // A reload in progress. A whole-magazine reload moves the rounds when its time is up; a tube-fed gun (reloadPerRound) puts one
    // round in every reloadSecs until it is full or the bag is empty. Rounds come out of the reserve (a->ammo), which the caller
    // takes back from the inventory, exactly as it does for a shot.
    if (a->reloading) {
        if (!mags) {
            a->reloading = false;                              // a weapon with no magazine has nothing to reload
            a->reloadT = 0.0f;
        } else {
            int *mag = &a->mag[a->current];
            a->reloadT += dt;
            if (def->reloadPerRound) {
                while (a->reloading && a->reloadT >= def->reloadSecs) {
                    a->reloadT -= def->reloadSecs;
                    if (*mag < def->magSize && a->ammo > 0) { ++*mag; --a->ammo; ev.reloadRound = true; }
                    if (*mag >= def->magSize || a->ammo <= 0) { a->reloading = false; a->reloadT = 0.0f; ev.reloadDone = true; }
                }
            } else if (a->reloadT >= def->reloadSecs) {
                const int need = def->magSize - *mag;
                const int take = need < a->ammo ? need : a->ammo;
                *mag += take;
                a->ammo -= take;
                a->reloading = false;
                a->reloadT = 0.0f;
                ev.reloadDone = true;
            }
        }
    }

    // Does the trigger want a shot? A gun on SEMI or PUMP needs a fresh press (or one made just before it was ready);
    // everything else repeats while held.
    const bool pressGun = isGun && def->fireMode != FIRE_AUTO;
    bool want = pressGun ? (pressed || a->pressBuf > 0.0f) : attackHeld;

    // The trigger and a reload: a tube-fed gun with a round in it stops loading and fires (and keeps what went in); any other gun
    // cannot fire until its reload is done, so the trigger is ignored.
    if (a->reloading && want) {
        if (def->reloadPerRound && a->mag[a->current] >= def->ammoPerShot) { a->reloading = false; a->reloadT = 0.0f; }
        else want = false;
    }

    if (want && a->cooldown <= 0.0f && a->equipT >= 0.6f) {
        a->cooldown = def->cooldown;
        a->pressBuf = 0.0f;
        if (isGun) {
            int *pool = mags ? &a->mag[a->current] : &a->ammo;   // a gun with a magazine fires from it; one without fires from the bag
            if (*pool >= def->ammoPerShot) {
                *pool -= def->ammoPerShot;
                ev.shot = true;
                a->recoil = 1.0f;
                a->flash = 1.0f;
                a->attackT = 0.0f;
                a->impactDone = true;
                a->heat += def->heatPerShot;  if (a->heat > 1.0f) a->heat = 1.0f;
            } else {
                ev.dryFire = true;
                a->dry = 1.0f;
                if (mags && Arsenal_StartReload(a)) { a->reloadPending = false; ev.reloadStart = true; }   // an empty gun reloads by itself
            }
        } else {
            a->attackT = 0.0f;
            a->impactDone = false;
            if (def->kind == WEAPON_KIND_UNARMED) {
                a->lastPunchHand = a->punchHand;
                a->punchHand ^= 1;
            }
        }
    }
    if (out) *out = ev;
}
