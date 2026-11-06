// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/characters.md, section "character_select.h and character_select.c"
// ============================================================================
#ifndef CHARACTER_SELECT_H
#define CHARACTER_SELECT_H

#include <stdbool.h>

#include "../input/ui_rect.h"

// The character selection screen, shown once at launch: a row of cards (one per character), the highlighted character's story and kit
// below, and a button to play as them. This file is the screen's logic and layout with no raylib in it, so the tests need only a C
// compiler; character_select_ui.c draws it. Everything is in canvas pixels (800 x 450).

typedef struct {
    int  count;       // characters on offer
    int  focus;       // the highlighted one
    bool done;        // the player chose: play as `focus`
} CharSelect;

void CharSelect_Init(CharSelect *s, int count);
void CharSelect_Resync(CharSelect *s, int count);      // the roster changed under the screen (the web build swaps in the real file): keep the focus on a card that exists
void CharSelect_Move(CharSelect *s, int dir);          // left (-1) or right (+1), wrapping round
void CharSelect_Tap(CharSelect *s, float x, float y);  // a card highlights it; the button chooses it; anywhere else does nothing
void CharSelect_Press(CharSelect *s);                  // the confirm key

// Layout. A card's width follows the count so any roster of up to CHARACTER_MAX fits in a row.
UiRect CharSelect_Card(int index, int count);
UiRect CharSelect_Panel(void);                         // the box holding the highlighted character
UiRect CharSelect_Confirm(void);                       // the PLAY AS button
UiRect CharSelect_StoryColumn(void);                   // the overview and the backstory
UiRect CharSelect_KitColumn(void);                     // the three parts of the kit

// What fits, in lines of CS_FONT_BODY text. The screen cuts a text that is longer than its lines (the loader's length limits keep that
// rare, and tests/characters_headless.c checks that the shipped texts fit).
#define CS_FONT_BODY       12
#define CS_LINE_H          14
#define CS_LABEL_H         16
#define CS_OVERVIEW_LINES  4
#define CS_BACKSTORY_LINES 12
#define CS_KIT_LINES       4
#define CS_BLOCK_GAP       8

// Word wrap by width. measure(text, len, fontSize, user) is the width in pixels of the first len characters of text. Fills at most
// maxLines lines (start and length into text); *cut is set when text was left over. A word wider than the line is broken by letters.
typedef float (*CharSelectMeasure)(const char *text, int len, int fontSize, void *user);
typedef struct { int start, len; } CharSelectLine;
int CharSelect_Wrap(const char *text, float maxWidth, int fontSize, CharSelectMeasure measure, void *user, CharSelectLine *out, int maxLines, bool *cut);

#endif
