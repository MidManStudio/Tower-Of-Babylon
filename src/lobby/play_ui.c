// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "play_ui.h and play_ui.c"
// ============================================================================
#include "play_ui.h"

#include <math.h>
#include <stdio.h>

#include "../dungeon/dungeon_renderer.h"
#include "../dungeon/staircase.h"
#include "../enemies/enemy_plan.h"
#include "../gvex/gvex_world.h"
#include "../hud/hud.h"
#include "../input/input.h"
#include "../lighting/lighting.h"
#include "../platform/platform.h"
#include "../ui/screen_fx.h"
#include "session_ui.h"

static Rectangle R(UiRect r) { return (Rectangle){ r.x, r.y, r.w, r.h }; }

Color PlayUI_PlayerColor(int id)
{
    static const Color c[NET_MAX_PLAYERS] = { { 232, 132, 48, 255 }, { 70, 160, 232, 255 }, { 112, 200, 100, 255 }, { 206, 92, 206, 255 } };
    return c[(id < 0 ? 0 : id) % NET_MAX_PLAYERS];
}

void PlayUI_Begin(const NetSession *s)
{
    Lighting_BuildForFloor(&s->client.map, s->client.floorNumber);     // the session's floor, not the one solo play was on
}

static Color Lit(Color c, float light)
{
    const float k = 0.30f + 0.70f * (light < 0.0f ? 0.0f : (light > 1.0f ? 1.0f : light));
    return (Color){ (unsigned char)((float)c.r * k), (unsigned char)((float)c.g * k), (unsigned char)((float)c.b * k), 255 };
}

// One other player: a body, a head and a nose that shows which way they face; as dark as the place they stand in.
static void DrawFigure(Vector3 feet, float yaw, int id)
{
    const Color col = Lit(PlayUI_PlayerColor(id), Lighting_AtPoint(feet));
    DrawCylinderEx((Vector3){ feet.x, feet.y + 0.05f, feet.z }, (Vector3){ feet.x, feet.y + 0.50f, feet.z }, 0.17f, 0.15f, 10, col);
    DrawSphere((Vector3){ feet.x, feet.y + 0.62f, feet.z }, 0.10f, col);
    const float fx = sinf(yaw), fz = cosf(yaw);
    DrawCube((Vector3){ feet.x + fx * 0.10f, feet.y + 0.62f, feet.z + fz * 0.10f }, 0.05f, 0.05f, 0.05f, Lit(WHITE, Lighting_AtPoint(feet)));
}

// One creature of the server's world (the G.V.E.X., docs/design/10-gvex-dossier.md). Its size and colour come from the same hash the single-player game uses for its
// cylinder enemies (enemy_plan.h: the floor map's seed and the creature's id, which both ends know), so the server only sends where it is and what it is doing.
// Before a blow it crouches and swells and turns warm, as the single-player cylinders do.
static float g_rear[NET_MAX_MONSTERS];      // drawing only: how far into its attack pose each creature is shown, 0 to 1, so a change of state eases instead of popping
static float g_lastHp[NET_MAX_MONSTERS];    // drawing only: the health each creature was last drawn with (0: not seen yet), to see a blow land
static float g_hurt[NET_MAX_MONSTERS];      // and how much of a flash it is still showing after one, 1 down to 0

