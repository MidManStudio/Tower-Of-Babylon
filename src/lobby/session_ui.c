// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/lobby.md, section "session_ui.h and session_ui.c"
// ============================================================================
#include "session_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

#include "../net/net_discovery.h"

static const Color COL_BG    = { 10, 8, 12, 255 };
static const Color COL_ACCENT = { 230, 170, 60, 255 };
static const Color COL_DIM   = { 130, 130, 140, 255 };
static const Color COL_TEXT  = { 220, 220, 225, 255 };
static const Color COL_FILL  = { 40, 36, 48, 255 };
static const Color COL_EDGE  = { 150, 150, 160, 255 };
static const Color COL_ROW   = { 24, 22, 28, 255 };
static const Color COL_EDGE_OFF = { 55, 55, 62, 255 };

static Rectangle R(UiRect r) { return (Rectangle){ r.x, r.y, r.w, r.h }; }

// The addresses this device can be reached at, best first, refreshed every couple of seconds (reading the interfaces is a system call).
static int Addresses(char out[3][16])
{
    static char cache[3][16];
    static int count = 0;
    static double at = -10.0;
    const double now = GetTime();
    if (now - at > 2.0) {
        at = now;
        NetDiscIface ifs[16];
        uint32_t ips[16];
        const int n = NetDisc_RankHostAddresses(ifs, NetDisc_ListInterfaces(ifs, 16), ips, 16);
        count = n < 3 ? n : 3;
        for (int i = 0; i < count; ++i) NetDisc_FormatIp(ips[i], cache[i]);
    }
    for (int i = 0; i < count; ++i) memcpy(out[i], cache[i], 16);
    return count;
}

static void Upper(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; ++i) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = 0;
}

void SessionUI_Draw(const NetSession *s, bool showKeys)
{
    ClearBackground(COL_BG);
    DrawText(TextFormat("%s: %s", s->hosting ? "HOSTING" : "JOINED", s->name), 10, 12, 22, YELLOW);
    const UiRect lv = SessionUI_Leave();
    DrawRectangleRec(R(lv), COL_FILL);
    DrawRectangleLines((int)lv.x, (int)lv.y, (int)lv.w, (int)lv.h, COL_EDGE);
    if (showKeys) DrawRectangleLinesEx(R(lv), 3.0f, YELLOW);
    DrawText("LEAVE", (int)(lv.x + (lv.w - (float)MeasureText("LEAVE", 16)) / 2.0f), (int)lv.y + 7, 16, WHITE);

    NetSessionSlot slots[NET_MAX_PLAYERS];
    const int n = NetSession_Slots(s, slots);
    if (s->state == NETS_CONNECTING) DrawText("CONNECTING...", 10, 56, 26, COL_ACCENT);
    else DrawText(TextFormat("PLAYERS  %d / %d", n, NET_MAX_PLAYERS), 10, 56, 26, WHITE);

    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        const Rectangle r = { 10.0f, 98.0f + (float)i * 52.0f, 520.0f, 46.0f };
        const bool used = i < n;
        DrawRectangleRec(r, COL_ROW);
        DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, used && slots[i].you ? YELLOW : COL_EDGE_OFF);
        if (!used) { DrawText("WAITING FOR A PLAYER...", (int)r.x + 12, (int)r.y + 14, 16, COL_EDGE_OFF); continue; }
        DrawText(TextFormat("PLAYER %d", slots[i].id + 1), (int)r.x + 12, (int)r.y + 6, 20, WHITE);
        char tags[48] = "";
        if (slots[i].host) strcat(tags, "HOST");
        if (slots[i].you) { if (tags[0]) strcat(tags, "   "); strcat(tags, "YOU"); }
        DrawText(tags, (int)r.x + 12, (int)r.y + 28, 12, COL_ACCENT);
        if (slots[i].character[0]) {
            char who[NET_CHARACTER_LEN];
            Upper(who, sizeof who, slots[i].character);
            DrawText(who, (int)(r.x + r.width) - 12 - MeasureText(who, 16), (int)r.y + 14, 16, COL_TEXT);
        }
    }

    // The right-hand column: how to reach the game, and how the connection is doing.
    int y = 56;
    if (s->hosting && s->host.open) {
        DrawText("OTHERS CAN JOIN AT", 548, y, 12, COL_DIM); y += 18;
        char addrs[3][16];
        const int na = Addresses(addrs);
        for (int i = 0; i < na; ++i) { DrawText(TextFormat("%s:%u", addrs[i], (unsigned)s->host.port), 548, y, 16, WHITE); y += 22; }
        if (na == 0) { DrawText("NO NETWORK FOUND", 548, y, 16, COL_ACCENT); y += 22; }
        DrawText("or from FIND GAMES", 548, y, 12, COL_DIM); y += 28;
    }
    DrawText("THE CONNECTION", 548, y, 12, COL_DIM); y += 18;
    const NetClient *c = &s->client;
    DrawText(TextFormat("PING  %d MS", (int)c->rttMs), 548, y, 14, COL_TEXT); y += 18;
    DrawText(TextFormat("SNAPSHOTS  %u", c->snapshotsSeen), 548, y, 14, COL_TEXT); y += 18;
    DrawText(TextFormat("CORRECTIONS  %u", c->corrections), 548, y, 14, COL_TEXT); y += 18;
    if (s->hosting && s->host.open) DrawText(TextFormat("SERVER TICK  %u", s->host.server.tick), 548, y, 14, COL_TEXT);

    DrawText("The walk starts as soon as the game answers.", 10, 330, 12, COL_DIM);
    if (showKeys) DrawText("ENTER: leave", 10, 428, 11, COL_DIM);
}
