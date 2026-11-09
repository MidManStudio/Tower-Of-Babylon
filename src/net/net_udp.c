// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_udp.h and net_udp.c"
// ============================================================================
#define _POSIX_C_SOURCE 200809L
#include "net_udp.h"

#include <string.h>

#include "../platform/platform.h"

#if TOWER_NET_UDP

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define K_DATAGRAM 0                                  // the byte after the connection id: 0 a datagram, 1 and 2 the reliable layer's data and acknowledgement

static float g_loss = 0.0f, g_dup = 0.0f;
static uint32_t g_rng = 0x6d2b79f5u;
void NetUdp_SetDebug(float lossChance, float dupChance) { g_loss = lossChance; g_dup = dupChance; }
static float Rnd01(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; g_rng = x; return (float)(x >> 8) / 16777216.0f; }

double NetUdp_NowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

// ---- sending ---------------------------------------------------------------------------------------------------------------
static bool RawSend(NetUdpHost *h, const NetUdpPeer *p, const uint8_t *body, size_t len)
{
    uint8_t pkt[4 + 1 + NET_MTU];
    if (len == 0 || len > 1 + NET_MTU) return false;
    pkt[0] = (uint8_t)p->connId; pkt[1] = (uint8_t)(p->connId >> 8); pkt[2] = (uint8_t)(p->connId >> 16); pkt[3] = (uint8_t)(p->connId >> 24);
    memcpy(pkt + 4, body, len);
    if (g_loss > 0.0f && Rnd01() < g_loss) return true;                              // sent, and gone
    const int copies = (g_dup > 0.0f && Rnd01() < g_dup) ? 2 : 1;
    for (int i = 0; i < copies; ++i) sendto(h->fd, pkt, 4 + len, 0, (const struct sockaddr *)p->addr, (socklen_t)p->addrLen);
    return true;
}

static void EmitPeer(void *user, const uint8_t *pkt, size_t len) { NetUdpPeer *p = (NetUdpPeer *)user; RawSend(p->host, p, pkt, len); }

// ---- receiving -------------------------------------------------------------------------------------------------------------
static bool SameAddr(const NetUdpPeer *p, const struct sockaddr *a, socklen_t alen)
{
    if (alen != (socklen_t)p->addrLen || alen > sizeof p->addr) return false;
    return memcmp(p->addr, a, alen) == 0;
}

static NetUdpPeer *FindPeer(NetUdpHost *h, uint32_t connId, const struct sockaddr *from, socklen_t fl)
{
    for (int i = 0; i < NET_UDP_MAX_PEERS; ++i)
        if (h->peers[i].used && h->peers[i].connId == connId && SameAddr(&h->peers[i], from, fl)) return &h->peers[i];
    return NULL;
}

static NetUdpPeer *NewPeer(NetUdpHost *h, uint32_t connId, const struct sockaddr *from, socklen_t fl, double now)
{
    if (fl > sizeof h->peers[0].addr) return NULL;
    for (int i = 0; i < NET_UDP_MAX_PEERS; ++i)
        if (!h->peers[i].used) {
            NetUdpPeer *p = &h->peers[i];
            memset(p, 0, sizeof *p);
            p->used = true; p->host = h; p->connId = connId; memcpy(p->addr, from, fl); p->addrLen = (int)fl;
            p->lastHeardMs = now;
            NetRel_Init(&p->rel);
            return p;
        }
    return NULL;                                                                       // no room: a stranger with no slot is ignored
}

