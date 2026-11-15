// Headless test of the character roster and the selection screen's logic: the shipped assets/config/characters.mdix, the loader's
// refusals, the choice kept as an id, the screen's navigation and taps, the layout, the word wrap, and that every shipped text fits
// the room the screen gives it. No window, no raylib calls. Run from the project root:
//   cc -O2 -std=c99 -DCHARACTERS_HEADLESS_TEST -Isrc -Iraylib/src tests/characters_headless.c src/characters/characters.c src/characters/character_select.c src/config/mdix_doc.c src/config/mdix_web.c src/config/config_baked.c -lm -o characters_headless
#ifdef CHARACTERS_HEADLESS_TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "characters/character_select.h"
#include "characters/characters.h"

extern const MdixBaked g_bakedCharacters;

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

// ---- a copy of the shipped table that a test can break -------------------------------------------------------------
#define MAXE 128
static MdixBakedEntry g_e[MAXE];
static int g_n;
static void Reset(void) { g_n = g_bakedCharacters.count; memcpy(g_e, g_bakedCharacters.entries, sizeof(MdixBakedEntry) * (size_t)g_n); }
static MdixBakedEntry *Find(const char *path) { for (int i = 0; i < g_n; ++i) if (strcmp(g_e[i].path, path) == 0) return &g_e[i]; return NULL; }
static void Drop(const char *path) { for (int i = 0; i < g_n; ++i) if (strcmp(g_e[i].path, path) == 0) { memmove(&g_e[i], &g_e[i + 1], sizeof(MdixBakedEntry) * (size_t)(g_n - i - 1)); --g_n; return; } }
static int Try(char *err, int cap)                         // loads the (broken) copy
{
    MdixBaked t = { "test.mdix", 0u, g_e, g_n };
    MdixDoc *d = MdixDoc_FromBaked(&t);
    const int ok = Characters_LoadFrom(d, err, cap);
    MdixDoc_Free(d);
    return ok;
}

// ---- measuring ---------------------------------------------------------------------------------------------------
static float Mono(const char *t, int len, int size, void *u) { (void)t; (void)size; (void)u; return (float)len * 6.0f; }
static float Wide(const char *t, int len, int size, void *u) { (void)t; (void)u; return (float)len * 0.65f * (float)size; }   // wider than raylib's default font on average

static int Lines(const char *text, float width, CharSelectMeasure m, int maxLines, bool *cut)
{
    CharSelectLine l[32];
    return CharSelect_Wrap(text, width, CS_FONT_BODY, m, NULL, l, maxLines, cut);
}

