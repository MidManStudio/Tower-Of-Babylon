// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_play.h and net_play.c"
// ============================================================================
#include "net_play.h"

#include <math.h>

void NetPlay_Init(NetPlay *n, float yaw)
{
    SimCmd_Init(&n->latch);
    InitPlayerController(&n->look, 0.0f, 0.0f);
    n->look.yaw = yaw;
}

void NetPlay_Input(NetPlay *n, const InputFrame *in, float maxLookPx)
{
    Vector2 look = in->look;
    const float raw = sqrtf(look.x * look.x + look.y * look.y);
    if (maxLookPx > 0.0f && raw > maxLookPx) { look.x *= maxLookPx / raw; look.y *= maxLookPx / raw; }
    PlayerApplyLook(&n->look, look);
    SimCmd_Add(&n->latch, in);
}

uint16_t NetPlay_Buttons(const SimCmd *s)
{
    uint16_t b = 0;
    if (s->sprint)  b |= NET_BTN_SPRINT;
    if (s->useHeld) b |= NET_BTN_USEHELD;
    if (s->jump)    b |= NET_BTN_JUMP;
    if (s->fire)    b |= NET_BTN_FIRE;
    if (s->use)     b |= NET_BTN_USE;
    if (s->swap)    b |= NET_BTN_SWAP;
    if (s->light)   b |= NET_BTN_LIGHT;
    if (s->slide)   b |= NET_BTN_SLIDE;
    if (s->dive)    b |= NET_BTN_DIVE;
    if (s->reload)  b |= NET_BTN_RELOAD;
    return b;
}

NetCmd NetPlay_Take(NetPlay *n)
{
    const SimCmd s = SimCmd_Take(&n->latch);
    return NetCmd_Make(s.move.x, s.move.y, n->look.yaw, n->look.pitch, NetPlay_Buttons(&s));
}

NetCmd NetPlay_CmdFn(void *user, uint32_t tick)
{
    (void)tick;
    return NetPlay_Take((NetPlay *)user);
}

void NetPlay_Camera(const NetPlay *n, const NetClient *c, Camera3D *out)
{
    PlayerController v = c->player;                              // a copy: the client's own player is the prediction and stays as it is
    v.position = NetClient_ViewPosAt(c, NetClient_Alpha(c));
    v.yaw = n->look.yaw;
    v.pitch = n->look.pitch;
    PlayerSyncCamera(&v);
    *out = v.camera;
    out->fovy = PlayerViewFov(&v, 0.0f);
}

bool NetPlay_SlideShown(const PlayerController *p) { return p->health.isAlive && (PlayerSprintingForward(p) || p->sliding); }
bool NetPlay_DiveShown(const PlayerController *p) { return PlayerDiveAvailable(p); }
