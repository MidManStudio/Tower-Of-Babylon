// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_mid.h and net_mid.c"
// ============================================================================
#include "net_mid.h"

#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"
#include "net_proto.h"
#include "net_reliable.h"

#if TOWER_MIDNET

#include "mid_net_quic.h"

#define LINGER_MS     500                  // a closing connection stays up this long so a last Disconnect goes out
#define OUTBOX_SLOTS  8                    // reliable messages kept while the handshake finishes (a Hello, a few more)
#define RELIABLE_STREAM 0u

typedef struct {
    MidNetQuicConn *conn;
    uint8_t  outbox[OUTBOX_SLOTS][NET_REL_MAX_MSG];
    uint16_t outboxLen[OUTBOX_SLOTS];
    int      outboxCount;
} MidConn;

static bool Flush(MidConn *m)                                                         // true when the connection is up and the outbox is empty
{
    const int st = mid_net_quic_conn_state(m->conn);
    if (st != MID_NET_QUIC_CONNECTED) return false;
    for (int i = 0; i < m->outboxCount; ++i) mid_net_quic_send_reliable(m->conn, RELIABLE_STREAM, m->outbox[i], m->outboxLen[i]);
    m->outboxCount = 0;
    return true;
}

static bool SendDatagram(void *self, const uint8_t *data, size_t len)
{
    MidConn *m = (MidConn *)self;
    if (len == 0 || len > NET_MTU) return false;
    const int st = mid_net_quic_conn_state(m->conn);
    if (st == MID_NET_QUIC_CLOSED_STATE || st < 0) return false;
    if (st == MID_NET_QUIC_CONNECTING) return true;                                   // not up yet: a datagram may be lost anyway
    Flush(m);
    return mid_net_quic_send_datagram(m->conn, data, len) == MID_NET_QUIC_OK;
}

static int RecvDatagram(void *self, uint8_t *buf, size_t cap)
{
    MidConn *m = (MidConn *)self;
    if (!Flush(m)) return 0;
    const int n = mid_net_quic_recv_datagram(m->conn, buf, cap);
    if (n > 0) return n;
    if (n == MID_NET_QUIC_BUFFER_TOO_SMALL) return -1;
    return 0;
}

static bool SendReliable(void *self, const uint8_t *data, size_t len)
{
    MidConn *m = (MidConn *)self;
    if (len == 0 || len > NET_REL_MAX_MSG) return false;
    const int st = mid_net_quic_conn_state(m->conn);
    if (st == MID_NET_QUIC_CLOSED_STATE || st < 0) return false;
    if (st == MID_NET_QUIC_CONNECTING) {
        if (m->outboxCount >= OUTBOX_SLOTS) return false;
        memcpy(m->outbox[m->outboxCount], data, len);
        m->outboxLen[m->outboxCount++] = (uint16_t)len;
        return true;
    }
    Flush(m);
    return mid_net_quic_send_reliable(m->conn, RELIABLE_STREAM, data, len) == MID_NET_QUIC_OK;
}

static int RecvReliable(void *self, uint8_t *buf, size_t cap)
{
    MidConn *m = (MidConn *)self;
    if (!Flush(m)) return 0;
    const int n = mid_net_quic_recv_reliable(m->conn, NULL, buf, cap);
    if (n > 0) return n;
    if (n == MID_NET_QUIC_BUFFER_TOO_SMALL) return -1;
    return 0;
}

static void CloseConn(void *self)
{
    MidConn *m = (MidConn *)self;
    if (m->conn) mid_net_quic_conn_close(m->conn, LINGER_MS);
    free(m);
}

static bool Wrap(MidNetQuicConn *conn, NetTransport *out)
{
    MidConn *m = (MidConn *)calloc(1, sizeof *m);
    if (!m) { mid_net_quic_conn_close(conn, 0); return false; }
    m->conn = conn;
    out->self = m;
    out->sendDatagram = SendDatagram;
    out->recvDatagram = RecvDatagram;
    out->sendReliable = SendReliable;
    out->recvReliable = RecvReliable;
    out->close = CloseConn;
    return true;
}

bool NetMid_Available(void) { return true; }

bool NetMidHost_Listen(NetMidHost *h, uint16_t port)
{
    memset(h, 0, sizeof *h);
    MidNetQuicServer *s = NULL;
    if (mid_net_quic_server_open(NULL, port, &s) != MID_NET_QUIC_OK || !s) return false;
    h->server = s;
    h->port = mid_net_quic_server_port(s);
    h->open = true;
    return true;
}

uint16_t NetMidHost_Port(const NetMidHost *h) { return h->port; }

bool NetMidHost_Accept(NetMidHost *h, NetTransport *out)
{
    if (!h->open) return false;
    MidNetQuicConn *c = NULL;
    if (mid_net_quic_server_accept((MidNetQuicServer *)h->server, &c) != 1 || !c) return false;
    return Wrap(c, out);
}

bool NetMidHost_Connect(NetMidHost *h, const char *host, uint16_t port, NetTransport *out)
{
    (void)h;
    MidNetQuicConn *c = NULL;
    if (mid_net_quic_connect(host, port, &c) != MID_NET_QUIC_OK || !c) return false;
    return Wrap(c, out);
}

void NetMidHost_Pump(NetMidHost *h) { (void)h; }

void NetMidHost_Close(NetMidHost *h)
{
    if (h->open && h->server) mid_net_quic_server_close((MidNetQuicServer *)h->server);
    h->open = false;
    h->server = NULL;
}

static bool Ours(const NetTransport *t) { return t && t->close == CloseConn && t->self; }

bool NetMid_TransportError(const NetTransport *t, char *buf, size_t cap)
{
    if (!Ours(t)) return false;
    if (buf && cap) buf[0] = '\0';
    mid_net_quic_conn_error(((MidConn *)t->self)->conn, buf, cap);
    return true;
}

int NetMid_TransportState(const NetTransport *t)
{
    if (!Ours(t)) return -1;
    return mid_net_quic_conn_state(((MidConn *)t->self)->conn);
}

#else   // the build did not link the QUIC library: every function says no

bool     NetMid_Available(void) { return false; }
bool     NetMidHost_Listen(NetMidHost *h, uint16_t port) { (void)port; memset(h, 0, sizeof *h); return false; }
uint16_t NetMidHost_Port(const NetMidHost *h) { return h->port; }
bool     NetMidHost_Accept(NetMidHost *h, NetTransport *out) { (void)h; (void)out; return false; }
bool     NetMidHost_Connect(NetMidHost *h, const char *host, uint16_t port, NetTransport *out) { (void)h; (void)host; (void)port; (void)out; return false; }
void     NetMidHost_Pump(NetMidHost *h) { (void)h; }
void     NetMidHost_Close(NetMidHost *h) { h->open = false; }
bool     NetMid_TransportError(const NetTransport *t, char *buf, size_t cap) { (void)t; (void)buf; (void)cap; return false; }
int      NetMid_TransportState(const NetTransport *t) { (void)t; return -1; }

#endif