void NetUdpHost_Pump(NetUdpHost *h)
{
    if (!h->open) return;
    const double now = NetUdp_NowMs();
    uint8_t buf[2048];
    for (int k = 0; k < 512; ++k) {
        struct sockaddr_storage from;
        socklen_t fl = sizeof from;
        const ssize_t n = recvfrom(h->fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) break;                                                              // nothing more waiting (or an error we cannot do anything about)
        if (n < 5) { ++h->badPackets; continue; }
        const uint32_t cid = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
        const uint8_t kind = buf[4];
        NetUdpPeer *p = FindPeer(h, cid, (const struct sockaddr *)&from, fl);
        if (!p) {
            // Only a server opens connections, and only for the first reliable message of one: a reliable data packet, sequence number 0, with something in it.
            // (A random packet passes that about once in sixteen million.)
            if (!h->isServer || kind != 1 || n < 8 || buf[5] != 0 || buf[6] != 0) { ++h->badPackets; continue; }
            p = NewPeer(h, cid, (const struct sockaddr *)&from, fl, now);
            if (!p) { ++h->badPackets; continue; }
        }
        p->lastHeardMs = now;
        if (p->closing) {                                                              // finishing: only acknowledgements matter now, so the last message can be acknowledged
            if (kind == 2) NetRel_OnPacket(&p->rel, buf + 4, (size_t)n - 4, now, EmitPeer, p);
            continue;
        }
        if (kind == K_DATAGRAM) {
            const size_t len = (size_t)n - 5;
            if (len == 0 || len > NET_MTU) { ++h->badPackets; continue; }
            if (p->inCount >= NET_UDP_INBOX) { ++p->inboxDrops; continue; }
            const int at = (p->inHead + p->inCount) % NET_UDP_INBOX;
            p->inbox[at].len = (uint16_t)len; memcpy(p->inbox[at].data, buf + 5, len);
            ++p->inCount;
        } else if (!NetRel_OnPacket(&p->rel, buf + 4, (size_t)n - 4, now, EmitPeer, p)) ++h->badPackets;
    }
    for (int i = 0; i < NET_UDP_MAX_PEERS; ++i) {
        NetUdpPeer *p = &h->peers[i];
        if (!p->used) continue;
        NetRel_Tick(&p->rel, now, EmitPeer, p);
        // A connection the game has closed is kept just long enough for its last message (a Disconnect) to get through.
        if (p->closing && (now >= p->closeAtMs || p->rel.sendBase == p->rel.sendNext)) p->used = false;
    }
}

// ---- the transport ----------------------------------------------------------------------------------------------------------
static bool SendDatagram(void *self, const uint8_t *data, size_t len)
{
    NetUdpPeer *p = (NetUdpPeer *)self;
    if (!p->used || p->closing || len == 0 || len > NET_MTU) return false;
    uint8_t body[1 + NET_MTU];
    body[0] = K_DATAGRAM; memcpy(body + 1, data, len);
    return RawSend(p->host, p, body, 1 + len);
}

static int RecvDatagram(void *self, uint8_t *buf, size_t cap)
{
    NetUdpPeer *p = (NetUdpPeer *)self;
    NetUdpHost_Pump(p->host);
    if (!p->used || p->closing || p->inCount == 0) return 0;
    const int len = p->inbox[p->inHead].len;
    const bool fits = (size_t)len <= cap;
    if (fits) memcpy(buf, p->inbox[p->inHead].data, (size_t)len);
    p->inHead = (p->inHead + 1) % NET_UDP_INBOX; --p->inCount;
    return fits ? len : -1;
}

static bool SendReliable(void *self, const uint8_t *data, size_t len)
{
    NetUdpPeer *p = (NetUdpPeer *)self;
    if (!p->used) return false;
    if (p->closing) return false;
    return NetRel_Send(&p->rel, data, len, NetUdp_NowMs(), EmitPeer, p);
}

static int RecvReliable(void *self, uint8_t *buf, size_t cap)
{
    NetUdpPeer *p = (NetUdpPeer *)self;
    NetUdpHost_Pump(p->host);
    if (!p->used || p->closing) return 0;
    return NetRel_Recv(&p->rel, buf, cap);
}

static void CloseConn(void *self)
{
    NetUdpPeer *p = (NetUdpPeer *)self;
    if (!p->used || p->closing) return;
    p->closing = true; p->closeAtMs = NetUdp_NowMs() + 2000.0; p->inCount = 0;
}

static NetTransport Transport(NetUdpPeer *p)
{
    NetTransport t;
    t.self = p; t.sendDatagram = SendDatagram; t.recvDatagram = RecvDatagram; t.sendReliable = SendReliable; t.recvReliable = RecvReliable; t.close = CloseConn;
    return t;
}

