// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "play_ui.h and play_ui.c"
// ============================================================================
#include "play_ui.h"

#include <math.h>
#include <stdio.h>

#include "../dungeon/dungeon_renderer.h"
#include "../dungeon/staircase.h"
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

    const UiRect lv = SessionUI_Leave();
    DrawRectangleRec(R(lv), Fade(BLACK, 0.55f));
    DrawRectangleLines((int)lv.x, (int)lv.y, (int)lv.w, (int)lv.h, leaveArmed ? RED : LIGHTGRAY);
    DrawText("LEAVE", (int)(lv.x + (lv.w - (float)MeasureText("LEAVE", 16)) / 2.0f), (int)lv.y + 7, 16, WHITE);
    if (showKeys) {
        const char *k = leaveArmed ? "PRESS TAB AGAIN TO LEAVE" : "TAB: LEAVE";
        DrawText(k, 790 - MeasureText(k, 12), 44, 12, leaveArmed ? RED : Fade(WHITE, 0.6f));
    }

    ScreenFx_DrawStaminaBar(800 / 2 - 100, 450 - 14, 200, 6, p->stamina / PLAYER_STAMINA_MAX, p->exhausted, STAMINA_EXHAUST_RECOVER / PLAYER_STAMINA_MAX, (float)GetTime());

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
