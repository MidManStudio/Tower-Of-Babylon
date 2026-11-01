// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "mdix_web.c"
// ============================================================================
#ifndef MDIX_WEB_H
#define MDIX_WEB_H

#include <stdbool.h>

// The bridge to the DixScript wasm (the module that ships as the npm package), for mdix_doc.c only.
// Documents live in JavaScript; C holds an integer handle. Builds that are not Emscripten get stubs that fail politely.
bool MdixWeb_BootStart(char *err, int cap);   // false when the files cannot be read; otherwise the module compiles in the background
int  MdixWeb_BootState(void);                 // 0 still loading, 1 ready, -1 failed
void MdixWeb_BootError(char *err, int cap);
int  MdixWeb_Load(const char *source, char *err, int cap);    // a handle above 0, or 0
void MdixWeb_Free(int handle);
bool MdixWeb_Has(int handle, const char *path);
int  MdixWeb_Len(int handle, const char *path);               // -1 when not an array
bool MdixWeb_Int(int handle, const char *path, int *out);
bool MdixWeb_Float(int handle, const char *path, double *out);
bool MdixWeb_Bool(int handle, const char *path, bool *out);
bool MdixWeb_Str(int handle, const char *path, char *out, int cap);

#endif
