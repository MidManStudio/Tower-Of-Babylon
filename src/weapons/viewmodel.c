// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/weapons.md, section "viewmodel.h and viewmodel.c"
// ============================================================================
#include "viewmodel.h"

#include <math.h>

#include "rlgl.h"
#include "../ui/view_clip.h"

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }
static Vector3 Lerp3(Vector3 a, Vector3 b, float t) { return V(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }

static const Color VM_SKIN   = { 224, 172, 138, 255 };
static const Color VM_SLEEVE = { 206, 92, 22, 255 };
static const Color VM_STEEL  = { 118, 122, 130, 255 };
static const Color VM_TAPE   = { 30, 30, 34, 255 };
static const Color VM_RED    = { 170, 30, 30, 255 };
static const Color VM_DARK   = { 74, 78, 90, 255 };
static const Color VM_WOOD   = { 78, 58, 44, 255 };
static const Color VM_FLASH  = { 255, 238, 150, 255 };
static const Color VM_POLY   = { 44, 46, 52, 255 };      // rifle furniture

// ---- Pose: a rotation about a pivot, then a shift, in the hand's own space -----------------
typedef struct { Vector3 pivot; float roll, pitch, yaw; Vector3 t; } Xf;

static Vector3 XfApply(const Xf *x, Vector3 p)
{
    Vector3 q = V(p.x - x->pivot.x, p.y - x->pivot.y, p.z - x->pivot.z);
    float c = cosf(x->roll), s = sinf(x->roll);          // roll: x toward y (the top of a held pipe leans left when positive)
    q = V(q.x * c - q.y * s, q.x * s + q.y * c, q.z);
    c = cosf(x->pitch); s = sinf(x->pitch);              // pitch: the front (z) rises; negative pitch brings the top of a pipe forward and down
    q = V(q.x, q.y * c + q.z * s, -q.y * s + q.z * c);
    c = cosf(x->yaw); s = sinf(x->yaw);                  // yaw: the front turns toward +x
    q = V(q.x * c + q.z * s, q.y, -q.x * s + q.z * c);
    return V(q.x + x->pivot.x + x->t.x, q.y + x->pivot.y + x->t.y, q.z + x->pivot.z + x->t.z);
}

// roll: the whole hand-and-gun picture turned about the view's centre line (radians; positive leans the top to the left). The aimed gun's
// sway to the side. Its sine and cosine are kept so every point is turned the same way.
typedef struct { const AimFrame *cam; PartList *list; float rollSin, rollCos; } Ctx;

// The hands are authored once, around a wrist at WRIST, then lifted a little into view. Rest() does
// that, so every part of a hand or held item goes through it first.
#define HAND_SCALE 1.00f
static const Vector3 WRIST = { 0.20f, -0.22f, 0.40f };
static const Vector3 REST_SHIFT = { -0.070f, 0.100f, 0.000f };

static Vector3 Rest(Vector3 q)
{
    return V(WRIST.x + (q.x - WRIST.x) * HAND_SCALE + REST_SHIFT.x,
             WRIST.y + (q.y - WRIST.y) * HAND_SCALE + REST_SHIFT.y,
             WRIST.z + (q.z - WRIST.z) * HAND_SCALE + REST_SHIFT.z);
}

// A point in the right hand's space (x toward the outside of the arm, y up, z forward) as a world point.
// side is +1 for the right hand and -1 for the left, which is the right hand mirrored.
static Vector3 World(const Ctx *c, int side, Vector3 p)
{
    const float hx = p.x * (float)side;
    const float x = hx * c->rollCos - p.y * c->rollSin, y = hx * c->rollSin + p.y * c->rollCos;     // turned about the view's centre line
    return V(c->cam->eye.x + c->cam->right.x * x + c->cam->up.x * y + c->cam->forward.x * p.z,
             c->cam->eye.y + c->cam->right.y * x + c->cam->up.y * y + c->cam->forward.y * p.z,
             c->cam->eye.z + c->cam->right.z * x + c->cam->up.z * y + c->cam->forward.z * p.z);
}

// The cross-sections' flat side follows the turn too (the camera's up, rolled the same way).
static Vector3 UpHint(const Ctx *c)
{
    return V(c->cam->up.x * c->rollCos - c->cam->right.x * c->rollSin,
             c->cam->up.y * c->rollCos - c->cam->right.y * c->rollSin,
             c->cam->up.z * c->rollCos - c->cam->right.z * c->rollSin);
}

// The tip and middle of the pipe in the last pose built.
static Vector3 g_pipeTip, g_pipeMid;
static bool    g_pipeInHand = false;

// ---- One hand and forearm -------------------------------------------------------------------
// Rest pose: a loose fist low in the corner of the view, knuckles forward. The forearm runs back
// from the wrist to a fixed elbow that is off screen, so it stretches when the hand moves.
// The fist is made of rounded blobs (a palm, a row of knuckles, two joints per finger and a thumb) that
// are smooth shaded and closed, so it reads as a fist from any angle. The first versions were boxes and
// thin cylinders, which looked like shards at the size the hands have on the phone.
static void AddHand(const Ctx *c, int side, const Xf *xf)
{
    #define P(q) World(c, side, XfApply(xf, Rest(q)))
    const Vector3 up = UpHint(c);

    const Vector3 elbow = World(c, side, V(0.36f, -0.66f, -0.10f));
    const Vector3 wrist = P(V(0.20f, -0.22f, 0.40f));
    const Vector3 sleeveEnd = Lerp3(elbow, wrist, 0.80f);
    PartList_Add(c->list, Part_Cyl(elbow, sleeveEnd, 0.092f, 0.060f, 8, VM_SLEEVE));
    PartList_Add(c->list, Part_Cyl(sleeveEnd, wrist, 0.040f, 0.031f, 8, VM_SKIN));

    // Back of the hand
    PartList_Add(c->list, Part_Blob(P(V(0.20f, -0.224f, 0.455f)), P(V(0.20f, -0.222f, 0.520f)), 0.053f, 0.84f, up, VM_SKIN));
    for (int i = 0; i < 4; ++i) {
        const float fx = 0.20f + (-0.0375f + 0.025f * (float)i);
        // knuckle, then the finger curling forward and down, then the tip tucked under
        PartList_Add(c->list, Part_BlobLow(P(V(fx, -0.204f, 0.504f)), P(V(fx, -0.204f, 0.520f)), 0.0150f, 1.0f, up, VM_SKIN));
        PartList_Add(c->list, Part_BlobLow(P(V(fx, -0.216f, 0.530f)), P(V(fx, -0.226f, 0.556f)), 0.0142f, 1.0f, up, VM_SKIN));
        PartList_Add(c->list, Part_BlobLow(P(V(fx, -0.248f, 0.549f)), P(V(fx, -0.268f, 0.543f)), 0.0134f, 1.0f, up, VM_SKIN));
    }
    // Thumb along the side, over the first fingers
    PartList_Add(c->list, Part_Blob(P(V(0.166f, -0.212f, 0.478f)), P(V(0.176f, -0.205f, 0.532f)), 0.0185f, 1.0f, up, VM_SKIN));
    #undef P
}

// ---- Held items (right hand space, same transform as the right hand) -------------------------
static void AddPipe(const Ctx *c, const Xf *xf)
{
    #define P(q) World(c, 1, XfApply(xf, Rest(q)))
    const Vector3 up = UpHint(c);
    const Vector3 base = P(V(0.205f, -0.34f, 0.462f)), top = P(V(0.250f, 0.10f, 0.690f));
    PartList_Add(c->list, Part_Cyl(base, top, 0.024f, 0.027f, 8, VM_STEEL));
    PartList_Add(c->list, Part_Blob(top, P(V(0.2535f, 0.1185f, 0.6985f)), 0.027f, 1.0f, up, VM_STEEL));                    // rounded end
    PartList_Add(c->list, Part_Cyl(P(V(0.205f, -0.30f, 0.465f)), P(V(0.210f, -0.15f, 0.490f)), 0.030f, 0.030f, 8, VM_TAPE));   // taped grip
    PartList_Add(c->list, Part_Cyl(P(V(0.244f, 0.02f, 0.640f)), P(V(0.246f, 0.055f, 0.654f)), 0.031f, 0.031f, 8, VM_RED));     // red band
    g_pipeTip = P(V(0.2535f, 0.1185f, 0.6985f));
    g_pipeMid = P(V(0.235f, -0.05f, 0.620f));
    g_pipeInHand = true;
    #undef P
}

static void AddPistol(const Ctx *c, const Xf *xf)
{
    #define P(q) World(c, 1, XfApply(xf, Rest(q)))
    const Vector3 up = UpHint(c);
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.20f, 0.45f)), P(V(0.20f, -0.30f, 0.43f)), 0.022f, 0.026f, up, VM_WOOD));       // grip
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.150f, 0.38f)), P(V(0.20f, -0.150f, 0.68f)), 0.024f, 0.030f, up, VM_DARK));    // slide
    PartList_Add(c->list, Part_Cyl(P(V(0.20f, -0.155f, 0.68f)), P(V(0.20f, -0.155f, 0.74f)), 0.014f, 0.014f, 8, VM_TAPE));      // muzzle
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.114f, 0.65f)), P(V(0.20f, -0.114f, 0.668f)), 0.004f, 0.007f, up, VM_STEEL));   // front sight
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.197f, 0.455f)), P(V(0.20f, -0.215f, 0.54f)), 0.006f, 0.004f, up, VM_DARK));    // trigger guard
    #undef P
}

