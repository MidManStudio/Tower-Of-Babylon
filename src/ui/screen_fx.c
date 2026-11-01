// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/player.md, section "screen_fx.h and screen_fx.c"
// ============================================================================
#include "screen_fx.h"

#include <math.h>

#define VIGNETTE_TEX_SIZE   256
#define VIGNETTE_CLEAR_FRAC 0.25f   // inner part of the texture that stays fully clear
#define VIGNETTE_HEALTHY_AT 0.75f   // no vignette at or above this fraction of health

static Texture2D g_vignette;
static bool g_vignetteReady = false;

void ScreenFx_Init(void)
{
    // A black ring that is clear in the middle and opaque at the rim and corners.
    Image img = GenImageGradientRadial(VIGNETTE_TEX_SIZE, VIGNETTE_TEX_SIZE, VIGNETTE_CLEAR_FRAC,
                                       (Color){ 0, 0, 0, 0 }, (Color){ 0, 0, 0, 255 });
    g_vignette = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(g_vignette, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(g_vignette, TEXTURE_WRAP_CLAMP);
    g_vignetteReady = true;
}

void ScreenFx_Unload(void)
{
    if (g_vignetteReady) UnloadTexture(g_vignette);
    g_vignetteReady = false;
}

float ScreenFx_VignetteStrength(float hpFraction)
{
    if (hpFraction >= VIGNETTE_HEALTHY_AT) return 0.0f;
    if (hpFraction < 0.0f) hpFraction = 0.0f;
    const float t = (VIGNETTE_HEALTHY_AT - hpFraction) / VIGNETTE_HEALTHY_AT;   // 0..1
    return t * t * (3.0f - 2.0f * t);                                           // smoothstep
}

void ScreenFx_DrawHealthEffects(int screenW, int screenH, float hpFraction, float hitFlash, float timeSecs)
{
    // A hit also darkens the edges for a moment, even at full health.
    float strength = ScreenFx_VignetteStrength(hpFraction) + 0.30f * hitFlash;
    if (strength > 1.0f) strength = 1.0f;

    if (g_vignetteReady && strength > 0.004f) {
        // The ring is drawn large when healthy, so only its faint outer edge reaches the screen,
        // and shrinks toward the screen size as health falls, closing in on the middle.
        float scale = 3.4f + (1.0f - 3.4f) * strength;
        if (hpFraction < 0.30f) scale *= 1.0f + 0.03f * sinf(timeSecs * 7.0f);   // slow heartbeat
        if (scale < 1.0f) scale = 1.0f;
        const float alpha = 0.35f + 0.65f * strength;
        const float w = (float)screenW * scale, h = (float)screenH * scale;
        DrawTexturePro(g_vignette,
                       (Rectangle){ 0.0f, 0.0f, (float)VIGNETTE_TEX_SIZE, (float)VIGNETTE_TEX_SIZE },
                       (Rectangle){ ((float)screenW - w) * 0.5f, ((float)screenH - h) * 0.5f, w, h },
                       (Vector2){ 0.0f, 0.0f }, 0.0f, Fade(WHITE, alpha));
    }

    if (hitFlash > 0.0f) DrawRectangle(0, 0, screenW, screenH, Fade((Color){ 200, 20, 20, 255 }, 0.40f * hitFlash));
}

void ScreenFx_DrawCrosshair(int screenW, int screenH)
{
    const int cx = screenW / 2, cy = screenH / 2;
    DrawRectangle(cx - 1, cy - 1, 3, 3, Fade(WHITE, 0.6f));
}

void ScreenFx_DrawHitMarker(int screenW, int screenH, float hit, float kill)
{
    const float cx = (float)screenW * 0.5f, cy = (float)screenH * 0.5f;
    if (hit > 0.0f) {
        const Color c = Fade(WHITE, hit > 1.0f ? 1.0f : hit);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                DrawLineEx((Vector2){ cx + sx * 6.0f, cy + sy * 6.0f }, (Vector2){ cx + sx * 13.0f, cy + sy * 13.0f }, 2.0f, c);
    }
    if (kill > 0.0f) {
        const Color c = Fade((Color){ 235, 40, 40, 255 }, kill > 1.0f ? 1.0f : kill);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                DrawLineEx((Vector2){ cx + sx * 9.0f, cy + sy * 9.0f }, (Vector2){ cx + sx * 22.0f, cy + sy * 22.0f }, 3.0f, c);
    }
}

void ScreenFx_DrawHealthBar(int x, int y, int w, int h, float hp, float maxHp)
{
    float f = maxHp > 0.0f ? hp / maxHp : 0.0f;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    const Color fill = (Color){ (unsigned char)(60 + 160.0f * (1.0f - f)), (unsigned char)(60 + 150.0f * f), 40, 255 };
    DrawRectangle(x, y, w, h, Fade(BLACK, 0.6f));
    DrawRectangle(x + 2, y + 2, (int)((float)(w - 4) * f), h - 4, fill);
    DrawRectangleLines(x, y, w, h, Fade(WHITE, 0.7f));
    DrawText(TextFormat("HP %d", (int)ceilf(hp)), x + 6, y - 14, 12, WHITE);
}

// ---- Frame graph -------------------------------------------------------------
static float g_ms[FRAME_GRAPH_SAMPLES];
static float g_look[FRAME_GRAPH_SAMPLES];
static float g_cpu[FRAME_GRAPH_SAMPLES];
static float g_stage[FRAME_GRAPH_SAMPLES][FRAME_STAGES];
static int   g_head = 0;
static int   g_filled = 0;

void FrameGraph_Push(float frameMs, float lookPx)
{
    g_ms[g_head] = frameMs;
    g_look[g_head] = lookPx;
    g_head = (g_head + 1) % FRAME_GRAPH_SAMPLES;
    if (g_filled < FRAME_GRAPH_SAMPLES) ++g_filled;
}

void FrameGraph_PushStages(const float stagesMs[FRAME_STAGES])
{
    const int idx = (g_head + FRAME_GRAPH_SAMPLES - 1) % FRAME_GRAPH_SAMPLES;   // the sample just pushed
    float sum = 0.0f;
    for (int i = 0; i < FRAME_STAGES; ++i) { g_stage[idx][i] = stagesMs[i]; sum += stagesMs[i]; }
    g_cpu[idx] = sum;
}

void FrameGraph_WorstCpu(float *cpuMs, float stagesMs[FRAME_STAGES])
{
    int worst = -1;
    float mx = -1.0f;
    for (int i = 0; i < g_filled; ++i)
        if (g_cpu[i] > mx) { mx = g_cpu[i]; worst = i; }
    if (cpuMs) *cpuMs = mx < 0.0f ? 0.0f : mx;
    for (int i = 0; i < FRAME_STAGES; ++i) stagesMs[i] = worst >= 0 ? g_stage[worst][i] : 0.0f;
}

void FrameGraph_Stats(float *avgMs, float *maxMs, float *maxLookPx)
{
    float sum = 0.0f, mx = 0.0f, ml = 0.0f;
    for (int i = 0; i < g_filled; ++i) {
        sum += g_ms[i];
        if (g_ms[i] > mx) mx = g_ms[i];
        if (g_look[i] > ml) ml = g_look[i];
    }
    if (avgMs) *avgMs = g_filled ? sum / (float)g_filled : 0.0f;
    if (maxMs) *maxMs = mx;
    if (maxLookPx) *maxLookPx = ml;
}

void FrameGraph_Draw(int x, int y, int w)
{
    const int hMs = 44, hLook = 30, gap = 4;
    const float msScale = 50.0f, lookScale = 80.0f;
    const float barW = (float)w / (float)FRAME_GRAPH_SAMPLES;

    DrawRectangle(x, y, w, hMs, Fade(BLACK, 0.55f));
    DrawRectangle(x, y + hMs + gap, w, hLook, Fade(BLACK, 0.55f));

    for (int i = 0; i < g_filled; ++i) {
        const int idx = (g_head + FRAME_GRAPH_SAMPLES - g_filled + i) % FRAME_GRAPH_SAMPLES;
        const float ms = g_ms[idx];
        const Color c = ms <= 18.5f ? (Color){ 70, 220, 90, 255 } : (ms <= 34.0f ? (Color){ 240, 210, 60, 255 } : (Color){ 235, 60, 50, 255 });
        float bh = ms / msScale; if (bh > 1.0f) bh = 1.0f;
        const int bx = x + (int)((float)i * barW), bw = barW < 1.0f ? 1 : (int)barW;
        DrawRectangle(bx, y + hMs - (int)(bh * (float)hMs), bw, (int)(bh * (float)hMs) + 1, c);

        float lh = g_look[idx] / lookScale; if (lh > 1.0f) lh = 1.0f;
        DrawRectangle(bx, y + hMs + gap + hLook - (int)(lh * (float)hLook), bw, (int)(lh * (float)hLook) + 1, (Color){ 120, 170, 255, 255 });
    }
    // 16.7 ms (60 fps) and 33.3 ms (30 fps) guides.
    DrawRectangle(x, y + hMs - (int)(16.7f / msScale * (float)hMs), w, 1, Fade(WHITE, 0.6f));
    DrawRectangle(x, y + hMs - (int)(33.3f / msScale * (float)hMs), w, 1, Fade(RED, 0.6f));

    float avg, mx, ml;
    FrameGraph_Stats(&avg, &mx, &ml);
    DrawText(TextFormat("frame avg %.1f max %.0f ms", avg, mx), x, y + hMs + gap + hLook + 4, 12, WHITE);
    DrawText(TextFormat("look max %.0f px/frame", ml), x, y + hMs + gap + hLook + 18, 12, (Color){ 150, 190, 255, 255 });

    float cpu, st[FRAME_STAGES];
    FrameGraph_WorstCpu(&cpu, st);
    DrawText(TextFormat("our code, worst frame %.1f ms:", cpu), x - 40, y + hMs + gap + hLook + 34, 10, (Color){ 255, 220, 140, 255 });
    DrawText(TextFormat("in %.1f sim %.1f ai %.1f 3d %.1f ui %.1f end %.1f", st[0], st[1], st[2], st[3], st[4], st[5]),
             x - 40, y + hMs + gap + hLook + 46, 10, (Color){ 255, 220, 140, 255 });
}

void ScreenFx_DrawStaminaBar(int x, int y, int w, int h, float fraction, bool exhausted, float recoverFraction, float timeSecs)
{
    float f = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
    Color fill = (Color){ 70, 150, 230, 255 };
    if (exhausted) {
        const float pulse = 0.65f + 0.35f * sinf(timeSecs * 9.0f);
        fill = (Color){ (unsigned char)(230.0f * pulse), 50, 40, 255 };
    } else if (f < 0.25f) {
        fill = (Color){ 235, 160, 40, 255 };
    }
    DrawRectangle(x, y, w, h, Fade(BLACK, 0.6f));
    DrawRectangle(x + 1, y + 1, (int)((float)(w - 2) * f), h - 2, fill);
    DrawRectangleLines(x, y, w, h, Fade(WHITE, 0.5f));
    if (exhausted) {
        const int tick = x + 1 + (int)((float)(w - 2) * recoverFraction);
        DrawRectangle(tick, y - 2, 1, h + 4, Fade(WHITE, 0.8f));
    }
}
