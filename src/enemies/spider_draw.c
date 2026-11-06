// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/enemies.md, section "spider_draw.c"
// ============================================================================
#include <math.h>

#include "spider.h"
#include "spider_math.h"

#define SPIDER_DRAW_RANGE 40.0f

static Color Lerp3(Color a, Color b, float t)
{
    return (Color){ (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
                    (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
                    (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t), 255 };
}

static float g_alpha = 1.0f;
void Spiders_SetRenderAlpha(float alpha) { g_alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha); }

void Spiders_Draw(Vector3 viewer)
{
    const float time = (float)GetTime();

    for (int n = 0; n < MAX_SPIDERS; ++n) {
        const Spider *s = Spiders_Get(n);
        if (!s || !s->active) continue;
        if (hypotf(s->pos.x - viewer.x, s->pos.z - viewer.z) > SPIDER_DRAW_RANGE) continue;

        // Animation. All of it is drawing only; the locomotion code never sees it.
        //  rear:  the front of the body lifts and leans back, the front legs rise
        //  lunge: the whole body and the front legs throw forward
        //  hurt:  a white flash and a shudder
        const float rear = s->rear, lunge = s->lunge;
        const float hurt = s->hurtTimer > 0.0f ? s->hurtTimer / SPIDER_HURT_SECS : 0.0f;
        // Drawn between two steps: the body is where it was at the start of the last step, moved part of the way to where it is now.
        const Vector3 off = V3Scale(V3Sub(s->pos, s->prevPos), g_alpha - 1.0f);        // from where it is now, back by the part of the step not yet shown
        const Vector3 shudder = V3Scale(V3Cross(s->fwd, s->up), sinf(time * 90.0f) * 0.05f * hurt);
        const Vector3 shift = V3Add(V3Add(V3Scale(s->fwd, lunge * 0.55f - rear * 0.10f), V3Scale(s->up, rear * 0.28f)), shudder);

        Vector3 c[3]; float r[3];
        Spider_BodySpheres(s, c, r);
        c[0] = V3Add(V3Add(V3Add(c[0], shift), V3Scale(s->up, rear * 0.14f)), off);   // the head rises most
        c[1] = V3Add(V3Add(c[1], V3Scale(shift, 0.8f)), off);
        c[2] = V3Add(V3Add(c[2], V3Scale(shift, 0.3f)), off);

        const Color body = Lerp3(s->tint, (Color){ 255, 235, 235, 255 }, hurt);
        DrawSphereEx(c[0], r[0], 8, 8, body);
        DrawSphereEx(c[1], r[1], 8, 8, body);
        DrawSphereEx(c[2], r[2], 8, 8, body);

        // Two red eyes on the front of the head, brighter while it rears.
        const Vector3 right = V3NormOr(V3Cross(s->fwd, s->up), V3(1.0f, 0.0f, 0.0f));
        const Vector3 eye = V3Add(V3Mad(c[0], s->fwd, r[0] * 0.8f), V3Scale(s->up, r[0] * 0.35f));
        const Color eyeCol = Lerp3(RED, (Color){ 255, 200, 60, 255 }, rear);
        DrawSphereEx(V3Mad(eye, right,  0.07f), 0.035f + 0.02f * rear, 4, 4, eyeCol);
        DrawSphereEx(V3Mad(eye, right, -0.07f), 0.035f + 0.02f * rear, 4, 4, eyeCol);

        const Color leg = Lerp3((Color){ 52, 44, 50, 255 }, (Color){ 255, 235, 235, 255 }, hurt);
        for (int i = 0; i < SPIDER_LEGS; ++i) {
            const SpiderLeg *l = &s->legs[i];
            Vector3 root = V3Add(V3Add(l->root, V3Scale(shift, 0.8f)), off);       // the hip goes with the body,
            Vector3 knee = V3Add(l->knee, V3Scale(off, 0.66f)), ankle = V3Add(l->ankle, V3Scale(off, 0.33f)), foot = l->foot;   // a planted foot stays on the floor
            if (l->row == 0) {                                   // front pair: raise and reach
                knee  = V3Add(knee,  V3Add(V3Scale(s->up, rear * 0.40f + lunge * 0.10f), V3Scale(s->fwd, rear * 0.05f + lunge * 0.35f)));
                ankle = V3Add(ankle, V3Add(V3Scale(s->up, rear * 0.55f + lunge * 0.10f), V3Scale(s->fwd, rear * 0.15f + lunge * 0.50f)));
                foot  = V3Add(foot,  V3Add(V3Scale(s->up, rear * 0.70f + lunge * 0.05f), V3Scale(s->fwd, rear * 0.25f + lunge * 0.65f)));
            } else {                                             // the others follow the body a little
                knee  = V3Add(knee,  V3Scale(shift, 0.4f));
                ankle = V3Add(ankle, V3Scale(shift, 0.15f));
            }
            DrawCylinderEx(root, knee, 0.055f, 0.045f, 6, leg);
            DrawCylinderEx(knee, ankle, 0.045f, 0.030f, 6, leg);
            DrawCylinderEx(ankle, foot, 0.030f, 0.012f, 6, leg);
        }
    }
}