// The muzzle flash at a muzzle y and z (in the hand's space), s times the pistol's size.
static void AddFlash(const Ctx *c, const Xf *xf, float flash, float muzzleY, float muzzleZ, float size)
{
    if (flash <= 0.0f) return;
    #define P(q) World(c, 1, XfApply(xf, Rest(q)))
    const Vector3 up = UpHint(c);
    const float s = (0.5f + 0.5f * flash) * size;
    const Vector3 tip = P(V(0.20f, muzzleY, muzzleZ)), far = P(V(0.20f, muzzleY, muzzleZ + 0.22f * s));
    PartList_Add(c->list, Part_Cyl(tip, far, 0.050f * s, 0.006f, 6, VM_FLASH));
    PartList_Add(c->list, Part_Box(P(V(0.20f - 0.12f * s, muzzleY, muzzleZ + 0.02f)), P(V(0.20f + 0.12f * s, muzzleY, muzzleZ + 0.02f)), 0.008f, 0.008f, up, VM_FLASH));
    PartList_Add(c->list, Part_Box(P(V(0.20f, muzzleY - 0.12f * s, muzzleZ + 0.02f)), P(V(0.20f, muzzleY + 0.12f * s, muzzleZ + 0.02f)), 0.008f, 0.008f, up, VM_FLASH));
    #undef P
}

