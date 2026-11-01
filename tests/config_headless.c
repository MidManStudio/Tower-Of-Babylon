// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "tests/config_headless.c"
// ============================================================================
// The config layer without a window or the wasm: the baked table (order, and that it is the one the shipped .mdix files
// make), the document getters, the weapon and projectile tables built from it, and that a bad document is refused
// without disturbing the tables already installed. The wasm half is tested by tests/mdix_bridge_test.mjs.
// Inside a macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DCONFIG_HEADLESS_TEST -Isrc -Iraylib/src tests/config_headless.c src/config/game_config.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c src/weapons/weapons.c src/flashlight/flashlight.c src/projectiles/projectile_config.c src/projectiles/projectile_patterns.c -lm -o config_headless
// Run from the project root (it reads assets/config/*.mdix):
//   ./config_headless
#ifdef CONFIG_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/game_config.h"
#include "flashlight/flashlight.h"
#include "projectiles/projectile_config.h"
#include "weapons/weapons.h"

extern const MdixBaked g_bakedWeapons, g_bakedProjectiles, g_bakedFlashlight;

static int g_failed = 0;
static void Check(int ok, const char *what) { printf("  %s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++g_failed; }
static int Near(float a, float b) { return fabsf(a - b) < 1e-4f; }

static char *ReadFile(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f); b[n] = 0; *len = (int)n;
    return b;
}

static int Sorted(const MdixBaked *b)
{
    for (int i = 1; i < b->count; ++i) if (strcmp(b->entries[i - 1].path, b->entries[i].path) >= 0) return 0;
    return b->count > 0;
}

// A copy of a baked table with one value changed, or one path renamed so it no longer matches.
static MdixBaked Doctored(const MdixBaked *src, MdixBakedEntry *store, const char *path, const char *newText, const char *newPath)
{
    for (int i = 0; i < src->count; ++i) {
        store[i] = src->entries[i];
        if (strcmp(store[i].path, path) == 0) { if (newText) store[i].text = newText; if (newPath) store[i].path = newPath; }
    }
    MdixBaked b = *src;
    b.entries = store;
    return b;
}

static int Rejects(const MdixBaked *w, const char *needle)
{
    MdixDoc *wd = MdixDoc_FromBaked(w), *pd = MdixDoc_FromBaked(&g_bakedProjectiles), *fd = MdixDoc_FromBaked(&g_bakedFlashlight);
    char err[160];
    const int refused = !GameConfig_LoadFrom(wd, pd, fd, err, (int)sizeof err);
    const int named = strstr(err, needle) != NULL;
    if (refused && !named) printf("      (error was: %s)\n", err);
    MdixDoc_Free(wd); MdixDoc_Free(pd); MdixDoc_Free(fd);
    return refused && named;
}

int main(void)
{
    printf("Baked table\n");
    Check(Sorted(&g_bakedWeapons) && Sorted(&g_bakedProjectiles) && Sorted(&g_bakedFlashlight), "all three tables are in strcmp order (the lookup is a binary search)");
    {
        int wl = 0, pl = 0, fl = 0;
        char *wt = ReadFile("assets/config/weapons.mdix", &wl), *pt = ReadFile("assets/config/projectiles.mdix", &pl), *ft = ReadFile("assets/config/flashlight.mdix", &fl);
        Check(wt && pt && ft, "assets/config/weapons.mdix, projectiles.mdix and flashlight.mdix are there (run from the project root)");
        Check(ft && Mdix_Hash(ft, fl) == g_bakedFlashlight.sourceHash, "flashlight.mdix is what config_baked.c was made from");
        free(ft);
        Check(wt && Mdix_Hash(wt, wl) == g_bakedWeapons.sourceHash, "weapons.mdix is what config_baked.c was made from (else run tools/mdix_bake.mjs)");
        Check(pt && Mdix_Hash(pt, pl) == g_bakedProjectiles.sourceHash, "projectiles.mdix is what config_baked.c was made from");
        free(wt); free(pt);
    }

    printf("Document getters on the baked table\n");
    MdixDoc *w = MdixDoc_FromBaked(&g_bakedWeapons), *p = MdixDoc_FromBaked(&g_bakedProjectiles);
    int i = 0; float f = 0; char s[32];
    Check(MdixDoc_ArrayLen(w, "weapons") == 5 && MdixDoc_ArrayLen(p, "projectiles") == 3, "arrays: 5 weapons, 3 projectile configs");
    Check(MdixDoc_ArrayLen(w, "weapons[0].id") == -1 && MdixDoc_ArrayLen(w, "nothing") == -1, "a non-array and a missing path have length -1");
    Check(MdixDoc_Has(w, "weapons[3].pellets") && !MdixDoc_Has(w, "weapons[3].nope") && !MdixDoc_Has(w, "weapons[9].id"), "has: yes, no, no");
    Check(MdixDoc_Int(w, "weapons[3].pellets", &i) && i == 8, "int: the shotgun has 8 pellets");
    Check(MdixDoc_Int(w, "weapons[4].fire_mode", &i) && i == FIRE_AUTO, "an enum reads as its ordinal: the rifle is AUTO");
    Check(MdixDoc_Float(w, "weapons[2].damage", &f) && Near(f, 25.0f), "float: the pistol does 25");
    Check(MdixDoc_Float(w, "weapons[3].pellets", &f) && Near(f, 8.0f), "an int widens to a float");
    Check(!MdixDoc_Int(w, "weapons[2].damage", &i), "an int read of a float says no");
    Check(!MdixDoc_Float(w, "weapons[2].id", &f) && !MdixDoc_Int(w, "weapons[2].id", &i), "a number read of a string says no");
    Check(MdixDoc_Str(w, "weapons[3].projectile", s, (int)sizeof s) && strcmp(s, "buckshot") == 0, "string: the shotgun fires buckshot");
    Check(!MdixDoc_Str(w, "weapons[3].projectile", s, 4), "a string that does not fit is refused, not cut");
    Check(!MdixDoc_Int(NULL, "x", &i) && !MdixDoc_Has(NULL, "x") && MdixDoc_ArrayLen(NULL, "x") == -1, "a NULL document answers no to everything");
    MdixDoc_Free(w); MdixDoc_Free(p);

    printf("Tables from the baked config\n");
    GameConfig_LoadBaked();
    Check(GameConfig_Status()->source == CONFIG_FROM_BAKED && GameConfig_Status()->note[0] == 0, "it loads without a complaint");
    Check(ProjectileConfig_Count() == 3 && ProjectileConfig_Find("pistol_round") == 0 && ProjectileConfig_Find("buckshot") == 1
          && ProjectileConfig_Find("rifle_round") == 2 && ProjectileConfig_Find("nope") == -1, "three projectile configs, found by id");
    const ProjectileConfig *rr = ProjectileConfig_Get(2);
    Check(rr && Near(rr->speedMin, 85.0f) && Near(rr->maxRange, 70.0f) && Near(rr->falloffMin, 0.70f) && rr->motion == MOVE_STRAIGHT
          && rr->piercing == PIERCE_NONE && rr->maxHits == 1 && rr->colorCore == 0xFFFFFFu && rr->colorGlow == 0xFFD060u, "the rifle round reads back as written");
    const ProjectileConfig *bs = ProjectileConfig_Get(1);
    Check(bs && Near(bs->speedMin, 42.0f) && Near(bs->speedMax, 52.0f) && Near(bs->gravity, 0.12f), "buckshot has a speed range and some drop");
    Check(ProjectileConfig_Get(3) == NULL && ProjectileConfig_Get(-1) == NULL, "out of range is NULL");

    const WeaponDef *hands = Weapon_Def(WEAPON_HANDS), *pipe = Weapon_Def(WEAPON_PIPE);
    Check(strcmp(hands->name, "HANDS") == 0 && hands->kind == WEAPON_KIND_UNARMED && Near(hands->damage, 10.0f) && Near(hands->cooldown, 0.45f)
          && Near(hands->windup, 0.12f) && Near(hands->duration, 0.34f) && Near(hands->reach, 1.7f) && Near(hands->arcDeg, 50.0f) && Near(hands->knockback, 0.4f),
          "hands are what they were before the config: 10 damage, 0.45 s, 1.7 m reach");
    Check(strcmp(pipe->name, "PIPE") == 0 && pipe->kind == WEAPON_KIND_MELEE && Near(pipe->damage, 35.0f) && Near(pipe->cooldown, 0.80f)
          && Near(pipe->windup, 0.31f) && Near(pipe->duration, 0.62f) && Near(pipe->reach, 2.6f) && Near(pipe->arcDeg, 60.0f) && Near(pipe->knockback, 1.0f),
          "the pipe is what it was: 35 damage, 0.8 s, the blow lands at 0.31 of 0.62 s");
    const WeaponDef *pistol = Weapon_Def(WEAPON_PISTOL), *shotgun = Weapon_Def(WEAPON_SHOTGUN), *rifle = Weapon_Def(WEAPON_RIFLE);
    Check(pistol->kind == WEAPON_KIND_GUN && Near(pistol->damage, 25.0f) && Near(pistol->cooldown, 0.30f) && pistol->fireMode == FIRE_AUTO
          && pistol->ammo == AMMO_PISTOL && pistol->ammoPerShot == 1 && pistol->pellets == 1 && pistol->pickupAmmo == 12,
          "the pistol keeps its 25 damage, 0.3 s and hold-to-repeat, and comes with 12 rounds");
    Check(shotgun->fireMode == FIRE_PUMP && shotgun->ammo == AMMO_SHELLS && shotgun->pellets == 8 && Near(shotgun->spreadDeg, 4.5f)
          && Near(shotgun->cooldown, 0.95f) && shotgun->projectile == 1, "the shotgun: pump, shells, 8 pellets in 4.5 degrees, buckshot");
    Check(rifle->fireMode == FIRE_AUTO && rifle->ammo == AMMO_RIFLE && Near(rifle->cooldown, 0.12f) && Near(rifle->spreadMaxDeg, 5.0f)
          && Near(rifle->heatPerShot, 0.12f) && rifle->projectile == 2, "the rifle: automatic, rifle rounds, 0.12 s, spread opens to 5 degrees, rifle_round");
    Check(Ammo_BoxAmount(AMMO_PISTOL) == 8 && Ammo_BoxAmount(AMMO_SHELLS) == 6 && Ammo_BoxAmount(AMMO_RIFLE) == 24 && Ammo_BoxAmount(AMMO_NONE) == 0,
          "ammo boxes hold 8, 6 and 24 rounds");
    Check(shotgun->noiseMetres > pistol->noiseMetres && Near(pistol->noiseMetres, 28.0f) && Near(pistol->noiseSecs, 1.5f),
          "the pistol is heard at 28 m for 1.5 s as before, and the shotgun is louder");
    Check(strcmp(Weapon_IdString(WEAPON_SHOTGUN), "shotgun") == 0 && Weapon_IdString((WeaponId)99)[0] == 0, "weapon id strings");

    printf("The flashlight table\n");
    {
        const FlashlightConfig *fc = Flashlight_Config();
        Check(Near(fc->capacitySecs, 200.0f) && Near(fc->startSecs, 80.0f) && Near(fc->batterySecs, 100.0f) && Near(fc->lowFraction, 0.20f), "capacity 200 s, a found one holds 80 s, a battery adds 100 s, it fails below 20%");
        Check(Near(fc->beamInnerDeg, 14.0f) && Near(fc->beamOuterDeg, 30.0f) && Near(fc->range, 20.0f) && Near(fc->strength, 0.8f) && Near(fc->lag, 14.0f), "the beam: 14 to 30 degrees, 20 m, strength 0.8, lag 14");
        Check(fc->tint == 0xFFE9B0u, "the tint reads back as written");
        MdixDoc *fd = MdixDoc_FromBaked(&g_bakedFlashlight);
        Check(MdixDoc_Has(fd, "flashlight.range") && !MdixDoc_Has(fd, "flashlight.nope") && MdixDoc_ArrayLen(fd, "flashlight") == -1, "a table is read by path, and is not an array");
        MdixDoc_Free(fd);
    }

    printf("Bad documents are refused and change nothing\n");
    MdixBakedEntry *store = (MdixBakedEntry *)malloc(sizeof(MdixBakedEntry) * (size_t)g_bakedWeapons.count);
    MdixBaked bad;
    bad = Doctored(&g_bakedWeapons, store, "weapons[3].pellets", NULL, "weapons[3].pelletz");
    Check(Rejects(&bad, "weapons[3].pellets"), "a missing value is refused and named");
    bad = Doctored(&g_bakedWeapons, store, "weapons[3].pellets", "0", NULL);
    Check(Rejects(&bad, "weapons[3].pellets"), "pellets of 0 is out of range");
    bad = Doctored(&g_bakedWeapons, store, "weapons[3].pellets", "99", NULL);
    Check(Rejects(&bad, "weapons[3].pellets"), "pellets of 99 is out of range");
    bad = Doctored(&g_bakedWeapons, store, "weapons[3].projectile", "slugs", NULL);
    Check(Rejects(&bad, "projectile"), "a projectile id that is not in projectiles.mdix is refused");
    bad = Doctored(&g_bakedWeapons, store, "weapons[4].id", "rifel", NULL);
    Check(Rejects(&bad, "weapons[4].id"), "a weapon id the game does not know is refused");
    bad = Doctored(&g_bakedWeapons, store, "weapons[4].id", "pistol", NULL);
    Check(Rejects(&bad, "twice"), "a weapon listed twice is refused");
    bad = Doctored(&g_bakedWeapons, store, "weapons[2].cooldown", "0", NULL);
    Check(Rejects(&bad, "weapons[2].cooldown"), "a cooldown of 0 is refused");
    bad = Doctored(&g_bakedWeapons, store, "weapons[3].spread_max_deg", "1", NULL);
    Check(Rejects(&bad, "weapons[3].spread_max_deg"), "a hot spread smaller than the cold one is refused");
    bad = Doctored(&g_bakedWeapons, store, "weapons[3].ammo", "0", NULL);
    Check(Rejects(&bad, "weapons[3].ammo"), "a gun with no ammo type is refused");
    {
        MdixBakedEntry *fs = (MdixBakedEntry *)malloc(sizeof(MdixBakedEntry) * (size_t)g_bakedFlashlight.count);
        MdixDoc *wd = MdixDoc_FromBaked(&g_bakedWeapons), *pd = MdixDoc_FromBaked(&g_bakedProjectiles);
        const char *cases[][3] = { { "flashlight.capacity_secs", "0", "capacity_secs" }, { "flashlight.start_secs", "999", "start_secs" },
                                   { "flashlight.beam_inner_deg", "40", "beam_outer_deg" }, { "flashlight.range", "0", "flashlight.range" } };
        int allRefused = 1;
        for (int k = 0; k < 4; ++k) {
            MdixBaked fb = Doctored(&g_bakedFlashlight, fs, cases[k][0], cases[k][1], NULL);
            MdixDoc *fd = MdixDoc_FromBaked(&fb);
            char err[160];
            if (GameConfig_LoadFrom(wd, pd, fd, err, (int)sizeof err) || !strstr(err, cases[k][2])) { allRefused = 0; printf("      (flashlight case %d: %s)\n", k, err); }
            MdixDoc_Free(fd);
        }
        Check(allRefused, "a flashlight with no capacity, a start above the capacity, an inner beam wider than the outer, or no range is refused and named");
        MdixBaked fb = Doctored(&g_bakedFlashlight, fs, "flashlight.range", NULL, "flashlight.rangez");
        MdixDoc *fd = MdixDoc_FromBaked(&fb);
        char err[160];
        Check(!GameConfig_LoadFrom(wd, pd, fd, err, (int)sizeof err) && strstr(err, "flashlight.range"), "a missing flashlight value is refused and named");
        MdixDoc_Free(fd); MdixDoc_Free(wd); MdixDoc_Free(pd); free(fs);
    }
    Check(Near(Weapon_Def(WEAPON_PISTOL)->damage, 25.0f) && Weapon_Def(WEAPON_SHOTGUN)->pellets == 8 && Near(Flashlight_Config()->range, 20.0f), "after all that the installed tables are untouched");

    printf("A good document of its own takes effect\n");
    bad = Doctored(&g_bakedWeapons, store, "weapons[2].damage", "31", NULL);
    {
        MdixDoc *wd = MdixDoc_FromBaked(&bad), *pd = MdixDoc_FromBaked(&g_bakedProjectiles), *fd = MdixDoc_FromBaked(&g_bakedFlashlight);
        char err[160];
        Check(GameConfig_LoadFrom(wd, pd, fd, err, (int)sizeof err) && Near(Weapon_Def(WEAPON_PISTOL)->damage, 31.0f), "a changed pistol damage is picked up");
        MdixDoc_Free(wd); MdixDoc_Free(pd); MdixDoc_Free(fd);
    }
    GameConfig_LoadBaked();
    Check(Near(Weapon_Def(WEAPON_PISTOL)->damage, 25.0f), "and loading the baked config puts it back");
    free(store);

    printf("Without the wasm (a native build)\n");
    GameConfig_Init();
    Check(Mdix_WasmState() == MDIX_WASM_FAILED && Mdix_WasmError()[0] != 0, "the wasm start fails politely with a reason");
    Check(GameConfig_Status()->source == CONFIG_FROM_BAKED && GameConfig_Status()->note[0] != 0, "the status says baked values are in use, and why");
    Check(Near(Weapon_Def(WEAPON_RIFLE)->cooldown, 0.12f), "the weapons are loaded and usable");
    GameConfig_Poll();
    Check(GameConfig_Status()->source == CONFIG_FROM_BAKED, "polling changes nothing");
    char err[64];
    Check(MdixDoc_Parse("@DATA( a = 1 )", err, (int)sizeof err) == NULL && err[0] != 0, "parsing text without the wasm gives NULL and a reason");

    printf(g_failed ? "\n%d FAILED\n" : "\nall passed\n", g_failed);
    return g_failed ? 1 : 0;
}

#endif