int main(void)
{
    char err[240];

    printf("assets/config/characters.mdix\n");
    {
        Reset();
        MdixDoc *doc = MdixDoc_FromBaked(&g_bakedCharacters);
        const int ok = Characters_LoadFrom(doc, err, sizeof err);
        MdixDoc_Free(doc);
        Check(ok, "the shipped file loads");
        Check(Characters_Count() == 4, "four characters");
        const char *ids[4] = { "steve", "brou", "khan", "lele" };
        const char *calls[4] = { "Steve", "Brou", "Khan", "Lele" };
        const char *names[4] = { "Sinless Steve", "Brouugenxxxgerrrrr", "Totour Khan", "Elelelelele" };
        int order = 1;
        for (int i = 0; i < 4; ++i) {
            const CharacterDef *c = Characters_Get(i);
            if (!c || strcmp(c->id, ids[i]) || strcmp(c->call, calls[i]) || strcmp(c->name, names[i])) order = 0;
        }
        Check(order, "in the order of the design document: Steve, Brou, Khan, Lele, with their full names");
        int complete = 1;
        for (int i = 0; i < 4; ++i) {
            const CharacterDef *c = Characters_Get(i);
            if (!c->role[0] || !c->overview[0] || !c->backstory[0] || !c->passive[0] || !c->activatable[0] || !c->special[0]) complete = 0;
        }
        Check(complete, "every character has a role, an overview, a backstory and the three parts of the kit");
        Check(Characters_Get(-1) == NULL && Characters_Get(4) == NULL, "an index out of range gives NULL");
        Check(Characters_IndexOfId("khan") == 2 && Characters_IndexOfId("nobody") == -1 && Characters_IndexOfId(NULL) == -1, "ids are looked up; an unknown one is -1");
        Check(sizeof(CharacterDef) == (size_t)(CHARACTER_ID_LEN + CHARACTER_NAME_LEN + CHARACTER_CALL_LEN + CHARACTER_ROLE_LEN + CHARACTER_OVERVIEW_LEN +
              CHARACTER_BACKSTORY_LEN + 3 * CHARACTER_KIT_LEN), "a character is words only: no numbers (health, speed) are decided, so none are stored");
    }

    printf("\nthe loader refuses a roster that is wrong, names where, and keeps the last good one\n");
    {
        static CharacterDef snap[4];
        for (int i = 0; i < 4; ++i) snap[i] = *Characters_Get(i);
        Reset(); Check(Try(err, sizeof err), "an unbroken copy loads (the baseline of the next checks)");
        Reset(); Drop("characters[2].backstory");
        Check(!Try(err, sizeof err) && strstr(err, "characters[2].backstory") && strstr(err, "missing"), "a missing field is refused and named");
        Reset(); Find("characters[1].role")->text = "";
        Check(!Try(err, sizeof err) && strstr(err, "characters[1].role") && strstr(err, "empty"), "an empty text is refused");
        static char longText[300];
        memset(longText, 'x', sizeof longText - 1); longText[sizeof longText - 1] = 0;
        Reset(); Find("characters[0].overview")->text = longText;
        Check(!Try(err, sizeof err) && strstr(err, "characters[0].overview"), "an overview longer than its limit is refused");
        Reset(); Find("characters[3].special")->text = longText;
        Check(!Try(err, sizeof err) && strstr(err, "characters[3].special"), "a kit line longer than its limit is refused");
        Reset(); Find("characters[1].call")->text = "caf\xc3\xa9";
        Check(!Try(err, sizeof err) && strstr(err, "characters[1].call") && strstr(err, "ASCII"), "text that is not plain ASCII is refused");
        Reset(); Find("characters[1].id")->text = "steve";
        Check(!Try(err, sizeof err) && strstr(err, "characters[1].id") && strstr(err, "twice"), "two characters with one id are refused");
        Reset(); Find("characters[2].id")->text = "Khan Two";
        Check(!Try(err, sizeof err) && strstr(err, "characters[2].id"), "an id with capitals or a space is refused");
        Reset(); Find("characters[]")->text = "0";
        Check(!Try(err, sizeof err) && strstr(err, "1 to 8"), "no characters at all is refused");
        Reset(); Find("characters[]")->text = "9";
        Check(!Try(err, sizeof err) && strstr(err, "1 to 8"), "more than CHARACTER_MAX is refused");
        Reset(); Find("characters[0].passive")->type = 'f'; Find("characters[0].passive")->text = "1.5";
        Check(!Try(err, sizeof err) && strstr(err, "characters[0].passive"), "a number where text belongs is refused");
        Check(!Characters_LoadFrom(NULL, err, sizeof err), "no document is refused");
        int same = Characters_Count() == 4;
        for (int i = 0; i < 4 && same; ++i) if (memcmp(&snap[i], Characters_Get(i), sizeof snap[i]) != 0) same = 0;
        Check(same, "after all those refusals the roster in use is, byte for byte, the one from before (a refused load changes nothing, not even part of it)");
        Reset(); Find("characters[]")->text = "2";                    // a valid roster of two: the first two entries
        Check(Try(err, sizeof err) && Characters_Count() == 2, "a shorter valid roster is taken");
        Reset(); Try(err, sizeof err);                                // back to the shipped four
    }

    printf("\nthe choice is kept as an id\n");
    {
        Check(Characters_Selected() == -1 && Characters_SelectedCall() == NULL && Characters_SelectedId()[0] == 0, "nothing is chosen at first");
        Check(!Characters_Select("nobody") && Characters_Selected() == -1, "an id that is not in the roster is refused");
        Check(Characters_Select("lele") && Characters_Selected() == 3 && strcmp(Characters_SelectedId(), "lele") == 0 && strcmp(Characters_SelectedCall(), "Lele") == 0, "choosing Lele");
        Reset();                                                      // reload the roster with Lele's entry removed from the end and Steve's id taken off the front: order changes under the choice
        Find("characters[]")->text = "3";
        Check(Try(err, sizeof err) && Characters_Selected() == -1 && strcmp(Characters_SelectedId(), "lele") == 0, "a roster without the chosen character: the id is kept, the index says none");
        Reset(); Try(err, sizeof err);
        Check(Characters_Selected() == 3, "and it is found again when the character is back");
    }

    printf("\nthe screen: choosing\n");
    {
        CharSelect s; CharSelect_Init(&s, 4);
        Check(s.focus == 0 && !s.done && s.count == 4, "it starts on the first character, not chosen");
        CharSelect_Move(&s, 1); CharSelect_Move(&s, 1);
        Check(s.focus == 2, "right twice: the third");
        CharSelect_Move(&s, -1);
        Check(s.focus == 1, "left: the second");
        CharSelect_Init(&s, 4); CharSelect_Move(&s, -1);
        Check(s.focus == 3, "left from the first wraps to the last");
        CharSelect_Move(&s, 1);
        Check(s.focus == 0, "right from the last wraps to the first");
        CharSelect_Move(&s, 0);
        Check(s.focus == 0, "no step does nothing");
        CharSelect_Init(&s, 1); CharSelect_Move(&s, 1); CharSelect_Move(&s, -1);
        Check(s.focus == 0, "with one character the focus stays");

        CharSelect_Init(&s, 4);
        const UiRect c2 = CharSelect_Card(2, 4);
        CharSelect_Tap(&s, c2.x + c2.w * 0.5f, c2.y + c2.h * 0.5f);
        Check(s.focus == 2 && !s.done, "a tap on a card highlights it and does not choose it");
        CharSelect_Tap(&s, 400.0f, 70.0f - 60.0f);
        Check(s.focus == 2 && !s.done, "a tap on nothing does nothing");
        CharSelect_Tap(&s, 1.0f, 1.0f);
        Check(s.focus == 2 && !s.done, "a tap in the corner does nothing");
        const UiRect b = CharSelect_Confirm();
        CharSelect_Tap(&s, b.x + b.w * 0.5f, b.y + b.h * 0.5f);
        Check(s.done && s.focus == 2, "a tap on the button chooses the highlighted character");
        CharSelect_Move(&s, 1);
        CharSelect_Tap(&s, CharSelect_Card(0, 4).x + 5.0f, CharSelect_Card(0, 4).y + 5.0f);
        Check(s.done && s.focus == 2, "once chosen, nothing moves it");

        CharSelect_Init(&s, 4); CharSelect_Move(&s, 1); CharSelect_Press(&s);
        Check(s.done && s.focus == 1, "the confirm key chooses the highlighted character");
        CharSelect_Init(&s, 0); CharSelect_Press(&s); CharSelect_Move(&s, 1);
        Check(!s.done, "with no characters there is nothing to choose");
        CharSelect_Init(&s, 4); CharSelect_Move(&s, -1);              // on the last of four...
        CharSelect_Resync(&s, 2);
        Check(s.focus == 1 && s.count == 2, "the roster shrinks under the screen (a file swapped in): the focus moves onto a card that exists");
        CharSelect_Resync(&s, 4);
        Check(s.focus == 1 && s.count == 4, "and growing again leaves it where it is");
    }

    printf("\nthe screen: leaving and opening again\n");
    {
        CharSelect s; CharSelect_Init(&s, 4);
        const UiRect bk = CharSelect_Back(), cf = CharSelect_Confirm();
        const float bx = bk.x + bk.w * 0.5f, by = bk.y + bk.h * 0.5f;
        Check(!s.back, "it starts with nobody having left");
        CharSelect_Tap(&s, bx, by);
        Check(s.back && !s.done, "a tap on BACK leaves without choosing");
        CharSelect_Move(&s, 1); CharSelect_Press(&s);
        CharSelect_Tap(&s, CharSelect_Card(1, 4).x + 5.0f, CharSelect_Card(1, 4).y + 5.0f);
        CharSelect_Tap(&s, cf.x + 5.0f, cf.y + 5.0f);
        Check(s.back && !s.done && s.focus == 0, "once left, nothing moves, highlights or chooses");
        CharSelect_Init(&s, 4); CharSelect_Move(&s, 1); CharSelect_Press(&s); CharSelect_Tap(&s, bx, by);
        Check(s.done && !s.back && s.focus == 1, "once chosen, BACK does nothing");
        CharSelect_Init(&s, 0); CharSelect_Tap(&s, bx, by);
        Check(s.back, "BACK works with no characters");
        CharSelect_Init(&s, 4); CharSelect_Tap(&s, 1.0f, 1.0f); CharSelect_Tap(&s, bk.x - 2.0f, by); CharSelect_Tap(&s, bx, bk.y + bk.h + 1.0f);
        Check(!s.back, "a tap just outside BACK is not BACK");

        CharSelect_Open(&s, 4, 2);
        Check(s.focus == 2 && !s.done && !s.back && s.count == 4, "opened again: the highlight on the chosen character, nothing chosen, nobody left");
        s.done = true; s.back = true;
        CharSelect_Open(&s, 3, -1);
        Check(s.focus == 0 && !s.done && !s.back && s.count == 3, "opened with nobody chosen (-1): the first card, and a screen that was done or left is fresh again");
        CharSelect_Open(&s, 3, 3);
        Check(s.focus == 0, "opened with a focus past the roster: the first card");
        CharSelect_Open(&s, 0, 0);
        Check(s.count == 0 && s.focus == 0 && !s.done, "opened with no characters");
    }

    printf("\nthe layout\n");
    {
        int fits = 1, apart = 1, clear = 1;
        for (int n = 1; n <= CHARACTER_MAX; ++n)
            for (int i = 0; i < n; ++i) {
                const UiRect r = CharSelect_Card(i, n), p = CharSelect_Panel();
                if (r.x < 0.0f || r.x + r.w > 800.0f || r.y < 40.0f) fits = 0;
                if (r.y + r.h > p.y) clear = 0;
                if (i > 0) { const UiRect q = CharSelect_Card(i - 1, n); if (UiRect_Overlaps(r, q)) apart = 0; }
            }
        Check(fits, "the cards, for any roster of 1 to 8, are on the screen and below the title");
        Check(apart, "and never overlap one another");
        Check(clear, "and end above the character's box");
        const UiRect p = CharSelect_Panel(), b = CharSelect_Confirm(), sc = CharSelect_StoryColumn(), kc = CharSelect_KitColumn();
        Check(p.x >= 0 && p.x + p.w <= 800 && p.y + p.h <= 450 && b.x >= 0 && b.x + b.w <= 800 && b.y + b.h <= 450, "the box and the button are on the 800 by 450 screen");
        Check(!UiRect_Overlaps(b, p), "the button is clear of the box");
        Check(b.w >= 120.0f && b.h >= 40.0f, "the button is big enough for a thumb");
        const UiRect bk = CharSelect_Back();
        int clearBack = 1;
        for (int n = 1; n <= CHARACTER_MAX; ++n) for (int i = 0; i < n; ++i) if (UiRect_Overlaps(CharSelect_Card(i, n), bk)) clearBack = 0;
        Check(bk.x >= 0 && bk.x + bk.w <= 800 && bk.y >= 0 && bk.y + bk.h <= 450, "BACK is on the 800 by 450 screen");
        Check(bk.w >= 80.0f && bk.h >= 28.0f, "BACK is big enough for a thumb");
        Check(clearBack && !UiRect_Overlaps(bk, p) && !UiRect_Overlaps(bk, b), "BACK is clear of every card (any roster of 1 to 8), the box and the button");
        Check(sc.x >= p.x && sc.y >= p.y && sc.x + sc.w <= p.x + p.w && sc.y + sc.h <= p.y + p.h && kc.x >= p.x && kc.y >= p.y && kc.x + kc.w <= p.x + p.w && kc.y + kc.h <= p.y + p.h, "both text columns are inside the box");
        Check(!UiRect_Overlaps(sc, kc), "and apart");
        Check((float)(CS_OVERVIEW_LINES * CS_LINE_H + 6 + CS_LABEL_H + CS_BACKSTORY_LINES * CS_LINE_H) <= sc.h, "the story column has room for its overview and backstory lines");
        Check((float)(3 * (CS_LABEL_H + CS_KIT_LINES * CS_LINE_H) + 2 * CS_BLOCK_GAP) <= kc.h, "the kit column has room for its three blocks");
    }

    printf("\nword wrap\n");
    {
        CharSelectLine l[8]; bool cut;
        int n = CharSelect_Wrap("one two three four", 60.0f, CS_FONT_BODY, Mono, NULL, l, 8, &cut);        // 10 chars a line
        Check(n == 2 && !cut && l[0].len == 7 && l[1].len == 10 && strncmp("one two three four" + l[1].start, "three four", 10) == 0, "it breaks between words: 'one two' / 'three four'");
        n = CharSelect_Wrap("", 60.0f, CS_FONT_BODY, Mono, NULL, l, 8, &cut);
        Check(n == 0 && !cut, "an empty text has no lines");
        n = CharSelect_Wrap("   one   two  ", 600.0f, CS_FONT_BODY, Mono, NULL, l, 8, &cut);
        Check(n == 1 && l[0].start == 3 && l[0].len == 9, "spaces at the ends are dropped, spaces inside stay ('one   two')");
        n = CharSelect_Wrap("abcdefghijklmnopqrstuvwxyz", 60.0f, CS_FONT_BODY, Mono, NULL, l, 8, &cut);
        Check(n == 3 && l[0].len == 10 && l[1].len == 10 && l[2].len == 6, "a word wider than the line is broken by letters, 10 + 10 + 6");
        n = CharSelect_Wrap("aaa bbb ccc ddd eee fff", 24.0f, CS_FONT_BODY, Mono, NULL, l, 2, &cut);
        Check(n == 2 && cut, "when the lines run out, the lines it has are kept and it says text was cut");
        n = CharSelect_Wrap("aaa bbb", 24.0f, CS_FONT_BODY, Mono, NULL, l, 2, &cut);
        Check(n == 2 && !cut, "and a text that exactly fills them is not cut");
        n = CharSelect_Wrap("abc", 3.0f, CS_FONT_BODY, Mono, NULL, l, 8, &cut);                            // not even one letter fits
        Check(n == 3, "a width too small for any letter still moves on, one letter a line (no endless loop)");
    }

    printf("\nthe shipped texts fit the screen (measured with letters wider than the real font)\n");
    {
        const UiRect sc = CharSelect_StoryColumn(), kc = CharSelect_KitColumn();
        int story = 1, kit = 1, cards = 1;
        for (int i = 0; i < Characters_Count(); ++i) {
            const CharacterDef *c = Characters_Get(i);
            bool cut;
            Lines(c->overview, sc.w, Wide, CS_OVERVIEW_LINES, &cut);       if (cut) { story = 0; printf("    %s: the overview does not fit\n", c->id); }
            Lines(c->backstory, sc.w, Wide, CS_BACKSTORY_LINES, &cut);     if (cut) { story = 0; printf("    %s: the backstory does not fit\n", c->id); }
            Lines(c->passive, kc.w, Wide, CS_KIT_LINES, &cut);             if (cut) { kit = 0; printf("    %s: the passive does not fit\n", c->id); }
            Lines(c->activatable, kc.w, Wide, CS_KIT_LINES, &cut);         if (cut) { kit = 0; printf("    %s: the activatable does not fit\n", c->id); }
            Lines(c->special, kc.w, Wide, CS_KIT_LINES, &cut);             if (cut) { kit = 0; printf("    %s: the special does not fit\n", c->id); }
            const UiRect card = CharSelect_Card(i, Characters_Count());
            if (Wide(c->call, (int)strlen(c->call), 20, NULL) > card.w - 8.0f || Wide(c->role, (int)strlen(c->role), 11, NULL) > card.w - 8.0f) { cards = 0; printf("    %s: the card is too narrow\n", c->id); }
        }
        Check(story, "every overview and backstory fits its lines");
        Check(kit, "every kit line fits its lines");
        Check(cards, "every short name and role fits its card");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