// The pump-action shotgun. There is no stock: it is tucked into the shoulder, behind the camera, as in most first person games,
// and drawn so close to the lens it would only be a block in the corner. The right hand holds the grip behind the receiver; the left hand (moved out to the forearm in
// Viewmodel_Build) works the pump, which slides back and forward by pump metres. All in the right hand's space.
static void AddShotgun(const Ctx *c, const Xf *xf, float pump)
{
    #define P(q) World(c, 1, XfApply(xf, Rest(q)))
    const Vector3 up = UpHint(c);
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.20f, 0.46f)), P(V(0.20f, -0.29f, 0.43f)), 0.022f, 0.026f, up, VM_WOOD));          // grip
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.150f, 0.36f)), P(V(0.20f, -0.150f, 0.62f)), 0.028f, 0.032f, up, VM_DARK));       // receiver
    PartList_Add(c->list, Part_Cyl(P(V(0.20f, -0.145f, 0.62f)), P(V(0.20f, -0.145f, 1.00f)), 0.0135f, 0.0135f, 8, VM_STEEL));     // barrel
    PartList_Add(c->list, Part_Cyl(P(V(0.20f, -0.145f, 1.00f)), P(V(0.20f, -0.145f, 1.03f)), 0.0165f, 0.0165f, 8, VM_TAPE));      // muzzle
    PartList_Add(c->list, Part_Cyl(P(V(0.20f, -0.188f, 0.62f)), P(V(0.20f, -0.188f, 0.92f)), 0.0125f, 0.0125f, 8, VM_DARK));      // magazine tube
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.190f, 0.72f + pump)), P(V(0.20f, -0.190f, 0.92f + pump)), 0.025f, 0.027f, up, VM_WOOD));   // the pump
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.118f, 0.96f)), P(V(0.20f, -0.118f, 0.975f)), 0.004f, 0.006f, up, VM_STEEL));     // bead sight
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.197f, 0.455f)), P(V(0.20f, -0.215f, 0.54f)), 0.006f, 0.004f, up, VM_DARK));      // trigger guard
    #undef P
}

