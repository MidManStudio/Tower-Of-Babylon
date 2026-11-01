// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lighting.md, section "highp_headless.c"
// ============================================================================
// The high precision shader's source: that the vertex shader is highp and says nothing of mediump, that the fragment
// shader asks for highp where the device has it, that every attribute and uniform name raylib's batches look for is
// still there, and that those names are the ones in raylib's own default shader (read from raylib/src/rlgl.h when it
// can be found). A shader cannot be compiled here (no GPU); this guards the text. Inside a macro so mray's build,
// which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DHIGHP_HEADLESS_TEST -Isrc -Iraylib/src tests/highp_headless.c src/ui/highp_shader.c -o highp_headless
// Run (from the project root, so raylib/src/rlgl.h is found):
//   ./highp_headless
#ifdef HIGHP_HEADLESS_TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "ui/highp_shader.h"

// highp_shader.c calls these; the test never reaches them (no web build, no GPU).
static int g_begin = 0, g_end = 0;
void BeginShaderMode(Shader s) { (void)s; ++g_begin; }
void EndShaderMode(void) { ++g_end; }
void TraceLog(int level, const char *text, ...) { (void)level; (void)text; }
unsigned int rlGetShaderIdDefault(void) { return 1; }
Shader LoadShaderFromMemory(const char *vs, const char *fs) { (void)vs; (void)fs; return (Shader){ 0 }; }
static int g_flushes = 0, g_uniformSets = 0;
void rlDrawRenderBatchActive(void) { ++g_flushes; }
void SetShaderValue(Shader s, int loc, const void *v, int type) { (void)s; (void)loc; (void)v; (void)type; ++g_uniformSets; }
int GetShaderLocation(Shader s, const char *name) { (void)s; (void)name; return 0; }

static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static char *ReadAll(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) buf[n] = 0; else { free(buf); buf = NULL; }
    fclose(f);
    return buf;
}

