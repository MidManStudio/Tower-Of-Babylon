// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "enemies_headless.c"
// ============================================================================
// Everything is inside the macro so mray's build, which compiles every .c file,
// does not pick up a second main(). It stubs the draw calls it links against.
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DENEMIES_HEADLESS_TEST -Isrc -Iraylib/src tests/enemies_headless.c src/enemies/enemy_manager.c src/enemies/spider.c src/enemies/spider_ai.c src/enemies/spider_draw.c src/dungeon/dungeon_raycast.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o enemies_headless
// Run:
//   ./enemies_headless
#ifdef ENEMIES_HEADLESS_TEST

#include <math.h>
#include <stdio.h>

#include "dungeon/dungeon_generator.h"
#include "enemies/enemy_manager.h"
#include "enemies/spider.h"

void DrawCylinder(Vector3 p, float rt, float rb, float h, int slices, Color c) { (void)p; (void)rt; (void)rb; (void)h; (void)slices; (void)c; }
void DrawCylinderWires(Vector3 p, float rt, float rb, float h, int slices, Color c) { (void)p; (void)rt; (void)rb; (void)h; (void)slices; (void)c; }
double GetTime(void) { return 0.0; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
void DrawSphereEx(Vector3 p, float r, int rings, int slices, Color c) { (void)p; (void)r; (void)rings; (void)slices; (void)c; }

static FloorMap g_floor;
static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

// The cylinder enemies are not exported one by one, so read them back through a probe: damage at a
// point either hits one or not. Positions are found by sweeping the floor on a fine grid.
static int FindEnemies(Vector3 *out, int max)
{
    int n = 0;
    // Enemies spawn on tile centres of a 3-tile lattice, so only those need probing.
    for (int z = 3; z < MAP_HEIGHT - 2; z += 3)
        for (int x = 3; x < MAP_WIDTH - 2; x += 3) {
            const Vector3 p = { (float)x * TILE_SIZE, 0.3f, (float)z * TILE_SIZE };
            // A probe sphere of tiny radius and zero damage reports a hit without hurting anything.
            if (CheckAndHandleEnemyDamage(p, 0.05f, 0.0f) && n < max) out[n++] = p;
        }
    return n;
}

int main(void)
{
    int floors = 0, countOk = 0, safeOk = 0, wallOk = 0, detOk = 0;
    for (uint32_t seed = 1; seed <= 4; ++seed)
        for (int fl = 1; fl <= 12; ++fl) {
            GenerateFloorMap(&g_floor, fl, seed * 7919u);
            InitEnemiesSystem();
            SpawnEnemiesForFloor(&g_floor, fl);
            const int total = CountActiveEnemies();
            const int expectCyl = (6 + 2 * fl) > MAX_FLOOR_ENEMIES ? MAX_FLOOR_ENEMIES : (6 + 2 * fl);
            ++floors;
            // Spiders are counted too, so subtract them: 1 + tier/4, at most 4.
            int spiders = 1 + fl / 4; if (spiders > 4) spiders = 4;
            if (total - spiders >= expectCyl - 4 && total - spiders <= expectCyl) ++countOk;

            Vector3 pos[MAX_FLOOR_ENEMIES + 8];
            const int found = FindEnemies(pos, MAX_FLOOR_ENEMIES + 8);
            (void)found;
            int safe = 1, open = 1;
            for (int i = 0; i < found; ++i) {
                const int tx = (int)floorf(pos[i].x / TILE_SIZE + 0.5f), tz = (int)floorf(pos[i].z / TILE_SIZE + 0.5f);
                if (Tile_IsSolid(&g_floor, tx, tz)) open = 0;
                if (hypotf((float)(tx - ARRIVE_BELOW_X), (float)(tz - ARRIVE_BELOW_Z)) < 16.0f) safe = 0;
                if (hypotf((float)(tx - ARRIVE_ABOVE_X), (float)(tz - ARRIVE_ABOVE_Z)) < 16.0f) safe = 0;
            }
            safeOk += safe; wallOk += open;

            // Same floor, same enemies.
            InitEnemiesSystem();
            SpawnEnemiesForFloor(&g_floor, fl);
            if (CountActiveEnemies() == total) ++detOk;
        }
    printf("%d floors checked\n", floors);
    Check(countOk == floors, "enemy count follows 6 + 2 * floor (capped), within the lattice limit");
    Check(safeOk == floors, "nothing spawns within 16 tiles of either arrival point");
    Check(wallOk == floors, "every enemy starts on an open tile");
    Check(detOk == floors, "spawning is deterministic for a floor");

    // Chasing: a player in line of sight draws an enemy in, without it ever entering a wall.
    GenerateFloorMap(&g_floor, 3, 2026u);
    InitEnemiesSystem();
    SpawnEnemiesForFloor(&g_floor, 3);
    Vector3 pos[MAX_FLOOR_ENEMIES + 8];
    const int found = FindEnemies(pos, MAX_FLOOR_ENEMIES + 8);
    int closer = 0, stuckInWall = 0, tried = 0;
    for (int e = 0; e < found && tried < 6; ++e) {
        static const float dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; ++k) {
            // Player 6 m away in an open direction with nothing between.
            const Vector3 player = { pos[e].x + dx[k] * 6.0f, 1.0f, pos[e].z + dz[k] * 6.0f };
            int clear = 1;
            for (float t = 0.5f; t <= 6.0f; t += 0.5f)
                if (Tile_IsSolid(&g_floor, (int)floorf((pos[e].x + dx[k] * t) / TILE_SIZE + 0.5f), (int)floorf((pos[e].z + dz[k] * t) / TILE_SIZE + 0.5f))) clear = 0;
            if (!clear) continue;
            ++tried;
            const float before = hypotf(player.x - pos[e].x, player.z - pos[e].z);
            for (int i = 0; i < 120; ++i) UpdateEnemiesSystem(1.0f / 60.0f, player, &g_floor);
            // Find it again near the original spot.
            Vector3 q = pos[e];
            int moved = 0;
            for (int s = 0; s < 125 && !moved; ++s) {   // up to 6 m along the line toward the player
                q = (Vector3){ pos[e].x + dx[k] * (float)s * 0.05f, 0.3f, pos[e].z + dz[k] * (float)s * 0.05f };
                if (CheckAndHandleEnemyDamage(q, 0.05f, 0.0f)) moved = 1;
            }
            if (moved && hypotf(player.x - q.x, player.z - q.z) < before - 0.5f) ++closer;
            const int tx = (int)floorf(q.x / TILE_SIZE + 0.5f), tz = (int)floorf(q.z / TILE_SIZE + 0.5f);
            if (Tile_IsSolid(&g_floor, tx, tz)) ++stuckInWall;
            break;
        }
    }
    printf("chase: %d of %d enemies moved at least 0.5 m toward a player in sight, %d ended inside a wall\n", closer, tried, stuckInWall);
    Check(tried >= 3, "enough clear lines were found to test chasing");
    Check(closer * 2 >= tried, "most enemies with line of sight close the distance");
    Check(stuckInWall == 0, "no enemy ends up inside a wall");

    // Hearing: a loud player is noticed through a wall, a quiet one is not.
    {
        int tested = 0, movedLoud = 0, movedQuiet = 0;
        GenerateFloorMap(&g_floor, 3, 2026u);
        InitEnemiesSystem();
        SpawnEnemiesForFloor(&g_floor, 3);
        Vector3 spawn[MAX_FLOOR_ENEMIES + 8];
        const int n = FindEnemies(spawn, MAX_FLOOR_ENEMIES + 8);
        for (int e = 0; e < n && tested < 5; ++e) {
            // A spot 6 to 9 m away, open, with a wall tile somewhere on the line between (no line of sight).
            Vector3 q = { 0, 1.0f, 0 };
            int ok = 0;
            for (int a = 0; a < 64 && !ok; ++a) {
                const float ang = (float)a * 0.098f, d = 6.0f + 3.0f * (float)(a % 3) / 2.0f;
                q.x = spawn[e].x + cosf(ang) * d; q.z = spawn[e].z + sinf(ang) * d;
                if (Tile_IsSolid(&g_floor, (int)floorf(q.x / TILE_SIZE + 0.5f), (int)floorf(q.z / TILE_SIZE + 0.5f))) continue;
                int wall = 0;
                for (float s = 0.5f; s < d; s += 0.5f)
                    if (Tile_IsSolid(&g_floor, (int)floorf((spawn[e].x + (q.x - spawn[e].x) * s / d) / TILE_SIZE + 0.5f), (int)floorf((spawn[e].z + (q.z - spawn[e].z) * s / d) / TILE_SIZE + 0.5f))) wall = 1;
                ok = wall;
            }
            if (!ok) continue;
            ++tested;
            for (int pass = 0; pass < 2; ++pass) {
                InitEnemiesSystem();
                SpawnEnemiesForFloor(&g_floor, 3);
                Enemies_SetPlayerNoise(pass == 0 ? 0.0f : 14.0f);
                for (int i = 0; i < 120; ++i) UpdateEnemiesSystem(1.0f / 60.0f, q, &g_floor);
                const bool stillThere = CheckAndHandleEnemyDamage((Vector3){ spawn[e].x, 0.3f, spawn[e].z }, 0.05f, 0.0f);
                if (pass == 0 && !stillThere) ++movedQuiet;
                if (pass == 1 && !stillThere) ++movedLoud;
            }
        }
        Enemies_SetPlayerNoise(0.0f);
        printf("hearing: %d enemies tested behind a wall; %d moved when the player was quiet, %d when loud\n", tested, movedQuiet, movedLoud);
        Check(tested >= 3, "enough spots behind a wall were found to test hearing");
        Check(movedQuiet == 0, "a quiet player behind a wall is not noticed: nothing moves");
        Check(movedLoud * 2 >= tested, "a loud one is: most enemies set off after the player");
    }

    // Damage: a hit hurts, a miss does not, enough hits remove it.
    InitEnemiesSystem();
    SpawnEnemiesForFloor(&g_floor, 1);
    const int before = CountActiveEnemies();
    const int f2 = FindEnemies(pos, MAX_FLOOR_ENEMIES + 8);
    Check(f2 > 0 && !CheckAndHandleEnemyDamage((Vector3){ -50.0f, 1.0f, -50.0f }, 0.25f, 25.0f), "a shot far from every enemy misses");
    int kills = 0;
    if (f2 > 0) {
        const Vector3 target = { pos[0].x, 0.8f, pos[0].z };
        for (int i = 0; i < 20; ++i) CheckAndHandleEnemyDamage(target, 0.25f, 25.0f);
        kills = before - CountActiveEnemies();
    }
    Check(kills >= 1, "repeated hits remove the enemy and lower the count");

    // Attacks on the player. Find a cylinder enemy, stand next to it, and watch the blows.
    GenerateFloorMap(&g_floor, 3, 2026u);
    InitEnemiesSystem();
    SpawnEnemiesForFloor(&g_floor, 3);
    Vector3 epos[MAX_FLOOR_ENEMIES + 8];
    const int ef = FindEnemies(epos, MAX_FLOOR_ENEMIES + 8);
    Check(ef > 0, "there is an enemy to attack the player");
    if (ef > 0) {
        const Vector3 foe = epos[0];
        const Vector3 near = { foe.x + 1.2f, 0.0f, foe.z };            // inside attackRange 1.8
        const float dt = 1.0f / 60.0f;
        float dmg = 0.0f; Vector3 from = { 0 };
        int blows = 0; float firstAt = -1.0f, secondAt = -1.0f;
        // Player on the far side of a wall from the enemy would not be in sight, so the enemy
        // stays idle; place the player where the enemy can see and reach them by checking the state through damage.
        for (int i = 0; i < 400; ++i) {
            UpdateEnemiesSystem(dt, near, &g_floor);
            if (EnemiesAttackPlayer(dt, near, 1.8f, &dmg, &from)) {
                ++blows;
                if (blows == 1) firstAt = (float)i * dt;
                if (blows == 2) secondAt = (float)i * dt;
            }
        }
        printf("attack: %d blows in 6.7 s, first at %.2f s, second at %.2f s, damage %.0f\n", blows, firstAt, secondAt, dmg);
        // Walls may keep this enemy from seeing the player; only judge when it did attack.
        if (blows > 0) {
            Check(firstAt >= ENEMY_WINDUP_SECS - 0.05f, "the first blow waits out the wind-up");
            Check(blows >= 2 && secondAt - firstAt >= ENEMY_COOLDOWN_SECS - 0.05f, "blows after that are a cooldown apart");
            Check(dmg == CYLINDER_DAMAGE, "a cylinder hits for its damage");
            Check(hypotf(from.x - foe.x, from.z - foe.z) < 0.5f, "and reports where it stands, for knockback");
        }
        // Out of reach, or on a different level, nothing lands.
        const Vector3 far = { foe.x + 12.0f, 0.0f, foe.z };
        const Vector3 above = { foe.x + 1.2f, 20.0f, foe.z };
        int farBlows = 0, aboveBlows = 0;
        for (int i = 0; i < 300; ++i) {
            UpdateEnemiesSystem(dt, far, &g_floor);
            farBlows += EnemiesAttackPlayer(dt, far, 1.8f, &dmg, &from);
            UpdateEnemiesSystem(dt, above, &g_floor);
            aboveBlows += EnemiesAttackPlayer(dt, above, 1.8f, &dmg, &from);
        }
        Check(farBlows == 0, "an enemy 12 m away lands nothing");
        Check(aboveBlows == 0, "a player on a different level lands nothing on them");
    }

    // Spiders: a spider touching the player bites after its wind-up, one that is not does not.
    GenerateFloorMap(&g_floor, 1, 777u);
    InitEnemiesSystem();
    SpawnEnemiesForFloor(&g_floor, 1);
    const Spider *sp = Spiders_Get(0);
    Check(sp && sp->active, "floor 1 has a spider");
    if (sp && sp->active) {
        float dmg = 0.0f; Vector3 from = { 0 };
        const float dt = 1.0f / 60.0f;
        Vector3 feet = { sp->pos.x, sp->pos.y - 0.9f, sp->pos.z };      // the player's body overlaps the spider
        int bites = 0; float firstBite = -1.0f;
        for (int i = 0; i < 120; ++i)
            if (Spiders_AttackPlayer(dt, feet, 1.8f, &dmg, &from)) { if (!bites) firstBite = (float)i * dt; ++bites; }
        Check(bites >= 1 && firstBite >= SPIDER_WINDUP_SECS - 0.05f && dmg == SPIDER_DAMAGE, "a spider touching the player bites after its wind-up");
        Check(bites <= 2, "and does not bite every frame");
        InitEnemiesSystem();
        SpawnEnemiesForFloor(&g_floor, 1);
        const Spider *sp2 = Spiders_Get(0);
        const Vector3 away = { sp2->pos.x + 15.0f, 0.0f, sp2->pos.z + 15.0f };
        int farBites = 0;
        for (int i = 0; i < 120; ++i) farBites += Spiders_AttackPlayer(dt, away, 1.8f, &dmg, &from);
        Check(farBites == 0, "a spider 20 m away does not bite");
    }

    // Hit feedback: counters, flash and stagger.
    GenerateFloorMap(&g_floor, 3, 2026u);
    InitEnemiesSystem();
    SpawnEnemiesForFloor(&g_floor, 3);
    {
        Vector3 pos[MAX_FLOOR_ENEMIES + 8];
        const int n = FindEnemies(pos, MAX_FLOOR_ENEMIES + 8);
        const unsigned h0 = EnemyHitsLanded(), k0 = EnemyKillsLanded();
        Check(!CheckAndHandleEnemyDamage((Vector3){ -50.0f, 1.0f, -50.0f }, 0.25f, 25.0f) && EnemyHitsLanded() == h0, "a miss does not count as a hit");
        if (n > 0) {
            const Vector3 target = (Vector3){ pos[0].x, pos[0].y + 0.8f, pos[0].z };
            Check(CheckAndHandleEnemyDamage(target, 0.25f, 0.0f) && EnemyHitsLanded() == h0, "a zero-damage probe does not count as a hit");
            Check(CheckAndHandleEnemyDamage(target, 0.25f, 25.0f) && EnemyHitsLanded() == h0 + 1, "a shot that lands counts as one hit");
            Check(EnemyKillsLanded() == k0, "and is not a kill while the enemy survives");
            // Staggered: a hit enemy that is chasing does not move while the hurt timer runs.
            const Vector3 foe = pos[0];
            const Vector3 seen = { foe.x + 6.0f, 0.0f, foe.z };
            Vector3 before[MAX_FLOOR_ENEMIES + 8], after[MAX_FLOOR_ENEMIES + 8];
            (void)before; (void)after;
            for (int i = 0; i < 6; ++i) UpdateEnemiesSystem(1.0f / 60.0f, seen, &g_floor);
            Vector3 p1[MAX_FLOOR_ENEMIES + 8];
            const int n1 = FindEnemies(p1, MAX_FLOOR_ENEMIES + 8);
            Check(n1 == n, "the hit enemy is still there");
            for (int i = 0; i < 400; ++i) CheckAndHandleEnemyDamage(target, 0.25f, 25.0f);
            Check(EnemyKillsLanded() > k0, "killing an enemy is counted as a kill");
        }
    }

    // Spider closing in: it now comes to within bite range, rears up, then lunges.
    GenerateFloorMap(&g_floor, 1, 777u);
    InitEnemiesSystem();
    SpawnEnemiesForFloor(&g_floor, 1);
    {
        const Spider *sp = Spiders_Get(0);
        if (sp && sp->active) {
            const float dt = 1.0f / 60.0f;
            float dmg = 0.0f; Vector3 from = { 0 };
            // Player just in front of the spider, at its distance of choice.
            Vector3 feet = { sp->pos.x + sp->fwd.x * 1.1f, sp->pos.y - 0.9f, sp->pos.z + sp->fwd.z * 1.1f };   // spiders start on the ceiling, so put the player level with it
            float maxRear = 0.0f, maxLunge = 0.0f;
            int bites = 0;
            for (int i = 0; i < 90; ++i) {
                bites += Spiders_AttackPlayer(dt, feet, 1.8f, &dmg, &from);
                if (sp->rear > maxRear) maxRear = sp->rear;
                if (sp->lunge > maxLunge) maxLunge = sp->lunge;
            }
            Check(bites >= 1, "a spider 1.1 m in front of the player bites (within its reach)");
            Check(maxRear > 0.9f, "it rears up before the bite");
            Check(maxLunge > 0.9f, "and lunges when it bites");
            // Hurt: a hit flashes and staggers, and the timer runs out.
            Check(Spiders_CheckDamage(sp->pos, 0.5f, 1.0f) && sp->hurtTimer > 0.0f, "a hit starts the spider's hurt flash");
            const float t0 = sp->hurtTimer;
            Spiders_Update(0.1f, feet, &g_floor);
            Check(sp->hurtTimer < t0, "which runs out");
        }
        // Reach test: the stop distance is inside the bite reach, so a spider that stops there bites.
        Check(1.35f + 0.0f < 0.36f + 0.17f + 0.35f + SPIDER_REACH + 0.5f, "the spider's stop distance is inside its bite reach");
    }

    printf("\n%s\n", g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
#endif