// The assault rifle: a polymer body with a handguard, a curved-looking magazine, a stock, sights and a muzzle brake.
static void AddRifle(const Ctx *c, const Xf *xf)
{
    #define P(q) World(c, 1, XfApply(xf, Rest(q)))
    const Vector3 up = UpHint(c);
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.20f, 0.46f)), P(V(0.20f, -0.29f, 0.43f)), 0.022f, 0.026f, up, VM_POLY));           // grip
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.150f, 0.36f)), P(V(0.20f, -0.150f, 0.70f)), 0.028f, 0.034f, up, VM_DARK));        // receiver
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.152f, 0.70f)), P(V(0.20f, -0.152f, 0.98f)), 0.026f, 0.028f, up, VM_POLY));        // handguard
    PartList_Add(c->list, Part_Cyl(P(V(0.20f, -0.148f, 0.98f)), P(V(0.20f, -0.148f, 1.08f)), 0.0105f, 0.0105f, 8, VM_STEEL));     // barrel
    PartList_Add(c->list, Part_Cyl(P(V(0.20f, -0.148f, 1.08f)), P(V(0.20f, -0.148f, 1.13f)), 0.0165f, 0.0165f, 8, VM_TAPE));      // muzzle brake
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.180f, 0.58f)), P(V(0.20f, -0.310f, 0.545f)), 0.018f, 0.026f, up, VM_POLY));       // magazine
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.114f, 1.02f)), P(V(0.20f, -0.114f, 1.04f)), 0.004f, 0.012f, up, VM_STEEL));       // front sight
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.108f, 0.40f)), P(V(0.20f, -0.108f, 0.43f)), 0.012f, 0.008f, up, VM_STEEL));       // rear sight
    PartList_Add(c->list, Part_Box(P(V(0.20f, -0.197f, 0.455f)), P(V(0.20f, -0.215f, 0.54f)), 0.006f, 0.004f, up, VM_DARK));       // trigger guard
    #undef P
}

// ---- Animation curves ------------------------------------------------------------------------
static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float EaseOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
static float EaseIn(float t) { return t * t; }
static float EaseInOut(float t) { return t * t * (3.0f - 2.0f * t); }
static float Mix(float a, float b, float t) { return a + (b - a) * t; }

// Punch: pull back, snap forward, return. 0 at rest, about 1 at full extension.
static float PunchCurve(float p)
{
    if (p < 0.25f) return -0.25f * EaseInOut(p / 0.25f);
    if (p < 0.50f) return -0.25f + 1.25f * EaseOut((p - 0.25f) / 0.25f);
    return 1.0f - EaseInOut((p - 0.50f) / 0.50f);
}

// The pipe's slash: a slanting overhead chop. p runs 0 to 1 over the swing; the blow lands at p = 0.5.
//   0.00 - 0.34  the arm rises and the pipe is cocked up and back over the right shoulder, leaning outward
//   0.34 - 0.50  the chop: down and across the body, front tip leading, fast (it accelerates all the way)
//   0.50 - 0.62  the follow-through, the pipe ends low and to the left
//   0.62 - 1.00  recover to the ready stance
typedef struct { float pitch, roll, yaw; Vector3 t; } SlashKey;
static const SlashKey SLASH_REST    = {  0.00f, -0.10f,  0.00f, { 0.00f,  0.00f,  0.00f } };
static const SlashKey SLASH_COCKED  = {  0.85f, -0.55f,  0.12f, { 0.08f,  0.17f, -0.05f } };
static const SlashKey SLASH_IMPACT  = { -1.55f,  0.25f, -0.14f, { -0.08f, -0.02f,  0.22f } };
static const SlashKey SLASH_THROUGH = { -1.80f,  0.38f, -0.20f, { -0.12f, -0.08f,  0.24f } };

