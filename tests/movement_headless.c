// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/player.md, section "movement_headless.c"
// ============================================================================
// Velocity based movement, sprinting and stamina: speeding up and slowing down, the speed caps, what a sprint costs,
// exhaustion and how it ends, the jump cost, sliding along a wall, the view effects. Links the real player
// controller and stairs and stubs the draw calls. Inside a macro so mray's build, which compiles every .c file,
// does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DMOVEMENT_HEADLESS_TEST -Isrc -Iraylib/src tests/movement_headless.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o movement_headless
// Run:
//   ./movement_headless
#ifdef MOVEMENT_HEADLESS_TEST

#include <math.h>
#include <stdio.h>

#include "dungeon/dungeon_types.h"
#include "enemies/enemy_types.h"
#include "player/player_controller.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static FloorMap g_floor;
static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

#define DT (1.0f / 60.0f)

static void OpenFloor(int wallColumn)
{
    for (int z = 0; z < MAP_HEIGHT; ++z)
        for (int x = 0; x < MAP_WIDTH; ++x) {
            g_floor.grid[z][x].type = (x == 0 || z == 0 || x == MAP_WIDTH - 1 || z == MAP_HEIGHT - 1 || x == wallColumn) ? TILE_WALL : TILE_FLOOR;
            g_floor.grid[z][x].variant = 0;
            g_floor.grid[z][x].flags = 0;
        }
}

// A slab of wall, x 23 to 25 and z 19 to 39 (tile column 12, rows 10 to 19), on open floor: the corner the leaning tests stand beside.
static void CornerFloor(void)
{
    OpenFloor(-1);
    for (int tz = 10; tz <= 19; ++tz) g_floor.grid[tz][12].type = TILE_WALL;
}

static PlayerController Place(float x, float z, float yaw)
{
    PlayerController p;
    InitPlayerController(&p, x, z);
    TeleportPlayer(&p, (Vector3){ x, 0.0f, z }, yaw);
    return p;
}

static void Run(PlayerController *p, Vector2 stick, bool sprint, float secs)
{
    for (float t = 0.0f; t < secs - 1e-4f; t += DT) {
        p->wantSprint = sprint;
        UpdatePlayerController(p, stick, (Vector2){ 0, 0 }, DT, &g_floor);
    }
}

static const Vector2 FWD = { 0.0f, -1.0f };     // stick pushed forward (y is negative forward)

