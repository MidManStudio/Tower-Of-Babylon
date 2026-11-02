// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "mdix_web.c"
// ============================================================================
#include "mdix_web.h"
#include "../platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TOWER_MDIX_WASM_BRIDGE

#include <emscripten.h>

// Starts the DixScript wasm. The two files arrive as bytes in the C heap; JavaScript copies them at once, so the caller
// frees them as soon as this returns. The glue is loaded as an ES module from a Blob URL (the browser would otherwise
// need the file served at a URL of its own) and the wasm is instantiated by hand with the glue as its one import: the
// same steps a bundler performs for the npm package. `import()` is written inside new Function so the Emscripten
// toolchain's JavaScript parser never has to read it.
EM_JS(void, mdix_js_boot, (const unsigned char *glue, int glueLen, const unsigned char *wasm, int wasmLen), {
    const g = HEAPU8.slice(glue, glue + glueLen);
    const w = HEAPU8.slice(wasm, wasm + wasmLen);
    const m = Module.mdix = { state: 0, api: null, docs: new Map(), next: 1, err: '' };
    m.str = function (p) { let e = p; while (HEAPU8[e] !== 0) e++; return new TextDecoder().decode(HEAPU8.subarray(p, e)); };
    m.put = function (s, p, cap) {                       // writes s at p as UTF-8 with a NUL, cut to fit; returns the bytes written or -1 when it did not fit
        const b = new TextEncoder().encode(s);
        if (b.length + 1 > cap) { if (cap > 0) HEAPU8[p] = 0; return -1; }
        HEAPU8.set(b, p); HEAPU8[p + b.length] = 0; return b.length;
    };
    (async function () {
        try {
            const url = URL.createObjectURL(new Blob([g], { type: 'text/javascript' }));
            const mod = await (new Function('u', 'return import(u)'))(url);
            URL.revokeObjectURL(url);
            const res = await WebAssembly.instantiate(w, { './mdix_wasm_bg.js': mod });
            mod.__wbg_set_wasm(res.instance.exports);
            res.instance.exports.__wbindgen_start();
            m.api = mod;
            m.state = 1;
        } catch (e) {
            m.err = 'wasm boot: ' + (e && e.message ? e.message : String(e));
            m.state = -1;
        }
    })();
});

EM_JS(int, mdix_js_state, (void), { return Module.mdix ? Module.mdix.state : 0; });

EM_JS(void, mdix_js_error, (char *out, int cap), {
    const m = Module.mdix;
    m.put(m.err || 'unknown', out, cap);
});

// A document: returns a handle above 0, or 0 with the reason in m.err (read back with mdix_js_load_error).
EM_JS(int, mdix_js_load, (const char *src), {
    const m = Module.mdix;
    if (!m || !m.api) { return 0; }
    try {
        const db = m.api.MdixDatabase.loadStr(m.str(src));
        const h = m.next++;
        m.docs.set(h, db);
        return h;
    } catch (e) {
        m.err = e && e.message ? e.message : String(e);
        return 0;
    }
});

EM_JS(void, mdix_js_free, (int h), {
    const m = Module.mdix; const d = m && m.docs.get(h);
    if (d) { try { d.free(); } catch (e) {} m.docs.delete(h); }
});

EM_JS(int, mdix_js_has, (int h, const char *path), {
    const m = Module.mdix; const d = m && m.docs.get(h);
    if (!d) return 0;
    try { return d.exists(m.str(path)) ? 1 : 0; } catch (e) { return 0; }
});

EM_JS(int, mdix_js_len, (int h, const char *path), {
    const m = Module.mdix; const d = m && m.docs.get(h);
    if (!d) return -1;
    try { return d.getArrayLength(m.str(path)); } catch (e) { return -1; }
});

// An int, or an enum's ordinal.
EM_JS(int, mdix_js_int, (int h, const char *path, int *out), {
    const m = Module.mdix; const d = m && m.docs.get(h);
    if (!d) return 0;
    try { HEAP32[out >> 2] = d.getInt(m.str(path)); return 1; } catch (e) { return 0; }
});

// A float or an int, as a double (getDouble widens whatever number it finds).
EM_JS(int, mdix_js_float, (int h, const char *path, double *out), {
    const m = Module.mdix; const d = m && m.docs.get(h);
    if (!d) return 0;
    try { HEAPF64[out >> 3] = d.getDouble(m.str(path)); return 1; } catch (e) { return 0; }
});

