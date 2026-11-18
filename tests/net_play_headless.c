// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/testing.md, section "net_play_headless.c"
// ============================================================================
// Headless test of walking in a networked game (src/net/net_play.c): what the fingers say becomes one command per tick, the look turns at once,
// the camera follows the prediction, and two players on a real (loopback) link walk, dive and see each other through that path. What it
// cannot see is the screen: the floor, the other players' bodies and the buttons are drawn by src/lobby/play_ui.c (checked for compiling only).
// Build and run from the project root (the raylib sources are only read for their headers):
//   cc -O2 -std=c99 -DNET_PLAY_HEADLESS_TEST -Isrc -Iraylib/src tests/net_play_headless.c src/net/net_wire.c src/net/net_proto.c src/net/net_loopback.c src/net/net_move_sim.c src/net/net_server.c src/net/net_client.c src/net/net_play.c src/input/input_latch.c src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c -lm -o net_play_headless
#ifdef NET_PLAY_HEADLESS_TEST
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "net/net_loopback.h"
#include "net/net_play.h"
#include "net/net_server.h"

void DrawCube(Vector3 p, float w, float h, float l, Color c) { (void)p; (void)w; (void)h; (void)l; (void)c; }
void DrawCylinderEx(Vector3 a, Vector3 b, float r0, float r1, int sides, Color c) { (void)a; (void)b; (void)r0; (void)r1; (void)sides; (void)c; }
bool CheckObstacleCollision(Vector3 pos, float radius) { (void)pos; (void)radius; return false; }

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }
static int Near(float a, float b, float e) { return fabsf(a - b) <= e; }

#define TICK_MS (1000.0 / 30.0)
static NetServer g_srv;
static NetLoopback g_lb[2];
static NetClient g_cl[2];
static NetPlay g_play[2];
static InputFrame g_in[2];                       // what each player's fingers say right now (set by the test)
static int g_slot[2];
static double g_now, g_nextTick, g_nextFrame;

static void World(void)
{
    g_now = 1000.0; g_nextTick = g_now + TICK_MS; g_nextFrame = g_now;
    NetServer_Init(&g_srv, 777u, 1);
    for (int k = 0; k < 2; ++k) {
        NetLoopback_Init(&g_lb[k], 90u + (uint32_t)k);
        NetLoopback_Advance(&g_lb[k], g_now);
        g_slot[k] = NetServer_AddClient(&g_srv, NetLoopback_End(&g_lb[k], 1), g_now);
        NetClient_Init(&g_cl[k], NetLoopback_End(&g_lb[k], 0), k == 0 ? "steve" : "brou", g_now);
        NetPlay_Init(&g_play[k], 0.0f);
        memset(&g_in[k], 0, sizeof g_in[k]);
    }
}

static void Step1ms(void)
{
    g_now += 1.0;
    for (int k = 0; k < 2; ++k) NetLoopback_Advance(&g_lb[k], g_now);
    NetServer_Poll(&g_srv, g_now);
    for (int k = 0; k < 2; ++k) NetClient_Poll(&g_cl[k], g_now);
    if (g_now >= g_nextTick) { g_nextTick += TICK_MS; NetServer_Tick(&g_srv, g_now); }
    if (g_now >= g_nextFrame) {                                                   // a 60 Hz screen: the fingers are read, the view turns
        g_nextFrame += 1000.0 / 60.0;
        for (int k = 0; k < 2; ++k) { NetPlay_Input(&g_play[k], &g_in[k], 400.0f); g_in[k].look = (Vector2){ 0, 0 }; g_in[k].jump = g_in[k].dive = g_in[k].slide = false; }
    }
    for (int k = 0; k < 2; ++k) NetClient_Update(&g_cl[k], g_now, NetPlay_CmdFn, &g_play[k]);
}
static void Run(double ms) { const double end = g_now + ms; while (g_now < end) Step1ms(); }
static const PlayerController *Srv(int k) { return &g_srv.c[g_slot[k]].player; }

