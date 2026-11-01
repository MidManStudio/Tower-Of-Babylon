// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "mdix_doc.h and mdix_doc.c"
// ============================================================================
#ifndef MDIX_DOC_H
#define MDIX_DOC_H

#include <stdbool.h>

// One parsed DixScript file, read by path ("weapons[2].damage", "weapons[]" for an array's length).
// There are two sources behind the same calls:
//   * the baked table in config_baked.c: every value of the shipped .mdix files, already evaluated (always there);
//   * the DixScript wasm (web only): parses .mdix text itself, so an edited file takes effect without a rebuild.
// The getters never throw and never return JSON: false means "not there, or not that type".

typedef struct { const char *path; char type; const char *text; } MdixBakedEntry;   // type: 'i' int or enum, 'f' float, 'b' bool, 's' string
typedef struct {
    const char *name;               // the file it came from
    unsigned    sourceHash;         // FNV-1a of that file's bytes when it was baked
    const MdixBakedEntry *entries;  // sorted by path (strcmp), so a lookup is a binary search
    int         count;
} MdixBaked;

typedef struct MdixDoc MdixDoc;

MdixDoc *MdixDoc_FromBaked(const MdixBaked *table);                  // never fails for a real table
MdixDoc *MdixDoc_Parse(const char *source, char *err, int errCap);   // wasm; NULL when the wasm is not loaded or the text does not parse (err says why)
void     MdixDoc_Free(MdixDoc *doc);

bool MdixDoc_Has(const MdixDoc *doc, const char *path);
int  MdixDoc_ArrayLen(const MdixDoc *doc, const char *path);          // -1 when it is not an array
bool MdixDoc_Int(const MdixDoc *doc, const char *path, int *out);     // an int, or an enum's ordinal
bool MdixDoc_Float(const MdixDoc *doc, const char *path, float *out); // a float, or an int widened
bool MdixDoc_Bool(const MdixDoc *doc, const char *path, bool *out);
bool MdixDoc_Str(const MdixDoc *doc, const char *path, char *out, int cap);   // false when it does not fit

unsigned Mdix_Hash(const char *bytes, int len);                       // FNV-1a, 32 bit: the same as tools/mdix_bake.mjs

// The wasm module (web builds). Starting it does not block: the module compiles in the background and the game polls
// Mdix_WasmState() once a frame. On other builds it goes straight to FAILED with a message saying so.
typedef enum { MDIX_WASM_IDLE = 0, MDIX_WASM_LOADING, MDIX_WASM_READY, MDIX_WASM_FAILED } MdixWasmState;
void          Mdix_WasmStart(void);    // once; reads assets/mdix/* and hands them to the browser
MdixWasmState Mdix_WasmState(void);
const char   *Mdix_WasmError(void);    // why it failed, "" otherwise

#endif
