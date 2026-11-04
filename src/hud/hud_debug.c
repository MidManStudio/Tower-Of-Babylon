// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/hud.md, section "hud_debug.c"
// ============================================================================
#include "hud.h"

#include "../audio/audio.h"
#include "../config/game_config.h"
#include "../lighting/lighting.h"
#include "../ui/highp_shader.h"
#include "../ui/screen_fx.h"

static int g_auditionId = 0;

bool HudDebug_HandleTap(Vector2 p)
{
    if (UiRect_Contains(HudLayout_DebugShaderRow(), p.x, p.y)) { Highp_SetEnabled(!Highp_Enabled()); return true; }
    if (UiRect_Contains(HudLayout_DebugSoundRow(), p.x, p.y)) {
        for (int tries = 0; tries < SND_COUNT; ++tries) {                // loops are skipped: they play by themselves (heartbeat at low health, the ambience)
            const SoundDef *sd = SoundBank_Get(g_auditionId);
            const int id = g_auditionId;
            g_auditionId = (g_auditionId + 1) % SND_COUNT;
            if (sd && !sd->loop) { Audio_Play(id, 1.0f); break; }
        }
        return true;
    }
    return false;
}

void HudDebug_Draw(const HudView *v)
{
    const Color blue = (Color){ 120, 200, 255, 255 };
    const HudDebug *d = &v->debug;
    DrawText(TextFormat("depth %d bit  msaa %d  shader %s (tap)", d->depthBits, d->msaaSamples,
                        Highp_Enabled() ? "highp" : (Highp_Available() ? "raylib default" : "default")), 10, 48, 14, blue);
    DrawText(TextFormat("lights on %d  dim %d  flicker %d  dead %d", Lighting_CountMode(LIGHT_ON), Lighting_CountMode(LIGHT_DIM),
                        Lighting_CountMode(LIGHT_FLICKER), Lighting_CountMode(LIGHT_DEAD)), 10, 66, 14, blue);
    DrawText(TextFormat("3d ms (submit): light %.1f  world %.1f  ents %.1f  hands %.1f", d->subMs[0], d->subMs[1], d->subMs[2], d->subMs[3]), 10, 84, 14, blue);
    const ConfigStatus *cs = GameConfig_Status();
    const char *wasmState = cs->wasm == MDIX_WASM_READY ? "wasm ready" : (cs->wasm == MDIX_WASM_LOADING ? "wasm loading" : (cs->wasm == MDIX_WASM_FAILED ? "wasm off" : "wasm idle"));
    DrawText(TextFormat("config: %s  (%s)%s", cs->source == CONFIG_FROM_FILES ? "from the .mdix files" : "baked copy", wasmState, cs->bakedStale ? "  baked copy is older than the files" : ""),
             10, 102, 14, cs->note[0] ? ORANGE : blue);
    if (cs->note[0]) DrawText(cs->note, 10, 118, 12, ORANGE);
    if (v->hasTorch)
        DrawText(TextFormat("flashlight: %s  %.0f s left  level %.2f  beam by %s", v->torchOn ? "on" : "off", (double)v->torchSecs, (double)v->torchLevel,
                            Highp_FlashlightAvailable() ? "shader" : "glow (no beam shader)"), 10, 134, 14, (Color){ 255, 233, 160, 255 });
    const AudioStatus *as = Audio_Status();
    DrawText(TextFormat("sound: %s  %d/%d built  %d playing  limiter %.2f  next: %s (tap)", as->deviceFailed ? "FAILED" : (as->deviceReady ? "on" : "waiting for a tap"),
                        as->built, as->total, as->voicesActive, (double)as->limiterGain, SoundBank_Name(g_auditionId)), 10, 152, 14,
             as->deviceFailed ? ORANGE : (Color){ 120, 255, 180, 255 });
    FrameGraph_Draw(800 - 210, 6, 204);
}
