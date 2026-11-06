// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/characters.md, section "character_select.h and character_select.c"
// ============================================================================
#include "character_select.h"

void CharSelect_Init(CharSelect *s, int count)
{
    s->count = count < 0 ? 0 : count;
    s->focus = 0;
    s->done = false;
}

void CharSelect_Resync(CharSelect *s, int count)
{
    s->count = count < 0 ? 0 : count;
    if (s->focus >= s->count) s->focus = s->count > 0 ? s->count - 1 : 0;
}

void CharSelect_Move(CharSelect *s, int dir)
{
    if (s->done || s->count < 1 || dir == 0) return;
    s->focus = ((s->focus + (dir > 0 ? 1 : -1)) % s->count + s->count) % s->count;
}

void CharSelect_Press(CharSelect *s) { if (!s->done && s->count > 0) s->done = true; }

void CharSelect_Tap(CharSelect *s, float x, float y)
{
    if (s->done || s->count < 1) return;
    for (int i = 0; i < s->count; ++i)
        if (UiRect_Contains(CharSelect_Card(i, s->count), x, y)) { s->focus = i; return; }
    if (UiRect_Contains(CharSelect_Confirm(), x, y)) s->done = true;
}

UiRect CharSelect_Card(int index, int count)
{
    const float gap = 6.0f, left = 8.0f, total = 784.0f;
    const int n = count < 1 ? 1 : count;
    const float w = (total - gap * (float)(n - 1)) / (float)n;
    return (UiRect){ left + (float)index * (w + gap), 44.0f, w, 54.0f };
}

UiRect CharSelect_Panel(void)       { return (UiRect){ 8.0f, 106.0f, 784.0f, 286.0f }; }
UiRect CharSelect_Confirm(void)     { return (UiRect){ 250.0f, 398.0f, 300.0f, 44.0f }; }
UiRect CharSelect_StoryColumn(void) { return (UiRect){ 18.0f, 140.0f, 452.0f, 246.0f }; }
UiRect CharSelect_KitColumn(void)   { return (UiRect){ 494.0f, 140.0f, 290.0f, 246.0f }; }

int CharSelect_Wrap(const char *text, float maxWidth, int fontSize, CharSelectMeasure measure, void *user, CharSelectLine *out, int maxLines, bool *cut)
{
    int n = 0, pos = 0;
    if (cut) *cut = false;
    while (text[pos]) {
        while (text[pos] == ' ') ++pos;
        if (!text[pos]) break;
        if (n == maxLines) { if (cut) *cut = true; break; }
        const int start = pos;
        int end = start, p = start;
        for (;;) {                                           // take words while they fit
            int w = p;
            while (text[w] && text[w] != ' ') ++w;
            if (w == p || measure(text + start, w - start, fontSize, user) > maxWidth) break;
            end = w;
            p = w;
            while (text[p] == ' ') ++p;
            if (!text[p]) break;
        }
        if (end == start) {                                  // one word wider than the line: break it by letters (at least one, so it always moves on)
            end = start + 1;
            while (text[end] && measure(text + start, end + 1 - start, fontSize, user) <= maxWidth) ++end;
        }
        out[n].start = start;
        out[n].len = end - start;
        ++n;
        pos = end;
    }
    return n;
}
