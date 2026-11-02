// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lighting.md, section "view_clip.h and view_clip.c"
// ============================================================================
#include "view_clip.h"

#include <math.h>

#include "rlgl.h"

#include "../platform/platform.h"

#if TOWER_GLES2
#include <GLES2/gl2.h>
#endif

static double g_fovyRad = 1.0471975512;   // 60 degrees until told otherwise

static void SetFrustum(double nearPlane, double farPlane)
{
    const double aspect = (double)GetScreenWidth() / (double)GetScreenHeight();
    const double top = nearPlane * tan(g_fovyRad * 0.5);
    const double right = top * aspect;
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlFrustum(-right, right, -top, top, nearPlane, farPlane);
    rlMatrixMode(RL_MODELVIEW);
}

void ViewClip_Apply(const Camera3D *cam, double nearPlane, double farPlane)
{
    g_fovyRad = (double)cam->fovy * (double)DEG2RAD;
    SetFrustum(nearPlane, farPlane);
}

void ViewClip_Reapply(double nearPlane, double farPlane)
{
    SetFrustum(nearPlane, farPlane);
}

void ViewClip_ClearDepth(void)
{
    rlDrawRenderBatchActive();
#if TOWER_GLES2
    glClear(GL_DEPTH_BUFFER_BIT);
#endif
}

void ViewClip_QueryBuffers(int *depthBits, int *samples)
{
    int d = -1, s = -1;
#if TOWER_GLES2
    GLint v = 0;
    glGetIntegerv(GL_DEPTH_BITS, &v);
    d = (int)v;
    v = 0;
    glGetIntegerv(GL_SAMPLES, &v);
    s = (int)v;
#endif
    if (depthBits) *depthBits = d;
    if (samples) *samples = s;
}
