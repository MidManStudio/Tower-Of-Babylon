// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/input.md, section "touch_controls.h and touch_controls.c"
// ============================================================================
#include "touch_controls.h"

#include <math.h>
#include <string.h>

static const TcLayout g_layout = {
    .stickRadius = 50.0f, .knobRadius = 22.0f, .deadzone = 0.08f,
    .stickZone = { 0.0f, 44.0f, 400.0f, 406.0f },
    .stickMinX = 62.0f, .stickMaxX = 340.0f, .stickMinY = 118.0f, .stickMaxY = 388.0f,
    .sprintOn = 0.92f, .sprintOff = 0.80f, .sprintConeOn = 0.342f, .sprintConeOff = 0.707f,   // cos 70 degrees, cos 45 degrees
    .sprintIconGap = 12.0f, .idleX = 100.0f, .idleY = 350.0f,
    .firePad = { 706.0f, 352.0f, 50.0f },
    .jump = { 556.0f, 330.0f, 92.0f, 84.0f },
    .use = { 560.0f, 225.0f, 210.0f, 70.0f },
    .swap = { 450.0f, 330.0f, 92.0f, 84.0f },
    .light = { 450.0f, 150.0f, 100.0f, 66.0f },
    .slide = { 570.0f, 150.0f, 110.0f, 66.0f },
};

const TcLayout *Tc_Layout(void) { return &g_layout; }

void Tc_Init(TcState *s)
{
    memset(s, 0, sizeof *s);
    s->anchorX = g_layout.idleX;
    s->anchorY = g_layout.idleY;
}

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool HasRole(const TcState *s, int role)
{
    for (int i = 0; i < TC_MAX_TOUCHES; ++i) if (s->slot[i].used && s->slot[i].role == role) return true;
    return false;
}

static int AssignRole(TcState *s, float x, float y, const TcContext *ctx)
{
    const TcLayout *L = &g_layout;
    for (int i = 0; i < ctx->blockedCount && i < TC_MAX_BLOCKED; ++i)
        if (UiRect_Contains(ctx->blocked[i], x, y)) return TC_ROLE_IGNORE;                 // a tap-only region: the tap is handled elsewhere

    if (x < L->stickZone.x + L->stickZone.w) {                                                // the left half: the stick
        if (HasRole(s, TC_ROLE_STICK) || y < L->stickZone.y) return TC_ROLE_IGNORE;
        s->anchorX = Clampf(x, L->stickMinX, L->stickMaxX);                                   // it appears under the thumb, kept fully on screen
        s->anchorY = Clampf(y, L->stickMinY, L->stickMaxY);
        return TC_ROLE_STICK;
    }
    if (ctx->walkOnly) return TC_ROLE_IGNORE;                                                  // the inventory is open: the right half is for taps

    if (ctx->useAvailable && UiRect_Contains(L->use, x, y)) return TC_ROLE_USE;
    if (ctx->swapAvailable && UiRect_Contains(L->swap, x, y)) return TC_ROLE_SWAP;
    if (ctx->lightAvailable && UiRect_Contains(L->light, x, y)) return TC_ROLE_LIGHT;
    if (ctx->slideAvailable && UiRect_Contains(L->slide, x, y)) return TC_ROLE_SLIDE;
    if (UiRect_Contains(L->jump, x, y)) return TC_ROLE_JUMP;
    if (UiCircle_Contains(L->firePad, x, y)) return HasRole(s, TC_ROLE_FIRE) ? TC_ROLE_IGNORE : TC_ROLE_FIRE;
    return HasRole(s, TC_ROLE_LOOK) ? TC_ROLE_IGNORE : TC_ROLE_LOOK;                          // a free spot on the right: the camera finger
}

