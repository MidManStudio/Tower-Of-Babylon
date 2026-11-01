// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "player_controller.c"
// ============================================================================
#include "player_controller.h"
#include "../obstacles/obstacles.h"
#include "../dungeon/staircase.h"
#include "raymath.h"
#include <math.h>

#define STAIR_CLEARANCE_RADIUS 0.30f   // how close the body may come to a stair block or railing
#define WALL_CLEARANCE         0.45f   // how close the body may come to a wall face, well outside the 0.1 m near plane
#define EYE_FOLLOW_RATE        16.0f
#define FLASH_DECAY            3.0f    // per second
#define SHAKE_DECAY            4.0f    // per second
#define SHAKE_YAW              0.035f  // radians at full shake
#define SHAKE_PITCH            0.025f

// Distance from (x, z) to the nearest wall tile, looking no further than reach.
// Tiles outside the map count as walls.
static float NearestWallDist(const FloorMap *map, float x, float z, float reach) {
    const float h = TILE_SIZE * 0.5f;
    const int x0 = (int)floorf((x - reach + h) / TILE_SIZE);
    const int x1 = (int)floorf((x + reach + h) / TILE_SIZE);
    const int z0 = (int)floorf((z - reach + h) / TILE_SIZE);
    const int z1 = (int)floorf((z + reach + h) / TILE_SIZE);
    float best = reach;

    for (int tz = z0; tz <= z1; ++tz) {
        for (int tx = x0; tx <= x1; ++tx) {
            const bool inside = tx >= 0 && tx < MAP_WIDTH && tz >= 0 && tz < MAP_HEIGHT;
            if (inside && map->grid[tz][tx].type != TILE_WALL) continue;
            const float cx = (float)tx * TILE_SIZE;
            const float cz = (float)tz * TILE_SIZE;
            const float px = fminf(fmaxf(x, cx - h), cx + h);
            const float pz = fminf(fmaxf(z, cz - h), cz + h);
            const float dx = x - px;
            const float dz = z - pz;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d < best) best = d;
        }
    }
    return best;
}

// A move into the wall clearance is refused, unless the body is already inside it and the
// move does not bring it closer (so a player placed near a wall can still walk away).
static bool WallBlocked(const FloorMap *map, Vector3 from, Vector3 to) {
    const float dTo = NearestWallDist(map, to.x, to.z, WALL_CLEARANCE);
    if (dTo >= WALL_CLEARANCE) return false;
    const float dFrom = NearestWallDist(map, from.x, from.z, WALL_CLEARANCE);
    return dTo < dFrom - 0.0001f;
}

static bool CheckCollisions(Vector3 from, Vector3 pos, const FloorMap *map, float radius, float height) {
    if (WallBlocked(map, from, pos)) {
        return true;
    }

    if (Staircase_Blocked(pos.x, pos.z, STAIR_CLEARANCE_RADIUS, pos.y, height)) {
        return true;
    }

    if (CheckObstacleCollision(pos, radius)) {
        return true;
    }

    return false;
}

static void SyncPlayerCamera(PlayerController *player) {
    // Head bob follows the distance walked, so it is quicker and bigger when sprinting and gone when standing; a tired
    // player also heaves with each breath.
    const float speed = sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
    float bob = player->isGrounded ? sinf(player->bobPhase) * 0.020f * fminf(speed / PLAYER_WALK_SPEED, 1.6f) : 0.0f;
    if (player->exhausted) bob += sinf(player->breathClock * 7.0f) * 0.018f;
    else if (player->stamina < 25.0f) bob += sinf(player->breathClock * 6.0f) * 0.009f;
    bob *= 1.0f - player->slideBlend;                                  // a slide is smooth
    player->camera.position = (Vector3){ player->position.x, player->eyeY + (player->height * 0.35f) + bob - PLAYER_SLIDE_DROP * player->slideBlend, player->position.z };
    // The shake is only in the view; the player's real yaw and pitch are untouched.
    const float yaw = player->yaw + sinf(player->shakeClock * 48.0f) * SHAKE_YAW * player->shake;
    float pitch = player->pitch + sinf(player->shakeClock * 61.0f + 1.3f) * SHAKE_PITCH * player->shake;
    if (pitch > 1.45f) pitch = 1.45f;
    if (pitch < -1.45f) pitch = -1.45f;
    Vector3 lookDir = {
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    };
    player->camera.target = Vector3Add(player->camera.position, lookDir);
}