// ---- opening and closing ------------------------------------------------------------------------------------------------------
static bool OpenSocket(NetUdpHost *h, uint16_t port, bool bindIt)
{
    memset(h, 0, sizeof *h);
    h->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (h->fd < 0) return false;
    const int fl = fcntl(h->fd, F_GETFL, 0);
    if (fl < 0 || fcntl(h->fd, F_SETFL, fl | O_NONBLOCK) < 0) { close(h->fd); return false; }
    if (bindIt) {
        // No SO_REUSEADDR: for UDP it would let a second server bind the same port and the two would share the packets without a word.
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(port);
        if (bind(h->fd, (struct sockaddr *)&a, sizeof a) < 0) { close(h->fd); return false; }
        socklen_t al = sizeof a;
        if (getsockname(h->fd, (struct sockaddr *)&a, &al) == 0) h->port = ntohs(a.sin_port);
    }
    h->open = true;
    return true;
}

bool NetUdpHost_Listen(NetUdpHost *h, uint16_t port)
{
    if (!OpenSocket(h, port, true)) return false;
    h->isServer = true;
    return true;
}

uint16_t NetUdpHost_Port(const NetUdpHost *h) { return h->port; }

bool NetUdpHost_Accept(NetUdpHost *h, NetTransport *out)
{
    NetUdpHost_Pump(h);
    for (int i = 0; i < NET_UDP_MAX_PEERS; ++i)
        if (h->peers[i].used && !h->peers[i].announced && !h->peers[i].closing) {
            h->peers[i].announced = true;
            *out = Transport(&h->peers[i]);
            return true;
        }
    return false;
}

bool NetUdpHost_Connect(NetUdpHost *h, const char *host, uint16_t port, NetTransport *out)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    char service[8];
    { int n = port, i = 0; char tmp[8]; do { tmp[i++] = (char)('0' + n % 10); n /= 10; } while (n && i < 6); for (int k = 0; k < i; ++k) service[k] = tmp[i - 1 - k]; service[i] = 0; }
    if (getaddrinfo(host, service, &hints, &res) != 0 || !res) return false;
    if (res->ai_addrlen > sizeof h->peers[0].addr || !OpenSocket(h, 0, false)) { freeaddrinfo(res); return false; }
    NetUdpPeer *p = &h->peers[0];
    memset(p, 0, sizeof *p);
    p->used = true; p->host = h; p->announced = true;
    memcpy(p->addr, res->ai_addr, res->ai_addrlen); p->addrLen = (int)res->ai_addrlen;
    freeaddrinfo(res);
    const double now = NetUdp_NowMs();
    uint32_t id = (uint32_t)(now * 1000.0) * 2654435761u ^ ((uint32_t)getpid() << 16) ^ (uint32_t)(uintptr_t)p ^ ((uint32_t)clock() * 40503u);
    if (id == 0) id = 1;
    p->connId = id;
    p->lastHeardMs = now;
    NetRel_Init(&p->rel);
    *out = Transport(p);
    return true;
}

void NetUdpHost_Close(NetUdpHost *h)
{
    if (h->open) close(h->fd);
    memset(h, 0, sizeof *h);
}

#else   // no UDP here: every function says so

double NetUdp_NowMs(void) { return 0.0; }
void NetUdp_SetDebug(float lossChance, float dupChance) { (void)lossChance; (void)dupChance; }
bool NetUdpHost_Listen(NetUdpHost *h, uint16_t port) { (void)h; (void)port; return false; }
uint16_t NetUdpHost_Port(const NetUdpHost *h) { (void)h; return 0; }
bool NetUdpHost_Accept(NetUdpHost *h, NetTransport *out) { (void)h; (void)out; return false; }
bool NetUdpHost_Connect(NetUdpHost *h, const char *host, uint16_t port, NetTransport *out) { (void)h; (void)host; (void)port; (void)out; return false; }
void NetUdpHost_Pump(NetUdpHost *h) { (void)h; }
void NetUdpHost_Close(NetUdpHost *h) { (void)h; }

#endif