int main(void)
{
    OpenFloor(-1);

    printf("Speeding up and slowing down\n");
    {
        PlayerController p = Place(20.0f, 6.0f, 0.0f);    // yaw 0 looks along +z
        Run(&p, FWD, false, 1.0f / 60.0f);
        const float first = PlayerSpeed(&p);
        Check(first > 0.0f && first < 1.0f, "one frame after pushing the stick the player has only just started moving");
        Run(&p, FWD, false, 0.05f);
        Check(PlayerSpeed(&p) < 2.5f, "after a twentieth of a second the player is still well below walking speed");
        Run(&p, FWD, false, 0.25f);
        Check(fabsf(PlayerSpeed(&p) - PLAYER_WALK_SPEED) < 0.02f, "and is at walking speed within about a third of a second");
        Check(p.position.z > 6.0f && fabsf(p.position.x - 20.0f) < 1e-3f, "having gone the way it faces");
        Run(&p, (Vector2){ 0, 0 }, false, 1.0f / 60.0f);
        Check(PlayerSpeed(&p) > 2.0f, "let go of the stick and the player does not stop at once");
        Run(&p, (Vector2){ 0, 0 }, false, 0.2f);
        Check(PlayerSpeed(&p) == 0.0f, "but is standing still a moment later");

        PlayerController d = Place(20.0f, 6.0f, 0.0f);
        Run(&d, (Vector2){ 1.0f, -1.0f }, false, 1.0f);
        Check(fabsf(PlayerSpeed(&d) - PLAYER_WALK_SPEED * PlayerDirectionMul(0.7071f, 0.7071f)) < 0.03f && PlayerSpeed(&d) < PLAYER_WALK_SPEED, "walking diagonally is not faster than walking straight (a bit slower: the sideways share is slower)");
        PlayerController h = Place(20.0f, 6.0f, 0.0f);
        Run(&h, (Vector2){ 0.0f, -0.5f }, false, 1.0f);
        Check(fabsf(PlayerSpeed(&h) - PLAYER_WALK_SPEED * 0.5f) < 0.05f, "a half push of the stick walks at half speed");
        PlayerController b = Place(20.0f, 40.0f, 0.0f);
        Run(&b, (Vector2){ 0.0f, 1.0f }, false, 1.0f);
        Check(b.position.z < 40.0f, "pushing the stick back walks backwards");
    }

    printf("Sprinting\n");
    {
        PlayerController p = Place(20.0f, 4.0f, 0.0f);
        Run(&p, FWD, true, 0.6f);
        Check(p.sprinting && fabsf(PlayerSpeed(&p) - PLAYER_SPRINT_SPEED) < 0.05f, "sprinting reaches the sprint speed");
        Check(PlayerSpeed(&p) > PLAYER_WALK_SPEED * 1.5f, "which is well over walking speed");
        const float before = p.stamina;
        Run(&p, FWD, true, 1.0f);
        Check(fabsf((before - p.stamina) - STAMINA_SPRINT_DRAIN) < 0.5f, "a second of sprinting costs the set amount of stamina");
        Check(p.camera.fovy > 60.0f + SPRINT_FOV_KICK * 0.8f, "the view widens while sprinting");
        Run(&p, FWD, false, 1.2f);
        Check(!p.sprinting && fabsf(p.camera.fovy - 60.0f) < 0.5f, "and narrows again when the sprint ends");

        // Omni sprint: any direction, slower sideways and backwards.
        PlayerController s = Place(20.0f, 40.0f, 0.0f);
        Run(&s, (Vector2){ 1.0f, 0.0f }, true, 1.0f);
        Check(s.sprinting && fabsf(PlayerSpeed(&s) - PLAYER_SPRINT_SPEED * PLAYER_STRAFE_MUL) < 0.05f, "sprinting sideways works, at the strafe share of the sprint speed");
        PlayerController l = Place(20.0f, 40.0f, 0.0f);
        Run(&l, (Vector2){ -1.0f, 0.0f }, true, 1.0f);
        Check(l.sprinting && fabsf(PlayerSpeed(&l) - PlayerSpeed(&s)) < 0.02f, "left is the same as right");
        PlayerController r = Place(20.0f, 40.0f, 0.0f);
        Run(&r, (Vector2){ 0.0f, 1.0f }, true, 1.0f);
        Check(r.sprinting && fabsf(PlayerSpeed(&r) - PLAYER_SPRINT_SPEED * PLAYER_BACKPEDAL_MUL) < 0.05f, "and backwards, at the backpedal share");
        Check(PlayerSpeed(&r) < PlayerSpeed(&s) && PlayerSpeed(&s) < PLAYER_SPRINT_SPEED, "backwards is slower than sideways, and both slower than forward");
        PlayerController dg = Place(20.0f, 40.0f, 0.0f);
        Run(&dg, (Vector2){ 1.0f, 1.0f }, true, 1.0f);
        Check(dg.sprinting && fabsf(PlayerSpeed(&dg) - PLAYER_SPRINT_SPEED * PlayerDirectionMul(-0.7071f, 0.7071f)) < 0.05f, "back and to the side lands between the two");
        {
            const float before = s.stamina;
            Run(&s, (Vector2){ 1.0f, 0.0f }, true, 1.0f);
            Check(fabsf((before - s.stamina) - STAMINA_SPRINT_DRAIN) < 0.5f, "a sideways sprint costs the same stamina as a forward one");
        }
        Check(PlayerDirectionMul(1.0f, 0.0f) == 1.0f && fabsf(PlayerDirectionMul(0.0f, 1.0f) - PLAYER_STRAFE_MUL) < 1e-4f && fabsf(PlayerDirectionMul(-1.0f, 0.0f) - PLAYER_BACKPEDAL_MUL) < 1e-4f, "the direction multiplier is 1 ahead, the strafe share to the side and the backpedal share behind");
        {
            float worst = 0.0f, prev = PlayerDirectionMul(1.0f, 0.0f);
            for (int a = 1; a <= 360; ++a) {
                const float th = (float)a * 3.14159265f / 180.0f;
                const float m = PlayerDirectionMul(cosf(th), sinf(th));
                if (fabsf(m - prev) > worst) worst = fabsf(m - prev);
                prev = m;
            }
            Check(worst < 0.01f, "going round the compass the multiplier never jumps (no step between ahead and behind)");
        }
        PlayerController st = Place(20.0f, 4.0f, 0.0f);
        Run(&st, (Vector2){ 0, 0 }, true, 1.0f);
        Check(!st.sprinting && st.stamina == PLAYER_STAMINA_MAX, "or standing still: it costs nothing");
        PlayerController lo = Place(20.0f, 4.0f, 0.0f);
        lo.stamina = STAMINA_SPRINT_MIN - 1.0f;
        lo.staminaDelay = 99.0f;
        Run(&lo, FWD, true, 0.5f);
        Check(!lo.sprinting, "with little stamina in hand a sprint cannot start");
    }

    printf("Stamina, tiredness and exhaustion\n");
    {
        PlayerController p = Place(20.0f, 4.0f, 0.0f);
        float t = 0.0f;
        while (!p.exhausted && t < 20.0f) { Run(&p, FWD, true, DT); t += DT; }
        printf("    a full bar lasts %.1f s of sprinting\n", t);
        Check(t > 10.5f && t < 13.0f, "a full stamina bar (200: twice the old one) is about twelve seconds of sprinting");
        Check(p.exhausted && p.stamina == 0.0f && !p.sprinting, "then the player is exhausted, with none left, and stops sprinting");
        Run(&p, FWD, true, 1.2f);
        Check(PlayerSpeed(&p) <= PLAYER_WALK_SPEED * 0.5f + 0.05f, "an exhausted player moves at half walking speed or less, though still pushing and holding sprint");
        Check(!p.sprinting, "and cannot sprint");
        // Sluggish: it takes longer to get moving.
        PlayerController e = Place(20.0f, 4.0f, 0.0f);
        e.exhausted = true; e.stamina = 0.0f;
        Run(&e, FWD, false, 0.1f);
        PlayerController f = Place(20.0f, 4.0f, 0.0f);
        Run(&f, FWD, false, 0.1f);
        Check(PlayerSpeed(&e) < PlayerSpeed(&f) * 0.6f, "an exhausted player is also slower to pick up speed");

        // Recovery
        PlayerController q = Place(20.0f, 40.0f, 0.0f);
        q.exhausted = true; q.stamina = 0.0f; q.staminaDelay = STAMINA_REGEN_DELAY;   // as a sprint that has just run dry leaves it
        Run(&q, (Vector2){ 0, 0 }, false, STAMINA_REGEN_DELAY * 0.8f);
        Check(q.stamina == 0.0f, "resting, no stamina comes back for the first second");
        float rt = STAMINA_REGEN_DELAY * 0.8f;
        while (q.exhausted && rt < 20.0f) { Run(&q, (Vector2){ 0, 0 }, false, DT); rt += DT; }
        printf("    exhaustion lasts %.1f s of resting\n", rt);
        Check(rt > 5.5f && rt < 8.0f, "an exhausted player is back on their feet after about seven seconds' rest (a bar twice the size refills at the same rate)");
        Check(q.stamina >= STAMINA_EXHAUST_RECOVER - 0.5f && q.stamina < STAMINA_EXHAUST_RECOVER + 1.0f, "when stamina is back to the recovery level, not before");
        Run(&q, FWD, true, 0.6f);
        Check(q.sprinting, "after which it can sprint again");

        // Ordinary regeneration
        PlayerController g = Place(20.0f, 4.0f, 0.0f);
        Run(&g, FWD, true, 2.0f);
        const float after = g.stamina;
        Run(&g, (Vector2){ 0, 0 }, false, STAMINA_REGEN_DELAY * 0.9f);
        Check(g.stamina == after, "after sprinting, stamina waits before it comes back");
        Run(&g, (Vector2){ 0, 0 }, false, 1.0f + STAMINA_REGEN_DELAY * 0.1f + 0.05f);
        Check(g.stamina > after + STAMINA_REGEN * 0.9f && g.stamina < after + STAMINA_REGEN * 1.3f, "then returns at the set rate");
        Run(&g, (Vector2){ 0, 0 }, false, 10.0f);
        Check(g.stamina == PLAYER_STAMINA_MAX, "and stops at the top");

        // Tiredness is a smooth, continuous slowdown, not a switch
        {
            float prev = 1e9f, worstStep = 0.0f;
            float at100 = 0, at60 = 0, at30 = 0, at1 = 0;
            for (int st = 100; st >= 0; st -= 5) {
                PlayerController tr = Place(20.0f, 4.0f, 0.0f);
                tr.stamina = (float)st * STAMINA_SCALE;   // st is a percentage of the bar
                tr.staminaDelay = 99.0f;                 // hold regeneration off for the test
                Run(&tr, FWD, false, 1.0f);
                const float v = PlayerSpeed(&tr);
                if (st == 100) at100 = v;
                if (st == 60) at60 = v;
                if (st == 30) at30 = v;
                if (st == 0) at1 = v;
                if (prev < 1e8f) { if (v > prev + 0.02f) worstStep = 99.0f; if (prev - v > worstStep && worstStep < 90.0f) worstStep = prev - v; }
                prev = v;
            }
            printf("    walking speed by stamina (as %% of the bar): 100 -> %.2f, 60 -> %.2f, 30 -> %.2f, 0 -> %.2f m/s (largest step per 5%% of the bar %.2f)\n", at100, at60, at30, at1, worstStep);
            Check(fabsf(at100 - PLAYER_WALK_SPEED) < 0.02f && fabsf(at60 - PLAYER_WALK_SPEED) < 0.02f, "with 60% of the bar or more (STAMINA_FRESH) there is no fatigue");
            Check(at30 < at60 - 0.3f && at30 > at1 + 0.3f, "below that the player slows more and more as stamina falls");
            Check(fabsf(at1 - PLAYER_WALK_SPEED * FATIGUE_MIN_MUL * EXHAUSTED_EXTRA_MUL) < 0.05f || fabsf(at1 - PLAYER_WALK_SPEED * FATIGUE_MIN_MUL) < 0.05f, "and at zero is down to the floor of the fatigue curve");
            Check(worstStep < 0.5f, "the slowdown is gradual: no step of 5% of the bar changes the speed by half a metre a second or more, and it never speeds up as stamina falls");
        }
        {
            PlayerController tr = Place(20.0f, 4.0f, 0.0f);
            tr.stamina = 40.0f * STAMINA_SCALE; tr.staminaDelay = 99.0f;
            Run(&tr, FWD, true, 1.5f);
            Check(tr.sprinting && PlayerSpeed(&tr) < PLAYER_SPRINT_SPEED - 0.5f, "a sprint slows as stamina is spent: a tired sprint is slower than a fresh one");
        }

        // Noise, which is the cost of running
        {
            PlayerController n = Place(20.0f, 4.0f, 0.0f);
            Run(&n, (Vector2){ 0, 0 }, false, 0.5f);
            const float still = PlayerNoise(&n);
            PlayerController w = Place(20.0f, 4.0f, 0.0f);
            Run(&w, FWD, false, 1.0f);
            const float walking = PlayerNoise(&w);
            PlayerController s = Place(20.0f, 4.0f, 0.0f);
            Run(&s, FWD, true, 1.0f);
            const float sprint = PlayerNoise(&s);
            PlayerController x = Place(20.0f, 4.0f, 0.0f);
            x.exhausted = true; x.stamina = 0.0f;
            const float gasp = PlayerNoise(&x);
            printf("    heard from: standing %.0f m, walking %.0f m, sprinting %.0f m, exhausted %.0f m\n", still, walking, sprint, gasp);
            Check(still > 2.0f && still < 4.0f, "a player standing still can be heard about 3 m away");
            Check(walking > still + 3.0f && walking < 10.0f, "a walking player further, up to about 8 m");
            Check(sprint >= walking * 2.0f, "a sprinting player at least twice as far as a walking one");
            Check(gasp > still + 4.0f, "and an exhausted player is loud even standing still");
        }
    }

    printf("Running does not outrun the enemies for long\n");
    {
        const float slowest = ENEMY_CHASE_SPEED_BASE;
        const float fastest = ENEMY_CHASE_SPEED_BASE + ENEMY_CHASE_SPEED_STEP * 3.0f;
        Check(fastest <= PLAYER_WALK_SPEED + 0.05f && slowest >= PLAYER_WALK_SPEED * 0.7f, "cylinder enemies chase from 0.8 up to a walking player's speed");
        Check(SPIDER_CHASE_SPEED > slowest && SPIDER_CHASE_SPEED < PLAYER_SPRINT_SPEED, "a spider is quicker than the slowest of them and slower than a sprint");
        Check(PLAYER_SPRINT_SPEED > fastest * 1.4f, "a fresh sprint is well quicker than any enemy: it buys a lead");
        // An exhausted player is slower than every enemy. Simulate holding sprint straight ahead with an enemy 8 m behind.
        PlayerController p = Place(20.0f, 6.0f, 0.0f);
        // The player is put back where they started after every step and the distance they covered is added up instead (a treadmill):
        // a full bar now lasts longer than any corridor on the map.
        float playerZ = p.position.z, enemyZ = -2.0f, caughtAt = -1.0f, minGap = 1e9f, maxGap = 0.0f;
        const float enemySpeed = fastest;
        for (float t = 0.0f; t < 90.0f; t += DT) {
            p.wantSprint = true;
            const float z0 = p.position.z;
            UpdatePlayerController(&p, FWD, (Vector2){ 0, 0 }, DT, &g_floor);
            playerZ += p.position.z - z0;
            p.position.z = z0;
            enemyZ += enemySpeed * DT;
            const float gap = playerZ - enemyZ;
            if (gap > maxGap) maxGap = gap;
            if (gap < minGap) minGap = gap;
            if (gap <= 0.5f && caughtAt < 0.0f) { caughtAt = t; break; }
        }
        printf("    holding sprint with the fastest cylinder enemy 8 m behind: the lead peaks at %.1f m; caught %s%.1f s\n", maxGap, caughtAt < 0.0f ? "not within 90 s (" : "at ", caughtAt);
        Check(maxGap > 8.0f + 8.0f, "the sprint opens a real lead on the fastest cylinder enemy: at least 8 m more than the 8 m it started with");
        Check(caughtAt > 15.0f && caughtAt < 40.0f, "but a player who keeps pushing on after the stamina runs out is still caught, after about 27 seconds (about 15 with the old bar), by an enemy that is no faster than a walk");
    }

    printf("Jumping\n");
    {
        PlayerController p = Place(20.0f, 4.0f, 0.0f);
        Check(PlayerTryJump(&p) && p.velocityY == PLAYER_JUMP_SPEED && !p.isGrounded, "a jump leaves the ground at full jump speed");
        Check(fabsf(p.stamina - (PLAYER_STAMINA_MAX - STAMINA_JUMP_COST)) < 1e-3f, "and costs stamina");
        Check(!PlayerTryJump(&p), "there is no jumping in the air");
        PlayerController w = Place(20.0f, 4.0f, 0.0f);
        w.exhausted = true; w.stamina = 0.0f;
        Check(PlayerTryJump(&w) && w.velocityY < PLAYER_JUMP_SPEED * 0.75f && w.velocityY > 0.0f, "an exhausted player manages only a weak hop");
        PlayerController l = Place(20.0f, 4.0f, 0.0f);
        l.stamina = STAMINA_JUMP_COST * 0.5f;
        Check(PlayerTryJump(&l) && l.velocityY < PLAYER_JUMP_SPEED * 0.75f && l.stamina == STAMINA_JUMP_COST * 0.5f, "so does one with less stamina than a jump costs, and it costs nothing more");
        PlayerController dead = Place(20.0f, 4.0f, 0.0f);
        dead.health.isAlive = false;
        Check(!PlayerTryJump(&dead), "the dead do not jump");
        // Air control: no sudden turns in the air.
        PlayerController a = Place(20.0f, 4.0f, 0.0f);
        Run(&a, FWD, false, 0.5f);
        PlayerTryJump(&a);
        Run(&a, (Vector2){ 0, 0 }, false, 0.1f);
        Check(PlayerSpeed(&a) > PLAYER_WALK_SPEED * 0.6f, "let go of the stick in the air and the player keeps most of their speed");
    }

    printf("Walls\n");
    {
        OpenFloor(12);                                 // a wall across the floor at tile column 12 (world x 23 to 25)
        PlayerController p = Place(14.0f, 40.0f, 1.5707963f);   // yaw 90 degrees: looks along +x
        Run(&p, FWD, true, 3.0f);
        Check(p.position.x < 23.0f - 0.40f && p.position.x > 23.0f - 0.60f, "running into a wall stops the player at the wall clearance");
        Check(fabsf(p.moveVel.x) < 1e-4f, "with no speed left into it");
        // Slide: push into the wall and along it.
        PlayerController s = Place(20.0f, 40.0f, 1.5707963f);
        Run(&s, FWD, false, 2.0f);                     // up against the wall
        const float z0 = s.position.z;
        Run(&s, (Vector2){ 1.0f, -1.0f }, false, 1.0f); // forward and right: along the wall one way
        Check(fabsf(s.position.z - z0) > 2.0f, "pushing diagonally into a wall slides along it");
        OpenFloor(-1);
    }

    printf("The view\n");
    {
        PlayerController p = Place(20.0f, 4.0f, 0.0f);
        Run(&p, (Vector2){ 0, 0 }, false, 0.5f);
        const float rest = p.camera.position.y;
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 90; ++i) { Run(&p, FWD, false, DT); if (p.camera.position.y < lo) lo = p.camera.position.y; if (p.camera.position.y > hi) hi = p.camera.position.y; }
        Check(hi - lo > 0.015f && hi - lo < 0.07f, "walking bobs the head by a few centimetres");
        PlayerController s = Place(20.0f, 4.0f, 0.0f);
        float slo = 1e9f, shi = -1e9f;
        for (int i = 0; i < 90; ++i) { Run(&s, (Vector2){ 0, 0 }, false, DT); if (s.camera.position.y < slo) slo = s.camera.position.y; if (s.camera.position.y > shi) shi = s.camera.position.y; }
        Check(shi - slo < 0.001f, "standing still does not");
        (void)rest;
        PlayerController e = Place(20.0f, 4.0f, 0.0f);
        e.exhausted = true; e.stamina = 0.0f;
        float elo = 1e9f, ehi = -1e9f;
        for (int i = 0; i < 90; ++i) { Run(&e, (Vector2){ 0, 0 }, false, DT); if (e.camera.position.y < elo) elo = e.camera.position.y; if (e.camera.position.y > ehi) ehi = e.camera.position.y; }
        Check(ehi - elo > 0.015f, "an exhausted player heaves for breath even standing still");
        PlayerController t = Place(20.0f, 4.0f, 0.0f);
        Run(&t, FWD, true, 1.0f);
        TeleportPlayer(&t, (Vector3){ 30.0f, 0.0f, 30.0f }, 0.0f);
        Check(PlayerSpeed(&t) == 0.0f, "a teleport stops the player dead");
        PlayerController n = Place(20.0f, 4.0f, 0.0f);
        Check(n.stamina == PLAYER_STAMINA_MAX && !n.exhausted && !n.sprinting, "a new player has full stamina");
    }

    printf("Sliding\n");
    {
        OpenFloor(-1);
        // It starts from a sprint, and only from one.
        PlayerController w = Place(20.0f, 4.0f, 0.0f);
        Run(&w, FWD, false, 1.0f);
        Check(!PlayerTrySlide(&w) && !w.sliding, "a walking player cannot slide");
        PlayerController idle = Place(20.0f, 4.0f, 0.0f);
        Run(&idle, (Vector2){ 0, 0 }, true, 0.5f);
        Check(!PlayerTrySlide(&idle), "nor can one standing still, whatever the run button says");

        PlayerController p = Place(20.0f, 4.0f, 0.0f);
        Run(&p, FWD, true, 0.7f);
        const float stamBefore = p.stamina, camBefore = p.camera.position.y;
        Check(p.sprinting && PlayerTrySlide(&p) && p.sliding, "a running player slides");
        Check(fabsf((stamBefore - p.stamina) - STAMINA_SLIDE_COST) < 1e-4f, "it costs 18 stamina, taken at the start");
        Check(!PlayerTrySlide(&p), "and cannot be started again while it is going");

        // Its speed: a bit faster than a sprint at first, running down to a crawl.
        Run(&p, FWD, true, DT);
        const float first = PlayerSpeed(&p);
        Check(first > PLAYER_SPRINT_SPEED * 1.12f && first < PLAYER_SPRINT_SPEED * PLAYER_SLIDE_SPEED_MUL + 0.01f, "it starts about a quarter faster than a sprint");
        float zAtStart = p.position.z - first * DT;       // where the slide began
        float zMid = 0.0f, midSpeed = 0.0f, camMid = 0.0f, noiseMid = 0.0f;
        for (float t = DT; t < 0.4f; t += DT) {
            p.wantSprint = true;
            UpdatePlayerController(&p, FWD, (Vector2){ 0, 0 }, DT, &g_floor);
            if (fabsf(t - 0.4f) < DT) { zMid = p.position.z; midSpeed = PlayerSpeed(&p); camMid = p.camera.position.y; noiseMid = PlayerNoise(&p); }
        }
        (void)zMid;
        midSpeed = PlayerSpeed(&p); camMid = p.camera.position.y; noiseMid = PlayerNoise(&p);
        Check(midSpeed < first - 1.5f && midSpeed > PLAYER_SLIDE_END_SPEED, "by 0.4 s it has slowed (not yet to its end speed)");
        Check(p.sliding && camBefore - camMid > PLAYER_SLIDE_DROP * 0.8f, "the camera has dropped about 25 cm");
        Check(fabsf(noiseMid - PLAYER_SLIDE_NOISE) < 0.01f && PlayerNoise(&idle) < PLAYER_SLIDE_NOISE, "a slide is heard at 14 m: less than a sprint's 20, more than walking");
        const float stamMid = p.stamina;
        int frames = 0;
        while (p.sliding && frames < 120) { p.wantSprint = true; UpdatePlayerController(&p, FWD, (Vector2){ 0, 0 }, DT, &g_floor); ++frames; }
        Check(!p.sliding && p.slideCooldown > 0.0f && p.slideT >= PLAYER_SLIDE_SECS - DT, "it is over after 0.85 s, and cooling down");
        Check(fabsf(p.stamina - stamMid) < 0.05f, "it cost nothing more, and gave no stamina back while it ran");
        const float dist = p.position.z - zAtStart;
        const float expect = PLAYER_SLIDE_END_SPEED * PLAYER_SLIDE_SECS + (PLAYER_SPRINT_SPEED * PLAYER_SLIDE_SPEED_MUL - PLAYER_SLIDE_END_SPEED) * PLAYER_SLIDE_SECS / 2.5f;
        printf("    slid %.2f m (the speed curve's integral is %.2f m)\n", (double)dist, (double)expect);
        Check(fabsf(dist - expect) < 0.35f, "it covers about five metres, as the speed curve says: a burst, not a way to travel");
        Run(&p, FWD, true, 0.15f);
        Check(PlayerSpeed(&p) > 3.0f && p.sprinting, "the player is still sprinting afterwards");
        Run(&p, FWD, true, 0.7f);                           // cooldown (0.45 s) over
        Check(PlayerTrySlide(&p), "after the cooldown another can start");
    }
    {
        // Camera returns after a slide.
        PlayerController p = Place(20.0f, 4.0f, 0.0f);
        Run(&p, FWD, true, 0.7f);
        const float eyeBefore = p.camera.position.y;
        PlayerTrySlide(&p);
        Run(&p, FWD, true, 0.5f);
        const float low = p.camera.position.y;
        Run(&p, (Vector2){ 0, 0 }, false, 1.0f);
        Check(low < eyeBefore - 0.15f && fabsf(p.camera.position.y - eyeBefore) < 0.04f, "the camera is down during the slide and back up shortly after it");
        Check(p.camera.fovy > 60.0f - 0.01f, "(the field of view is back to normal too)");
    }
    {
        // Conditions that refuse a slide.
        PlayerController t = Place(20.0f, 4.0f, 0.0f);
        Run(&t, FWD, true, 0.7f);
        t.stamina = STAMINA_SLIDE_COST - 1.0f;
        Check(!PlayerTrySlide(&t) && !t.sliding && t.stamina == STAMINA_SLIDE_COST - 1.0f, "with less than 18 stamina there is no slide, and nothing is spent");
        PlayerController e = Place(20.0f, 4.0f, 0.0f);
        Run(&e, FWD, true, 0.7f);
        e.exhausted = true;
        Check(!PlayerTrySlide(&e), "an exhausted player cannot slide");
        PlayerController a = Place(20.0f, 4.0f, 0.0f);
        Run(&a, FWD, true, 0.7f);
        PlayerTryJump(&a);
        Run(&a, FWD, true, DT * 3);
        Check(!a.isGrounded && !PlayerTrySlide(&a), "nor in the air");
        PlayerController d = Place(20.0f, 4.0f, 0.0f);
        Run(&d, FWD, true, 0.7f);
        d.health.isAlive = false;
        Check(!PlayerTrySlide(&d), "nor a dead one");
        PlayerController tired = Place(20.0f, 4.0f, 0.0f);
        Run(&tired, FWD, true, 0.7f);
        tired.stamina = 40.0f * STAMINA_SCALE;                              // tired: fatigue slows everything
        PlayerTrySlide(&tired);
        Run(&tired, FWD, true, DT);
        Check(PlayerSpeed(&tired) < PLAYER_SPRINT_SPEED * PLAYER_SLIDE_SPEED_MUL * 0.95f, "a tired player's slide starts slower than a fresh one's");
    }
    {
        // Ends early on a wall, on a jump; steers; does not depend on the frame rate.
        OpenFloor(25);                                      // a wall column at world x = 50 (its face at 49)
        PlayerController wl = Place(44.0f, 20.0f, 1.5708f);  // yaw pi/2 looks along +x
        Run(&wl, FWD, true, 0.55f);
        PlayerTrySlide(&wl);
        Run(&wl, FWD, true, 0.6f);
        Check(!wl.sliding && wl.slideT < 0.6f && wl.position.x < 49.0f, "a slide that runs into a wall ends there, and does not go through it");
        OpenFloor(-1);

        PlayerController j = Place(20.0f, 4.0f, 0.0f);
        Run(&j, FWD, true, 0.7f);
        PlayerTrySlide(&j);
        Run(&j, FWD, true, 0.2f);
        const float before = PlayerSpeed(&j);
        Check(PlayerTryJump(&j) && !j.sliding && j.slideCooldown > 0.0f, "jumping out of a slide is allowed and ends it");
        Run(&j, FWD, true, 0.1f);
        Check(PlayerSpeed(&j) > before * 0.6f, "the speed is kept into the jump");

        PlayerController st = Place(20.0f, 4.0f, 0.0f);
        Run(&st, FWD, true, 0.7f);
        PlayerTrySlide(&st);
        const float h0 = atan2f(st.slideDir.x, st.slideDir.z);
        for (float t = 0; t < 0.5f; t += DT) { st.wantSprint = true; UpdatePlayerController(&st, (Vector2){ 1.0f, -0.1f }, (Vector2){ 0, 0 }, DT, &g_floor); }
        const float turned = fabsf(atan2f(st.slideDir.x, st.slideDir.z) - h0);
        Check(turned > 0.45f && turned < 0.75f, "holding the stick sideways turns a slide (about 1.4 radians a second, so about 0.7 in half a second)");

        float distFor[3];
        const float dts[3] = { 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f };
        for (int k = 0; k < 3; ++k) {
            PlayerController q = Place(20.0f, 4.0f, 0.0f);
            for (float t = 0; t < 0.7f; t += 1.0f / 60.0f) { q.wantSprint = true; UpdatePlayerController(&q, FWD, (Vector2){ 0, 0 }, 1.0f / 60.0f, &g_floor); }
            PlayerTrySlide(&q);
            const float z0 = q.position.z;
            for (float t = 0; t < 1.0f; t += dts[k]) { q.wantSprint = true; UpdatePlayerController(&q, FWD, (Vector2){ 0, 0 }, dts[k], &g_floor); }
            distFor[k] = q.position.z - z0;
        }
        Check(fabsf(distFor[0] - distFor[2]) < 0.6f && fabsf(distFor[1] - distFor[2]) < 0.4f, "the distance a slide covers hardly depends on the frame rate");
    }

    printf("A slide goes forward only (left, right and back are for the dive)\n");
    {
        OpenFloor(-1);
        PlayerController f = Place(20.0f, 40.0f, 0.0f);
        Run(&f, FWD, true, 0.7f);
        Check(PlayerSprintingForward(&f) && PlayerTrySlide(&f), "a forward sprint can slide");
        PlayerController dg = Place(20.0f, 40.0f, 0.0f);
        Run(&dg, (Vector2){ 0.707f, -0.707f }, true, 0.7f);
        Check(PlayerSprintingForward(&dg) && PlayerTrySlide(&dg), "so can one forward and to the side (within 45 degrees of ahead)");
        Check(dg.slideSpeed0 < f.slideSpeed0, "and the diagonal one starts a little slower, as that sprint was");
        PlayerController sd = Place(20.0f, 40.0f, 0.0f);
        Run(&sd, (Vector2){ 1.0f, 0.0f }, true, 0.7f);
        Check(sd.sprinting && !PlayerSprintingForward(&sd) && !PlayerTrySlide(&sd) && !sd.sliding && sd.stamina < PLAYER_STAMINA_MAX, "a sideways sprint cannot slide (and nothing is taken for the refusal)");
        PlayerController bk = Place(20.0f, 40.0f, 0.0f);
        Run(&bk, (Vector2){ 0.0f, 1.0f }, true, 0.7f);
        Check(bk.sprinting && !PlayerTrySlide(&bk) && !bk.sliding, "nor a backwards one");
        PlayerController hd = Place(20.0f, 40.0f, 0.0f);
        Run(&hd, (Vector2){ 1.0f, -0.5f }, true, 0.7f);
        Check(!PlayerTrySlide(&hd), "nor one that is more sideways than forward");
        const float st = sd.stamina;
        PlayerTrySlide(&sd);
        Check(sd.stamina == st, "a refused slide costs nothing");
    }

    printf("Aiming down sights\n");
    {
        OpenFloor(-1);
        PlayerController hip = Place(20.0f, 40.0f, 0.0f);
        Run(&hip, FWD, false, 1.0f);
        const float hipSpeed = PlayerSpeed(&hip);

        PlayerController p = Place(20.0f, 40.0f, 0.0f);
        p.adsFov = 45.0f; p.adsSecs = 0.20f; p.adsMoveMul = 0.60f;
        Run(&p, (Vector2){ 0, 0 }, false, 0.3f);
        Check(p.aimBlend == 0.0f && fabsf(p.camera.fovy - 60.0f) < 0.01f, "without wanting it there is no ADS");
        p.wantAim = true;
        Run(&p, (Vector2){ 0, 0 }, false, 0.1f);
        Check(p.aimBlend > 0.4f && p.aimBlend < 0.6f, "halfway through its time it is halfway in (adsSecs is the whole way in)");
        Run(&p, (Vector2){ 0, 0 }, false, 0.3f);
        Check(p.aimBlend == 1.0f && fabsf(p.camera.fovy - 45.0f) < 0.01f, "then fully in, at the weapon's field of view");
        Run(&p, FWD, false, 1.0f);
        Check(fabsf(PlayerSpeed(&p) - hipSpeed * 0.60f) < 0.05f, "walking aimed down sights is at the weapon's move share of the walking speed");
        Check(PlayerAimAmount(&p) == 1.0f && PlayerAimAmount(&hip) == 0.0f, "PlayerAimAmount is 0 at the hip and 1 when in");
        {
            PlayerController q = p;
            q.aimBlend = 0.5f;
            Check(fabsf(PlayerAimAmount(&q) - 0.5f) < 1e-4f && PlayerViewFov(&q, 0.5f) > 45.0f && PlayerViewFov(&q, 0.5f) < 60.0f, "half way in (smoothstep 0.5) the view is between the two fields of view");
        }

        const float yaw0 = p.yaw;
        PlayerApplyLook(&p, (Vector2){ 100.0f, 0.0f });
        const float ratio = tanf(45.0f * 3.14159265f / 360.0f) / tanf(30.0f * 3.14159265f / 180.0f);
        Check(fabsf((yaw0 - p.yaw) - 100.0f * 0.005f * ratio) < 1e-4f && ratio < 0.8f, "zoomed in, the same swipe turns less, by the ratio of the two fields of view");
        PlayerController h2 = Place(20.0f, 40.0f, 0.0f);
        PlayerApplyLook(&h2, (Vector2){ 100.0f, 0.0f });
        Check(fabsf(-h2.yaw - 0.5f) < 1e-4f, "and at the hip it is unchanged");

        PlayerController sp = Place(20.0f, 40.0f, 0.0f);
        sp.adsFov = 45.0f; sp.adsMoveMul = 0.60f;
        Run(&sp, FWD, true, 0.8f);
        Check(sp.sprinting, "(sprinting works at the hip)");
        sp.wantAim = true;
        Run(&sp, FWD, true, DT);
        Check(!sp.sprinting, "wanting ADS ends the sprint at once");
        Run(&sp, FWD, true, 1.5f);
        Check(!sp.sprinting && PlayerSpeed(&sp) < PLAYER_WALK_SPEED, "and there is no sprinting while aimed, even with the stick at the rim");
        sp.wantAim = false;
        Run(&sp, FWD, true, 1.0f);
        Check(sp.sprinting && sp.aimBlend == 0.0f && fabsf(sp.camera.fovy - (60.0f + SPRINT_FOV_KICK * sp.sprintBlend)) < 0.01f, "let go: the view comes back out, and the sprint carries on");

        PlayerController out = Place(20.0f, 40.0f, 0.0f);
        out.adsFov = 45.0f; out.adsSecs = 0.20f; out.wantAim = true;
        Run(&out, (Vector2){ 0, 0 }, false, 0.4f);
        out.wantAim = false;
        Run(&out, (Vector2){ 0, 0 }, false, 0.1f);
        Check(out.aimBlend > 0.0f && out.aimBlend < 0.5f, "coming out is quicker than going in (0.6 of the time) but is not instant");
        Run(&out, (Vector2){ 0, 0 }, false, 0.2f);
        Check(out.aimBlend == 0.0f, "and finishes");

        PlayerController nz = Place(20.0f, 40.0f, 0.0f);
        nz.wantAim = true;
        Run(&nz, FWD, false, 1.0f);
        Check(fabsf(nz.camera.fovy - 60.0f) < 0.01f && fabsf(PlayerSpeed(&nz) - hipSpeed) < 0.05f, "a weapon with no zoom or slowing set (adsFov 0, move 0) changes neither the view nor the speed");

        PlayerController dead = Place(20.0f, 40.0f, 0.0f);
        dead.adsFov = 45.0f; dead.wantAim = true;
        dead.health.isAlive = false;
        Run(&dead, (Vector2){ 0, 0 }, false, 0.5f);
        Check(dead.aimBlend == 0.0f, "a dead player does not aim");
    }

    printf("Leaning: automatic, at the corner of a wall, toward the opening\n");
    {
        // A slab of wall, x 23 to 25 and z 19 to 39, with open floor on both ends of it. A player at x 21.8 facing +x (yaw 90 degrees) has the
        // slab's face 1.2 m ahead; right is +z. Near z = 39 the eye, leaned right, is past the slab's end and sees far down the opening.
        CornerFloor();
        const float YAW_E = 1.5708f;
        PlayerController c = Place(21.8f, 38.8f, YAW_E);
        c.leanAllowed = true;
        Run(&c, (Vector2){ 0, 0 }, false, DT);
        Check(c.leanDir == 0 && c.leanBlend == 0.0f, "the first step at the corner does nothing: a lean waits a moment (PLAYER_LEAN_DWELL), so walking past is not a peek");
        Run(&c, (Vector2){ 0, 0 }, false, PLAYER_LEAN_DWELL + 0.05f);
        Check(c.leanDir == 1 && c.leanBlend > 0.0f, "a moment later the player leans toward the opening, which is on the right, with no button");
        const float y0 = Place(21.8f, 38.8f, YAW_E).camera.position.y;
        Run(&c, (Vector2){ 0, 0 }, false, 0.4f);
        Check(c.leanBlend == 1.0f, "and it goes to full");
        Check(fabsf(c.camera.position.z - (38.8f + PLAYER_LEAN_SHIFT)) < 0.01f && fabsf(c.camera.position.x - 21.8f) < 0.01f, "the eye moves sideways toward the opening by the lean distance, and not forward or back");
        Check(c.camera.position.y < y0 - PLAYER_LEAN_DIP * 0.9f, "and dips a little");
        Check(fabsf(c.position.z - 38.8f) < 1e-4f && fabsf(c.position.x - 21.8f) < 1e-4f, "the body does not move");
        Check(c.camera.up.z > 0.1f && fabsf(c.camera.up.y) > 0.9f, "the view tilts toward the lean (the top of the view leans over to the right)");
        Check(c.leanRoom[1] >= PLAYER_LEAN_SHIFT - 1e-4f && c.camera.position.z - 38.8f <= c.leanRoom[1] + 1e-4f, "the eye was within the room it had");

        PlayerController l = Place(21.8f, 19.2f, YAW_E);      // the slab's other end: the opening is on the left
        l.leanAllowed = true;
        Run(&l, (Vector2){ 0, 0 }, false, 0.5f);
        Check(l.leanDir == -1 && l.leanBlend == -1.0f && l.camera.position.z < 19.2f - PLAYER_LEAN_SHIFT * 0.9f && l.camera.up.z < -0.1f, "at the other end the opening is on the left and the lean goes left");

        PlayerController fist = Place(21.8f, 38.8f, YAW_E);   // no gun in hand: the rules leave leanAllowed off
        Run(&fist, (Vector2){ 0, 0 }, false, 0.5f);
        Check(fist.leanDir == 0 && fist.leanBlend == 0.0f && fist.camera.up.y == 1.0f, "with no gun in hand there is no lean");

        PlayerController flat = Place(21.8f, 30.0f, YAW_E);   // the middle of the slab: a flat wall
        flat.leanAllowed = true;
        Run(&flat, (Vector2){ 0, 0 }, false, 0.5f);
        Check(flat.leanDir == 0 && flat.leanBlend == 0.0f, "facing a flat wall nothing is revealed by leaning, so there is no lean");

        PlayerController away = Place(15.0f, 38.8f, YAW_E);   // the same corner from 8 m away
        away.leanAllowed = true;
        Run(&away, (Vector2){ 0, 0 }, false, 0.5f);
        Check(away.leanDir == 0 && away.leanBlend == 0.0f, "from far away, not close to the wall, there is no lean");

        PlayerController along = Place(21.8f, 38.8f, 0.0f);   // facing along the wall (+z), nothing ahead
        along.leanAllowed = true;
        Run(&along, (Vector2){ 0, 0 }, false, 0.5f);
        Check(along.leanDir == 0, "looking along the wall, with nothing close ahead, there is no lean");

        // It lets go when the opening is gone, or the player turns away.
        Run(&c, (Vector2){ 0, 0 }, false, 0.1f);
        c.position.z = 36.0f;                                 // back along the slab: the leaned eye no longer clears its end
        Run(&c, (Vector2){ 0, 0 }, false, 0.4f);
        Check(c.leanDir == 0 && c.leanBlend == 0.0f && c.camera.up.y == 1.0f, "moving along the wall until the opening is out of reach lets the lean go, and the view comes upright");
        c.position.z = 38.8f;
        Run(&c, (Vector2){ 0, 0 }, false, 0.5f);
        Check(c.leanDir == 1 && c.leanBlend == 1.0f, "and back at the corner it comes again");
        c.yaw = 0.0f;
        Run(&c, (Vector2){ 0, 0 }, false, 0.4f);
        Check(c.leanDir == 0 && c.leanBlend == 0.0f, "turning to face along the wall lets it go");

        // Hysteresis: a lean that has started is kept while its opening is still worth seeing (the middle of the band between ON and OFF).
        // (The sight lines are straight, so the opening grows or goes in steps of the leaned eye's position: here a short move that keeps it.)
        PlayerController h = Place(21.8f, 38.8f, YAW_E);
        h.leanAllowed = true;
        Run(&h, (Vector2){ 0, 0 }, false, 0.5f);
        h.position.x = 21.8f - 0.4f;                           // back a little: the wall is 1.6 m ahead, further than it takes to start (1.5) and nearer than it takes to let go (2.0)
        Run(&h, (Vector2){ 0, 0 }, false, 0.3f);
        Check(h.leanDir == 1 && h.leanBlend == 1.0f, "a lean that has started is held a little beyond the distance it took to start");
        PlayerController h2 = Place(21.8f - 0.4f, 38.8f, YAW_E);
        h2.leanAllowed = true;
        Run(&h2, (Vector2){ 0, 0 }, false, 0.5f);
        Check(h2.leanDir == 0, "but one that has not started does not start from there");

        // No room, no lean: a second wall piece beside the player leaves the leaned eye nowhere to go.
        g_floor.grid[20][11].type = TILE_WALL;                // x 21 to 23, z 39 to 41: right beside the opening
        PlayerController nr = Place(21.8f, 38.8f, YAW_E);
        nr.leanAllowed = true;
        Run(&nr, (Vector2){ 0, 0 }, false, 0.5f);
        Check(nr.leanDir == 0 && nr.leanBlend == 0.0f, "when a wall right beside the player leaves the eye no room to lean, it does not");
        CornerFloor();

        // The eye is never in a wall, even from right up against one.
        PlayerController up = Place(21.8f, 38.8f, YAW_E);
        up.leanAllowed = true;
        Run(&up, (Vector2){ 0, 0 }, false, 0.6f);
        {
            float worst = 99.0f;
            for (int k = 0; k < 20; ++k) {
                const float ex = up.camera.position.x, ez = up.camera.position.z;
                const float h2d = TILE_SIZE * 0.5f; float best = 99.0f;
                for (int tz = 0; tz < MAP_HEIGHT; ++tz) for (int tx = 0; tx < MAP_WIDTH; ++tx) {
                    if (g_floor.grid[tz][tx].type != TILE_WALL) continue;
                    const float cx = (float)tx * TILE_SIZE, cz = (float)tz * TILE_SIZE;
                    if (fabsf(cx - ex) > 3.0f || fabsf(cz - ez) > 3.0f) continue;
                    const float px = fminf(fmaxf(ex, cx - h2d), cx + h2d), pz = fminf(fmaxf(ez, cz - h2d), cz + h2d);
                    const float d = sqrtf((ex - px) * (ex - px) + (ez - pz) * (ez - pz));
                    if (d < best) best = d;
                }
                if (best < worst) worst = best;
                Run(&up, (Vector2){ 0, 0 }, false, DT);
            }
            Check(worst >= PLAYER_LEAN_CAM_CLEAR - 0.011f, "the leaned eye stays at least PLAYER_LEAN_CAM_CLEAR from every wall");
        }

        // Sprinting, sliding, diving and the air all end or refuse it.
        PlayerController sp = Place(21.8f, 38.8f, YAW_E);
        sp.leanAllowed = true;
        Run(&sp, (Vector2){ 0, 0 }, false, 0.5f);
        Check(sp.leanBlend == 1.0f, "(leaning at the corner)");
        Run(&sp, (Vector2){ 0, 1.0f }, true, 0.6f);          // sprint, backwards, away from the wall
        Check(sp.leanDir == 0 && sp.leanBlend == 0.0f && sp.sprinting, "a sprint cancels the lean at once, and then the sprint is not held back by it");
        PlayerController air = Place(21.8f, 38.8f, YAW_E);
        air.leanAllowed = true;
        Run(&air, (Vector2){ 0, 0 }, false, 0.5f);
        PlayerTryJump(&air);
        Run(&air, (Vector2){ 0, 0 }, false, 0.3f);
        Check(!air.isGrounded && air.leanDir == 0 && air.leanBlend == 0.0f, "no leaning in the air: a jump lets it go");
        PlayerController gone = Place(21.8f, 38.8f, YAW_E);
        gone.leanAllowed = true;
        Run(&gone, (Vector2){ 0, 0 }, false, 0.5f);
        TeleportPlayer(&gone, (Vector3){ 20.0f, 0.0f, 40.0f }, 0.0f);
        Check(gone.leanBlend == 0.0f && gone.leanDir == 0 && gone.camera.up.y == 1.0f, "a teleport (a new floor) starts upright");
        PlayerController dead = Place(21.8f, 38.8f, YAW_E);
        dead.leanAllowed = true;
        dead.health.isAlive = false;
        Run(&dead, (Vector2){ 0, 0 }, false, 0.5f);
        Check(dead.leanBlend == 0.0f, "a dead player does not lean");
        PlayerController gunAway = Place(21.8f, 38.8f, YAW_E);
        gunAway.leanAllowed = true;
        Run(&gunAway, (Vector2){ 0, 0 }, false, 0.5f);
        gunAway.leanAllowed = false;                          // the gun is put away
        Run(&gunAway, (Vector2){ 0, 0 }, false, 0.4f);
        Check(gunAway.leanDir == 0 && gunAway.leanBlend == 0.0f, "putting the gun away lets it go");
        OpenFloor(-1);
    }

    printf("Diving: from the air after a sprinting jump, any direction, a fixed arc, then a roll\n");
    {
        OpenFloor(-1);
        // A sprinting player jumps; secs later (in the air) the dive is asked for with the stick given.
        PlayerController base = Place(20.0f, 20.0f, 0.0f);
        Run(&base, FWD, true, 0.8f);
        Check(base.sprinting && !PlayerDiveAvailable(&base), "no dive on the ground");
        PlayerTryJump(&base);
        Check(base.jumpedSprinting && PlayerDiveAvailable(&base), "the moment the feet leave the floor in a sprinting jump the dive is available");
        Run(&base, FWD, false, 0.15f);
        Check(!base.isGrounded && PlayerDiveAvailable(&base), "in the air after a sprinting jump the dive is available");

        PlayerController walk = Place(20.0f, 20.0f, 0.0f);
        Run(&walk, FWD, false, 1.0f);
        PlayerTryJump(&walk); Run(&walk, FWD, false, 0.15f);
        Check(!walk.isGrounded && !PlayerDiveAvailable(&walk) && !PlayerTryDive(&walk, FWD), "after a walking jump there is no dive: it is run, jump, dive");

        // Directions. yaw 0 faces +z; right is -x. Each dive is measured from the press to the landing.
        struct { const char *name; Vector2 stick; float dx, dz; } dirs[] = {
            { "forward", { 0.0f, -1.0f }, 0.0f, 1.0f }, { "centred (forward)", { 0.0f, 0.0f }, 0.0f, 1.0f },
            { "right", { 1.0f, 0.0f }, -1.0f, 0.0f }, { "left", { -1.0f, 0.0f }, 1.0f, 0.0f }, { "back", { 0.0f, 1.0f }, 0.0f, -1.0f },
            { "forward-right", { 0.707f, -0.707f }, -0.707f, 0.707f },
        };
        for (int i = 0; i < 6; ++i) {
            PlayerController p = Place(40.0f, 40.0f, 0.0f);
            Run(&p, FWD, true, 0.8f);
            PlayerTryJump(&p); Run(&p, FWD, false, 0.15f);
            const float sBefore = p.stamina;
            const bool started = PlayerTryDive(&p, dirs[i].stick);
            const float x0 = p.position.x, z0 = p.position.z;
            float t = 0.0f;
            while (p.diving && t < 2.0f) { Run(&p, (Vector2){ 0, 0 }, true, DT); t += DT; }
            const float dx = p.position.x - x0, dz = p.position.z - z0, dist = sqrtf(dx * dx + dz * dz);
            char msg[160];
            snprintf(msg, sizeof msg, "dive %s: started, %.2f m in %.2f s, the right way, and it costs %.0f stamina", dirs[i].name, (double)dist, (double)t, (double)STAMINA_DIVE_COST);
            Check(started && fabsf(dist - PLAYER_DIVE_DIST) < 0.2f && fabsf(t - PLAYER_DIVE_SECS) < 0.06f &&
                  fabsf(dx - dirs[i].dx * dist) < 0.25f && fabsf(dz - dirs[i].dz * dist) < 0.25f && p.rolling && fabsf((sBefore - p.stamina) - STAMINA_DIVE_COST) < 1.0f, msg);
        }

        // The arc is the same wherever in the jump it was asked.
        float airFor[3];
        const float asked[3] = { 0.05f, 0.25f, 0.55f };
        for (int i = 0; i < 3; ++i) {
            PlayerController p = Place(40.0f, 20.0f, 0.0f);
            Run(&p, FWD, true, 0.8f);
            PlayerTryJump(&p); Run(&p, FWD, false, asked[i]);
            PlayerTryDive(&p, FWD);
            float t = 0.0f;
            while (p.diving && t < 2.0f) { Run(&p, (Vector2){ 0, 0 }, false, DT); t += DT; }
            airFor[i] = t;
        }
        Check(fabsf(airFor[0] - PLAYER_DIVE_SECS) < 0.07f && fabsf(airFor[1] - PLAYER_DIVE_SECS) < 0.07f && fabsf(airFor[2] - PLAYER_DIVE_SECS) < 0.07f,
              "whether asked just after take-off, near the top or on the way down, the dive lands after the same time (a committed arc)");

        // Refusals.
        PlayerController tired = Place(40.0f, 20.0f, 0.0f);
        Run(&tired, FWD, true, 0.8f); PlayerTryJump(&tired); Run(&tired, FWD, false, 0.15f);
        tired.stamina = STAMINA_DIVE_COST - 1.0f;
        Check(PlayerDiveAvailable(&tired) && !PlayerTryDive(&tired, FWD) && !tired.diving && tired.stamina == STAMINA_DIVE_COST - 1.0f, "with too little stamina the dive is refused and costs nothing (the button is still there, shown tired)");
        tired.stamina = 100.0f; tired.exhausted = true;
        Check(!PlayerTryDive(&tired, FWD), "and an exhausted player cannot dive");
        PlayerController dead = Place(40.0f, 20.0f, 0.0f);
        Run(&dead, FWD, true, 0.8f); PlayerTryJump(&dead); Run(&dead, FWD, false, 0.15f);
        dead.health.isAlive = false;
        Check(!PlayerDiveAvailable(&dead) && !PlayerTryDive(&dead, FWD), "a dead player does not dive");

        // In the dive: committed, no sprint, no slide, no jump, a dropped camera, loud, and it can be aimed.
        PlayerController d = Place(40.0f, 20.0f, 0.0f);
        Run(&d, FWD, true, 0.8f); PlayerTryJump(&d); Run(&d, FWD, false, 0.15f);
        const float standEye = Place(40.0f, 20.0f, 0.0f).camera.position.y;      // the eye above the feet, standing
        PlayerTryDive(&d, (Vector2){ 1.0f, 0.0f });             // right: -x
        d.wantAim = true; d.adsFov = 45.0f; d.adsMoveMul = 0.5f;
        Check(!PlayerDiveAvailable(&d) && !PlayerTryDive(&d, FWD), "once it has started it cannot be asked for again");
        Run(&d, (Vector2){ 0.0f, 1.0f }, true, 0.2f);            // the stick pulled the other way, sprint held
        Check(d.moveVel.x < -5.5f && fabsf(d.moveVel.z) < 0.2f, "the stick does not steer a dive (it was asked for to the right and stays to the right)");
        Check(!d.sprinting && !PlayerTrySlide(&d) && !PlayerTryJump(&d), "no sprint, no slide and no jump while it lasts");
        Check(d.aimBlend > 0.3f, "and aiming down the sights works during it (the gun's numbers slow nothing: the dive is committed)");
        Check((d.camera.position.y - d.position.y) < standEye - PLAYER_DIVE_DROP * 0.5f && d.diveBlend > 0.5f, "the camera drops (the eye is well below where it is standing, measured from the feet)");
        Check(PlayerNoise(&d) >= PLAYER_DIVE_NOISE, "and it is heard from PLAYER_DIVE_NOISE metres");
        if (PLAYER_DIVE_GRACE_SECS <= 0.0f) Check(DamagePlayer(&d, 1.0f, (Vector3){ 0, 0, 0 }), "a dive does not make the player harder to hurt (Call of Duty gives no such window; PLAYER_DIVE_GRACE_SECS is 0)");
        else Check(!DamagePlayer(&d, 1.0f, (Vector3){ 0, 0, 0 }), "a dive protects for PLAYER_DIVE_GRACE_SECS");

        // The roll.
        PlayerController r = Place(40.0f, 20.0f, 0.0f);
        Run(&r, FWD, true, 0.8f); PlayerTryJump(&r); Run(&r, FWD, false, 0.15f);
        PlayerTryDive(&r, FWD);
        float t = 0.0f;
        while (r.diving && t < 2.0f) { Run(&r, (Vector2){ 0, 0 }, true, DT); t += DT; }
        Check(r.rolling && !r.diving && r.isGrounded, "the dive lands into a roll");
        const float rollSpeed0 = PlayerSpeed(&r);
        Run(&r, (Vector2){ 0, 0 }, true, DT * 2.0f);
        Check(PlayerSpeed(&r) < PLAYER_DIVE_DIST / PLAYER_DIVE_SECS - 1.5f && PlayerSpeed(&r) > PLAYER_ROLL_END_SPEED && PlayerSpeed(&r) <= PLAYER_ROLL_SPEED + 0.1f, "the roll is slower than the dive and runs down");
        (void)rollSpeed0;
        Check(!r.sprinting && !PlayerTrySlide(&r) && !PlayerTryJump(&r), "no sprint, slide or jump in the roll either, even with sprint held");
        Run(&r, (Vector2){ 0, 0 }, true, PLAYER_ROLL_SECS);
        Check(!r.rolling && !r.diving, "the roll ends after PLAYER_ROLL_SECS");
        Run(&r, (Vector2){ 0, 0 }, false, 0.5f);
        Check(r.diveBlend < 0.01f && !r.jumpedSprinting && !PlayerDiveAvailable(&r), "the camera comes back up, and a dive cannot be asked for on the ground (the next one needs a new sprinting jump)");
        Run(&r, FWD, true, 1.0f);
        Check(r.sprinting && PlayerSpeed(&r) > 7.0f, "after the roll the player is in control again and can sprint");

        // A slide into a jump counts as a sprinting jump.
        PlayerController sj = Place(40.0f, 20.0f, 0.0f);
        Run(&sj, FWD, true, 0.7f);
        PlayerTrySlide(&sj);
        Run(&sj, FWD, true, 0.2f);
        PlayerTryJump(&sj); Run(&sj, FWD, false, 0.15f);
        Check(!sj.isGrounded && PlayerDiveAvailable(&sj), "jumping out of a slide can be followed by a dive");

        // Walls stop a dive like any other move.
        OpenFloor(23);                                        // a wall column at x 45 to 47
        PlayerController w = Place(41.0f, 40.0f, 1.5708f);    // facing +x
        Run(&w, FWD, true, 0.6f);
        PlayerTryJump(&w); Run(&w, FWD, false, 0.15f);
        PlayerTryDive(&w, FWD);
        Run(&w, (Vector2){ 0, 0 }, false, 1.0f);
        Check(w.position.x < 45.0f - 0.3f, "a dive into a wall stops at it");
        OpenFloor(-1);

        PlayerController tp = Place(40.0f, 20.0f, 0.0f);
        Run(&tp, FWD, true, 0.8f); PlayerTryJump(&tp); Run(&tp, FWD, false, 0.15f); PlayerTryDive(&tp, FWD);
        TeleportPlayer(&tp, (Vector3){ 20.0f, 0.0f, 40.0f }, 0.0f);
        Check(!tp.diving && !tp.rolling && tp.diveBlend == 0.0f && !tp.jumpedSprinting, "a teleport (a new floor) ends it");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
