// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/player.md, section "movement_headless.c"
// ============================================================================
// Velocity based movement, sprinting and stamina: speeding up and slowing down, the speed caps, what a sprint costs,
// exhaustion and how it ends, the jump cost, sliding along a wall, the view effects. Links the real player
// controller and stairs and stubs the draw calls. Inside a macro so mray's build, which compiles every .c file,
// does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DMOVEMENT_HEADLESS_TEST -Isrc -Iraylib/src tests/movement_headless.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o movement_headless
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
        Check(fabsf(PlayerSpeed(&d) - PLAYER_WALK_SPEED) < 0.02f, "walking diagonally is not faster than walking straight");
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

        PlayerController s = Place(20.0f, 40.0f, 0.0f);
        Run(&s, (Vector2){ 1.0f, 0.0f }, true, 1.0f);
        Check(!s.sprinting && PlayerSpeed(&s) <= PLAYER_WALK_SPEED + 0.02f, "there is no sprinting sideways");
        PlayerController r = Place(20.0f, 40.0f, 0.0f);
        Run(&r, (Vector2){ 0.0f, 1.0f }, true, 1.0f);
        Check(!r.sprinting && PlayerSpeed(&r) <= PLAYER_WALK_SPEED + 0.02f, "or backwards");
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
        Check(t > 5.0f && t < 6.5f, "a full stamina bar is about six seconds of sprinting");
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
        Check(rt > 3.0f && rt < 5.5f, "an exhausted player is back on their feet after a few seconds' rest");
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
                tr.stamina = (float)st;
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
            printf("    walking speed by stamina: 100 -> %.2f, 60 -> %.2f, 30 -> %.2f, 0 -> %.2f m/s (largest step per 5 stamina %.2f)\n", at100, at60, at30, at1, worstStep);
            Check(fabsf(at100 - PLAYER_WALK_SPEED) < 0.02f && fabsf(at60 - PLAYER_WALK_SPEED) < 0.02f, "with 60 stamina or more there is no fatigue");
            Check(at30 < at60 - 0.3f && at30 > at1 + 0.3f, "below that the player slows more and more as stamina falls");
            Check(fabsf(at1 - PLAYER_WALK_SPEED * FATIGUE_MIN_MUL * EXHAUSTED_EXTRA_MUL) < 0.05f || fabsf(at1 - PLAYER_WALK_SPEED * FATIGUE_MIN_MUL) < 0.05f, "and at zero is down to the floor of the fatigue curve");
            Check(worstStep < 0.5f, "the slowdown is gradual: no step of 5 stamina changes the speed by half a metre a second or more, and it never speeds up as stamina falls");
        }
        {
            PlayerController tr = Place(20.0f, 4.0f, 0.0f);
            tr.stamina = 40.0f; tr.staminaDelay = 99.0f;
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
        float enemyZ = -2.0f, caughtAt = -1.0f, minGap = 1e9f, maxGap = 0.0f;
        const float enemySpeed = fastest;
        for (float t = 0.0f; t < 40.0f && p.position.z < 80.0f; t += DT) {
            p.wantSprint = true;
            UpdatePlayerController(&p, FWD, (Vector2){ 0, 0 }, DT, &g_floor);
            enemyZ += enemySpeed * DT;
            const float gap = p.position.z - enemyZ;
            if (gap > maxGap) maxGap = gap;
            if (gap < minGap) minGap = gap;
            if (gap <= 0.5f && caughtAt < 0.0f) { caughtAt = t; break; }
        }
        printf("    holding sprint with the fastest cylinder enemy 8 m behind: the lead peaks at %.1f m; caught %s%.1f s\n", maxGap, caughtAt < 0.0f ? "not before the map ends (" : "at ", caughtAt);
        Check(maxGap > 8.0f + 8.0f, "the sprint opens a real lead on the fastest cylinder enemy: at least 8 m more than the 8 m it started with");
        Check(caughtAt > 8.0f && caughtAt < 25.0f, "but a player who keeps pushing on after the stamina runs out is caught, within about 15 to 25 seconds, by an enemy that is no faster than a walk");
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
        tired.stamina = 40.0f;                              // tired: fatigue slows everything
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

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
