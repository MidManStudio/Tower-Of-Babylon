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

static float Smooth01(float t) { t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); return t * t * (3.0f - 2.0f * t); }

float PlayerDirectionMul(float forward, float right)
{
    // The unit direction is scaled axis by axis (forward at 1, backward at its own cap, sideways at its own cap) and the length of
    // what comes out is the multiplier: an ellipse, so every direction in between is continuous with its neighbours.
    const float f = forward >= 0.0f ? forward : forward * PLAYER_BACKPEDAL_MUL;
    const float r = right * PLAYER_STRAFE_MUL;
    return sqrtf(f * f + r * r);
}

float PlayerAimAmount(const PlayerController *player) { return Smooth01(player->aimBlend); }
float PlayerAimEase(float aimBlend) { return Smooth01(aimBlend); }

float PlayerViewFov(const PlayerController *player, float aimBlend)
{
    const float zoomed = player->adsFov > 1.0f ? player->adsFov : PLAYER_BASE_FOV;
    return PLAYER_BASE_FOV + (zoomed - PLAYER_BASE_FOV) * Smooth01(aimBlend) + SPRINT_FOV_KICK * player->sprintBlend;
}

Vector3 PlayerViewUp(Vector3 forward, float leanBlend)
{
    const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
    if (leanBlend > -0.0005f && leanBlend < 0.0005f) return worldUp;
    const Vector3 r = Vector3Normalize(Vector3CrossProduct(forward, worldUp));        // the view's right
    if (Vector3Length(r) < 0.001f) return worldUp;
    const Vector3 u0 = Vector3CrossProduct(r, forward);
    const float a = leanBlend * PLAYER_LEAN_ROLL;
    return Vector3Add(Vector3Scale(u0, cosf(a)), Vector3Scale(r, sinf(a)));
}

// How far the eye may move along dir (a unit vector on the floor) from pos before it would come within PLAYER_LEAN_CAM_CLEAR of a wall.
static float LeanRoom(const FloorMap *map, Vector3 pos, float dx, float dz)
{
    float room = 0.0f;
    for (float d = 0.05f; d <= PLAYER_LEAN_SHIFT + 0.001f; d += 0.05f) {
        if (NearestWallDist(map, pos.x + dx * d, pos.z + dz * d, PLAYER_LEAN_CAM_CLEAR) < PLAYER_LEAN_CAM_CLEAR) break;
        room = d;
    }
    return room;
}

// Metres from (x, z) along dir (a unit vector on the floor) to the first wall, looking no further than reach. Outside the map is wall.
// A tile is TILE_SIZE (2 m) across, so a 0.1 m step cannot jump over one.
static float SightDist(const FloorMap *map, float x, float z, float dx, float dz, float reach)
{
    const float h = TILE_SIZE * 0.5f;
    for (float d = 0.1f; d < reach; d += 0.1f) {
        const int tx = (int)floorf((x + dx * d + h) / TILE_SIZE);
        const int tz = (int)floorf((z + dz * d + h) / TILE_SIZE);
        if (Tile_IsSolid(map, tx, tz)) return d;
    }
    return reach;
}

