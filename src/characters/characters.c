// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/characters.md, section "characters.h and characters.c"
// ============================================================================
#include "characters.h"

#include <stdio.h>
#include <string.h>

static CharacterDef g_roster[CHARACTER_MAX];
static int          g_count = 0;
static char         g_selectedId[CHARACTER_ID_LEN] = "";

int Characters_Count(void) { return g_count; }
const CharacterDef *Characters_Get(int index) { return (index >= 0 && index < g_count) ? &g_roster[index] : NULL; }

int Characters_IndexOfId(const char *id)
{
    if (!id) return -1;
    for (int i = 0; i < g_count; ++i) if (strcmp(g_roster[i].id, id) == 0) return i;
    return -1;
}

// Plain printable ASCII only: the screen's font has nothing else, and the baker holds the strings to the same rule.
static bool PlainAscii(const char *s) { for (; *s; ++s) if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7e) return false; return true; }

static bool IdChars(const char *s)
{
    if (!*s) return false;
    for (; *s; ++s) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) return false;
    return true;
}

static void Fail(char *err, int cap, const char *fmt, const char *path, int n)
{
    if (err && cap > 0) snprintf(err, (size_t)cap, fmt, path, n);
}

// Reads one string of character i into out (which holds cap bytes). False, with err filled in, when it is missing, empty, too long
// for cap, or not plain ASCII.
static bool ReadText(const MdixDoc *doc, int i, const char *key, char *out, int cap, char *err, int errCap)
{
    char path[48];
    snprintf(path, sizeof path, "characters[%d].%s", i, key);
    if (!MdixDoc_Has(doc, path)) { Fail(err, errCap, "%s: missing", path, 0); return false; }
    if (!MdixDoc_Str(doc, path, out, cap)) { Fail(err, errCap, "%s: not text, or longer than %d characters", path, cap - 1); return false; }
    if (out[0] == 0) { Fail(err, errCap, "%s: empty", path, 0); return false; }
    if (!PlainAscii(out)) { Fail(err, errCap, "%s: only plain ASCII text is allowed", path, 0); return false; }
    return true;
}

bool Characters_LoadFrom(const MdixDoc *doc, char *err, int errCap)
{
    if (err && errCap > 0) err[0] = 0;
    if (!doc) { if (err && errCap > 0) snprintf(err, (size_t)errCap, "no document"); return false; }
    const int n = MdixDoc_ArrayLen(doc, "characters");
    if (n < 1 || n > CHARACTER_MAX) {
        if (err && errCap > 0) snprintf(err, (size_t)errCap, "characters: %d entries, it needs 1 to %d", n, CHARACTER_MAX);
        return false;
    }
    static CharacterDef next[CHARACTER_MAX];
    memset(next, 0, sizeof next);
    for (int i = 0; i < n; ++i) {
        CharacterDef *c = &next[i];
        if (!ReadText(doc, i, "id", c->id, CHARACTER_ID_LEN, err, errCap)) return false;
        if (!IdChars(c->id)) { if (err && errCap > 0) snprintf(err, (size_t)errCap, "characters[%d].id: \"%s\" must be lowercase letters, digits and _", i, c->id); return false; }
        for (int j = 0; j < i; ++j)
            if (strcmp(next[j].id, c->id) == 0) { if (err && errCap > 0) snprintf(err, (size_t)errCap, "characters[%d].id: \"%s\" is used twice", i, c->id); return false; }
        if (!ReadText(doc, i, "name", c->name, CHARACTER_NAME_LEN, err, errCap)) return false;
        if (!ReadText(doc, i, "call", c->call, CHARACTER_CALL_LEN, err, errCap)) return false;
        if (!ReadText(doc, i, "role", c->role, CHARACTER_ROLE_LEN, err, errCap)) return false;
        if (!ReadText(doc, i, "overview", c->overview, CHARACTER_OVERVIEW_LEN, err, errCap)) return false;
        if (!ReadText(doc, i, "backstory", c->backstory, CHARACTER_BACKSTORY_LEN, err, errCap)) return false;
        if (!ReadText(doc, i, "passive", c->passive, CHARACTER_KIT_LEN, err, errCap)) return false;
        if (!ReadText(doc, i, "activatable", c->activatable, CHARACTER_KIT_LEN, err, errCap)) return false;
        if (!ReadText(doc, i, "special", c->special, CHARACTER_KIT_LEN, err, errCap)) return false;
    }
    memcpy(g_roster, next, sizeof g_roster);
    g_count = n;
    return true;
}

bool Characters_Select(const char *id)
{
    if (Characters_IndexOfId(id) < 0) return false;
    snprintf(g_selectedId, sizeof g_selectedId, "%s", id);
    return true;
}

int Characters_Selected(void) { return Characters_IndexOfId(g_selectedId); }
const char *Characters_SelectedId(void) { return g_selectedId; }
const char *Characters_SelectedCall(void) { const int i = Characters_Selected(); return i >= 0 ? g_roster[i].call : NULL; }
