// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_move_sim.h and net_move_sim.c"
// ============================================================================
#include "net_move_sim.h"

#include <math.h>

void NetSim_Spawn(PlayerController *p, float x, float z) { InitPlayerController(p, x, z); }

void NetSim_Step(PlayerController *p, const NetCmd *cmd, const FloorMap *map)
{
    p->yaw = NetCmd_Yaw(cmd);
    p->pitch = NetCmd_Pitch(cmd);
    PlayerSyncCamera(p);
    if (cmd->buttons & NET_BTN_SLIDE) PlayerTrySlide(p);                  // the order of GameSim_Step (src/sim/game_sim.c): slide, sprint, step, jump
    p->wantSprint = (cmd->buttons & NET_BTN_SPRINT) != 0;
    float mx, my;
    NetCmd_Move(cmd, &mx, &my);
    PlayerStep(p, (Vector2){ mx, my }, NET_TICK_SECS, map);
    if (cmd->buttons & NET_BTN_JUMP) PlayerTryJump(p);
}

void NetSim_Capture(const PlayerController *p, NetOwnerState *o)
{
    o->px = p->position.x; o->py = p->position.y; o->pz = p->position.z; o->velY = p->velocityY;
    o->moveVx = p->moveVel.x; o->moveVz = p->moveVel.z; o->knockX = p->knock.x; o->knockZ = p->knock.z;
    o->stamina = p->stamina; o->staminaDelay = p->staminaDelay;
    o->slideT = p->slideT; o->slideSpeed0 = p->slideSpeed0; o->slideCooldown = p->slideCooldown; o->slideDirX = p->slideDir.x; o->slideDirZ = p->slideDir.z;
    o->flags = (uint8_t)((p->isGrounded ? NET_OWN_GROUNDED : 0) | (p->exhausted ? NET_OWN_EXHAUSTED : 0) | (p->sprinting ? NET_OWN_SPRINTING : 0) | (p->sliding ? NET_OWN_SLIDING : 0));
}

void NetSim_Apply(PlayerController *p, const NetOwnerState *o)
{
    p->position = (Vector3){ o->px, o->py, o->pz }; p->velocityY = o->velY;
    p->moveVel = (Vector3){ o->moveVx, 0.0f, o->moveVz }; p->knock = (Vector3){ o->knockX, 0.0f, o->knockZ };
    p->stamina = o->stamina; p->staminaDelay = o->staminaDelay;
    p->slideT = o->slideT; p->slideSpeed0 = o->slideSpeed0; p->slideCooldown = o->slideCooldown; p->slideDir = (Vector3){ o->slideDirX, 0.0f, o->slideDirZ };
    p->isGrounded = (o->flags & NET_OWN_GROUNDED) != 0; p->exhausted = (o->flags & NET_OWN_EXHAUSTED) != 0;
    p->sprinting = (o->flags & NET_OWN_SPRINTING) != 0; p->sliding = (o->flags & NET_OWN_SLIDING) != 0;
}

float NetSim_Error(const PlayerController *p, const NetOwnerState *o)
{
    const float dx = p->position.x - o->px, dy = p->position.y - o->py, dz = p->position.z - o->pz;
    float err = sqrtf(dx * dx + dy * dy + dz * dz);
    NetOwnerState mine; NetSim_Capture(p, &mine);
    if (fabsf(mine.stamina - o->stamina) > 1.0f) err += 10.0f;                            // a gap here grows into a different sprint a second from now
    if (fabsf(mine.moveVx - o->moveVx) > 0.1f || fabsf(mine.moveVz - o->moveVz) > 0.1f) err += 10.0f;
    if ((mine.flags & (NET_OWN_EXHAUSTED | NET_OWN_SLIDING)) != (o->flags & (NET_OWN_EXHAUSTED | NET_OWN_SLIDING))) err += 10.0f;
    return err;
}

void NetSim_Remote(const PlayerController *p, uint8_t id, NetRemote *r)
{
    r->id = id;
    r->px = (int32_t)lroundf(p->position.x * 1000.0f); r->py = (int32_t)lroundf(p->position.y * 1000.0f); r->pz = (int32_t)lroundf(p->position.z * 1000.0f);
    const NetCmd c = NetCmd_Make(0.0f, 0.0f, p->yaw, p->pitch, 0);
    r->yaw = c.yaw; r->pitch = c.pitch;
    r->flags = (uint8_t)((p->sprinting ? NET_REM_SPRINTING : 0) | (p->sliding ? NET_REM_SLIDING : 0) | (p->isGrounded ? NET_REM_GROUNDED : 0));
}