// Automatic lean (see PLAYER_LEAN_*). Looks from the body straight ahead, and from where the eye would be at full lean to each side;
// a leaned line that gets clearly further than the middle one has found an opening, and that is the side to lean toward. A lean
// that has started holds until its opening is gone (or the wall is no longer close), and one that has not started waits
// PLAYER_LEAN_DWELL seconds, so walking past a corner does not make the view flap. Then the blend eases toward it.
static void UpdateAutoLean(PlayerController *player, bool sprintIntent, float dt, const FloorMap *map)
{
    const bool allowed = player->leanAllowed && player->health.isAlive && player->isGrounded && !player->sliding &&
                         !player->diving && !player->rolling && !player->lying && !sprintIntent;
    int dir = 0;
    bool looked = false;
    if (allowed) {
        const float fx = sinf(player->yaw), fz = cosf(player->yaw);
        const float rx = sinf(player->yaw - 1.5708f), rz = cosf(player->yaw - 1.5708f);
        const float px = player->position.x, pz = player->position.z;
        const float ahead = SightDist(map, px, pz, fx, fz, PLAYER_LEAN_SIGHT);
        if (ahead <= PLAYER_LEAN_NEAR_LET_GO) {
            looked = true;
            player->leanRoom[0] = LeanRoom(map, player->position, -rx, -rz);
            player->leanRoom[1] = LeanRoom(map, player->position,  rx,  rz);
            const float sl = fminf(PLAYER_LEAN_SHIFT, player->leanRoom[0]), sr = fminf(PLAYER_LEAN_SHIFT, player->leanRoom[1]);
            const float gl = sl > 0.10f ? SightDist(map, px - rx * sl, pz - rz * sl, fx, fz, PLAYER_LEAN_SIGHT) - ahead : 0.0f;
            const float gr = sr > 0.10f ? SightDist(map, px + rx * sr, pz + rz * sr, fx, fz, PLAYER_LEAN_SIGHT) - ahead : 0.0f;
            dir = player->leanDir;
            if (dir != 0 && ((dir > 0 ? gr : gl) < PLAYER_LEAN_GAIN_OFF)) dir = 0;        // its opening is gone
            if (dir != 0) {
                player->leanDwell = 0.0f;
            } else if (ahead <= PLAYER_LEAN_NEAR_WALL && (gr >= PLAYER_LEAN_GAIN_ON || gl >= PLAYER_LEAN_GAIN_ON)) {
                player->leanDwell += dt;
                if (player->leanDwell >= PLAYER_LEAN_DWELL) dir = gr >= gl ? 1 : -1;
            } else {
                player->leanDwell = 0.0f;
            }
        }
    }
    if (!looked) { player->leanDwell = 0.0f; dir = 0; }
    player->leanDir = dir;
    const float rate = dt / PLAYER_LEAN_SECS;
    const float target = (float)dir;
    if (player->leanBlend < target) { player->leanBlend += rate; if (player->leanBlend > target) player->leanBlend = target; }
    else if (player->leanBlend > target) { player->leanBlend -= rate; if (player->leanBlend < target) player->leanBlend = target; }
    if (!looked && player->leanBlend != 0.0f) {                                // easing out: the camera still needs the room to each side
        player->leanRoom[0] = LeanRoom(map, player->position, -sinf(player->yaw - 1.5708f), -cosf(player->yaw - 1.5708f));
        player->leanRoom[1] = LeanRoom(map, player->position,  sinf(player->yaw - 1.5708f),  cosf(player->yaw - 1.5708f));
    }
}