static void SlashPose(float p, float *pitch, float *roll, float *yaw, Vector3 *t)
{
    const SlashKey *a, *b;
    float u;
    if (p < 0.34f)      { a = &SLASH_REST;    b = &SLASH_COCKED;  u = EaseInOut(p / 0.34f); }
    else if (p < 0.50f) { a = &SLASH_COCKED;  b = &SLASH_IMPACT;  u = EaseIn((p - 0.34f) / 0.16f); }
    else if (p < 0.62f) { a = &SLASH_IMPACT;  b = &SLASH_THROUGH; u = EaseOut((p - 0.50f) / 0.12f); }
    else                { a = &SLASH_THROUGH; b = &SLASH_REST;    u = EaseInOut((p - 0.62f) / 0.38f); }
    *pitch = Mix(a->pitch, b->pitch, u);
    *roll = Mix(a->roll, b->roll, u);
    *yaw = Mix(a->yaw, b->yaw, u);
    *t = Lerp3(a->t, b->t, u);
}

// How far the gun and hands slide, at full ADS, to put the sights on the centre of the view (the sight line at the hip is a little right of
// it and, for the long guns that sit lower, a little below it), in the hands' space after Rest().
#define ADS_SLIDE_X_PISTOL   -0.130f
#define ADS_SLIDE_Y_PISTOL    0.010f
#define ADS_SLIDE_X_LONG     -0.150f
#define ADS_SLIDE_Y_LONG      0.034f
#define ADS_SWAY_LEFT         0.30f      // the share of the sway left when aimed
#define ADS_STRAFE_TILT       0.08f      // radians (4.6 degrees) the aimed gun tilts at a full strafe
#define VIEWMODEL_STRAFE_EASE_SECS 0.10f // the tilt's time to follow the move

float Viewmodel_StrafeTilt(float strafe01, float ads01)
{
    if (strafe01 > 1.0f) strafe01 = 1.0f;
    if (strafe01 < -1.0f) strafe01 = -1.0f;
    return -strafe01 * ADS_STRAFE_TILT * Clamp01(ads01);     // moving right (positive): the roll is negative, the top of the gun leans right
}

float Viewmodel_StrafeSwayStep(float current, float target, float dt)
{
    if (target > 1.0f) target = 1.0f;
    if (target < -1.0f) target = -1.0f;
    if (dt <= 0.0f) return current;
    return current + (target - current) * (1.0f - expf(-dt / VIEWMODEL_STRAFE_EASE_SECS));
}

void Viewmodel_Build(PartList *out, const Arsenal *a, const AimFrame *cam, float walk01, float t)
{
    Viewmodel_BuildAimed(out, a, cam, walk01, t, 0.0f);
}

void Viewmodel_BuildAimed(PartList *out, const Arsenal *a, const AimFrame *cam, float walk01, float t, float ads01)
{
    Viewmodel_BuildAimedSway(out, a, cam, walk01, t, ads01, 0.0f);
}

