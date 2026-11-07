// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_wire.h and net_wire.c"
// ============================================================================
#ifndef NET_WIRE_H
#define NET_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Bytes in and out of a buffer, little-endian whatever the machine, with every access checked. A writer that runs out of room, or a reader
// asked for more than is left, sets err and goes on harmlessly (writes nothing, reads zeros): nothing is read or written outside the buffer,
// whatever a stranger sends. Check err (or NetR_Done) once at the end.
typedef struct { uint8_t *p; size_t cap, len; bool err; } NetW;
typedef struct { const uint8_t *p; size_t len, pos; bool err; } NetR;

void NetW_Init(NetW *w, uint8_t *buf, size_t cap);
void NetW_U8(NetW *w, uint8_t v);
void NetW_U16(NetW *w, uint16_t v);
void NetW_U32(NetW *w, uint32_t v);
void NetW_I8(NetW *w, int8_t v);
void NetW_I16(NetW *w, int16_t v);
void NetW_I32(NetW *w, int32_t v);
void NetW_F32(NetW *w, float v);                       // the IEEE bits
void NetW_Bytes(NetW *w, const void *data, size_t n);

void NetR_Init(NetR *r, const uint8_t *buf, size_t len);
uint8_t  NetR_U8(NetR *r);
uint16_t NetR_U16(NetR *r);
uint32_t NetR_U32(NetR *r);
int8_t   NetR_I8(NetR *r);
int16_t  NetR_I16(NetR *r);
int32_t  NetR_I32(NetR *r);
float    NetR_F32(NetR *r);
void     NetR_Bytes(NetR *r, void *out, size_t n);
bool     NetR_Done(const NetR *r);                     // no error, and every byte was read: a packet with bytes left over is refused

#endif
