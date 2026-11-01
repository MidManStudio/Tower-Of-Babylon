// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "spider.c"
// ============================================================================
#include "spider.h"

#include <math.h>
#include <string.h>

#include "spider_math.h"
#include "../dungeon/dungeon_raycast.h"

// ---- Dimensions in metres --------------------------------------------------
#define BODY_H        0.50f   // body centre above the surface
#define FEMUR         0.95f
#define TIBIA         0.95f
#define TARSUS        0.30f
#define REACH         (FEMUR + TIBIA)
#define REACH_LIMIT   0.98f   // fraction of REACH at which a foot must step
#define SIZE_REF      2.2f    // speed that counts as 1.0 in the gait formulas

// ---- Locomotion tuning -----------------------------------------------------
#define TURN_RATE     3.2f    // rad/s
#define ACCEL         8.0f
#define DECEL         12.0f
#define LOOKAHEAD     1.25f   // forward ray length for concave corners
#define HEAD_REACH    0.45f   // anchor to the front of the head
#define STOP_GAP      0.28f   // gap kept between head and wall
#define CONVEX_AHEAD  0.55f   // how far ahead the edge probe looks
#define CONVEX_STOP   0.18f
#define PROBE_EXTEND  0.50f   // front feet land this far along the new surface
#define PROBE_DUR     0.30f
#define LIFT_H        0.35f
#define LIFT_HIGH     0.55f
#define STEP_DRIFT    0.10f   // step when a cycle ends this far off the ideal spot
#define STEP_STOP_DRIFT 0.75f // step while standing still when this far off
#define FAILSAFE_SECS 1.4f

static const float kHipX = 0.22f;
static const float kHipZ[4]  = {  0.30f,  0.12f, -0.08f, -0.28f };
static const float kRestX[4] = {  1.00f,  1.15f,  1.15f,  1.00f };
static const float kRestZ[4] = {  0.75f,  0.25f, -0.25f, -0.75f };
static const int   kRing[8]  = { 1, 3, 5, 7, 6, 4, 2, 0 };   // feet in order around the body

// ---------------------------------------------------------------------------
// Frame helpers
// ---------------------------------------------------------------------------
static Vector3 RightOf(const Spider *s) { return V3NormOr(V3Cross(s->fwd, s->up), V3(1.0f, 0.0f, 0.0f)); }

static Vector3 HipRoot(const Spider *s, int i)
{
    const SpiderLeg *l = &s->legs[i];
    const Vector3 right = RightOf(s);
    return V3Add(V3Add(V3Mad(s->pos, right, kHipX * (float)l->side), V3Scale(s->fwd, kHipZ[l->row])), V3Scale(s->up, 0.05f));
}

static Vector3 IdealFoot(const Spider *s, int i)
{
    const SpiderLeg *l = &s->legs[i];
    const Vector3 right = RightOf(s);
    return V3Add(V3Add(V3Mad(s->pos, right, kRestX[l->row] * (float)l->side), V3Scale(s->fwd, kRestZ[l->row])), V3Scale(s->up, -BODY_H));
}