static void DrawCreature(const NetMonsterView *m, uint32_t mapSeed, float dt)
{
    float height, radius; Color tint;
    EnemyLook(EnemyHash32(mapSeed + (uint32_t)m->id * 0x9e3779b1U), &height, &radius, &tint);
    float rear = m->state == GVEX_STATE_ATTACK ? 1.0f : 0.0f;
    if (m->id < NET_MAX_MONSTERS) {
        const float k = dt * 14.0f > 1.0f ? 1.0f : dt * 14.0f;                     // about 70 ms to cover two thirds of the way
        g_rear[m->id] += (rear - g_rear[m->id]) * k;
        rear = g_rear[m->id];
    }
    const float light = Lighting_AtPoint(m->pos);
    Color col = Lit(tint, light);
    if (m->id < NET_MAX_MONSTERS) {                                                // a blow that landed (on anyone's punch) flashes it red for about a sixth of a second
        if (g_lastHp[m->id] > 0.0f && m->health < g_lastHp[m->id] - 0.004f) g_hurt[m->id] = 1.0f;
        g_lastHp[m->id] = m->health;
        const float flash = g_hurt[m->id];
        if (flash > 0.0f) {
            const Color hit = Lit((Color){ 255, 70, 60, 255 }, light);
            col = (Color){ (unsigned char)((float)col.r + ((float)hit.r - (float)col.r) * flash), (unsigned char)((float)col.g + ((float)hit.g - (float)col.g) * flash),
                           (unsigned char)((float)col.b + ((float)hit.b - (float)col.b) * flash), 255 };
            g_hurt[m->id] = flash - dt * 6.0f > 0.0f ? flash - dt * 6.0f : 0.0f;
        }
    }
    if (rear > 0.0f) {
        const Color warm = Lit((Color){ 255, 140, 90, 255 }, light);
        col = (Color){ (unsigned char)(((int)col.r * 4 + (int)warm.r * 6) / 10), (unsigned char)(((int)col.g * 4 + (int)warm.g * 6) / 10), (unsigned char)(((int)col.b * 4 + (int)warm.b * 6) / 10), 255 };
    }
    const float r = radius * (1.0f + 0.22f * rear), h = height * (1.0f - 0.10f * rear);
    DrawCylinder(m->pos, r, r, h, 8, col);
    DrawCylinderWires(m->pos, r * 1.02f, r * 1.02f, h, 8, BLACK);        // a little outside, so the lines are not in the surface's depth
}

void PlayUI_Draw3D(const NetSession *s, const Camera3D *cam, double nowMs)
{
    const NetClient *c = &s->client;
    const Vector3 look = { cam->target.x - cam->position.x, cam->target.y - cam->position.y, cam->target.z - cam->position.z };
    DrawFloorMapFlashlight(NULL, cam->position, false);                 // no torch in a session yet
    DrawFloorMapCollapse(0.0f, 0.0f, -1.0f);                            // and no collapsing floor
    DrawFloorMapView(&c->map, cam->position, look, 0.0f);
    static const float closed[DOOR_COUNT] = { 0.0f, 0.0f };
    DrawStairs(&c->map, closed, c->player.position);
    for (int id = 0; id < NET_MAX_PLAYERS; ++id) {
        if (id == c->id) continue;
        Vector3 pos; float yaw;
        if (NetClient_RemoteView(c, (uint8_t)id, nowMs, &pos, &yaw)) DrawFigure(pos, yaw, id);
    }
    static double lastMs = 0.0;                                          // the screen's frame time, for easing the attack pose
    float dt = (float)((nowMs - lastMs) / 1000.0);
    lastMs = nowMs;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    NetMonsterView creatures[NET_MAX_MONSTERS];                          // empty when the server has no world of creatures (a build without TOWER_GVEX)
    const int n = NetClient_MonsterViews(c, nowMs, creatures, NET_MAX_MONSTERS);
    for (int i = 0; i < n; ++i) {
        if (hypotf(creatures[i].pos.x - cam->position.x, creatures[i].pos.z - cam->position.z) > 40.0f) continue;
        DrawCreature(&creatures[i], c->map.seed, dt);
    }
}

void PlayUI_DrawTags(const NetSession *s, const Camera3D *cam, double nowMs)
{
    const NetClient *c = &s->client;
    const Vector3 fwd = { cam->target.x - cam->position.x, cam->target.y - cam->position.y, cam->target.z - cam->position.z };
    for (int id = 0; id < NET_MAX_PLAYERS; ++id) {
        if (id == c->id) continue;
        Vector3 pos; float yaw;
        if (!NetClient_RemoteView(c, (uint8_t)id, nowMs, &pos, &yaw)) continue;
        const Vector3 head = { pos.x, pos.y + 0.85f, pos.z };
        const Vector3 d = { head.x - cam->position.x, head.y - cam->position.y, head.z - cam->position.z };
        if (d.x * fwd.x + d.y * fwd.y + d.z * fwd.z <= 0.0f) continue;      // behind the view
        if (d.x * d.x + d.y * d.y + d.z * d.z > 30.0f * 30.0f) continue;     // too far to read
        const Vector2 at = GetWorldToScreen(head, *cam);
        const char *tag = TextFormat("P%d", id + 1);
        DrawText(tag, (int)at.x - MeasureText(tag, 14) / 2, (int)at.y - 8, 14, PlayUI_PlayerColor(id));
    }
}