int main(void)
{
    const char *vs = HIGHP_VS_ES2, *fs = HIGHP_FS_ES2;

    printf("The source\n");
    Check(strncmp(vs, "#version 100", 12) == 0 && strncmp(fs, "#version 100", 12) == 0, "both are GLSL ES 1.00, which WebGL 1 and 2 both accept, with the version line first");
    Check(strstr(vs, "precision highp float;") != NULL, "the vertex shader declares highp floats");
    Check(strstr(vs, "mediump") == NULL && strstr(vs, "lowp") == NULL, "and mentions no lower precision at all");
    Check(strstr(fs, "#ifdef GL_FRAGMENT_PRECISION_HIGH") != NULL && strstr(fs, "precision highp float;") != NULL, "the fragment shader asks for highp where the device has it");
    Check(strstr(fs, "precision mediump float;") != NULL, "and falls back to mediump where it does not, so it still compiles");
    const char *vsNames[] = { "attribute vec3 vertexPosition", "attribute vec2 vertexTexCoord", "attribute vec4 vertexColor", "uniform mat4 mvp", "varying vec2 fragTexCoord", "varying vec4 fragColor", "gl_Position = mvp*vec4(vertexPosition, 1.0)" };
    const char *fsNames[] = { "varying vec2 fragTexCoord", "varying vec4 fragColor", "uniform sampler2D texture0", "uniform vec4 colDiffuse", "texture2D(texture0, fragTexCoord)", "gl_FragColor = texelColor*colDiffuse*fragColor" };
    int vsOk = 1, fsOk = 1;
    for (size_t i = 0; i < sizeof vsNames / sizeof *vsNames; ++i) if (!strstr(vs, vsNames[i])) { vsOk = 0; printf("    missing in the vertex shader: %s\n", vsNames[i]); }
    for (size_t i = 0; i < sizeof fsNames / sizeof *fsNames; ++i) if (!strstr(fs, fsNames[i])) { fsOk = 0; printf("    missing in the fragment shader: %s\n", fsNames[i]); }
    Check(vsOk, "the vertex shader has every attribute, uniform and varying raylib's batches use");
    Check(fsOk, "the fragment shader has every varying, sampler and tint raylib's batches use");

    printf("Against raylib's own default shader\n");
    char *rl = ReadAll("raylib/src/rlgl.h");
    if (!rl) {
        printf("  (raylib/src/rlgl.h not found from here: skipped)\n");
    } else {
        const char *all[] = { "vertexPosition", "vertexTexCoord", "vertexColor", "fragTexCoord", "fragColor", "texture0", "colDiffuse", "mvp" };
        int same = 1;
        for (size_t i = 0; i < sizeof all / sizeof *all; ++i) if (!strstr(rl, all[i])) { same = 0; printf("    raylib no longer uses the name %s\n", all[i]); }
        Check(same, "every name used here is a name raylib's default shader uses (so a raylib update that renames one is noticed)");
        // The reason this module exists: the default ES2 vertex shader declares mediump. Reported, not required: if a newer
        // raylib fixes it this note changes and the module can be retired.
        const char *marker = "#elif defined(GRAPHICS_API_OPENGL_ES2)";
        const char *at = strstr(rl, "\"uniform mat4 mvp;");
        int mediumInVs = 0;
        if (at) {
            const char *p = at;
            for (int back = 0; back < 1600 && p > rl; ++back, --p) {
                if (strncmp(p, "\"#version 100", 13) == 0) { mediumInVs = strstr(p, "precision mediump float") != NULL && strstr(p, "precision mediump float") < at; break; }
            }
        }
        (void)marker;
        printf("  [note] raylib's default web vertex shader %s mediump\n", mediumInVs ? "declares" : "does not declare");
        free(rl);
    }

    printf("Using it\n");
    Highp_Init();
    Check(!Highp_Available() && !Highp_Enabled(), "off the web there is no shader to load, and it reports so");
    Highp_Begin(); Highp_End();
    Check(g_begin == 0 && g_end == 0, "so begin and end do nothing, and never leave a shader mode open");
    Highp_SetEnabled(false);
    Highp_SetEnabled(true);
    Check(!Highp_Enabled(), "enabling something that failed to load changes nothing");

    printf("The flashlight shader's source\n");
    {
        const char *fvs = HIGHP_FLASH_VS_ES2, *ffs = HIGHP_FLASH_FS_ES2;
        Check(strncmp(fvs, "#version 100", 12) == 0 && strncmp(ffs, "#version 100", 12) == 0, "both are GLSL ES 1.00 like the plain pair");
        Check(strstr(fvs, "precision highp float;") != NULL && strstr(fvs, "mediump") == NULL && strstr(fvs, "lowp") == NULL, "the beam's vertex shader is highp and mentions no lower precision (the glitch fix holds)");
        Check(strstr(ffs, "#ifdef GL_FRAGMENT_PRECISION_HIGH") != NULL && strstr(ffs, "precision highp float;") != NULL && strstr(ffs, "precision mediump float;") != NULL, "the beam's fragment shader asks for highp and falls back to mediump where it must");
        Check(strstr(fvs, "varying vec3 fragWorld;") != NULL && strstr(ffs, "varying vec3 fragWorld;") != NULL && strstr(fvs, "fragWorld = vertexPosition;") != NULL, "the world position is passed from the vertex shader to the fragment shader");
        const char *names[] = { "uniform vec3 flashPos;", "uniform vec3 flashDir;", "uniform vec4 flashParams;", "uniform vec3 flashTint;" };
        int all = 1;
        for (int i = 0; i < 4; ++i) if (strstr(ffs, names[i]) == NULL) all = 0;
        Check(all, "the fragment shader has the four beam uniforms highp_shader.c looks up by name");
        // Everything raylib's batches rely on is still there, same as in the plain pair.
        const char *rl[] = { "attribute vec3 vertexPosition;", "attribute vec2 vertexTexCoord;", "attribute vec4 vertexColor;", "uniform mat4 mvp;", "varying vec2 fragTexCoord;", "varying vec4 fragColor;" };
        int rlOk = 1;
        for (int i = 0; i < 6; ++i) if (strstr(fvs, rl[i]) == NULL) rlOk = 0;
        Check(rlOk && strstr(ffs, "uniform sampler2D texture0;") != NULL && strstr(ffs, "uniform vec4 colDiffuse;") != NULL, "and everything raylib's batches use by name is still there");
        Check(strstr(ffs, "flashParams.w > 0.0") != NULL, "a strength of 0 skips the beam (and the shader then draws what the plain one does: tests/shader_gles_test.py compares the pixels)");
        Check(HIGHP_FLASH_FS_ES2 != HIGHP_FS_ES2 && HIGHP_FLASH_VS_ES2 != HIGHP_VS_ES2, "it is a second program: the plain one is untouched, so if this one fails to compile the glitch fix stays");
        Check(!Highp_FlashlightAvailable(), "off the web there is no beam shader");
        Highp_SetFlashlight((Vector3){ 0, 0, 0 }, (Vector3){ 0, 0, 1 }, 0.9f, 0.5f, 20.0f, 0.8f, 0xFFE9B0u);
        Highp_FlashlightOff();
        Check(g_flushes == 0 && g_uniformSets == 0, "with no shader the beam calls do nothing at all: no flush, no uniform");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