static void SyncPlayerCamera(PlayerController *player) {
    // Head bob follows the distance walked, so it is quicker and bigger when sprinting and gone when standing; a tired
    // player also heaves with each breath.
    const float speed = sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
    float bob = player->isGrounded ? sinf(player->bobPhase) * 0.020f * fminf(speed / PLAYER_WALK_SPEED, 1.6f) : 0.0f;
    if (player->exhausted) bob += sinf(player->breathClock * 7.0f) * 0.018f;
    else if (player->stamina < STAMINA_LOW) bob += sinf(player->breathClock * 6.0f) * 0.009f;
    bob *= (1.0f - player->slideBlend) * (1.0f - player->diveBlend);   // a slide, a dive and a roll are smooth
    player->camera.position = (Vector3){ player->position.x, player->eyeY + (player->height * 0.35f) + bob - PLAYER_SLIDE_DROP * player->slideBlend
                                         - PLAYER_DIVE_DROP * player->diveBlend, player->position.z };
    // A lean moves the eye sideways (never further than there is room for) and a little down.
    if (player->leanBlend > 0.0005f || player->leanBlend < -0.0005f) {
        const float room = player->leanBlend > 0.0f ? player->leanRoom[1] : player->leanRoom[0];
        const float shift = player->leanBlend * fminf(PLAYER_LEAN_SHIFT, room);            // negative to the left
        player->camera.position.x += sinf(player->yaw - 1.5708f) * shift;
        player->camera.position.z += cosf(player->yaw - 1.5708f) * shift;
        player->camera.position.y -= PLAYER_LEAN_DIP * fabsf(player->leanBlend);
    }
    // The eye never goes lower than PLAYER_CAM_MIN_HEIGHT above the floor under it, however low the slide, the dive or lying would put it:
    // closer than that the near plane cuts the floor away and the view looks out under it.
    {
        const float floorY = Staircase_GroundHeight(player->camera.position.x, player->camera.position.z, player->position.y);
        if (player->camera.position.y < floorY + PLAYER_CAM_MIN_HEIGHT) player->camera.position.y = floorY + PLAYER_CAM_MIN_HEIGHT;
    }
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
    player->camera.up = PlayerViewUp(lookDir, player->leanBlend);
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
    player->wantAim = false;
    player->aimBlend = 0.0f;
    player->adsFov = 0.0f;
    player->adsSecs = 0.0f;
    player->adsMoveMul = 0.0f;
    player->leanAllowed = false;
    player->leanDir = 0;
    player->leanDwell = 0.0f;
    player->leanBlend = 0.0f;
    player->leanRoom[0] = player->leanRoom[1] = 0.0f;
    player->jumpedSprinting = false;
    player->diving = false;
    player->rolling = false;
    player->diveT = 0.0f;
    player->diveDir = (Vector3){ 0.0f, 0.0f, 1.0f };
    player->diveBlend = 0.0f;
    player->lying = false;
    player->lieStick = (Vector2){ 0.0f, 0.0f };
    player->stimLeft = 0.0f;
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
    if (player->diving || player->rolling) n = PLAYER_DIVE_NOISE;      // a body hitting the floor
    const float gasp = player->exhausted ? 6.0f : (player->stamina < STAMINA_LOW ? 3.0f : 0.0f);
    return n + gasp;
}

float PlayerSpeed(const PlayerController *player)
{
    return sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
}

bool PlayerSprintingForward(const PlayerController *player)
{
    if (!player->sprinting) return false;
    const float speed = sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
    if (speed < 0.5f) return false;
    const float fwd = (sinf(player->yaw) * player->moveVel.x + cosf(player->yaw) * player->moveVel.z) / speed;
    return fwd >= PLAYER_SLIDE_MIN_FORWARD;
}