void PlayUI_DrawOverlay(const NetSession *s, bool showKeys, bool leaveArmed)
{
    const NetClient *c = &s->client;
    const PlayerController *p = &c->player;
    const int players = NetSession_Players(s);
    DrawText(TextFormat("P%d   %d / %d PLAYERS   PING %d MS", c->id + 1, players, NET_MAX_PLAYERS, (int)c->rttMs), 10, 8, 14, Fade(WHITE, 0.85f));
    DrawText(s->hosting ? TextFormat("HOSTING  %s", s->name) : TextFormat("JOINED  %s", s->name), 10, 26, 12, Fade(YELLOW, 0.8f));

    // NO SIGNAL: nothing heard from the other side for a second and a half. The game goes on (the player is predicted), and the link ends by itself at ten seconds.
    const uint32_t lost = NetSession_NoSignalMs(s);
    if (lost > 0) {
        const float pulse = 0.65f + 0.35f * sinf((float)GetTime() * 6.0f);
        const char *l1 = "NO SIGNAL", *l2 = TextFormat("NOTHING HEARD FOR %u.%u S", (unsigned)(lost / 1000), (unsigned)((lost % 1000) / 100));
        const int w = MeasureText(l1, 22), w2 = MeasureText(l2, 12), bw = (w > w2 ? w : w2) + 24;
        DrawRectangle(400 - bw / 2, 52, bw, 50, Fade(BLACK, 0.6f));
        DrawText(l1, 400 - w / 2, 58, 22, Fade(RED, pulse));
        DrawText(l2, 400 - w2 / 2, 84, 12, Fade(WHITE, 0.85f));
    }

    const UiRect lv = SessionUI_Leave();
    DrawRectangleRec(R(lv), Fade(BLACK, 0.55f));
    DrawRectangleLines((int)lv.x, (int)lv.y, (int)lv.w, (int)lv.h, leaveArmed ? RED : LIGHTGRAY);
    DrawText("LEAVE", (int)(lv.x + (lv.w - (float)MeasureText("LEAVE", 16)) / 2.0f), (int)lv.y + 7, 16, WHITE);
    if (showKeys) {
        const char *k = leaveArmed ? "PRESS TAB AGAIN TO LEAVE" : "TAB: LEAVE";
        DrawText(k, 790 - MeasureText(k, 12), 44, 12, leaveArmed ? RED : Fade(WHITE, 0.6f));
    }

    HudStamina_Draw(p->stamina / PLAYER_STAMINA_MAX, p->exhausted, STAMINA_EXHAUST_RECOVER / PLAYER_STAMINA_MAX);      // the same always-on bar the single-player HUD draws

#if TOWER_INPUT_TOUCH
    if (Input_Scheme() == INPUT_SCHEME_TOUCH) {
        HudView hv = { 0 };
        hv.alive = p->health.isAlive; hv.sprinting = p->sprinting; hv.sliding = p->sliding; hv.exhausted = p->exhausted;
        hv.stamina01 = p->stamina / PLAYER_STAMINA_MAX;
        hv.slideShown = NetPlay_SlideShown(p); hv.diveShown = NetPlay_DiveShown(p);
        hv.tooTiredToSlide = p->stamina < STAMINA_SLIDE_COST; hv.tooTiredToDive = p->exhausted || p->stamina < STAMINA_DIVE_COST;
        HudControls_Draw(&hv);
    }
#endif
}