void Tc_Update(TcState *s, const TcTouch *touches, int count, const TcContext *ctx, TcOut *out)
{
    const TcLayout *L = &g_layout;
    memset(out, 0, sizeof *out);
    if (count > TC_MAX_TOUCHES) count = TC_MAX_TOUCHES;

    // 1. A finger that lifted gives up its job.
    for (int i = 0; i < TC_MAX_TOUCHES; ++i) {
        if (!s->slot[i].used) continue;
        bool still = false;
        for (int t = 0; t < count; ++t) if (touches[t].id == s->slot[i].id) { still = true; break; }
        if (!still) s->slot[i].used = false;
    }
    // 2. The inventory just opened: every finger but the stick becomes a plain tap, so a look finger does not carry on turning the camera.
    if (ctx->walkOnly && !s->prevWalkOnly)
        for (int i = 0; i < TC_MAX_TOUCHES; ++i)
            if (s->slot[i].used && s->slot[i].role != TC_ROLE_STICK) s->slot[i].role = TC_ROLE_IGNORE;
    s->prevWalkOnly = ctx->walkOnly;

    // 3. A finger that just landed is given a job, for as long as it stays down.
    for (int t = 0; t < count; ++t) {
        bool known = false;
        for (int i = 0; i < TC_MAX_TOUCHES; ++i) if (s->slot[i].used && s->slot[i].id == touches[t].id) { known = true; break; }
        if (known) continue;
        int free = -1;
        for (int i = 0; i < TC_MAX_TOUCHES; ++i) if (!s->slot[i].used) { free = i; break; }
        if (free < 0) continue;
        TcSlot *sl = &s->slot[free];
        sl->used = true; sl->fresh = true; sl->id = touches[t].id;
        sl->lastX = touches[t].x; sl->lastY = touches[t].y;
        sl->role = AssignRole(s, touches[t].x, touches[t].y, ctx);
    }

    // 4. What each finger is doing now.
    float mag = 0.0f, ux = 0.0f, uy = 0.0f;
    for (int i = 0; i < TC_MAX_TOUCHES; ++i) {
        TcSlot *sl = &s->slot[i];
        if (!sl->used) continue;
        float px = sl->lastX, py = sl->lastY;
        for (int t = 0; t < count; ++t) if (touches[t].id == sl->id) { px = touches[t].x; py = touches[t].y; break; }
        switch (sl->role) {
            case TC_ROLE_STICK: {
                const float dx = px - s->anchorX, dy = py - s->anchorY, d = sqrtf(dx * dx + dy * dy);
                out->stickActive = true;
                out->anchorX = s->anchorX; out->anchorY = s->anchorY;
                mag = Clampf(d / L->stickRadius, 0.0f, 1.0f);
                if (d > 0.001f) { ux = dx / d; uy = dy / d; }
                const float shown = d < L->stickRadius ? d : L->stickRadius;
                out->knobX = s->anchorX + ux * shown; out->knobY = s->anchorY + uy * shown;
                if (mag > L->deadzone) {
                    const float scaled = (mag - L->deadzone) / (1.0f - L->deadzone);
                    out->moveX = ux * scaled; out->moveY = uy * scaled;
                }
                break;
            }
            case TC_ROLE_LOOK:
                out->lookActive = true;
                out->lookDX += px - sl->lastX; out->lookDY += py - sl->lastY;
                break;
            case TC_ROLE_FIRE:                                                   // the fire finger aims as well
                out->fireHeld = true;
                out->lookDX += px - sl->lastX; out->lookDY += py - sl->lastY;
                break;
            case TC_ROLE_JUMP: out->jumpHeld = true; break;
            case TC_ROLE_USE:  out->useHeld = true; break;
            case TC_ROLE_SWAP: out->swapHeld = true; break;
            case TC_ROLE_LIGHT: if (sl->fresh) out->lightPressed = true; break;
            case TC_ROLE_SLIDE: if (sl->fresh) out->slidePressed = true; break;
            default: break;
        }
        sl->lastX = px; sl->lastY = py; sl->fresh = false;
    }

    // 5. Sprint: the knob out at the rim, and ahead. Once started it holds until the knob comes in or swings away.
    if (!out->stickActive) {
        s->sprinting = false;
    } else {
        const float ahead = -uy;                                                  // forward is up the screen, -y
        if (!s->sprinting) { if (mag >= L->sprintOn && ahead >= L->sprintConeOn) s->sprinting = true; }
        else if (mag < L->sprintOff || ahead < L->sprintConeOff) s->sprinting = false;
        out->sprint = s->sprinting;
        out->sprintFill = Clampf((mag - 0.55f) / (L->sprintOn - 0.55f), 0.0f, 1.0f);
    }

    // 6. What the game allows right now.
    if (ctx->locked) {
        out->moveX = out->moveY = out->lookDX = out->lookDY = 0.0f;
        out->sprint = out->fireHeld = out->jumpHeld = out->useHeld = out->swapHeld = out->lightPressed = out->slidePressed = false;
    } else if (ctx->walkOnly) {
        out->lookDX = out->lookDY = 0.0f;
        out->fireHeld = out->jumpHeld = out->useHeld = out->swapHeld = out->lightPressed = out->slidePressed = false;
    }
}
