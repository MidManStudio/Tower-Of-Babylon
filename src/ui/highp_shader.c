// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lighting.md, section "highp_shader.h and highp_shader.c"
// ============================================================================
#include "highp_shader.h"

#include "raylib.h"
#include "rlgl.h"

// Copied from raylib's default ES2 shaders (rlgl.h, rlLoadShaderDefault) with only the precision lines changed.
const char *const HIGHP_VS_ES2 =
    "#version 100                       \n"
    "precision highp float;             \n"
    "attribute vec3 vertexPosition;     \n"
    "attribute vec2 vertexTexCoord;     \n"
    "attribute vec4 vertexColor;        \n"
    "varying vec2 fragTexCoord;         \n"
    "varying vec4 fragColor;            \n"
    "uniform mat4 mvp;                  \n"
    "void main()                        \n"
    "{                                  \n"
    "    fragTexCoord = vertexTexCoord; \n"
    "    fragColor = vertexColor;       \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                  \n";

const char *const HIGHP_FS_ES2 =
    "#version 100                       \n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH  \n"
    "precision highp float;             \n"
    "#else                              \n"
    "precision mediump float;           \n"
    "#endif                             \n"
    "varying vec2 fragTexCoord;         \n"
    "varying vec4 fragColor;            \n"
    "uniform sampler2D texture0;        \n"
    "uniform vec4 colDiffuse;           \n"
    "void main()                        \n"
    "{                                  \n"
    "    vec4 texelColor = texture2D(texture0, fragTexCoord); \n"
    "    gl_FragColor = texelColor*colDiffuse*fragColor;      \n"
    "}                                  \n";

// Plain high precision shader plus the flashlight beam. Same precision rules; the vertex shader passes the world position
// (raylib's batches hold positions already moved into the world, so vertexPosition is one) to the fragment shader.
const char *const HIGHP_FLASH_VS_ES2 =
    "#version 100                       \n"
    "precision highp float;             \n"
    "attribute vec3 vertexPosition;     \n"
    "attribute vec2 vertexTexCoord;     \n"
    "attribute vec4 vertexColor;        \n"
    "varying vec2 fragTexCoord;         \n"
    "varying vec4 fragColor;            \n"
    "varying vec3 fragWorld;            \n"
    "uniform mat4 mvp;                  \n"
    "void main()                        \n"
    "{                                  \n"
    "    fragTexCoord = vertexTexCoord; \n"
    "    fragColor = vertexColor;       \n"
    "    fragWorld = vertexPosition;    \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                  \n";

const char *const HIGHP_FLASH_FS_ES2 =
    "#version 100                       \n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH  \n"
    "precision highp float;             \n"
    "#else                              \n"
    "precision mediump float;           \n"
    "#endif                             \n"
    "varying vec2 fragTexCoord;         \n"
    "varying vec4 fragColor;            \n"
    "varying vec3 fragWorld;            \n"
    "uniform sampler2D texture0;        \n"
    "uniform vec4 colDiffuse;           \n"
    "uniform vec3 flashPos;             \n"
    "uniform vec3 flashDir;             \n"
    "uniform vec4 flashParams;          \n"
    "uniform vec3 flashTint;            \n"
    "void main()                        \n"
    "{                                  \n"
    "    vec4 base = texture2D(texture0, fragTexCoord)*colDiffuse*fragColor; \n"
    "    float beam = 0.0;              \n"
    "    if (flashParams.w > 0.0)       \n"
    "    {                              \n"
    "        vec3 v = fragWorld - flashPos; \n"
    "        float d = max(length(v), 0.001); \n"
    "        float spot = clamp((dot(v, flashDir)/d - flashParams.y)/(flashParams.x - flashParams.y), 0.0, 1.0); \n"
    "        spot = spot*spot*(3.0 - 2.0*spot); \n"
    "        float fall = clamp(1.0 - d*flashParams.z, 0.0, 1.0); \n"
    "        beam = spot*fall*fall*flashParams.w; \n"
    "    }                              \n"
    "    float peak = max(base.r, max(base.g, base.b)); \n"
    "    gl_FragColor = vec4(base.rgb + beam*(0.85*flashTint*(1.0 - 0.55*min(peak, 1.0)) + 0.35*base.rgb), base.a); \n"
    "}                                  \n";