void Viewmodel_BuildAimedSway(PartList *out, const Arsenal *a, const AimFrame *cam, float walk01, float t, float ads01, float strafe01)
{
    PartList_Clear(out);
    g_pipeInHand = false;
    const WeaponDef *def = Weapon_Def(a->current);
    const float p = a->attackT >= 0.0f ? Clamp01(a->attackT / def->duration) : -1.0f;
    const float ads = def->kind == WEAPON_KIND_GUN ? Clamp01(ads01) : 0.0f;       // only guns have sights
    const float roll = Viewmodel_StrafeTilt(strafe01, ads);                       // 0 at the hip and for what has no sights
    const Ctx ctx = { cam, out, sinf(roll), cosf(roll) };

    // Sway and bob, shared by both hands. Aimed, they steady.
    const float phase = t * 8.5f;
    const float calm = 1.0f - (1.0f - ADS_SWAY_LEFT) * ads;
    const Vector3 sway = V((sinf(phase) * 0.012f * walk01 + sinf(t * 1.1f) * 0.003f) * calm,
                           (fabsf(sinf(phase)) * 0.010f * walk01 + sinf(t * 1.7f) * 0.004f) * calm, 0.0f);
    const float raise = (1.0f - EaseOut(a->equipT)) * 0.40f;          // weapon comes up from below

    Xf right = { Rest(WRIST), 0.0f, 0.0f, 0.0f, V(sway.x, sway.y - raise, 0.0f) };
    Xf left  = { Rest(WRIST), 0.0f, 0.0f, 0.0f, V(sway.x * 0.8f - 0.01f, sway.y * 1.1f - raise - 0.03f, -0.04f) };
    Xf pipeXf = right;                                                   // the pipe turns by more than the hand does

    if (def->kind == WEAPON_KIND_UNARMED && p >= 0.0f) {
        Xf *fist = a->lastPunchHand == 0 ? &right : &left;
        const float e = PunchCurve(p);
        fist->t.z += 0.40f * e;
        fist->t.x -= 0.11f * e;
        fist->t.y += 0.06f * e;
        fist->yaw = -0.22f * e;
        fist->pitch = 0.10f * e;
    }

    if (def->kind == WEAPON_KIND_MELEE) {
        // Both the hand and the pipe turn about the middle of the grip, so the pipe never slides in the fist.
        right.pivot = Rest(V(0.20f, -0.25f, 0.46f));
        pipeXf = right;
        if (p >= 0.0f) {
            float pitch, roll, yaw;
            Vector3 d;
            SlashPose(p, &pitch, &roll, &yaw, &d);
            right.t = V(right.t.x + d.x, right.t.y + d.y, right.t.z + d.z);
            pipeXf.t = right.t;
            pipeXf.pitch = pitch;  pipeXf.roll = roll;  pipeXf.yaw = yaw;      // the pipe takes the whole turn
            right.pitch = pitch * 0.45f;  right.roll = roll * 0.55f;  right.yaw = yaw * 0.55f;   // the fist only part of it
        } else {
            pipeXf.roll = right.roll = -0.10f;                          // rests leaning a little outward
        }
        left.t.x -= 0.02f;
    }

    float pump = 0.0f;
    if (def->kind == WEAPON_KIND_GUN) {
        // How hard it kicks: the pistol's 1.0 is the reference; heavier guns kick further, lighter ones less.
        const float k = powf(def->kick, 0.7f);
        const bool twoHanded = a->current == WEAPON_SHOTGUN || a->current == WEAPON_RIFLE;
        if (!twoHanded) {
            // The left hand cups the right under the grip.
            left.pivot = Rest(WRIST);
            left.t = V(-0.30f + sway.x * 0.8f, -0.05f + sway.y - raise, 0.05f);
            left.yaw = -0.55f;
        } else {
            // A long gun sits a little lower and to the right so the barrel stays out of the crosshair, and the left hand
            // goes out to the foregrip (the rifle's handguard, the shotgun's pump), under the barrel.
            right.t.x += 0.02f;
            right.t.y -= 0.03f;
            if (a->current == WEAPON_SHOTGUN && p >= 0.0f) {
                // The pump stroke fills the cooldown after the shot: back, then forward.
                const float u = Clamp01((p - 0.25f) / 0.60f);
                pump = -0.15f * sinf(3.14159265f * u);
            }
            left.pivot = Rest(WRIST);
            left.t = V(-0.28f + sway.x * 0.8f, -0.05f + sway.y * 1.1f - raise, 0.34f + pump);
            left.yaw = 0.0f;
        }
        // Recoil: the gun kicks back and up, and both hands go with it. A long gun's receiver is close to the lens, so it
        // turns and moves back less than the pistol does for the same kick.
        const float back = twoHanded ? 0.06f : 0.07f, tilt = twoHanded ? 0.13f : 0.20f;
        right.t.z -= back * k * a->recoil;
        right.t.y += 0.015f * k * a->recoil;
        right.pitch = tilt * k * a->recoil;
        left.t.z -= 0.035f * k * a->recoil;

        // A reload: the gun drops and tips up toward the eye, and the off hand goes down and back for the magazine (a tube-fed gun holds
        // the pose and the hand bobs once per shell). reloadBlend eases it in and out, so the pose never pops.
        if (a->reloadBlend > 0.0f) {
            const float b = EaseInOut(a->reloadBlend);
            const float f = Arsenal_ReloadFraction(a);
            const float hand = def->reloadPerRound ? 0.5f - 0.5f * cosf(6.2831853f * f) : sinf(3.14159265f * Clamp01((f - 0.2f) / 0.6f));
            right.t.y -= 0.07f * b;
            right.t.z -= 0.03f * b;
            right.pitch += 0.30f * b;
            left.t.y -= (0.06f + 0.08f * hand) * b;
            left.t.z -= (twoHanded ? 0.10f : 0.04f) * b;
        }
    }

    // Aimed down the sights: the gun and both hands move together to the middle of the view.
    if (ads > 0.0f) {
        const bool longGun = a->current == WEAPON_SHOTGUN || a->current == WEAPON_RIFLE;
        const float dx = (longGun ? ADS_SLIDE_X_LONG : ADS_SLIDE_X_PISTOL) * ads, dy = (longGun ? ADS_SLIDE_Y_LONG : ADS_SLIDE_Y_PISTOL) * ads;
        right.t.x += dx; right.t.y += dy;
        left.t.x  -= dx; left.t.y  += dy;                     // (the left hand is the right one mirrored: its x runs the other way)
    }

    AddHand(&ctx, -1, &left);
    AddHand(&ctx, 1, &right);
    if (def->kind == WEAPON_KIND_MELEE)  AddPipe(&ctx, &pipeXf);
    if (a->current == WEAPON_PISTOL)  { AddPistol(&ctx, &right);  AddFlash(&ctx, &right, a->flash, -0.155f, 0.75f, 1.0f); }
    if (a->current == WEAPON_SHOTGUN) { AddShotgun(&ctx, &right, pump); AddFlash(&ctx, &right, a->flash, -0.145f, 1.04f, 1.7f); }
    if (a->current == WEAPON_RIFLE)   { AddRifle(&ctx, &right);   AddFlash(&ctx, &right, a->flash, -0.148f, 1.14f, 1.0f); }
}