EM_JS(int, mdix_js_bool, (int h, const char *path, int *out), {
    const m = Module.mdix; const d = m && m.docs.get(h);
    if (!d) return 0;
    try { HEAP32[out >> 2] = d.getBool(m.str(path)) ? 1 : 0; return 1; } catch (e) { return 0; }
});

EM_JS(int, mdix_js_str, (int h, const char *path, char *out, int cap), {
    const m = Module.mdix; const d = m && m.docs.get(h);
    if (!d) return 0;
    try { return m.put(d.getString(m.str(path)), out, cap) >= 0 ? 1 : 0; } catch (e) { return 0; }
});

static unsigned char *ReadAll(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n > 0 ? (unsigned char *)malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) *len = (int)n;
    return buf;
}

bool MdixWeb_BootStart(char *err, int cap)
{
    int gn = 0, wn = 0;
    unsigned char *g = ReadAll("assets/mdix/mdix_wasm_bg.js", &gn);
    unsigned char *w = ReadAll("assets/mdix/mdix_wasm_bg.wasm", &wn);
    if (!g || !w) {
        snprintf(err, (size_t)cap, "assets/mdix/mdix_wasm_bg.%s is missing", g ? "wasm" : "js");
        free(g); free(w);
        return false;
    }
    mdix_js_boot(g, gn, w, wn);
    free(g); free(w);
    return true;
}

int  MdixWeb_BootState(void) { return mdix_js_state(); }
void MdixWeb_BootError(char *err, int cap) { mdix_js_error(err, cap); }

int MdixWeb_Load(const char *source, char *err, int cap)
{
    const int h = mdix_js_load(source);
    if (h <= 0 && err && cap > 0) mdix_js_error(err, cap);
    return h;
}

void MdixWeb_Free(int h) { mdix_js_free(h); }
bool MdixWeb_Has(int h, const char *path) { return mdix_js_has(h, path) != 0; }
int  MdixWeb_Len(int h, const char *path) { return mdix_js_len(h, path); }
bool MdixWeb_Int(int h, const char *path, int *out) { return mdix_js_int(h, path, out) != 0; }

bool MdixWeb_Float(int h, const char *path, double *out)
{
    static double slot;                       // 8-byte aligned storage for JavaScript to write into
    if (!mdix_js_float(h, path, &slot)) return false;
    *out = slot;
    return true;
}

bool MdixWeb_Bool(int h, const char *path, bool *out)
{
    int v = 0;
    if (!mdix_js_bool(h, path, &v)) return false;
    *out = v != 0;
    return true;
}

bool MdixWeb_Str(int h, const char *path, char *out, int cap) { return mdix_js_str(h, path, out, cap) != 0; }

#else   // ---- not a browser: the same calls, all of which say "no" ------------------------------------------------------

bool MdixWeb_BootStart(char *err, int cap) { if (err && cap > 0) snprintf(err, (size_t)cap, "the DixScript wasm only runs in the web build"); return false; }
int  MdixWeb_BootState(void) { return -1; }
void MdixWeb_BootError(char *err, int cap) { if (err && cap > 0) snprintf(err, (size_t)cap, "the DixScript wasm only runs in the web build"); }
int  MdixWeb_Load(const char *source, char *err, int cap) { (void)source; if (err && cap > 0) snprintf(err, (size_t)cap, "no DixScript wasm in this build"); return 0; }
void MdixWeb_Free(int h) { (void)h; }
bool MdixWeb_Has(int h, const char *path) { (void)h; (void)path; return false; }
int  MdixWeb_Len(int h, const char *path) { (void)h; (void)path; return -1; }
bool MdixWeb_Int(int h, const char *path, int *out) { (void)h; (void)path; (void)out; return false; }
bool MdixWeb_Float(int h, const char *path, double *out) { (void)h; (void)path; (void)out; return false; }
bool MdixWeb_Bool(int h, const char *path, bool *out) { (void)h; (void)path; (void)out; return false; }
bool MdixWeb_Str(int h, const char *path, char *out, int cap) { (void)h; (void)path; (void)out; (void)cap; return false; }

#endif