int main(void)
{
    printf("The buttons and the command\n");
    {
        SimCmd s; SimCmd_Init(&s);
        Check(NetPlay_Buttons(&s) == 0, "nothing pressed: no buttons");
        s.sprint = true;  Check(NetPlay_Buttons(&s) == NET_BTN_SPRINT, "sprint"); s.sprint = false;
        s.useHeld = true; Check(NetPlay_Buttons(&s) == NET_BTN_USEHELD, "use held"); s.useHeld = false;
        s.jump = true;    Check(NetPlay_Buttons(&s) == NET_BTN_JUMP, "jump"); s.jump = false;
        s.fire = true;    Check(NetPlay_Buttons(&s) == NET_BTN_FIRE, "fire"); s.fire = false;
        s.use = true;     Check(NetPlay_Buttons(&s) == NET_BTN_USE, "use"); s.use = false;
        s.swap = true;    Check(NetPlay_Buttons(&s) == NET_BTN_SWAP, "swap"); s.swap = false;
        s.light = true;   Check(NetPlay_Buttons(&s) == NET_BTN_LIGHT, "light"); s.light = false;
        s.slide = true;   Check(NetPlay_Buttons(&s) == NET_BTN_SLIDE, "slide"); s.slide = false;
        s.dive = true;    Check(NetPlay_Buttons(&s) == NET_BTN_DIVE && NET_BTN_DIVE > 255, "dive (a ninth bit: past the old byte)"); s.dive = false;
        s.reload = true;  Check(NetPlay_Buttons(&s) == NET_BTN_RELOAD && NET_BTN_RELOAD > 255, "reload (a tenth)"); s.reload = false;

        NetPlay n; NetPlay_Init(&n, 1.0f);
        InputFrame in; memset(&in, 0, sizeof in);
        in.move = (Vector2){ 0.5f, -1.0f }; in.sprint = true; in.jump = true;
        NetPlay_Input(&n, &in, 400.0f);
        NetCmd c = NetPlay_Take(&n);
        float mx, my; NetCmd_Move(&c, &mx, &my);
        Check(fabsf(mx - 0.447f) < 0.02f && fabsf(my + 0.894f) < 0.02f, "the stick goes into the command (its length held to 1 as on the wire)");
        Check((c.buttons & NET_BTN_SPRINT) && (c.buttons & NET_BTN_JUMP), "sprint held and a jump pressed are in it");
        Check(Near(NetCmd_Yaw(&c), 1.0f, 0.001f), "and the yaw the player is looking at");
        in.jump = false;
        NetPlay_Input(&n, &in, 400.0f);
        c = NetPlay_Take(&n);
        Check((c.buttons & NET_BTN_SPRINT) && !(c.buttons & NET_BTN_JUMP), "the next tick: the held sprint stays, the one press was used up");
        memset(&in, 0, sizeof in); in.dive = true;
        NetPlay_Input(&n, &in, 400.0f);
        memset(&in, 0, sizeof in);
        NetPlay_Input(&n, &in, 400.0f);                                  // the press came and went between two ticks
        c = NetPlay_Take(&n);
        Check((c.buttons & NET_BTN_DIVE) && c.moveX == 0 && c.moveY == 0, "a press that came and went between two ticks is not lost (and the stick is let go)");
        c = NetPlay_Take(&n);
        Check(!(c.buttons & NET_BTN_DIVE), "and is sent once");
        Check(NetPlay_CmdFn(&n, 5).buttons == 0, "NetPlay_CmdFn is the same, for the client");
    }

    printf("\nThe look\n");
    {
        NetPlay n; NetPlay_Init(&n, 0.0f);
        InputFrame in; memset(&in, 0, sizeof in);
        in.look = (Vector2){ 100.0f, 0.0f };
        NetPlay_Input(&n, &in, 400.0f);
        Check(Near(n.look.yaw, -0.5f, 1e-4f), "a swipe turns the view at once, before any tick (100 px: 0.5 rad, as in single play)");
        NetPlay_Input(&n, &in, 400.0f);
        Check(Near(n.look.yaw, -1.0f, 1e-4f), "two frames between ticks turn it twice, and the next command carries both");
        Check(Near(NetCmd_Yaw(&(NetCmd){ 0, 0, NetCmd_Make(0, 0, n.look.yaw, 0, 0).yaw, 0, 0 }), 6.2832f - 1.0f, 0.001f), "(a negative yaw wraps on the wire into one turn)");
        in.look = (Vector2){ 0.0f, 100000.0f };
        NetPlay_Input(&n, &in, 400.0f);
        Check(n.look.pitch >= -1.4f - 1e-4f && n.look.pitch <= 1.4f + 1e-4f && n.look.pitch < 0.0f, "a huge swipe is cut, and the pitch stops short of straight up and down");
        NetPlay m; NetPlay_Init(&m, 0.0f);
        in.look = (Vector2){ 1000.0f, 0.0f };
        NetPlay_Input(&m, &in, 200.0f);
        Check(Near(m.look.yaw, -1.0f, 1e-4f), "a swipe longer than the cap is cut to it (200 px: 1.0 rad)");
        NetPlay k; NetPlay_Init(&k, 2.0f);
        Check(Near(k.look.yaw, 2.0f, 1e-6f), "the view can start the way the player faces");
    }

    printf("\nThe camera\n");
    {
        static NetClient c; memset(&c, 0, sizeof c);
        InitPlayerController(&c.player, 10.0f, 10.0f);
        c.prevPos = (Vector3){ 9.0f, 0.0f, 10.0f };
        c.player.position = (Vector3){ 10.0f, 0.0f, 10.0f };
        c.accum = 1000.0 / 30.0 * 0.5;
        NetPlay n; NetPlay_Init(&n, 0.0f);
        n.look.yaw = 1.5708f; n.look.pitch = 0.3f;
        Camera3D cam;
        NetPlay_Camera(&n, &c, &cam);
        const Vector3 d = { cam.target.x - cam.position.x, cam.target.y - cam.position.y, cam.target.z - cam.position.z };
        Check(Near(cam.position.x, 9.5f, 0.001f) && Near(cam.position.z, 10.0f, 0.001f), "the eye is half way between the last two ticks when the next tick is half way");
        Check(d.x > 0.9f * cosf(0.3f) && fabsf(d.z) < 0.01f && Near(d.y, sinf(0.3f), 0.01f), "looking where the player's view turned, not where the last tick left the body");
        Check(Near(c.player.yaw, 0.0f, 1e-6f) && Near(c.player.position.x, 10.0f, 1e-6f), "and the client's own player is left as it was");
        c.accum = 0.0;
        NetPlay_Camera(&n, &c, &cam);
        Check(Near(cam.position.x, 9.0f, 0.001f), "at the start of a tick it is the tick before");
        c.errOffset = (Vector3){ 0.3f, 0.0f, 0.0f };
        NetPlay_Camera(&n, &c, &cam);
        Check(Near(cam.position.x, 9.3f, 0.001f), "a fading correction is added");
        Check(Near(cam.fovy, 60.0f, 0.5f), "the field of view is the walking one");
        c.player.health.isAlive = true;
        Check(!NetPlay_SlideShown(&c.player) && !NetPlay_DiveShown(&c.player), "standing: no slide or dive button");
    }

    printf("\nTwo players walking on a real link\n");
    {
        World();
        Run(1000.0);
        Check(g_cl[0].state == NETC_PLAYING && g_cl[1].state == NETC_PLAYING, "both are in the game");
        const Vector3 s0 = Srv(0)->position, s1 = Srv(1)->position;
        g_in[0].move = (Vector2){ 0.0f, -1.0f };                          // player 0 pushes the stick forward; player 1 stands
        Run(2500.0);
        const Vector3 e0 = Srv(0)->position;
        const float walked = hypotf(e0.x - s0.x, e0.z - s0.z);
        Check(walked > 6.0f && walked < 14.0f, "on the server player 0 walked forward from the stick alone (about 4.5 m/s, 2.5 s)");
        Check(e0.z - s0.z > 0.8f * walked, "and forward is the way the player faces (yaw 0 looks along +z)");
        Check(hypotf(Srv(1)->position.x - s1.x, Srv(1)->position.z - s1.z) < 0.01f, "player 1, who pushed nothing, stayed");
        Check(g_cl[0].corrections == 0 && g_cl[1].corrections == 0, "the clients' predictions were never corrected: the same walk on both ends");
        Vector3 seen; float yaw;
        const bool sees = NetClient_RemoteView(&g_cl[1], 0, g_now, &seen, &yaw);
        const float behind = hypotf(seen.x - e0.x, seen.z - e0.z);
        Check(sees && behind < 1.2f, "player 1 sees player 0 where they are (a tenth of a second behind, which is up to half a metre at a walk)");
        const bool sees0 = NetClient_RemoteView(&g_cl[0], 1, g_now, &seen, &yaw);
        Check(sees0 && hypotf(seen.x - Srv(1)->position.x, seen.z - Srv(1)->position.z) < 0.05f, "and player 0 sees player 1 standing where they are");
        Camera3D live;
        NetPlay_Camera(&g_play[0], &g_cl[0], &live);
        Check(Near(live.position.y, g_cl[0].player.eyeY + g_cl[0].player.height * 0.35f, 0.05f) && hypotf(live.position.x - e0.x, live.position.z - e0.z) < 0.6f, "the camera of a walking player is at eye height (the controller's own: the body's 0.35 of its height over the floor) over where they are");
        const Vector3 mine = NetClient_ViewPos(&g_cl[0]);
        Check(hypotf(mine.x - e0.x, mine.z - e0.z) < 0.6f, "a player's own view is at (or a tick ahead of) where the server has them");

        g_in[0].look = (Vector2){ -314.16f, 0.0f };                       // turn a quarter of a turn to the left of the swipe: yaw +1.57
        Run(100.0);
        const Vector3 t0 = Srv(0)->position;
        Run(1500.0);
        const Vector3 t1 = Srv(0)->position;
        Check(Near(NetPlay_Take(&g_play[0]).yaw, NetCmd_Make(0, 0, 1.5708f, 0, 0).yaw, 40) && t1.x - t0.x > 3.0f && fabsf(t1.z - t0.z) < 1.5f, "after turning, forward is the new way: the walk goes along +x, as the view faces");
        Check(g_cl[0].corrections == 0, "and turning while walking is not a correction either");

        const Vector2 tries[3] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f } };       // right, left, back: whichever way is open (the floor is random, a wall may be there)
        bool open = false;
        for (int i = 0; i < 3 && !open; ++i) {
            g_in[0].move = tries[i]; g_in[0].sprint = true;
            Run(1000.0);
            open = Srv(0)->sprinting && PlayerSprintingSideways(Srv(0)) && hypotf(Srv(0)->moveVel.x, Srv(0)->moveVel.z) > 4.0f;
        }
        Check(open, "a sprint to the side (or back): sprinting sideways on the server");
        g_in[0].dive = true;
        Run(150.0);
        Check(Srv(0)->diving || Srv(0)->rolling, "a press of DIVE (one frame) makes the server's player dive");
        Run(2500.0);
        g_in[0].move = (Vector2){ 0.0f, 0.0f }; g_in[0].sprint = false;
        Run(1500.0);
        Check(g_cl[0].corrections == 0 && g_cl[0].hardSnaps == 0, "the whole dive, roll and lying down were predicted exactly: no corrections");
        Check(Srv(0)->lying == g_cl[0].player.lying && Srv(0)->diving == g_cl[0].player.diving, "client and server agree on whether the player is down");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
