// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/input.md, section "touch_controls.h and touch_controls.c"
// ============================================================================
#include "touch_controls.h"

#include <math.h>
#include <string.h>

// Canvas pixels (800 x 450). Measured off the hand-drawn layout sketch (1920 x 1080, scaled by 5/12): stick ring r 87 with an inner
// disc r 56, the sprint icon r 22 above it, PICKUP r 46 and JUMP r 37 on the upper right, the fire pad r 71 with a disc r 42, SLIDE
// r 37 to its left. The sketch has no SWAP or LIGHT button: they sit in free spots, SWAP between PICKUP and the pad (the thumb
// leaves the pad for a moment) and LIGHT above them, both away from where the look finger works.
static const TcLayout g_layout = {
    .stickRadius = 87.0f, .stickBaseRadius = 56.0f, .knobRadius = 30.0f, .deadzone = 0.04f, .stickFullAt = 0.50f,
    .stickGlideRate = 12.0f, .stickReleaseRate = 6.0f,
    .stickZone = { 0.0f, 44.0f, 400.0f, 406.0f },
    .stickMinX = 99.0f, .stickMaxX = 340.0f, .stickMinY = 160.0f, .stickMaxY = 363.0f,
    .sprintOn = 0.92f, .sprintOff = 0.80f,
    .sprintIconGap = 23.0f, .sprintIconRadius = 22.0f, .idleX = 128.0f, .idleY = 326.0f,
    .firePad = { 724.0f, 315.0f, 71.0f }, .fireInnerRadius = 42.0f,
    .lookGain = 2.0f, .fireLookGain = 1.0f, .aimDeadzone = 0.10f, .aimFullAt = 0.85f, .aimCurve = 1.6f, .aimRatePx = 720.0f,
    .jump = { 760.0f, 175.0f, 37.0f },
    .use = { 639.0f, 172.0f, 46.0f },
    .swap = { 632.0f, 258.0f, 30.0f },
    .light = { 700.0f, 88.0f, 32.0f },
    .slide = { 606.0f, 357.0f, 37.0f },
    .reload = { 668.0f, 408.0f, 30.0f },       // not in the sketch: the one free spot big enough, clear of SLIDE and the fire pad by 13 and 7 px
    .dive = { 606.0f, 357.0f, 37.0f },         // where SLIDE is: they are never there together (a slide needs a forward sprint on the ground, a dive the air after a sprinting jump)
};

const TcLayout *Tc_Layout(void) { return &g_layout; }

