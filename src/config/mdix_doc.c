// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "mdix_doc.h and mdix_doc.c"
// ============================================================================
#include "mdix_doc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdix_web.h"

struct MdixDoc {
    const MdixBaked *baked;   // set: reads the baked table
    int              handle;  // otherwise: a document inside the wasm
};

unsigned Mdix_Hash(const char *bytes, int len)
{
    unsigned h = 0x811c9dc5u;
    for (int i = 0; i < len; ++i) { h ^= (unsigned char)bytes[i]; h *= 0x01000193u; }
    return h;
}

static const MdixBakedEntry *Find(const MdixBaked *b, const char *path)
{
    int lo = 0, hi = b->count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = strcmp(path, b->entries[mid].path);
        if (c == 0) return &b->entries[mid];
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return NULL;
}

MdixDoc *MdixDoc_FromBaked(const MdixBaked *table)
{
    if (!table) return NULL;
    MdixDoc *d = (MdixDoc *)calloc(1, sizeof *d);
    if (d) d->baked = table;
    return d;
}

MdixDoc *MdixDoc_Parse(const char *source, char *err, int errCap)
{
    const int h = MdixWeb_Load(source, err, errCap);
    if (h <= 0) return NULL;
    MdixDoc *d = (MdixDoc *)calloc(1, sizeof *d);
    if (!d) { MdixWeb_Free(h); return NULL; }
    d->handle = h;
    return d;
}

void MdixDoc_Free(MdixDoc *doc)
{
    if (!doc) return;
    if (!doc->baked) MdixWeb_Free(doc->handle);
    free(doc);
}

bool MdixDoc_Has(const MdixDoc *doc, const char *path)
{
    if (!doc || !path) return false;
    return doc->baked ? Find(doc->baked, path) != NULL : MdixWeb_Has(doc->handle, path);
}

int MdixDoc_ArrayLen(const MdixDoc *doc, const char *path)
{
    if (!doc || !path) return -1;
    if (!doc->baked) return MdixWeb_Len(doc->handle, path);
    char key[96];
    snprintf(key, sizeof key, "%s[]", path);
    const MdixBakedEntry *e = Find(doc->baked, key);
    return e ? atoi(e->text) : -1;
}

bool MdixDoc_Int(const MdixDoc *doc, const char *path, int *out)
{
    if (!doc || !path || !out) return false;
    if (!doc->baked) return MdixWeb_Int(doc->handle, path, out);
    const MdixBakedEntry *e = Find(doc->baked, path);
    if (!e || e->type != 'i') return false;
    *out = (int)strtol(e->text, NULL, 10);
    return true;
}

bool MdixDoc_Float(const MdixDoc *doc, const char *path, float *out)
{
    if (!doc || !path || !out) return false;
    if (!doc->baked) {
        double v;
        if (!MdixWeb_Float(doc->handle, path, &v)) return false;
        *out = (float)v;
        return true;
    }
    const MdixBakedEntry *e = Find(doc->baked, path);
    if (!e || (e->type != 'f' && e->type != 'i')) return false;
    *out = (float)strtod(e->text, NULL);
    return true;
}

bool MdixDoc_Bool(const MdixDoc *doc, const char *path, bool *out)
{
    if (!doc || !path || !out) return false;
    if (!doc->baked) return MdixWeb_Bool(doc->handle, path, out);
    const MdixBakedEntry *e = Find(doc->baked, path);
    if (!e || e->type != 'b') return false;
    *out = strcmp(e->text, "true") == 0;
    return true;
}

bool MdixDoc_Str(const MdixDoc *doc, const char *path, char *out, int cap)
{
    if (!doc || !path || !out || cap < 1) return false;
    if (!doc->baked) return MdixWeb_Str(doc->handle, path, out, cap);
    const MdixBakedEntry *e = Find(doc->baked, path);
    if (!e || e->type != 's' || (int)strlen(e->text) >= cap) return false;
    strcpy(out, e->text);
    return true;
}

// ---- the wasm module ---------------------------------------------------------------------------------------------
static MdixWasmState g_wasmState = MDIX_WASM_IDLE;
static char g_wasmErr[160] = "";

void Mdix_WasmStart(void)
{
    if (g_wasmState != MDIX_WASM_IDLE) return;
    g_wasmState = MdixWeb_BootStart(g_wasmErr, (int)sizeof g_wasmErr) ? MDIX_WASM_LOADING : MDIX_WASM_FAILED;
}

MdixWasmState Mdix_WasmState(void)
{
    if (g_wasmState == MDIX_WASM_LOADING) {
        const int s = MdixWeb_BootState();
        if (s > 0) g_wasmState = MDIX_WASM_READY;
        else if (s < 0) { g_wasmState = MDIX_WASM_FAILED; MdixWeb_BootError(g_wasmErr, (int)sizeof g_wasmErr); }
    }
    return g_wasmState;
}

const char *Mdix_WasmError(void) { return g_wasmErr; }