void InitPlayerController(PlayerController *player, float startX, float startZ) {
    player->position = (Vector3){ startX, 0.0f, startZ };
    player->velocityY = 0.0f;
    player->radius = 0.35f;
    player->height = 1.8f;
    player->isGrounded = true;
    player->eyeY = 0.0f;
    player->fireCooldown = 0.0f;
    player->fireRate = 0.15f;
    ResetPlayerHealth(player);

    player->yaw = 0.0f;
    player->pitch = 0.0f;

    player->camera = (Camera3D){ 0 };
    player->camera.position = (Vector3){ player->position.x, player->position.y + (player->height * 0.4f), player->position.z };
    player->camera.target = (Vector3){ 0.0f, 1.0f, -1.0f };
    player->camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    player->camera.fovy = 60.0f;
    player->camera.projection = CAMERA_PERSPECTIVE;

    player->moveVel = (Vector3){ 0.0f, 0.0f, 0.0f };
    player->stamina = PLAYER_STAMINA_MAX;
    player->staminaDelay = 0.0f;
    player->exhausted = false;
    player->wantSprint = false;
    player->sprinting = false;
    player->sprintBlend = 0.0f;
    player->bobPhase = 0.0f;
    player->breathClock = 0.0f;
    player->sliding = false;
    player->slideT = 0.0f;
    player->slideSpeed0 = 0.0f;
    player->slideCooldown = 0.0f;
    player->slideBlend = 0.0f;
    player->slideDir = (Vector3){ 0.0f, 0.0f, 1.0f };
}

float PlayerFatigue(const PlayerController *player)
{
    float t = player->stamina / STAMINA_FRESH;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.0f) t = 0.0f;
    const float eased = t * t * (3.0f - 2.0f * t);                 // smoothstep: no sudden change where fatigue begins
    float f = FATIGUE_MIN_MUL + (1.0f - FATIGUE_MIN_MUL) * eased;
    if (player->exhausted) f *= EXHAUSTED_EXTRA_MUL;
    return f;
}

float PlayerNoise(const PlayerController *player)
{
    const float speed = sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
    float n = 3.0f + 5.0f * fminf(speed / PLAYER_WALK_SPEED, 1.0f);                // standing 3 m, walking 8 m
    if (player->sprinting) n = 20.0f;
    if (player->sliding) n = PLAYER_SLIDE_NOISE;                       // scraping along the floor: loud, but not a sprint's pounding
    const float gasp = player->exhausted ? 6.0f : (player->stamina < 25.0f ? 3.0f : 0.0f);
    return n + gasp;
}

float PlayerSpeed(const PlayerController *player)
{
    return sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
}

bool PlayerTrySlide(PlayerController *player)
{
    if (!player->health.isAlive || player->sliding || player->slideCooldown > 0.0f || !player->isGrounded) return false;
    if (!player->sprinting || player->exhausted || player->stamina < STAMINA_SLIDE_COST) return false;
    const float speed = sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
    if (speed < 2.0f) return false;                                    // a sprint that has barely begun has nothing to slide with
    player->slideDir = (Vector3){ player->moveVel.x / speed, 0.0f, player->moveVel.z / speed };
    const float start = PLAYER_SPRINT_SPEED * PLAYER_SLIDE_SPEED_MUL * PlayerFatigue(player);   // a tired slide is a slower one
    player->slideSpeed0 = start > speed ? start : speed;
    player->sliding = true;
    player->slideT = 0.0f;
    player->stamina -= STAMINA_SLIDE_COST;
    player->staminaDelay = STAMINA_REGEN_DELAY;
    return true;
}

static void EndSlide(PlayerController *player)
{
    player->sliding = false;
    player->slideCooldown = PLAYER_SLIDE_COOLDOWN;
}