static Shader g_shader;
static Shader g_flashShader;
static bool   g_available = false;
static bool   g_flashAvailable = false;
static bool   g_enabled = true;
static bool   g_inside = false;
static bool   g_insideFlash = false;
static int    g_locPos = -1, g_locDir = -1, g_locParams = -1, g_locTint = -1;

void Highp_Init(void)
{
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)      // OpenGL ES 2.0 either way: the browser, and the phone's own driver in the Android build
    g_shader = LoadShaderFromMemory(HIGHP_VS_ES2, HIGHP_FS_ES2);
    // A shader that fails to compile comes back as raylib's default, which is no use to us.
    g_available = g_shader.id != 0 && g_shader.id != rlGetShaderIdDefault();
    TraceLog(LOG_INFO, "SHADER: high precision shader %s", g_available ? "loaded" : "FAILED, using raylib's default");

    // The beam version is a second program: if it will not compile the plain one carries on, without a beam.
    g_flashShader = LoadShaderFromMemory(HIGHP_FLASH_VS_ES2, HIGHP_FLASH_FS_ES2);
    g_flashAvailable = g_available && g_flashShader.id != 0 && g_flashShader.id != rlGetShaderIdDefault();
    if (g_flashAvailable) {
        g_locPos    = GetShaderLocation(g_flashShader, "flashPos");
        g_locDir    = GetShaderLocation(g_flashShader, "flashDir");
        g_locParams = GetShaderLocation(g_flashShader, "flashParams");
        g_locTint   = GetShaderLocation(g_flashShader, "flashTint");
        if (g_locPos < 0 || g_locDir < 0 || g_locParams < 0 || g_locTint < 0) g_flashAvailable = false;     // a uniform the compiler dropped: no beam
    }
    TraceLog(LOG_INFO, "SHADER: flashlight shader %s", g_flashAvailable ? "loaded" : "not available, the glow fallback is used");
#endif
}

bool Highp_FlashlightAvailable(void) { return g_flashAvailable && g_enabled; }

bool Highp_Available(void) { return g_available; }
void Highp_SetEnabled(bool on) { g_enabled = on; }
bool Highp_Enabled(void) { return g_enabled && g_available; }

void Highp_Begin(void)
{
    if (!Highp_Enabled()) return;
    g_insideFlash = g_flashAvailable;
    BeginShaderMode(g_insideFlash ? g_flashShader : g_shader);
    g_inside = true;
    if (g_insideFlash) Highp_FlashlightOff();      // a new frame starts with no beam
}

void Highp_End(void)
{
    if (g_inside) { EndShaderMode(); g_inside = false; g_insideFlash = false; }
}

void Highp_SetFlashlight(Vector3 pos, Vector3 dir, float cosInner, float cosOuter, float range, float strength, unsigned tintRGB)
{
    if (!g_inside || !g_insideFlash) return;
    const float params[4] = { cosInner, cosOuter, range > 0.001f ? 1.0f / range : 0.0f, strength };
    const float tint[3] = { (float)((tintRGB >> 16) & 255u) / 255.0f, (float)((tintRGB >> 8) & 255u) / 255.0f, (float)(tintRGB & 255u) / 255.0f };
    rlDrawRenderBatchActive();                     // what is queued is drawn with the beam it was queued under
    SetShaderValue(g_flashShader, g_locPos, &pos, SHADER_UNIFORM_VEC3);
    SetShaderValue(g_flashShader, g_locDir, &dir, SHADER_UNIFORM_VEC3);
    SetShaderValue(g_flashShader, g_locParams, params, SHADER_UNIFORM_VEC4);
    SetShaderValue(g_flashShader, g_locTint, tint, SHADER_UNIFORM_VEC3);
}

void Highp_FlashlightOff(void)
{
    if (!g_inside || !g_insideFlash) return;
    static const float off[4] = { 1.0f, 0.0f, 1.0f, 0.0f };      // strength 0: the beam adds nothing
    rlDrawRenderBatchActive();
    SetShaderValue(g_flashShader, g_locParams, off, SHADER_UNIFORM_VEC4);
}
