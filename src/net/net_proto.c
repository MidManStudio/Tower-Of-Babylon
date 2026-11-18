// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_proto.h and net_proto.c"
// ============================================================================
#include "net_proto.h"

#include <math.h>
#include <string.h>

#include "net_wire.h"

#define TWO_PI 6.28318530718f
#define PITCH_MAX 1.5533430f                 // 89 degrees

NetCmd NetCmd_Make(float moveX, float moveY, float yawRad, float pitchRad, uint16_t buttons)
{
    NetCmd c;
    if (!(moveX == moveX)) moveX = 0.0f;                                   // NaN to nothing
    if (!(moveY == moveY)) moveY = 0.0f;
    if (moveX > 1.0f) moveX = 1.0f;
    if (moveX < -1.0f) moveX = -1.0f;
    if (moveY > 1.0f) moveY = 1.0f;
    if (moveY < -1.0f) moveY = -1.0f;
    c.moveX = (int8_t)lroundf(moveX * 127.0f);
    c.moveY = (int8_t)lroundf(moveY * 127.0f);
    if (!(yawRad == yawRad) || !(pitchRad == pitchRad)) { yawRad = 0.0f; pitchRad = 0.0f; }
    float y = fmodf(yawRad, TWO_PI);
    if (y < 0.0f) y += TWO_PI;
    c.yaw = (uint16_t)((uint32_t)lroundf(y / TWO_PI * 65536.0f) & 0xffffu);
    if (pitchRad > PITCH_MAX) pitchRad = PITCH_MAX;
    if (pitchRad < -PITCH_MAX) pitchRad = -PITCH_MAX;
    c.pitch = (int16_t)lroundf(pitchRad / PITCH_MAX * 32767.0f);
    c.buttons = buttons;
    return c;
}

void NetCmd_Move(const NetCmd *c, float *x, float *y)
{
    float fx = (float)c->moveX / 127.0f, fy = (float)c->moveY / 127.0f;
    if (fx < -1.0f) fx = -1.0f;                                           // -128 is one step past -1
    if (fy < -1.0f) fy = -1.0f;
    const float l = sqrtf(fx * fx + fy * fy);
    if (l > 1.0f) { fx /= l; fy /= l; }
    *x = fx; *y = fy;
}
float NetCmd_Yaw(const NetCmd *c) { return (float)c->yaw / 65536.0f * TWO_PI; }
float NetCmd_Pitch(const NetCmd *c) { return (float)c->pitch / 32767.0f * PITCH_MAX; }

static void PutCmd(NetW *w, const NetCmd *c) { NetW_I8(w, c->moveX); NetW_I8(w, c->moveY); NetW_U16(w, c->yaw); NetW_I16(w, c->pitch); NetW_U16(w, c->buttons); }
static void GetCmd(NetR *r, NetCmd *c) { c->moveX = NetR_I8(r); c->moveY = NetR_I8(r); c->yaw = NetR_U16(r); c->pitch = NetR_I16(r); c->buttons = NetR_U16(r); }

static void PutOwner(NetW *w, const NetOwnerState *o)
{
    const float f[20] = { o->px, o->py, o->pz, o->velY, o->moveVx, o->moveVz, o->knockX, o->knockZ, o->stamina, o->staminaDelay,
                          o->slideT, o->slideSpeed0, o->slideCooldown, o->slideDirX, o->slideDirZ,
                          o->diveT, o->diveDirX, o->diveDirZ, o->lieStickX, o->lieStickY };
    for (int i = 0; i < 20; ++i) NetW_F32(w, f[i]);
    NetW_U8(w, o->flags);
}
static bool GetOwner(NetR *r, NetOwnerState *o)
{
    float f[20];
    for (int i = 0; i < 20; ++i) { f[i] = NetR_F32(r); if (!isfinite(f[i])) return false; }
    o->px = f[0]; o->py = f[1]; o->pz = f[2]; o->velY = f[3]; o->moveVx = f[4]; o->moveVz = f[5]; o->knockX = f[6]; o->knockZ = f[7];
    o->stamina = f[8]; o->staminaDelay = f[9]; o->slideT = f[10]; o->slideSpeed0 = f[11]; o->slideCooldown = f[12]; o->slideDirX = f[13]; o->slideDirZ = f[14];
    o->diveT = f[15]; o->diveDirX = f[16]; o->diveDirZ = f[17]; o->lieStickX = f[18]; o->lieStickY = f[19];
    o->flags = NetR_U8(r);
    return true;                                                           // all eight flag bits mean something
}

static bool CharacterOk(const char *s, size_t n)                          // lowercase letters, digits and _ (a roster id), at most NET_CHARACTER_LEN - 1
{
    if (n > NET_CHARACTER_LEN - 1) return false;
    for (size_t i = 0; i < n; ++i) if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) return false;
    return true;
}