bool PlayerTryJump(PlayerController *player)
{
    if (!player->isGrounded || !player->health.isAlive) return false;
    if (player->sliding) EndSlide(player);                             // jumping out of a slide keeps the speed it had
    float power = PLAYER_JUMP_SPEED;
    if (player->exhausted || player->stamina < STAMINA_JUMP_COST) power *= 0.7f;      // a weak hop
    else player->stamina -= STAMINA_JUMP_COST;
    if (player->stamina < 0.0f) player->stamina = 0.0f;
    player->staminaDelay = STAMINA_REGEN_DELAY;
    player->velocityY = power;
    player->isGrounded = false;
    return true;
}

bool HealPlayer(PlayerController *player, float amount)
{
    if (!player->health.isAlive || player->health.hp >= player->health.maxHp || amount <= 0.0f) return false;
    player->health.hp += amount;
    if (player->health.hp > player->health.maxHp) player->health.hp = player->health.maxHp;
    return true;
}

void ResetPlayerHealth(PlayerController *player) {
    InitHealth(&player->health, PLAYER_MAX_HP);
    player->hitFlash = 0.0f;
    player->shake = 0.0f;
    player->knock = (Vector3){ 0.0f, 0.0f, 0.0f };
}

bool DamagePlayer(PlayerController *player, float amount, Vector3 from) {
    if (!player->health.isAlive || player->health.invulnerabilityTimer > 0.0f) return false;

    ApplyDamage(&player->health, amount);
    player->health.invulnerabilityTimer = PLAYER_HIT_GRACE_SECS;
    player->hitFlash = 1.0f;
    player->shake = 1.0f;

    // Knockback: straight away from the source on the floor plane. A blow from directly
    // above or below pushes the player backwards from where they are looking.
    float dx = player->position.x - from.x;
    float dz = player->position.z - from.z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) {
        dx = -sinf(player->yaw);
        dz = -cosf(player->yaw);
        len = 1.0f;
    }
    player->knock = (Vector3){ dx / len * PLAYER_KNOCKBACK_SPEED, 0.0f, dz / len * PLAYER_KNOCKBACK_SPEED };
    return true;
}

void TeleportPlayer(PlayerController *player, Vector3 pos, float yaw) {
    player->position = pos;
    player->moveVel = (Vector3){ 0.0f, 0.0f, 0.0f };
    player->knock = (Vector3){ 0.0f, 0.0f, 0.0f };
    player->velocityY = 0.0f;
    player->isGrounded = true;
    player->sliding = false;
    player->slideBlend = 0.0f;
    player->eyeY = pos.y;
    player->yaw = yaw;
    player->pitch = 0.0f;
    SyncPlayerCamera(player);
}

