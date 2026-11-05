// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/input.md, section "input.h and input.c"
// ============================================================================
#include "input.h"

#include <string.h>

#include "../platform/platform.h"

#define MOUSE_FINGER_ID 100000         // a mouse button held down is treated as one more finger, so a PC browser plays like a phone

static TcState     g_tc;
static TcOut       g_touchOut;
static InputScheme g_scheme = INPUT_SCHEME_TOUCH;
static bool        g_prevUse = false, g_prevSwap = false;

void Input_Init(void)
{
    Tc_Init(&g_tc);
    memset(&g_touchOut, 0, sizeof g_touchOut);
    g_scheme = TOWER_INPUT_TOUCH ? INPUT_SCHEME_TOUCH : INPUT_SCHEME_KEYBOARD;
    g_prevUse = g_prevSwap = false;
#if TOWER_INPUT_POINTER_LOCK
    DisableCursor();                  // desktop: the mouse turns the camera and does not leave the window
#endif
}

const TcOut *Input_Touch(void) { return &g_touchOut; }
InputScheme Input_Scheme(void) { return g_scheme; }

bool Input_NewTap(Vector2 *out)
{
    static int prevIds[16];
    static int prevN = 0;
    bool got = false;
    const int n = GetTouchPointCount();
    int ids[16];
    const int m = n < 16 ? n : 16;
    for (int i = 0; i < m; ++i) {
        ids[i] = GetTouchPointId(i);
        bool seen = false;
        for (int j = 0; j < prevN; ++j) if (prevIds[j] == ids[i]) { seen = true; break; }
        if (!seen && !got) { *out = GetTouchPosition(i); got = true; }
    }
    for (int i = 0; i < m; ++i) prevIds[i] = ids[i];
    prevN = m;
    if (n == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { *out = GetMousePosition(); got = true; }
    return got;
}

void Input_Update(const InputContext *ctx, InputFrame *f)
{
    memset(f, 0, sizeof *f);
    bool useHeld = false, swapHeld = false;

#if TOWER_INPUT_TOUCH
    {
        TcTouch pts[TC_MAX_TOUCHES];
        int n = 0;
        const int raw = GetTouchPointCount();
        for (int i = 0; i < raw && n < TC_MAX_TOUCHES; ++i) {
            const Vector2 p = GetTouchPosition(i);
            pts[n++] = (TcTouch){ GetTouchPointId(i), p.x, p.y };
        }
        if (raw > 0) { g_scheme = INPUT_SCHEME_TOUCH; f->gesture = true; }
        if (raw == 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vector2 p = GetMousePosition();
            pts[n++] = (TcTouch){ MOUSE_FINGER_ID, p.x, p.y };
            f->gesture = true;                      // (a mouse does not switch the scheme: only a real finger does)
        }
        Tc_Update(&g_tc, pts, n, ctx, &g_touchOut);
        f->move = (Vector2){ g_touchOut.moveX, g_touchOut.moveY };
        f->look = (Vector2){ g_touchOut.lookDX, g_touchOut.lookDY };
        f->sprint = g_touchOut.sprint;
        f->fire = g_touchOut.fireHeld;
        f->jump = g_touchOut.jumpHeld;
        f->light = g_touchOut.lightPressed;
        f->slide = g_touchOut.slidePressed;
        useHeld = g_touchOut.useHeld;
        swapHeld = g_touchOut.swapHeld;
    }
#endif

#if TOWER_INPUT_KEYBOARD
    {
        bool key = false;
        if (IsKeyDown(KEY_W)) { f->move.y = -1.0f; key = true; }
        if (IsKeyDown(KEY_S)) { f->move.y = 1.0f; key = true; }
        if (IsKeyDown(KEY_A)) { f->move.x = -1.0f; key = true; }
        if (IsKeyDown(KEY_D)) { f->move.x = 1.0f; key = true; }
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) { f->sprint = true; key = true; }
        if (IsKeyPressed(KEY_SPACE)) { f->jump = true; key = true; }
        if (IsKeyDown(KEY_F)) { f->fire = true; key = true; }
        if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_LEFT_CONTROL)) { f->slide = true; key = true; }
        if (IsKeyPressed(KEY_L)) { f->light = true; key = true; }
        if (IsKeyDown(KEY_E)) { useHeld = true; key = true; }
        if (IsKeyPressed(KEY_Q)) { swapHeld = true; key = true; }
        if (IsKeyPressed(KEY_I) || IsKeyPressed(KEY_TAB)) { f->inventoryToggle = true; key = true; }
        if (IsKeyPressed(KEY_P)) { f->statsToggle = true; key = true; }
        if (key) { g_scheme = INPUT_SCHEME_KEYBOARD; f->gesture = true; }
    }
#endif

#if TOWER_INPUT_POINTER_LOCK
    {
        const Vector2 d = GetMouseDelta();                 // desktop: the camera follows the mouse, the left button fires
        f->look.x += d.x; f->look.y += d.y;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) f->fire = true;
        f->gesture = true;
    }
#endif

    // The same rules as for the touch controls, applied to the keyboard and mouse too.
    if (ctx->locked) {
        const bool gesture = f->gesture;
        memset(f, 0, sizeof *f);                                   // a locked game takes nothing from the player...
        f->gesture = gesture;                                      // ...but a touch still counts for opening the sound card
    } else if (ctx->walkOnly) {
        f->look = (Vector2){ 0 };
        f->jump = f->fire = f->light = f->slide = false;
        useHeld = swapHeld = false;
    }
    f->useHeld = useHeld;
    f->use = useHeld && !g_prevUse;                                // USE and SWAP act once per press, not on every frame they are held
    f->swap = swapHeld && !g_prevSwap;
    g_prevUse = useHeld;
    g_prevSwap = swapHeld;
    f->scheme = g_scheme;
}