size_t NetProto_Encode(const NetMsg *m, uint8_t *buf, size_t cap)
{
    NetW w; NetW_Init(&w, buf, cap);
    NetW_U8(&w, 'T'); NetW_U8(&w, 'B');
    switch (m->kind) {
    case NET_MSG_HELLO: {
        const NetHello *h = &m->u.hello;
        size_t n = 0;
        while (n < NET_CHARACTER_LEN && h->character[n]) ++n;
        if (n >= NET_CHARACTER_LEN || !CharacterOk(h->character, n)) return 0;
        NetW_U8(&w, 16); NetW_U8(&w, h->version); NetW_U8(&w, (uint8_t)n); NetW_Bytes(&w, h->character, n);
        break; }
    case NET_MSG_WELCOME: {
        const NetWelcome *v = &m->u.welcome;
        if (!isfinite(v->spawnX) || !isfinite(v->spawnZ)) return 0;
        NetW_U8(&w, 17); NetW_U8(&w, v->version); NetW_U8(&w, v->tickRate); NetW_U8(&w, v->playerId); NetW_U8(&w, v->maxPlayers);
        NetW_U32(&w, v->floorSeed); NetW_I32(&w, v->floorNumber); NetW_U32(&w, v->serverTick); NetW_F32(&w, v->spawnX); NetW_F32(&w, v->spawnZ);
        break; }
    case NET_MSG_DISCONNECT:
        NetW_U8(&w, 18); NetW_U8(&w, m->u.disc.reason);
        break;
    case NET_MSG_INPUT: {
        const NetInputMsg *in = &m->u.input;
        if (in->count < 1 || in->count > NET_INPUT_REDUNDANCY) return 0;
        NetW_U8(&w, 1); NetW_U32(&w, in->tick); NetW_U32(&w, in->ackServerTick); NetW_U8(&w, in->count);
        for (int i = 0; i < in->count; ++i) PutCmd(&w, &in->cmds[i]);
        break; }
    case NET_MSG_SNAPSHOT: {
        const NetSnapshot *s = &m->u.snap;
        if (s->nRemote > NET_MAX_PLAYERS - 1) return 0;
        NetW_U8(&w, 2); NetW_U32(&w, s->serverTick); NetW_U8(&w, s->started ? 1 : 0); NetW_I8(&w, s->margin);
        PutOwner(&w, &s->owner);
        NetW_U8(&w, s->nRemote);
        for (int i = 0; i < s->nRemote; ++i) {
            const NetRemote *r = &s->remote[i];
            NetW_U8(&w, r->id); NetW_I32(&w, r->px); NetW_I32(&w, r->py); NetW_I32(&w, r->pz); NetW_U16(&w, r->yaw); NetW_I16(&w, r->pitch); NetW_U8(&w, r->flags);
        }
        break; }
    default: return 0;
    }
    return w.err ? 0 : w.len;
}

bool NetProto_Decode(const uint8_t *buf, size_t len, NetMsg *out)
{
    memset(out, 0, sizeof *out);
    NetR r; NetR_Init(&r, buf, len);
    if (NetR_U8(&r) != 'T' || NetR_U8(&r) != 'B') return false;
    const uint8_t kind = NetR_U8(&r);
    switch (kind) {
    case 16: {
        NetHello *h = &out->u.hello;
        out->kind = NET_MSG_HELLO;
        h->version = NetR_U8(&r);
        const uint8_t n = NetR_U8(&r);
        if (n > NET_CHARACTER_LEN - 1) return false;
        NetR_Bytes(&r, h->character, n);
        if (!CharacterOk(h->character, n)) return false;
        break; }
    case 17: {
        NetWelcome *v = &out->u.welcome;
        out->kind = NET_MSG_WELCOME;
        v->version = NetR_U8(&r); v->tickRate = NetR_U8(&r); v->playerId = NetR_U8(&r); v->maxPlayers = NetR_U8(&r);
        v->floorSeed = NetR_U32(&r); v->floorNumber = NetR_I32(&r); v->serverTick = NetR_U32(&r); v->spawnX = NetR_F32(&r); v->spawnZ = NetR_F32(&r);
        if (!isfinite(v->spawnX) || !isfinite(v->spawnZ)) return false;
        if (v->tickRate < 1 || v->maxPlayers < 1 || v->maxPlayers > NET_MAX_PLAYERS || v->playerId >= v->maxPlayers) return false;
        break; }
    case 18:
        out->kind = NET_MSG_DISCONNECT;
        out->u.disc.reason = NetR_U8(&r);
        if (out->u.disc.reason > NET_DISC_BAD) return false;
        break;
    case 1: {
        NetInputMsg *in = &out->u.input;
        out->kind = NET_MSG_INPUT;
        in->tick = NetR_U32(&r); in->ackServerTick = NetR_U32(&r); in->count = NetR_U8(&r);
        if (in->count < 1 || in->count > NET_INPUT_REDUNDANCY) return false;
        for (int i = 0; i < in->count; ++i) GetCmd(&r, &in->cmds[i]);
        break; }
    case 2: {
        NetSnapshot *s = &out->u.snap;
        out->kind = NET_MSG_SNAPSHOT;
        s->serverTick = NetR_U32(&r); s->started = NetR_U8(&r); s->margin = NetR_I8(&r);
        if (s->started > 1) return false;
        if (!GetOwner(&r, &s->owner)) return false;
        s->nRemote = NetR_U8(&r);
        if (s->nRemote > NET_MAX_PLAYERS - 1) return false;
        for (int i = 0; i < s->nRemote; ++i) {
            NetRemote *m = &s->remote[i];
            m->id = NetR_U8(&r); m->px = NetR_I32(&r); m->py = NetR_I32(&r); m->pz = NetR_I32(&r); m->yaw = NetR_U16(&r); m->pitch = NetR_I16(&r); m->flags = NetR_U8(&r);
            if (m->id >= NET_MAX_PLAYERS || (m->flags & ~(NET_REM_SPRINTING | NET_REM_SLIDING | NET_REM_GROUNDED))) return false;
        }
        break; }
    default: return false;
    }
    return NetR_Done(&r);
}