void UpdatePlayerController(PlayerController *player, Vector2 moveDelta, Vector2 lookDelta, float dt, const FloorMap *map) {
    // 1. Camera Look Rotation
    float lookSensitivity = 0.005f;
    player->yaw -= lookDelta.x * lookSensitivity;
    player->pitch -= lookDelta.y * lookSensitivity;
    if (player->pitch > 1.4f) player->pitch = 1.4f;
    if (player->pitch < -1.4f) player->pitch = -1.4f;

    Vector3 forward = { sinf(player->yaw), 0.0f, cosf(player->yaw) };
    Vector3 right = { sinf(player->yaw - 1.5708f), 0.0f, cosf(player->yaw - 1.5708f) };

    // 2. Movement. The player has a velocity that speeds up and slows down, and sprinting spends stamina.
    float inLen = sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
    const bool moving = inLen > 0.12f;
    if (inLen > 1.0f) inLen = 1.0f;

    // Stamina. Sprinting needs forward movement, some stamina left, and a player who is not exhausted.
    // A sprint that is already going carries on until the stamina is gone; starting one takes a little in hand.
    const bool canSprint = !player->exhausted && player->isGrounded &&
                           (player->sprinting ? player->stamina > 0.0f : player->stamina > STAMINA_SPRINT_MIN);
    player->sprinting = player->wantSprint && moving && moveDelta.y < -0.25f && canSprint;
    if (player->sprinting && player->sliding) {
        player->staminaDelay = STAMINA_REGEN_DELAY;                    // the slide was paid for when it started: no drain, no regeneration
    } else if (player->sprinting) {
        player->stamina -= STAMINA_SPRINT_DRAIN * dt;
        player->staminaDelay = STAMINA_REGEN_DELAY;
        if (player->stamina <= 0.0f) { player->stamina = 0.0f; player->exhausted = true; player->sprinting = false; }
    } else if (player->staminaDelay > 0.0f) {
        player->staminaDelay -= dt;
    } else if (player->stamina < PLAYER_STAMINA_MAX) {
        player->stamina += STAMINA_REGEN * (player->exhausted ? EXHAUSTED_REGEN_MUL : 1.0f) * dt;
        if (player->stamina > PLAYER_STAMINA_MAX) player->stamina = PLAYER_STAMINA_MAX;
    }
    if (player->exhausted && player->stamina >= STAMINA_EXHAUST_RECOVER) player->exhausted = false;
    player->breathClock += dt;

    // Target speed: walking or sprinting, slower the more tired the player is (smoothly, from STAMINA_FRESH down to zero).
    const float fatigue = PlayerFatigue(player);
    const float topSpeed = (player->sprinting ? PLAYER_SPRINT_SPEED : PLAYER_WALK_SPEED) * fatigue;
    Vector3 want = { 0.0f, 0.0f, 0.0f };
    if (moving) {
        // The stick's direction at its own length (a gentle push walks slowly), then turned into world directions.
        const float ux = moveDelta.x / sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
        const float uy = moveDelta.y / sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
        want.x = (-forward.x * uy + right.x * ux) * topSpeed * inLen;
        want.z = (-forward.z * uy + right.z * ux) * topSpeed * inLen;
    }
    if (player->slideCooldown > 0.0f) player->slideCooldown -= dt;
    if (player->sliding) {
        // A slide ignores the walking rules: it goes the way it was going (the stick may turn it a little) at a speed that
        // runs down from where it started to PLAYER_SLIDE_END_SPEED.
        player->slideT += dt;
        if (moving) {
            const float ux = moveDelta.x / sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
            const float uy = moveDelta.y / sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
            const Vector3 aim = { -forward.x * uy + right.x * ux, 0.0f, -forward.z * uy + right.z * ux };
            const float cross = player->slideDir.x * aim.z - player->slideDir.z * aim.x;
            const float dot = player->slideDir.x * aim.x + player->slideDir.z * aim.z;
            float turn = atan2f(cross, dot);
            const float maxTurn = PLAYER_SLIDE_STEER * dt;
            if (turn > maxTurn) turn = maxTurn; else if (turn < -maxTurn) turn = -maxTurn;
            const float c = cosf(turn), s = sinf(turn);
            const Vector3 d = { player->slideDir.x * c - player->slideDir.z * s, 0.0f, player->slideDir.x * s + player->slideDir.z * c };
            player->slideDir = d;
        }
        const float u = fminf(player->slideT / PLAYER_SLIDE_SECS, 1.0f);
        const float speed = PLAYER_SLIDE_END_SPEED + (player->slideSpeed0 - PLAYER_SLIDE_END_SPEED) * powf(1.0f - u, 1.5f);
        player->moveVel.x = player->slideDir.x * speed;
        player->moveVel.z = player->slideDir.z * speed;
    } else {
    // Toward the wanted velocity by at most accel * dt. Speeding up is gentler than stopping, the air steers less,
    // and an exhausted player is slow to get going.
    {
        float rate = moving ? PLAYER_ACCEL : PLAYER_DECEL;
        if (!player->isGrounded) rate *= PLAYER_AIR_CONTROL;
        rate *= fatigue;                                   // sluggish to get going as well when tired
        const float dx = want.x - player->moveVel.x, dz = want.z - player->moveVel.z;
        const float dl = sqrtf(dx * dx + dz * dz);
        const float step = rate * dt;
        if (dl <= step) { player->moveVel.x = want.x; player->moveVel.z = want.z; }
        else { player->moveVel.x += dx / dl * step; player->moveVel.z += dz / dl * step; }
    }
    }

    // Sliding collision, an axis at a time. A wall stops the velocity along that axis, so the player slides.
    Vector3 nextPos = player->position;
    nextPos.x += player->moveVel.x * dt;
    if (!CheckCollisions(player->position, nextPos, map, player->radius, player->height)) player->position.x = nextPos.x;
    else player->moveVel.x = 0.0f;

    nextPos = player->position;
    nextPos.z += player->moveVel.z * dt;
    if (!CheckCollisions(player->position, nextPos, map, player->radius, player->height)) player->position.z = nextPos.z;
    else player->moveVel.z = 0.0f;

    {
        const float speed = sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
        if (player->isGrounded) player->bobPhase += speed * dt * 2.1f;
        // The view widens as the sprint builds and narrows when it ends.
        float k = dt * 6.0f;
        if (k > 1.0f) k = 1.0f;
        player->sprintBlend += (((player->sprinting || player->sliding) ? 1.0f : 0.0f) - player->sprintBlend) * k;
        player->camera.fovy = 60.0f + SPRINT_FOV_KICK * player->sprintBlend;
        // The camera drops into a slide and comes back up after it.
        float kd = dt * 12.0f;
        if (kd > 1.0f) kd = 1.0f;
        player->slideBlend += ((player->sliding ? 1.0f : 0.0f) - player->slideBlend) * kd;
        // A slide ends when its time is up, when it has run into something and slowed to a crawl, or when the feet leave the floor.
        if (player->sliding && (player->slideT >= PLAYER_SLIDE_SECS || speed < 1.5f)) EndSlide(player);
    }

    // 2b. Knockback: slides along the floor and stops against walls like normal movement.
    if (fabsf(player->knock.x) > 0.05f || fabsf(player->knock.z) > 0.05f) {
        nextPos = player->position;
        nextPos.x += player->knock.x * dt;
        if (!CheckCollisions(player->position, nextPos, map, player->radius, player->height)) player->position.x = nextPos.x;
        else player->knock.x = 0.0f;

        nextPos = player->position;
        nextPos.z += player->knock.z * dt;
        if (!CheckCollisions(player->position, nextPos, map, player->radius, player->height)) player->position.z = nextPos.z;
        else player->knock.z = 0.0f;

        const float decay = expf(-PLAYER_KNOCK_DECAY * dt);
        player->knock.x *= decay;
        player->knock.z *= decay;
    } else {
        player->knock.x = player->knock.z = 0.0f;
    }

    // 3. Gravity & Vertical Physics. Ground is the floor or the top of a stair step.
    float gravity = 14.0f;
    float ground = Staircase_GroundHeight(player->position.x, player->position.z, player->position.y);

    if (player->isGrounded && player->velocityY <= 0.0f && player->position.y - ground <= STEP_UP_MAX) {
        // Stay glued to the surface: climbs a step, and follows a flight down without a hop.
        player->position.y = ground;
        player->velocityY = 0.0f;
    } else {
        player->velocityY -= gravity * dt;
        player->position.y += player->velocityY * dt;
        ground = Staircase_GroundHeight(player->position.x, player->position.z, player->position.y);
        if (player->position.y <= ground) {
            player->position.y = ground;
            player->velocityY = 0.0f;
            player->isGrounded = true;
        } else {
            player->isGrounded = false;
        }
    }
    if (player->sliding && !player->isGrounded) EndSlide(player);       // off a step and into the air

    // 4. Update Camera View Matrix. The eye follows the body height smoothly.
    {
        float k = dt * EYE_FOLLOW_RATE;
        if (k > 1.0f) k = 1.0f;
        player->eyeY += (player->position.y - player->eyeY) * k;
    }
    // Hit effects fade out.
    UpdateHealthTimer(&player->health, dt);
    player->shakeClock += dt;
    player->hitFlash -= FLASH_DECAY * dt;
    if (player->hitFlash < 0.0f) player->hitFlash = 0.0f;
    player->shake -= SHAKE_DECAY * dt;
    if (player->shake < 0.0f) player->shake = 0.0f;

    SyncPlayerCamera(player);

    if (player->fireCooldown > 0.0f) {
        player->fireCooldown -= dt;
    }
}