bool Viewmodel_PipePoints(Vector3 *tip, Vector3 *mid)
{
    if (!g_pipeInHand) return false;
    if (tip) *tip = g_pipeTip;
    if (mid) *mid = g_pipeMid;
    return true;
}

// ---- The slash trail ------------------------------------------------------------------------------
// Samples of the pipe's tip and middle, stored relative to the camera so the trail follows the view
// while the player turns, and drawn as a ribbon that fades out.
#define TRAIL_MAX     16
#define TRAIL_LIFE    0.20f
typedef struct { float time; Vector3 tip, mid; } TrailSample;   // camera space: x right, y up, z forward
static TrailSample g_trail[TRAIL_MAX];
static int g_trailN = 0;

static Vector3 ToCam(const AimFrame *c, Vector3 w)
{
    const Vector3 d = V(w.x - c->eye.x, w.y - c->eye.y, w.z - c->eye.z);
    return V(d.x * c->right.x + d.y * c->right.y + d.z * c->right.z,
             d.x * c->up.x + d.y * c->up.y + d.z * c->up.z,
             d.x * c->forward.x + d.y * c->forward.y + d.z * c->forward.z);
}

static Vector3 FromCam(const AimFrame *c, Vector3 v)
{
    return V(c->eye.x + c->right.x * v.x + c->up.x * v.y + c->forward.x * v.z,
             c->eye.y + c->right.y * v.x + c->up.y * v.y + c->forward.y * v.z,
             c->eye.z + c->right.z * v.x + c->up.z * v.y + c->forward.z * v.z);
}

static void TrailRecord(const AimFrame *cam, float timeSecs)
{
    Vector3 tip, mid;
    if (!Viewmodel_PipePoints(&tip, &mid)) return;
    if (g_trailN == TRAIL_MAX) { for (int i = 1; i < TRAIL_MAX; ++i) g_trail[i - 1] = g_trail[i]; --g_trailN; }
    g_trail[g_trailN++] = (TrailSample){ timeSecs, ToCam(cam, tip), ToCam(cam, mid) };
}

