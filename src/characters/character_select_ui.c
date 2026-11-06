// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/characters.md, section "character_select_ui.h and character_select_ui.c"
// ============================================================================
#include "character_select_ui.h"

#include <string.h>

#include "raylib.h"

#include "characters.h"

static Rectangle R(UiRect r) { return (Rectangle){ r.x, r.y, r.w, r.h }; }

static float MeasureRaylib(const char *text, int len, int fontSize, void *user)
{
    (void)user;
    char buf[CHARACTER_BACKSTORY_LEN + 8];
    if (len > (int)sizeof buf - 1) len = (int)sizeof buf - 1;
    memcpy(buf, text, (size_t)len);
    buf[len] = 0;
    return (float)MeasureText(buf, fontSize);
}

// Draws text wrapped into at most maxLines lines from (x, y); returns the height used. A text that does not fit ends in "...".
static int DrawWrapped(const char *text, int x, int y, float width, int maxLines, Color color)
{
    CharSelectLine lines[16];
    bool cut = false;
    const int n = CharSelect_Wrap(text, width, CS_FONT_BODY, MeasureRaylib, NULL, lines, maxLines < 16 ? maxLines : 16, &cut);
    for (int i = 0; i < n; ++i) {
        char buf[CHARACTER_BACKSTORY_LEN + 8];
        int len = lines[i].len;
        if (len > (int)sizeof buf - 8) len = (int)sizeof buf - 8;
        memcpy(buf, text + lines[i].start, (size_t)len);
        buf[len] = 0;
        if (cut && i == n - 1) { if (len > 3) len -= 3; memcpy(buf + len, "...", 4); }
        DrawText(buf, x, y + i * CS_LINE_H, CS_FONT_BODY, color);
    }
    return n * CS_LINE_H;
}

static void DrawKit(const char *label, const char *text, int x, int y, float width)
{
    DrawText(label, x, y, 11, (Color){ 230, 170, 60, 255 });
    DrawWrapped(text, x, y + CS_LABEL_H - 2, width, CS_KIT_LINES, (Color){ 220, 220, 225, 255 });
}

void CharSelectUI_Draw(const CharSelect *s, bool showKeys)
{
    ClearBackground((Color){ 10, 8, 12, 255 });
    DrawText("CHOOSE YOUR CONDEMNED", 10, 12, 22, YELLOW);
    if (s->count < 1) { DrawText("NO CHARACTERS", 10, 60, 16, RED); return; }

    for (int i = 0; i < s->count; ++i) {
        const CharacterDef *c = Characters_Get(i);
        if (!c) continue;
        const UiRect r = CharSelect_Card(i, s->count);
        const bool on = i == s->focus;
        DrawRectangleRec(R(r), on ? (Color){ 70, 24, 24, 255 } : (Color){ 24, 22, 28, 255 });
        DrawRectangleLines((int)r.x, (int)r.y, (int)r.w, (int)r.h, on ? YELLOW : (Color){ 90, 90, 100, 255 });
        const int nameSize = 20;
        DrawText(c->call, (int)(r.x + (r.w - (float)MeasureText(c->call, nameSize)) / 2.0f), (int)r.y + 8, nameSize, on ? WHITE : (Color){ 190, 190, 195, 255 });
        DrawText(c->role, (int)(r.x + (r.w - (float)MeasureText(c->role, 11)) / 2.0f), (int)r.y + 34, 11, on ? (Color){ 230, 170, 60, 255 } : (Color){ 130, 130, 140, 255 });
    }

    const CharacterDef *c = Characters_Get(s->focus);
    if (c) {
        const UiRect p = CharSelect_Panel(), sc = CharSelect_StoryColumn(), kc = CharSelect_KitColumn();
        DrawRectangleRec(R(p), Fade(BLACK, 0.7f));
        DrawRectangleLines((int)p.x, (int)p.y, (int)p.w, (int)p.h, (Color){ 90, 90, 100, 255 });
        DrawText(c->name, (int)p.x + 10, (int)p.y + 8, 20, WHITE);
        DrawText(c->role, (int)(p.x + p.w) - 10 - MeasureText(c->role, 14), (int)p.y + 12, 14, (Color){ 230, 170, 60, 255 });

        DrawWrapped(c->overview, (int)sc.x, (int)sc.y, sc.w, CS_OVERVIEW_LINES, (Color){ 235, 235, 240, 255 });
        const int by = (int)sc.y + CS_OVERVIEW_LINES * CS_LINE_H + 6;
        DrawText("BACKSTORY", (int)sc.x, by, 11, (Color){ 170, 170, 180, 255 });
        DrawWrapped(c->backstory, (int)sc.x, by + CS_LABEL_H - 2, sc.w, CS_BACKSTORY_LINES, (Color){ 190, 190, 198, 255 });

        const int step = CS_LABEL_H + CS_KIT_LINES * CS_LINE_H + CS_BLOCK_GAP;
        DrawKit("PASSIVE", c->passive, (int)kc.x, (int)kc.y, kc.w);
        DrawKit("ACTIVATABLE", c->activatable, (int)kc.x, (int)kc.y + step, kc.w);
        DrawKit("SPECIAL", c->special, (int)kc.x, (int)kc.y + 2 * step, kc.w);
    }

    const UiRect b = CharSelect_Confirm();
    DrawRectangleRec(R(b), (Color){ 20, 90, 40, 255 });
    DrawRectangleLines((int)b.x, (int)b.y, (int)b.w, (int)b.h, WHITE);
    const char *label = c ? TextFormat("PLAY AS %s", c->call) : "PLAY";
    DrawText(label, (int)(b.x + (b.w - (float)MeasureText(label, 22)) / 2.0f), (int)b.y + 11, 22, WHITE);
    if (showKeys) DrawText("A / D or arrows: choose     ENTER: play", 10, 428, 11, (Color){ 130, 130, 140, 255 });
}
