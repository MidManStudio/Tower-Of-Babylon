// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_wire.h and net_wire.c"
// ============================================================================
#include "net_wire.h"

#include <string.h>

void NetW_Init(NetW *w, uint8_t *buf, size_t cap) { w->p = buf; w->cap = cap; w->len = 0; w->err = false; }

static void Put(NetW *w, const uint8_t *b, size_t n)
{
    if (w->err || n > w->cap - w->len) { w->err = true; return; }
    memcpy(w->p + w->len, b, n);
    w->len += n;
}

void NetW_U8(NetW *w, uint8_t v) { Put(w, &v, 1); }
void NetW_U16(NetW *w, uint16_t v) { const uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; Put(w, b, 2); }
void NetW_U32(NetW *w, uint32_t v) { const uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; Put(w, b, 4); }
void NetW_I8(NetW *w, int8_t v) { NetW_U8(w, (uint8_t)v); }
void NetW_I16(NetW *w, int16_t v) { NetW_U16(w, (uint16_t)v); }
void NetW_I32(NetW *w, int32_t v) { NetW_U32(w, (uint32_t)v); }
void NetW_F32(NetW *w, float v) { uint32_t u; memcpy(&u, &v, 4); NetW_U32(w, u); }
void NetW_Bytes(NetW *w, const void *data, size_t n) { Put(w, (const uint8_t *)data, n); }

void NetR_Init(NetR *r, const uint8_t *buf, size_t len) { r->p = buf; r->len = len; r->pos = 0; r->err = false; }

static const uint8_t *Take(NetR *r, size_t n)
{
    if (r->err || n > r->len - r->pos) { r->err = true; return NULL; }
    const uint8_t *s = r->p + r->pos;
    r->pos += n;
    return s;
}

uint8_t NetR_U8(NetR *r) { const uint8_t *s = Take(r, 1); return s ? s[0] : 0; }
uint16_t NetR_U16(NetR *r) { const uint8_t *s = Take(r, 2); return s ? (uint16_t)(s[0] | (s[1] << 8)) : 0; }
uint32_t NetR_U32(NetR *r)
{
    const uint8_t *s = Take(r, 4);
    return s ? ((uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24)) : 0;
}
int8_t NetR_I8(NetR *r) { return (int8_t)NetR_U8(r); }
int16_t NetR_I16(NetR *r) { return (int16_t)NetR_U16(r); }
int32_t NetR_I32(NetR *r) { return (int32_t)NetR_U32(r); }
float NetR_F32(NetR *r) { const uint32_t u = NetR_U32(r); float f; memcpy(&f, &u, 4); return f; }
void NetR_Bytes(NetR *r, void *out, size_t n)
{
    const uint8_t *s = Take(r, n);
    if (s) memcpy(out, s, n); else if (n) memset(out, 0, n);
}
bool NetR_Done(const NetR *r) { return !r->err && r->pos == r->len; }