// ---------------------------------------------------------------------------
// Terrain search: three rays tried in order
// ---------------------------------------------------------------------------
static bool TerrainSearch(const FloorMap *f, Vector3 root, Vector3 target, Vector3 up, Vector3 out, Vector3 *pt, Vector3 *nrm)
{
    DungeonHit h;
    const Vector3 d = V3Sub(target, root);
    const float len = V3Len(d);

    // 1. Root to a slightly extended target. A wall in the way gives footing at a concave corner.
    if (len > 1e-4f && Dungeon_Raycast(f, root, V3Scale(d, 1.0f / len), len + 0.20f, &h) && h.t <= REACH) {
        *pt = h.point; *nrm = h.normal; return true;
    }
    // 2. From above the target straight down: ordinary ground.
    if (Dungeon_Raycast(f, V3Mad(target, up, 0.6f), V3Scale(up, -1.0f), 1.3f, &h)) {
        *pt = h.point; *nrm = h.normal; return true;
    }
    // 3. Horizontally from beneath the target back toward the body: the underside of an edge.
    if (Dungeon_Raycast(f, V3Mad(target, up, -0.5f), V3Scale(out, -1.0f), 1.2f, &h)) {
        *pt = h.point; *nrm = h.normal; return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Legs
// ---------------------------------------------------------------------------
static void BeginStep(SpiderLeg *l, Vector3 to, Vector3 toN, float dur, float liftH, bool probe)
{
    l->from = l->foot; l->fromNormal = l->footNormal;
    l->to = to; l->toNormal = toN;
    l->t = 0.0f; l->dur = dur; l->liftH = liftH;
    l->swinging = true; l->grounded = false;
    l->probing = probe; l->probeDone = false;
}

static Vector3 SwingPoint(const SpiderLeg *l, Vector3 bodyUp)
{
    const float e = l->t * l->t * (3.0f - 2.0f * l->t);   // smoothstep
    const Vector3 base = V3Lerp(l->from, l->to, e);
    const Vector3 liftDir = V3NormOr(V3Add(V3Add(l->fromNormal, l->toNormal), bodyUp), bodyUp);
    return V3Mad(base, liftDir, sinf(3.14159265f * l->t) * l->liftH);
}

static bool Settled(const Spider *s, int j) { return s->legs[j].grounded && !s->legs[j].swinging; }

// A leg may lift only when its left-right partner and its front and back
// neighbours on the same side are all planted.
static bool NeighboursGrounded(const Spider *s, int i)
{
    if (!Settled(s, i ^ 1)) return false;
    if (i >= 2 && !Settled(s, i - 2)) return false;
    if (i + 2 < SPIDER_LEGS && !Settled(s, i + 2)) return false;
    return true;
}

// Front leg reaches for the next surface: the ideal landing projected onto it,
// pushed further along it, and snapped to real geometry. The target is fixed
// for the whole swing.
static void StartProbe(Spider *s, const FloorMap *f, int i)
{
    SpiderLeg *l = &s->legs[i];
    const Vector3 ideal = IdealFoot(s, i);
    const float dn = V3Dot(V3Sub(ideal, s->transPoint), s->transNormal);
    const Vector3 onPlane = V3Mad(ideal, s->transNormal, -dn);
    Vector3 target = V3Mad(onPlane, s->transHeading, PROBE_EXTEND);
    Vector3 nrm = s->transNormal;

    DungeonHit h;
    if (Dungeon_Raycast(f, V3Mad(target, s->transNormal, 0.4f), V3Scale(s->transNormal, -1.0f), 0.9f, &h)) {
        target = h.point;
        nrm = h.normal;
    }
    BeginStep(l, target, nrm, PROBE_DUR, LIFT_HIGH, true);
}

static int UpdateLegs(Spider *s, const FloorMap *f, float dt, float freq, float swingRatio)
{
    int landed = 0;
    const bool moving = s->speed > 0.05f;
    const Vector3 right = RightOf(s);
    const float swingDur = fmaxf(0.12f, swingRatio / freq);
    const float halfStance = 0.5f * (1.0f - swingRatio) / freq;

    for (int i = 0; i < SPIDER_LEGS; ++i) {
        SpiderLeg *l = &s->legs[i];
        const Vector3 root = HipRoot(s, i);
        const Vector3 out = V3Scale(right, (float)l->side);

        l->prevPhase = l->phase;
        l->phase = FracF(s->gaitPhase + l->phaseOffset);
        if (moving && l->phase < l->prevPhase) l->due = true;

        if (l->swinging) {
            l->t += dt / l->dur;
            if (l->t >= 1.0f) {
                l->t = 1.0f;
                l->swinging = false;
                l->grounded = true;
                l->foot = l->to;
                l->footNormal = l->toNormal;
                if (l->probing) l->probeDone = true;
                l->probing = false;
                ++landed;
            } else {
                l->foot = SwingPoint(l, s->up);
            }
            continue;
        }

        // While a surface switch is pending the front legs reach for it without waiting for the gait.
        if (s->transActive && l->row == 0) {
            if (!l->probeDone) StartProbe(s, f, i);
            continue;
        }

        const Vector3 ideal = IdealFoot(s, i);

        if (!l->grounded) {   // folded leg: look for ground again
            Vector3 pt, nr;
            if (TerrainSearch(f, root, ideal, s->up, out, &pt, &nr)) BeginStep(l, pt, nr, swingDur, LIFT_H, false);
            continue;
        }

        const float drift = V3Len(V3Sub(l->foot, ideal));
        const float reachDist = V3Len(V3Sub(l->foot, root));
        const bool overReach = reachDist > REACH_LIMIT * REACH;
        const bool step = (l->due && drift > STEP_DRIFT) || overReach || (!moving && drift > STEP_STOP_DRIFT);
        if (l->due && drift <= STEP_DRIFT) l->due = false;
        if (!step) continue;
        if (!overReach && !NeighboursGrounded(s, i)) continue;

        // Aim ahead by how far the body travels while this foot is in the air and half the stance after.
        const Vector3 target = V3Mad(ideal, s->heading, s->speed * (swingDur + halfStance));
        Vector3 pt, nr;
        if (TerrainSearch(f, root, target, s->up, out, &pt, &nr)) {
            const float climb = fabsf(V3Dot(V3Sub(pt, l->foot), s->up));
            BeginStep(l, pt, nr, swingDur, climb > 0.25f ? LIFT_HIGH : LIFT_H, false);
            l->due = false;
        } else if (overReach) {
            l->foot = V3Add(V3Mad(root, s->up, -0.55f), V3Scale(out, 0.30f));   // fold under the root
            l->footNormal = s->up;
            l->grounded = false;
        }
    }
    return landed;
}

// ---------------------------------------------------------------------------
// Body
// ---------------------------------------------------------------------------
static void UpdateBody(Spider *s, const FloorMap *f, float dt, int landed)
{
    Vector3 pts[SPIDER_LEGS];
    Vector3 sumN = V3(0.0f, 0.0f, 0.0f), centroid = V3(0.0f, 0.0f, 0.0f);
    float wsum = 0.0f;

    for (int i = 0; i < SPIDER_LEGS; ++i) {
        const SpiderLeg *l = &s->legs[i];
        Vector3 n = s->normal;
        if (l->swinging) {                    // the takeoff point stays the support until the foot lands
            pts[i] = l->from; n = l->fromNormal;
        } else if (l->grounded) {
            pts[i] = l->foot; n = l->footNormal;
        } else {
            pts[i] = IdealFoot(s, i);         // no ground: neutral
        }
        // Surfaces close to the anchor's surface count for more, so the front and back of a thin board cannot cancel.
        const float al = fmaxf(0.0f, V3Dot(n, s->normal));
        const float w = 0.25f + 0.75f * al * al;
        sumN = V3Mad(sumN, n, w);
        wsum += w;
        centroid = V3Add(centroid, pts[i]);
    }
    centroid = V3Scale(centroid, 1.0f / (float)SPIDER_LEGS);
    const Vector3 supportN = V3NormOr(sumN, s->normal);
    (void)wsum;

    // Plane of the feet by Newell's method over the ring of foot tips.
    Vector3 N = V3(0.0f, 0.0f, 0.0f);
    for (int k = 0; k < SPIDER_LEGS; ++k) {
        const Vector3 a = pts[kRing[k]], b = pts[kRing[(k + 1) % SPIDER_LEGS]];
        N.x += (a.y - b.y) * (a.z + b.z);
        N.y += (a.z - b.z) * (a.x + b.x);
        N.z += (a.x - b.x) * (a.y + b.y);
    }
    Vector3 footN = V3NormOr(N, supportN);
    if (V3Dot(footN, supportN) < 0.0f) footN = V3Scale(footN, -1.0f);

    // Up: 35% support surface, 65% foot plane.
    const Vector3 upTarget = V3NormOr(V3Add(V3Scale(supportN, 0.35f), V3Scale(footN, 0.65f)), s->normal);
    s->up = V3NormOr(V3Lerp(s->up, upTarget, 1.0f - expf(-10.0f * dt)), upTarget);

    // Position: anchor and feet blended 35:65, followed with a critically damped spring.
    const Vector3 fromAnchor = V3Mad(s->anchor, s->normal, BODY_H);
    const Vector3 fromFeet = V3Mad(centroid, footN, BODY_H);
    const Vector3 posTarget = V3Add(V3Scale(fromAnchor, 0.35f), V3Scale(fromFeet, 0.65f));
    const float w0 = 14.0f;
    const Vector3 acc = V3Sub(V3Scale(V3Sub(posTarget, s->pos), w0 * w0), V3Scale(s->vel, 2.0f * w0));
    s->vel = V3Mad(s->vel, acc, dt);
    if (landed) s->vel = V3Mad(s->vel, s->up, -0.30f * (float)landed);
    s->pos = V3Mad(s->pos, s->vel, dt);

    // Facing follows the direction of travel, flattened against the body's up.
    const Vector3 fwdT = V3NormOr(V3OnPlane(s->heading, s->up), s->fwd);
    s->fwd = V3NormOr(V3Lerp(s->fwd, fwdT, 1.0f - expf(-12.0f * dt)), fwdT);
    s->fwd = V3NormOr(V3OnPlane(s->fwd, s->up), fwdT);

    // Three spheres on head, thorax and abdomen are pushed out of the terrain.
    Vector3 centers[3];
    float radii[3];
    Spider_BodySpheres(s, centers, radii);
    for (int k = 0; k < 3; ++k) {
        Vector3 c = centers[k];
        const Vector3 before = c;
        if (Dungeon_PushOutSphere(f, &c, radii[k])) {
            s->pos = V3Add(s->pos, V3Sub(c, before));
            if (k < 2) Spider_BodySpheres(s, centers, radii);   // later spheres follow the corrected body
        }
    }
}

void Spider_BodySpheres(const Spider *s, Vector3 centers[3], float radii[3])
{
    static const float offF[3] = { 0.36f, 0.0f, -0.42f };
    static const float offU[3] = { 0.02f, 0.0f, 0.04f };
    static const float rad[3]  = { 0.17f, 0.22f, 0.28f };
    for (int k = 0; k < 3; ++k) {
        centers[k] = V3Add(V3Mad(s->pos, s->fwd, offF[k]), V3Scale(s->up, offU[k]));
        radii[k] = rad[k];
    }
}

// ---------------------------------------------------------------------------
// Two-bone IK and the tarsus
// ---------------------------------------------------------------------------
static void SolveLegIK(Spider *s, int i)
{
    SpiderLeg *l = &s->legs[i];
    const Vector3 out = V3Scale(RightOf(s), (float)l->side);
    const Vector3 root = HipRoot(s, i);

    // The tarsus hangs from the ankle to the foot tip, tilted between up, out and the surface normal.
    const Vector3 tDir = V3NormOr(V3Add(V3Add(V3Scale(s->up, 0.5f), V3Scale(out, 0.3f)), V3Scale(l->footNormal, 0.8f)), s->up);
    Vector3 ankle = V3Mad(l->foot, tDir, TARSUS);

    Vector3 d = V3Sub(ankle, root);
    float dist = V3Len(d);
    const float maxR = REACH * 0.999f, minR = fabsf(FEMUR - TIBIA) + 0.02f;
    Vector3 dir = V3NormOr(d, s->fwd);
    if (dist > maxR) dist = maxR;
    if (dist < minR) dist = minR;
    ankle = V3Mad(root, dir, dist);

    const float a = (FEMUR * FEMUR - TIBIA * TIBIA + dist * dist) / (2.0f * dist);
    const float h2 = FEMUR * FEMUR - a * a;
    const float h = h2 > 0.0f ? sqrtf(h2) : 0.0f;
    const Vector3 pole = V3NormOr(V3Add(V3Scale(s->up, 0.9f), V3Scale(out, 0.5f)), s->up);
    const Vector3 bend = V3NormOr(V3OnPlane(pole, dir), V3Cross(dir, s->fwd));

    l->root = root;
    l->knee = V3Add(V3Mad(root, dir, a), V3Scale(bend, h));
    l->ankle = ankle;
}

// ---------------------------------------------------------------------------
// Anchor and surface switching
// ---------------------------------------------------------------------------
typedef struct {
    bool    convex;
    Vector3 point, normal;
} NextSurface;

// Looks for the surface the spider will have to move onto. Concave: a forward
// ray meets a surface turned a long way from this one. Convex: a short step
// ahead the surface is gone, so a ray fired backward from under the edge finds
// the surface that wraps around it. dist is how far the corner is along the heading.
static bool DetectNextSurface(const Spider *s, const FloorMap *f, NextSurface *out, float *dist)
{
    DungeonHit h;
    const Vector3 o = V3Mad(s->anchor, s->normal, 0.30f);
    // Only a wall that is turned a long way from this surface and is actually in the way (head-on) counts.
    if (Dungeon_Raycast(f, o, s->heading, LOOKAHEAD + HEAD_REACH, &h) && fabsf(V3Dot(h.normal, s->normal)) < 0.5f &&
        V3Dot(h.normal, s->heading) < -0.5f) {
        out->convex = false; out->point = h.point; out->normal = h.normal;
        *dist = V3Dot(V3Sub(h.point, s->anchor), s->heading);
        return true;
    }

    const Vector3 ahead = V3Mad(s->anchor, s->heading, CONVEX_AHEAD);
    if (!Dungeon_Raycast(f, V3Mad(ahead, s->normal, 0.25f), V3Scale(s->normal, -1.0f), 0.70f, &h)) {
        const Vector3 under = V3Mad(ahead, s->normal, -0.45f);
        if (Dungeon_Raycast(f, under, V3Scale(s->heading, -1.0f), CONVEX_AHEAD + 0.6f, &h) && V3Dot(h.normal, s->heading) > 0.5f) {
            out->convex = true; out->point = h.point; out->normal = h.normal;
            *dist = V3Dot(V3Sub(h.point, s->anchor), s->heading);
            return true;
        }
    }
    return false;
}

static void StartTransition(Spider *s, const NextSurface *n)
{
    s->transActive = true;
    s->transConvex = n->convex;
    s->transPoint = n->point;
    s->transNormal = n->normal;
    s->transHeading = V3NormOr(V3OnPlane(V3Transport(s->heading, s->normal, n->normal), n->normal), s->heading);
    s->transTimer = 0.0f;
    for (int i = 0; i < SPIDER_LEGS; ++i)
        if (s->legs[i].row == 0) { s->legs[i].probing = false; s->legs[i].probeDone = false; }
}

static void SwitchSurface(Spider *s, const FloorMap *f)
{
    // The old anchor lies on the surface being left, so its projection onto the new
    // plane lies on the corner between them and therefore on the new surface too.
    const float dn = V3Dot(V3Sub(s->anchor, s->transPoint), s->transNormal);
    const Vector3 onPlane = V3Mad(s->anchor, s->transNormal, -dn);
    const Vector3 guess = V3Mad(onPlane, s->transHeading, s->transConvex ? 0.25f : 0.30f);
    const Vector3 contact = s->transPoint;   // found by a ray, so certainly on the new surface

    s->normal = s->transNormal;
    s->heading = s->transHeading;
    s->transActive = false;
    s->speed = 0.0f;
    ++s->switches;
    for (int i = 0; i < SPIDER_LEGS; ++i)
        if (s->legs[i].row == 0) { s->legs[i].probing = false; s->legs[i].probeDone = false; }

    DungeonHit h;
    const Vector3 down = V3Scale(s->normal, -1.0f);
    if (Dungeon_Raycast(f, V3Mad(guess, s->normal, 0.30f), down, 0.80f, &h) && V3Dot(h.normal, s->normal) > 0.9f)
        s->anchor = h.point;
    else if (Dungeon_Raycast(f, V3Mad(contact, s->normal, 0.30f), down, 0.80f, &h) && V3Dot(h.normal, s->normal) > 0.9f)
        s->anchor = h.point;
    else
        s->anchor = contact;
}

// Moves the anchor and re-attaches it to the surface by firing a ray back along the normal.
static bool Reproject(Spider *s, const FloorMap *f, Vector3 np)
{
    DungeonHit h;
    if (Dungeon_Raycast(f, V3Mad(np, s->normal, 0.30f), V3Scale(s->normal, -1.0f), 0.75f, &h) && V3Dot(h.normal, s->normal) > 0.9f) {
        s->anchor = h.point;
        s->normal = h.normal;
        s->heading = V3NormOr(V3OnPlane(s->heading, s->normal), s->heading);
        return true;
    }
    return false;
}

static void AnchorStep(Spider *s, const FloorMap *f, float dt, Vector3 want, float wantSpeed)
{
    float turnAbs = 0.0f;

    // 1. Steer: project the wish onto the surface and turn toward it at a capped rate.
    if (!s->transActive) {
        Vector3 w = V3OnPlane(want, s->normal);
        const float wl = V3Len(w);
        if (wl > 1e-3f) {
            w = V3Scale(w, 1.0f / wl);
            const float ang = V3SignedAngle(s->heading, w, s->normal);
            const float a = ClampF(ang, -TURN_RATE * dt, TURN_RATE * dt);
            s->heading = V3NormOr(V3OnPlane(V3RotateAxis(s->heading, s->normal, a), s->normal), s->heading);
            turnAbs = fabsf(ang);
        } else {
            wantSpeed = 0.0f;
        }
    }

    // 2. Look for a corner. Slow down for it and, when close, start the switch.
    float limit = wantSpeed;
    if (s->transActive) {
        limit = 0.0f;
    } else {
        NextSurface n; float dist;
        if (DetectNextSurface(s, f, &n, &dist)) {
            const float stop = n.convex ? CONVEX_STOP : HEAD_REACH + STOP_GAP;
            const float trigger = n.convex ? CONVEX_AHEAD + 0.05f : LOOKAHEAD - 0.05f;
            if (dist <= trigger) StartTransition(s, &n);
            limit = fminf(limit, sqrtf(2.0f * DECEL * fmaxf(0.0f, dist - stop)));
        }
        limit *= ClampF(1.0f - turnAbs / 1.6f, 0.25f, 1.0f);
    }

    // The nav point must not run far ahead of the body. The floor on the factor
    // keeps the gait turning, otherwise a large lag would stop the legs that
    // are needed to close it.
    const float lag = V3Len(V3Sub(V3Mad(s->anchor, s->normal, BODY_H), s->pos));
    limit *= ClampF(1.0f - (lag - 1.2f) / 1.0f, 0.35f, 1.0f);

    // 3. Speed and move.
    if (s->speed < limit) s->speed = fminf(limit, s->speed + ACCEL * dt);
    else                  s->speed = fmaxf(limit, s->speed - DECEL * dt);
    if (s->speed > 1e-4f) {
        const Vector3 move = V3Scale(s->heading, s->speed * dt);
        if (!Reproject(s, f, V3Add(s->anchor, move))) {
            // The full move would leave the surface (the edge of a wall, say). Slide
            // along the two in-plane axes one at a time, larger component first.
            Vector3 e1 = V3OnPlane(V3(0.0f, 1.0f, 0.0f), s->normal);
            if (V3Len(e1) < 0.3f) e1 = V3OnPlane(V3(1.0f, 0.0f, 0.0f), s->normal);
            e1 = V3NormOr(e1, s->heading);
            const Vector3 e2 = V3NormOr(V3Cross(s->normal, e1), s->heading);
            const float m1 = V3Dot(move, e1), m2 = V3Dot(move, e2);
            const Vector3 a1 = V3Scale(e1, m1), a2 = V3Scale(e2, m2);
            const bool firstIs1 = fabsf(m1) >= fabsf(m2);
            const Vector3 first = firstIs1 ? a1 : a2, second = firstIs1 ? a2 : a1;
            if (Reproject(s, f, V3Add(s->anchor, first))) {
                s->heading = V3NormOr(V3OnPlane(first, s->normal), s->heading);
            } else if (Reproject(s, f, V3Add(s->anchor, second))) {
                s->heading = V3NormOr(V3OnPlane(second, s->normal), s->heading);
            } else {
                s->speed = 0.0f;
                ++s->lostSurface;
            }
        }
    }

    // 4. The anchor changes surface only once both front feet stand on the new one.
    if (s->transActive) {
        s->transTimer += dt;
        bool both = true;
        for (int i = 0; i < SPIDER_LEGS; ++i)
            if (s->legs[i].row == 0 && !s->legs[i].probeDone) both = false;
        if (both) {
            SwitchSurface(s, f);
        } else if (s->transTimer > FAILSAFE_SECS) {
            ++s->failsafes;
            SwitchSurface(s, f);
        }
    }
}

// ---------------------------------------------------------------------------
// Public locomotion API
// ---------------------------------------------------------------------------
void Spider_Init(Spider *s, const FloorMap *f, Vector3 anchor, Vector3 normal, Vector3 heading)
{
    memset(s, 0, sizeof *s);
    s->active = true;
    s->anchor = anchor;
    s->normal = V3NormOr(normal, V3(0.0f, 1.0f, 0.0f));
    Vector3 h = V3OnPlane(heading, s->normal);
    if (V3Len(h) < 1e-3f) h = V3Cross(s->normal, V3(1.0f, 0.0f, 0.0f));
    if (V3Len(h) < 1e-3f) h = V3Cross(s->normal, V3(0.0f, 0.0f, 1.0f));
    s->heading = V3NormOr(h, V3(1.0f, 0.0f, 0.0f));
    s->up = s->normal;
    s->fwd = s->heading;
    s->pos = V3Mad(anchor, s->normal, BODY_H);
    s->tint = (Color){ 28, 24, 30, 255 };
    InitHealth(&s->health, 90.0f);

    for (int i = 0; i < SPIDER_LEGS; ++i) {
        SpiderLeg *l = &s->legs[i];
        l->row = i / 2;
        l->side = (i % 2 == 0) ? -1 : 1;
        // Alternating groups: L1 R2 L3 R4 against R1 L2 R3 L4, with a light wave toward the rear.
        const bool groupA = (i == 0 || i == 3 || i == 4 || i == 7);
        l->phaseOffset = (groupA ? 0.0f : 0.5f) + 0.03f * (float)l->row;
        l->phase = l->prevPhase = FracF(l->phaseOffset);
    }
    for (int i = 0; i < SPIDER_LEGS; ++i) {
        SpiderLeg *l = &s->legs[i];
        const Vector3 ideal = IdealFoot(s, i), root = HipRoot(s, i);
        const Vector3 out = V3Scale(RightOf(s), (float)l->side);
        Vector3 pt, nr;
        if (TerrainSearch(f, root, ideal, s->up, out, &pt, &nr)) {
            l->foot = pt; l->footNormal = nr; l->grounded = true;
        } else {
            l->foot = ideal; l->footNormal = s->normal; l->grounded = false;
        }
        SolveLegIK(s, i);
    }
}

void Spider_Step(Spider *s, const FloorMap *f, float dt, Vector3 want, float wantSpeed)
{
    if (!s->active || dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    AnchorStep(s, f, dt, want, wantSpeed);

    // Gait: frequency follows speed, the swing share of the cycle grows with it.
    const float speedN = s->speed / SIZE_REF;
    const float freq = fminf(4.6f, 1.4f + 1.15f * speedN);
    const float swingRatio = 0.36f + (0.50f - 0.36f) * ClampF(speedN / 1.5f, 0.0f, 1.0f);
    if (s->speed > 0.05f) s->gaitPhase = FracF(s->gaitPhase + freq * dt);

    const int landed = UpdateLegs(s, f, dt, freq, swingRatio);
    UpdateBody(s, f, dt, landed);
    for (int i = 0; i < SPIDER_LEGS; ++i) SolveLegIK(s, i);

    const float probe = s->pos.x + s->pos.y + s->pos.z + s->anchor.x + s->anchor.y + s->anchor.z;
    if (!isfinite(probe)) s->active = false;
}