static void TrailDraw(const AimFrame *cam, float timeSecs)
{
    // Drop what has faded.
    int keep = 0;
    for (int i = 0; i < g_trailN; ++i)
        if (timeSecs - g_trail[i].time < TRAIL_LIFE && timeSecs >= g_trail[i].time) g_trail[keep++] = g_trail[i];
    g_trailN = keep;
    if (g_trailN < 2) return;

    rlDisableDepthMask();
    for (int i = 0; i + 1 < g_trailN; ++i) {
        const TrailSample *s0 = &g_trail[i], *s1 = &g_trail[i + 1];
        const float a0 = 1.0f - (timeSecs - s0->time) / TRAIL_LIFE, a1 = 1.0f - (timeSecs - s1->time) / TRAIL_LIFE;
        const Vector3 w[4] = { FromCam(cam, s0->tip), FromCam(cam, s1->tip), FromCam(cam, s1->mid), FromCam(cam, s0->mid) };
        const float al[4] = { a0, a1, a1 * 0.25f, a0 * 0.25f };   // bright at the tip, gone toward the grip
        if (rlCheckRenderBatchLimit(8)) rlDrawRenderBatchActive();
        rlBegin(RL_QUADS);
        for (int k = 0; k < 4; ++k) { rlColor4ub(235, 238, 255, (unsigned char)(150.0f * al[k] * al[k])); rlVertex3f(w[k].x, w[k].y, w[k].z); }
        // The same quad again facing the other way, so it shows whichever side the camera sees.
        for (int k = 3; k >= 0; --k) { rlColor4ub(235, 238, 255, (unsigned char)(150.0f * al[k] * al[k])); rlVertex3f(w[k].x, w[k].y, w[k].z); }
        rlEnd();
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void Viewmodel_Draw(const Arsenal *a, const AimFrame *cam, float walk01, float t, float brightness)
{
    Viewmodel_DrawAimed(a, cam, walk01, t, brightness, 0.0f);
}

void Viewmodel_DrawAimed(const Arsenal *a, const AimFrame *cam, float walk01, float t, float brightness, float ads01)
{
    Viewmodel_DrawAimedSway(a, cam, walk01, t, brightness, ads01, 0.0f);
}

void Viewmodel_DrawAimedSway(const Arsenal *a, const AimFrame *cam, float walk01, float t, float brightness, float ads01, float strafe01)
{
    PartList list;
    Viewmodel_BuildAimedSway(&list, a, cam, walk01, t, ads01, strafe01);

    // Light from above and to the left of the camera, in world space.
    const Vector3 light = V(-0.40f * cam->right.x + 0.80f * cam->up.x - 0.45f * cam->forward.x,
                            -0.40f * cam->right.y + 0.80f * cam->up.y - 0.45f * cam->forward.y,
                            -0.40f * cam->right.z + 0.80f * cam->up.z - 0.45f * cam->forward.z);

    // A fresh depth buffer and a depth range made for things half a metre from the eye.
    ViewClip_ClearDepth();
    ViewClip_Reapply(HANDS_NEAR, HANDS_FAR);

    // The torch of the muzzle flash lights the hands; otherwise they take the light of the place.
    float b = brightness;
    if (a->flash > 0.0f && b < 0.55f + 0.45f * a->flash) b = 0.55f + 0.45f * a->flash;
    Parts_SetBrightness(0.22f + 0.78f * b);
    PartList_Draw(&list, light);
    Parts_SetBrightness(1.0f);
    rlDrawRenderBatchActive();

    // The trail: samples during the chop, from the start of the cocked-back arm to the follow-through.
    const WeaponDef *def = Weapon_Def(a->current);
    const float p = a->attackT >= 0.0f ? a->attackT / def->duration : -1.0f;
    if (def->kind == WEAPON_KIND_MELEE && p >= 0.36f && p <= 0.66f) TrailRecord(cam, t);
    TrailDraw(cam, t);

    ViewClip_Reapply(WORLD_NEAR, WORLD_FAR);
}