void Tc_Init(TcState *s)
{
    memset(s, 0, sizeof *s);
    s->anchorX = g_layout.idleX;
    s->anchorY = g_layout.idleY;
}

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The move stick's value is not the finger's position but a point that chases it at a fixed speed in ring radii per SECOND, and falls back to
// the centre at another when the finger comes in or lifts. That is what makes a stick feel smooth: a thumb that lands or flicks does not turn
// the player from stopped to full speed in one frame, and one that lifts does not stop it dead. (The same idea as the Unity MID_Joystick, whose
// stick image slews toward the finger by a step per frame and whose axis is read from the image; here the step is per second, so a 30, 60 or 120
// Hz screen feels the same.) With no frame time, or a rate of 0, it is the finger exactly.
static void Glide(TcState *s, float tx, float ty, float dt, const TcLayout *L)
{
    if (dt <= 0.0f || L->stickGlideRate <= 0.0f || L->stickReleaseRate <= 0.0f) { s->glideX = tx; s->glideY = ty; return; }
    if (dt > 0.1f) dt = 0.1f;                                           // a frame that took long (the tab was away) is not a reason to jump
    const float dx = tx - s->glideX, dy = ty - s->glideY, d = sqrtf(dx * dx + dy * dy);
    const float tm = sqrtf(tx * tx + ty * ty), cm = sqrtf(s->glideX * s->glideX + s->glideY * s->glideY);
    const float step = (tm >= cm ? L->stickGlideRate : L->stickReleaseRate) * dt;      // going out (or round) is quick, coming in is a little slower
    if (d <= step) { s->glideX = tx; s->glideY = ty; return; }
    s->glideX += dx / d * step; s->glideY += dy / d * step;
}

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

    if (ctx->useAvailable && UiCircle_Contains(L->use, x, y)) return TC_ROLE_USE;
    if (ctx->swapAvailable && UiCircle_Contains(L->swap, x, y)) return TC_ROLE_SWAP;
    if (ctx->lightAvailable && UiCircle_Contains(L->light, x, y)) return TC_ROLE_LIGHT;
    if (ctx->diveAvailable && UiCircle_Contains(L->dive, x, y)) return TC_ROLE_DIVE;
    if (ctx->slideAvailable && UiCircle_Contains(L->slide, x, y)) return TC_ROLE_SLIDE;
    if (ctx->reloadAvailable && UiCircle_Contains(L->reload, x, y)) return TC_ROLE_RELOAD;
    if (UiCircle_Contains(L->jump, x, y)) return TC_ROLE_JUMP;
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
    bool stickDown = false;
    for (int i = 0; i < TC_MAX_TOUCHES; ++i) {
        TcSlot *sl = &s->slot[i];
        if (!sl->used) continue;
        float px = sl->lastX, py = sl->lastY;
        for (int t = 0; t < count; ++t) if (touches[t].id == sl->id) { px = touches[t].x; py = touches[t].y; break; }
        switch (sl->role) {
            case TC_ROLE_STICK: {
                const float dx = px - s->anchorX, dy = py - s->anchorY, d = sqrtf(dx * dx + dy * dy);
                stickDown = true;
                mag = Clampf(d / L->stickRadius, 0.0f, 1.0f);
                if (d > 0.001f) { ux = dx / d; uy = dy / d; }
                break;
            }
            case TC_ROLE_LOOK:
                out->lookActive = true;
                out->lookDX += (px - sl->lastX) * L->lookGain; out->lookDY += (py - sl->lastY) * L->lookGain;
                break;
            case TC_ROLE_FIRE: {                                                 // the fire finger aims as well, from a pad that stays where it is
                out->fireHeld = true;
                const float dx = px - L->firePad.x, dy = py - L->firePad.y, d = sqrtf(dx * dx + dy * dy);
                const float fx = d > 0.001f ? dx / d : 0.0f, fy = d > 0.001f ? dy / d : 0.0f;
                const float shown = d < L->firePad.r ? d : L->firePad.r;
                if (ctx->aimMode == TC_AIM_DRAG) {                               // turn by how far the finger moved, one to one: the disc stays where it is
                    out->lookDX += (px - sl->lastX) * L->fireLookGain; out->lookDY += (py - sl->lastY) * L->fireLookGain;
                } else {                                                         // a joystick: the knob follows the finger, and the camera turns at a rate set by how far out it is
                    out->aimActive = true;
                    out->aimKnobX = L->firePad.x + fx * shown; out->aimKnobY = L->firePad.y + fy * shown;
                    const float m = Clampf(d / L->firePad.r, 0.0f, 1.0f);
                    if (m > L->aimDeadzone) {
                        const float t = powf(Clampf((m - L->aimDeadzone) / (L->aimFullAt - L->aimDeadzone), 0.0f, 1.0f), L->aimCurve);
                        out->aimX = fx * t; out->aimY = fy * t;
                        out->lookDX += out->aimX * L->aimRatePx * ctx->dt; out->lookDY += out->aimY * L->aimRatePx * ctx->dt;
                    }
                }
                break;
            }
            case TC_ROLE_JUMP: out->jumpHeld = true; break;
            case TC_ROLE_USE:  out->useHeld = true; break;
            case TC_ROLE_SWAP: out->swapHeld = true; break;
            case TC_ROLE_LIGHT: if (sl->fresh) out->lightPressed = true; break;
            case TC_ROLE_SLIDE: if (sl->fresh) out->slidePressed = true; break;
            case TC_ROLE_RELOAD: if (sl->fresh) out->reloadPressed = true; break;
            case TC_ROLE_DIVE: if (sl->fresh) out->divePressed = true; break;
            default: break;
        }
        sl->lastX = px; sl->lastY = py; sl->fresh = false;
    }

    // 4b. The move stick: the value that moves the player and draws the knob is the finger's deflection, smoothed (Glide). It keeps going for a
    // moment after the finger lifts, and the stick stays drawn until it has come to rest.
    Glide(s, stickDown ? ux * mag : 0.0f, stickDown ? uy * mag : 0.0f, ctx->dt, L);
    {
        const float gm = sqrtf(s->glideX * s->glideX + s->glideY * s->glideY);
        if (stickDown || gm > 0.0f) {
            out->stickActive = true;
            out->anchorX = s->anchorX; out->anchorY = s->anchorY;
            const float shown = gm < 1.0f ? gm : 1.0f;
            const float gx = gm > 0.0001f ? s->glideX / gm : 0.0f, gy = gm > 0.0001f ? s->glideY / gm : 0.0f;
            out->knobX = s->anchorX + gx * shown * L->stickRadius; out->knobY = s->anchorY + gy * shown * L->stickRadius;
            if (gm > L->deadzone) {                                                        // full speed at stickFullAt of the ring, not at its rim
                const float scaled = Clampf((gm - L->deadzone) / (L->stickFullAt - L->deadzone), 0.0f, 1.0f);
                out->moveX = gx * scaled; out->moveY = gy * scaled;
            }
        }
    }

    // 5. Sprint: the knob out at the rim, in any direction (omni sprint: how fast each direction goes is up to the player controller).
    // Once started it holds until the knob comes back in.
    if (!stickDown) {                                                       // the finger lifted: the sprint ends now, even while the stick coasts to rest
        s->sprinting = false;
    } else {
        if (!s->sprinting) { if (mag >= L->sprintOn) s->sprinting = true; }
        else if (mag < L->sprintOff) s->sprinting = false;
        out->sprint = s->sprinting;
        out->sprintFill = Clampf((mag - 0.55f) / (L->sprintOn - 0.55f), 0.0f, 1.0f);
    }

    // 6. What the game allows right now.
    if (ctx->locked) {
        out->moveX = out->moveY = out->lookDX = out->lookDY = out->aimX = out->aimY = 0.0f;
        out->sprint = out->fireHeld = out->jumpHeld = out->useHeld = out->swapHeld = out->lightPressed = out->slidePressed = out->reloadPressed = out->divePressed = false;
        out->aimActive = false;
    } else if (ctx->walkOnly) {
        out->lookDX = out->lookDY = out->aimX = out->aimY = 0.0f;
        out->fireHeld = out->jumpHeld = out->useHeld = out->swapHeld = out->lightPressed = out->slidePressed = out->reloadPressed = out->divePressed = false;
        out->aimActive = false;
    }
}