bool PlayerTrySlide(PlayerController *player)
{
    if (!player->health.isAlive || player->sliding || player->slideCooldown > 0.0f || !player->isGrounded) return false;
    if (player->diving || player->rolling || player->lying) return false;
    if (!player->sprinting || player->exhausted || player->stamina < STAMINA_SLIDE_COST) return false;
    const float speed = sqrtf(player->moveVel.x * player->moveVel.x + player->moveVel.z * player->moveVel.z);
    if (speed < 2.0f) return false;                                    // a sprint that has barely begun has nothing to slide with
    if (!PlayerSprintingForward(player)) return false;                 // a slide goes forward only: left, right and back are what the dive is for
    player->slideDir = (Vector3){ player->moveVel.x / speed, 0.0f, player->moveVel.z / speed };
    // A slide goes the way the sprint was going (within 45 degrees of ahead); a diagonal one starts a little slower, as that sprint was.
    const float fwd = sinf(player->yaw) * player->slideDir.x + cosf(player->yaw) * player->slideDir.z;
    const float rgt = sinf(player->yaw - 1.5708f) * player->slideDir.x + cosf(player->yaw - 1.5708f) * player->slideDir.z;
    const float start = PLAYER_SPRINT_SPEED * PLAYER_SLIDE_SPEED_MUL * PlayerFatigue(player) * PlayerDirectionMul(fwd, rgt);   // a tired slide is a slower one
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

bool PlayerDiveAvailable(const PlayerController *player)
{
    return player->health.isAlive && !player->isGrounded && player->jumpedSprinting && !player->diving && !player->rolling;
}

bool PlayerTryDive(PlayerController *player, Vector2 moveDelta)
{
    if (!PlayerDiveAvailable(player) || player->exhausted || player->stamina < STAMINA_DIVE_COST) return false;
    // The direction is the stick now, against where the player faces; a centred stick is straight ahead.
    float f = 1.0f, r = 0.0f;
    const float len = sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
    if (len > 0.12f) { f = -moveDelta.y / len; r = moveDelta.x / len; }
    const Vector3 fwd = { sinf(player->yaw), 0.0f, cosf(player->yaw) };
    const Vector3 rgt = { sinf(player->yaw - 1.5708f), 0.0f, cosf(player->yaw - 1.5708f) };
    player->diveDir = (Vector3){ fwd.x * f + rgt.x * r, 0.0f, fwd.z * f + rgt.z * r };
    // The arc lands after PLAYER_DIVE_SECS on level ground wherever in the jump it was asked: from the height h the upward speed that does
    // it solves h + v T - g T^2 / 2 = 0.
    const float ground = Staircase_GroundHeight(player->position.x, player->position.z, player->position.y);
    const float h = player->position.y > ground ? player->position.y - ground : 0.0f;
    player->velocityY = (0.5f * PLAYER_GRAVITY * PLAYER_DIVE_SECS * PLAYER_DIVE_SECS - h) / PLAYER_DIVE_SECS;
    player->diving = true;
    player->diveT = 0.0f;
    player->sprinting = false;
    player->stamina -= STAMINA_DIVE_COST;
    player->staminaDelay = STAMINA_REGEN_DELAY;
    if (PLAYER_DIVE_GRACE_SECS > 0.0f && player->health.invulnerabilityTimer < PLAYER_DIVE_GRACE_SECS)
        player->health.invulnerabilityTimer = PLAYER_DIVE_GRACE_SECS;
    return true;
}

bool PlayerTryStim(PlayerController *player)
{
    if (!player->health.isAlive || player->stimLeft > 0.0f) return false;
    player->stimLeft = PLAYER_STIM_SECS;
    return true;
}

bool PlayerTryJump(PlayerController *player)
{
    if (!player->isGrounded || !player->health.isAlive) return false;
    if (player->diving || player->rolling || player->lying) return false;   // no jumping out of a roll, and none from lying down: the stick gets the player up
    player->jumpedSprinting = player->sprinting;                       // a jump from a sprint (or a slide) can be followed by a dive
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
    player->diving = player->rolling = player->jumpedSprinting = false;
    player->lying = false;
    player->lieStick = (Vector2){ 0.0f, 0.0f };
    player->stimLeft = 0.0f;
    player->diveBlend = 0.0f;
    player->aimBlend = 0.0f;
    player->leanBlend = 0.0f;
    player->leanDir = 0;
    player->leanDwell = 0.0f;
    player->eyeY = pos.y;
    player->yaw = yaw;
    player->pitch = 0.0f;
    SyncPlayerCamera(player);
}

void PlayerApplyLook(PlayerController *player, Vector2 lookDelta) {
    float lookSensitivity = 0.005f;
    if (player->adsFov > 1.0f && player->aimBlend > 0.0f) {                  // zoomed in: the same crosshair travel across the world
        const float ratio = tanf(player->adsFov * DEG2RAD * 0.5f) / tanf(PLAYER_BASE_FOV * DEG2RAD * 0.5f);
        lookSensitivity *= 1.0f + (ratio - 1.0f) * PlayerAimAmount(player);
    }
    player->yaw -= lookDelta.x * lookSensitivity;
    player->pitch -= lookDelta.y * lookSensitivity;
    if (player->pitch > 1.4f) player->pitch = 1.4f;
    if (player->pitch < -1.4f) player->pitch = -1.4f;
}

void PlayerSyncCamera(PlayerController *player) { SyncPlayerCamera(player); }

void PlayerStep(PlayerController *player, Vector2 moveDelta, float dt, const FloorMap *map) {

    Vector3 forward = { sinf(player->yaw), 0.0f, cosf(player->yaw) };
    Vector3 right = { sinf(player->yaw - 1.5708f), 0.0f, cosf(player->yaw - 1.5708f) };

    // 2. Movement. The player has a velocity that speeds up and slows down, and sprinting spends stamina.
    float inLen = sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
    const bool moving = inLen > 0.12f;
    if (inLen > 1.0f) inLen = 1.0f;

    // Lying (after a dive's roll): the player stays down until the stick is moved. It counts as moved when it is let go and pushed again,
    // or pushed on from where it was held when the roll ended by PLAYER_LIE_PUSH; a finger that stays where it was does not get the player up.
    if (player->lying) {
        if (!moving) {
            player->lieStick = (Vector2){ 0.0f, 0.0f };
        } else {
            const float dx = moveDelta.x - player->lieStick.x, dy = moveDelta.y - player->lieStick.y;
            if (dx * dx + dy * dy > PLAYER_LIE_PUSH * PLAYER_LIE_PUSH) player->lying = false;
        }
    }
    const bool wantMove = moving && !player->lying;                  // a player lying down goes nowhere

    // Aiming down sights eases in while wanted and out (a little quicker) when not.
    {
        const float secs = player->adsSecs > 0.01f ? player->adsSecs : PLAYER_ADS_DEFAULT_SECS;
        const bool aimWanted = player->wantAim && player->health.isAlive;
        const float rate = (aimWanted ? 1.0f : 1.0f / PLAYER_ADS_OUT_SHARE) * dt / secs;
        if (aimWanted) { player->aimBlend += rate; if (player->aimBlend > 1.0f) player->aimBlend = 1.0f; }
        else           { player->aimBlend -= rate; if (player->aimBlend < 0.0f) player->aimBlend = 0.0f; }
    }
    // Leaning, automatic at a wall's corner: a gun in hand, on the ground, not sliding, diving or sprinting; the eye never goes into a wall.
    UpdateAutoLean(player, player->wantSprint && moving, dt, map);
    const bool leaning = player->leanBlend > 0.05f || player->leanBlend < -0.05f;

    // Stamina. Sprinting works in every direction (omni sprint), needs some stamina left and a player who is not exhausted, and does
    // not go with aiming down sights or leaning.
    // A sprint that is already going carries on until the stamina is gone; starting one takes a little in hand.
    const bool canSprint = !player->exhausted && player->isGrounded && !player->wantAim && !leaning && !player->diving && !player->rolling && !player->lying &&
                           (player->sprinting ? player->stamina > 0.0f : player->stamina > STAMINA_SPRINT_MIN);
    player->sprinting = player->wantSprint && wantMove && canSprint;
    // A stim shot adds to the stamina whatever else is happening (before the sprint's drain, so a nearly empty bar is not called exhausted
    // in the same step it is being filled) and gives its health back a little each step.
    if (player->stimLeft > 0.0f) {
        const float used = fminf(dt, player->stimLeft);
        player->stimLeft -= used;
        player->stamina += STAMINA_STIM_RATE * used;
        HealPlayer(player, PLAYER_STIM_HEAL / PLAYER_STIM_SECS * used);
    }
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
    if (player->stamina > PLAYER_STAMINA_MAX) player->stamina = PLAYER_STAMINA_MAX;
    if (player->exhausted && player->stamina >= STAMINA_EXHAUST_RECOVER) player->exhausted = false;
    player->breathClock += dt;

    // Target speed: walking or sprinting, slower the more tired the player is (smoothly, from STAMINA_FRESH down to zero).
    const float fatigue = PlayerFatigue(player);
    float topSpeed = (player->sprinting ? PLAYER_SPRINT_SPEED : PLAYER_WALK_SPEED) * fatigue;
    if (player->adsMoveMul > 0.0f) topSpeed *= 1.0f + (player->adsMoveMul - 1.0f) * PlayerAimAmount(player);   // aimed down sights: slower
    topSpeed *= 1.0f + (PLAYER_LEAN_MOVE_MUL - 1.0f) * fabsf(player->leanBlend);                               // and leaning
    Vector3 want = { 0.0f, 0.0f, 0.0f };
    if (wantMove) {
        // The stick's direction at its own length (a gentle push walks slowly), then turned into world directions.
        const float ux = moveDelta.x / sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
        const float uy = moveDelta.y / sqrtf(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
        // Each axis has its own top speed: forward at 1, left and right and backwards slower (omni movement).
        const float f = -uy * (uy > 0.0f ? PLAYER_BACKPEDAL_MUL : 1.0f), r = ux * PLAYER_STRAFE_MUL;
        want.x = (forward.x * f + right.x * r) * topSpeed * inLen;
        want.z = (forward.z * f + right.z * r) * topSpeed * inLen;
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
    } else if (player->diving || player->rolling) {
        // A dive and its roll are committed: the stick does not steer them. The dive covers PLAYER_DIVE_DIST in PLAYER_DIVE_SECS in the air;
        // the roll starts at PLAYER_ROLL_SPEED and runs down.
        player->diveT += dt;
        float speed = PLAYER_DIVE_DIST / PLAYER_DIVE_SECS;
        if (player->rolling) {
            const float u = fminf(player->diveT / PLAYER_ROLL_SECS, 1.0f);
            speed = PLAYER_ROLL_SPEED + (PLAYER_ROLL_END_SPEED - PLAYER_ROLL_SPEED) * u;
        }
        player->moveVel.x = player->diveDir.x * speed;
        player->moveVel.z = player->diveDir.z * speed;
    } else {
    // Toward the wanted velocity by at most accel * dt. Speeding up is gentler than stopping, the air steers less,
    // and an exhausted player is slow to get going.
    {
        float rate = wantMove ? PLAYER_ACCEL : PLAYER_DECEL;
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
        player->sprintBlend += (((player->sprinting || player->sliding || player->diving) ? 1.0f : 0.0f) - player->sprintBlend) * k;
        player->camera.fovy = PlayerViewFov(player, player->aimBlend);
        // The camera drops into a slide and comes back up after it.
        float kd = dt * 12.0f;
        if (kd > 1.0f) kd = 1.0f;
        player->slideBlend += ((player->sliding ? 1.0f : 0.0f) - player->slideBlend) * kd;
        player->diveBlend += (((player->diving || player->rolling || player->lying) ? 1.0f : 0.0f) - player->diveBlend) * kd;
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
    float gravity = PLAYER_GRAVITY;
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
    if (player->isGrounded) player->jumpedSprinting = false;            // a dive belongs to the jump it was started in
    // The dive lands into its roll, and the roll ends after its time (a dive that has somehow not landed long after it should have still lets go).
    if (player->diving && (player->isGrounded || player->diveT > 3.0f * PLAYER_DIVE_SECS)) { player->diving = false; player->rolling = true; player->diveT = 0.0f; }
    else if (player->rolling && player->diveT >= PLAYER_ROLL_SECS) {
        player->rolling = false;
        player->lying = true;                                           // the roll ends flat on the floor, and stays there until the stick is moved
        player->lieStick = moveDelta;
    }

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

// Look and movement together, as one call: what the game did every frame before the rules ran on fixed steps, and what the
// movement tests still use. The game itself calls PlayerApplyLook every frame and PlayerStep every tick.
void UpdatePlayerController(PlayerController *player, Vector2 moveDelta, Vector2 lookDelta, float dt, const FloorMap *map) {
    PlayerApplyLook(player, lookDelta);
    PlayerStep(player, moveDelta, dt, map);
}
