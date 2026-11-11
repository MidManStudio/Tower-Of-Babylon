// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_discovery.h and net_discovery.c"
// ============================================================================
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE                    // getifaddrs and IFF_* on glibc; the other platforms have them anyway
#include "net_discovery.h"

#include <string.h>

#include "../platform/platform.h"
#include "net_wire.h"

#define K_ASK      1
#define K_ANNOUNCE 2
#define ANNOUNCE_FIXED 21                  // magic 4, version 1, kind 1, nonce 4, protocol 2, port 2, players 1, max 1, host id 4, name length 1

// ---- the packets -----------------------------------------------------------------------------------------------------------------------
bool NetDisc_NameOk(const char *name)
{
    if (!name) return false;
    const size_t n = strlen(name);
    if (n < 1 || n > NET_DISC_NAME_MAX) return false;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7e) return false;
    return true;
}

size_t NetDisc_EncodeAsk(const NetDiscAsk *ask, uint8_t *buf, size_t cap)
{
    if (!ask || !buf || cap < NET_DISC_ASK_SIZE) return 0;
    NetW w;
    NetW_Init(&w, buf, cap);
    NetW_U32(&w, NET_DISC_MAGIC);
    NetW_U8(&w, NET_DISC_VERSION);
    NetW_U8(&w, K_ASK);
    NetW_U32(&w, ask->nonce);
    static const uint8_t zeros[NET_DISC_ASK_SIZE] = { 0 };
    NetW_Bytes(&w, zeros, NET_DISC_ASK_SIZE - w.len);
    return w.err || w.len != NET_DISC_ASK_SIZE ? 0 : w.len;
}

bool NetDisc_DecodeAsk(const uint8_t *buf, size_t len, NetDiscAsk *out)
{
    if (!buf || !out || len != NET_DISC_ASK_SIZE) return false;
    NetR r;
    NetR_Init(&r, buf, len);
    if (NetR_U32(&r) != NET_DISC_MAGIC || NetR_U8(&r) != NET_DISC_VERSION || NetR_U8(&r) != K_ASK) return false;
    const uint32_t nonce = NetR_U32(&r);
    uint8_t pad[NET_DISC_ASK_SIZE];
    const size_t padLen = NET_DISC_ASK_SIZE - r.pos;
    NetR_Bytes(&r, pad, padLen);
    if (!NetR_Done(&r)) return false;
    for (size_t i = 0; i < padLen; ++i) if (pad[i] != 0) return false;
    out->nonce = nonce;
    return true;
}

size_t NetDisc_EncodeAnnounce(const NetDiscAnnounce *a, uint8_t *buf, size_t cap)
{
    if (!a || !buf || a->port == 0 || a->maxPlayers < 1 || a->maxPlayers > NET_MAX_PLAYERS || a->players > a->maxPlayers) return 0;
    if (!memchr(a->name, '\0', sizeof a->name)) return 0;                  // a name with no end inside its field is not read past it
    if (!NetDisc_NameOk(a->name)) return 0;
    const size_t nameLen = strlen(a->name);
    if (cap < ANNOUNCE_FIXED + nameLen) return 0;
    NetW w;
    NetW_Init(&w, buf, cap);
    NetW_U32(&w, NET_DISC_MAGIC);
    NetW_U8(&w, NET_DISC_VERSION);
    NetW_U8(&w, K_ANNOUNCE);
    NetW_U32(&w, a->nonce);
    NetW_U16(&w, a->protocol);
    NetW_U16(&w, a->port);
    NetW_U8(&w, a->players);
    NetW_U8(&w, a->maxPlayers);
    NetW_U32(&w, a->hostId);
    NetW_U8(&w, (uint8_t)nameLen);
    NetW_Bytes(&w, a->name, nameLen);
    return w.err ? 0 : w.len;
}

bool NetDisc_DecodeAnnounce(const uint8_t *buf, size_t len, NetDiscAnnounce *out)
{
    if (!buf || !out || len < ANNOUNCE_FIXED + 1 || len > ANNOUNCE_FIXED + NET_DISC_NAME_MAX) return false;
    NetR r;
    NetR_Init(&r, buf, len);
    if (NetR_U32(&r) != NET_DISC_MAGIC || NetR_U8(&r) != NET_DISC_VERSION || NetR_U8(&r) != K_ANNOUNCE) return false;
    NetDiscAnnounce a;
    memset(&a, 0, sizeof a);
    a.nonce = NetR_U32(&r);
    a.protocol = NetR_U16(&r);
    a.port = NetR_U16(&r);
    a.players = NetR_U8(&r);
    a.maxPlayers = NetR_U8(&r);
    a.hostId = NetR_U32(&r);
    const uint8_t nameLen = NetR_U8(&r);
    if (nameLen < 1 || nameLen > NET_DISC_NAME_MAX) return false;
    NetR_Bytes(&r, a.name, nameLen);
    if (!NetR_Done(&r)) return false;
    a.name[nameLen] = '\0';
    if (!NetDisc_NameOk(a.name) || strlen(a.name) != nameLen) return false;      // a zero byte in the middle makes the length lie
    if (a.port == 0 || a.maxPlayers < 1 || a.maxPlayers > NET_MAX_PLAYERS || a.players > a.maxPlayers) return false;
    *out = a;
    return true;
}

// ---- what a searcher has found ------------------------------------------------------------------------------------------------------
void NetLobbies_Init(NetLobbies *t) { memset(t, 0, sizeof *t); }

NetLobbyResult NetLobbies_Update(NetLobbies *t, uint32_t srcAddr, const NetDiscAnnounce *a, double nowMs)
{
    NetLobby *free = NULL;
    for (int i = 0; i < NET_DISC_MAX_LOBBIES; ++i) {
        NetLobby *l = &t->lobby[i];
        if (!l->used) { if (!free) free = l; continue; }
        // The same lobby is the same game port at the same address (a host that restarted keeps its place) or with the same host id (a host
        // that answered from another of its addresses, say its Wi-Fi and its hotspot, is not listed twice; the address first heard is kept).
        if (l->info.port == a->port && (l->addr == srcAddr || l->info.hostId == a->hostId)) {
            const bool same = l->info.hostId == a->hostId && l->info.protocol == a->protocol && l->info.players == a->players &&
                              l->info.maxPlayers == a->maxPlayers && !strcmp(l->info.name, a->name);
            l->info = *a;
            l->compatible = a->protocol == NET_PROTOCOL_VERSION;
            l->lastSeenMs = nowMs;
            return same ? NETLOBBY_SAME : NETLOBBY_CHANGED;
        }
    }
    if (!free) return NETLOBBY_IGNORED;
    free->used = true;
    free->addr = srcAddr;
    free->info = *a;
    free->compatible = a->protocol == NET_PROTOCOL_VERSION;
    free->seq = t->nextSeq++;
    free->lastSeenMs = nowMs;
    return NETLOBBY_NEW;
}

int NetLobbies_Expire(NetLobbies *t, double nowMs, double timeoutMs)
{
    int removed = 0;
    for (int i = 0; i < NET_DISC_MAX_LOBBIES; ++i)
        if (t->lobby[i].used && nowMs - t->lobby[i].lastSeenMs > timeoutMs) { t->lobby[i].used = false; ++removed; }
    return removed;
}

int NetLobbies_Count(const NetLobbies *t)
{
    int n = 0;
    for (int i = 0; i < NET_DISC_MAX_LOBBIES; ++i) if (t->lobby[i].used) ++n;
    return n;
}

const NetLobby *NetLobbies_Nth(const NetLobbies *t, int n)
{
    // by order of first hearing: repeatedly take the smallest sequence number above the last one taken
    const NetLobby *prev = NULL;
    for (int k = 0; k <= n; ++k) {
        const NetLobby *best = NULL;
        for (int i = 0; i < NET_DISC_MAX_LOBBIES; ++i) {
            const NetLobby *l = &t->lobby[i];
            if (!l->used || (prev && l->seq <= prev->seq)) continue;
            if (!best || l->seq < best->seq) best = l;
        }
        if (!best) return NULL;
        prev = best;
    }
    return prev;
}

const NetLobby *NetLobbies_FirstJoinable(const NetLobbies *t)
{
    for (int n = 0; n < NET_DISC_MAX_LOBBIES; ++n) {
        const NetLobby *l = NetLobbies_Nth(t, n);
        if (!l) return NULL;
        if (l->compatible && l->info.players < l->info.maxPlayers) return l;
    }
    return NULL;
}

// ---- addresses ------------------------------------------------------------------------------------------------------------------------
static bool Starts(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

bool NetDisc_IfaceNameUsable(const char *name)
{
    if (!name || !name[0]) return false;
    if (name[0] == 'l' && name[1] == 'o' && (name[2] == '\0' || (name[2] >= '0' && name[2] <= '9'))) return false;     // lo, lo0
    static const char *const no[] = { "rmnet", "ccmni", "clat", "v4-", "dummy", "tun", "ppp", "wwan", "docker", "veth" };
    for (size_t i = 0; i < sizeof no / sizeof no[0]; ++i) if (Starts(name, no[i])) return false;
    return true;
}

uint32_t NetDisc_DirectedBroadcast(uint32_t ip, uint32_t mask) { return ip | ~mask; }

bool NetDisc_IsHotspotRange(uint32_t ip)
{
    const uint32_t net24 = ip >> 8;
    return net24 == 0xC0A82Bu /* 192.168.43 */ || net24 == 0xC0A831u /* 192.168.49 */ || net24 == 0xAC140Au /* 172.20.10 */ || net24 == 0xC0A889u /* 192.168.137 */;
}

int NetDisc_RankHostAddresses(const NetDiscIface *ifs, int n, uint32_t *out, int maxOut)
{
    int count = 0;
    for (int tier = 0; tier < 2; ++tier)
        for (int i = 0; i < n && count < maxOut; ++i) {
            const NetDiscIface *f = &ifs[i];
            if (!f->up || f->loopback || f->ip == 0 || !NetDisc_IfaceNameUsable(f->name)) continue;
            if ((f->ip >> 16) == 0xA9FEu) continue;                                   // 169.254.x.x: no address was ever given, nobody can reach it
            if ((f->ip >> 24) == 127u) continue;
            if (NetDisc_IsHotspotRange(f->ip) != (tier == 1)) continue;
            out[count++] = f->ip;
        }
    return count;
}

void NetDisc_FormatIp(uint32_t ip, char out[16])
{
    static const char digits[] = "0123456789";
    char *p = out;
    for (int part = 3; part >= 0; --part) {
        const unsigned v = (ip >> (part * 8)) & 0xffu;
        if (v >= 100) *p++ = digits[v / 100];
        if (v >= 10) *p++ = digits[(v / 10) % 10];
        *p++ = digits[v % 10];
        if (part) *p++ = '.';
    }
    *p = '\0';
}

bool NetDisc_ParseIp(const char *text, uint32_t *ip)
{
    if (!text || !ip) return false;
    uint32_t v = 0;
    for (int part = 0; part < 4; ++part) {
        unsigned digits = 0, n = 0;
        const char first = *text;
        while (*text >= '0' && *text <= '9') { n = n * 10 + (unsigned)(*text - '0'); ++text; ++digits; if (digits > 3) return false; }
        if (digits == 0 || n > 255 || (first == '0' && digits > 1)) return false;
        v = (v << 8) | n;
        if (part < 3) { if (*text != '.') return false; ++text; }
    }
    if (*text != '\0') return false;
    *ip = v;
    return true;
}

// ---- sockets ---------------------------------------------------------------------------------------------------------------------------
#if TOWER_NET_UDP

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static uint32_t g_rng = 0;
static uint32_t Random32(void)
{
    if (g_rng == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        g_rng = (uint32_t)ts.tv_nsec ^ ((uint32_t)ts.tv_sec * 2654435761u) ^ ((uint32_t)getpid() * 40503u) ^ 0x9e3779b9u;
        if (g_rng == 0) g_rng = 1;
    }
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return g_rng;
}

static bool MakeUdp(int *fdOut, uint16_t bindPort, uint16_t *boundPort, bool broadcast)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); return false; }
    if (broadcast) {
        const int on = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on) < 0) { close(fd); return false; }
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(bindPort);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return false; }
    socklen_t len = sizeof a;
    if (getsockname(fd, (struct sockaddr *)&a, &len) < 0) { close(fd); return false; }
    *fdOut = fd;
    if (boundPort) *boundPort = ntohs(a.sin_port);
    return true;
}

static bool SendTo(int fd, uint32_t ip, uint16_t port, const uint8_t *data, size_t len)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(ip);
    a.sin_port = htons(port);
    return sendto(fd, data, len, 0, (struct sockaddr *)&a, sizeof a) == (ssize_t)len;
}

int NetDisc_ListInterfaces(NetDiscIface *out, int max)
{
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return 0;
    int n = 0;
    for (const struct ifaddrs *ifa = list; ifa && n < max; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || !ifa->ifa_netmask) continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK) || !NetDisc_IfaceNameUsable(ifa->ifa_name)) continue;
        NetDiscIface *f = &out[n++];
        memset(f, 0, sizeof *f);
        f->ip = ntohl(((const struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr);
        f->mask = ntohl(((const struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr);
        f->up = true;
        f->canBroadcast = (ifa->ifa_flags & IFF_BROADCAST) != 0;
        strncpy(f->name, ifa->ifa_name, sizeof f->name - 1);
    }
    freeifaddrs(list);
    return n;
}

// ---- the host -----------------------------------------------------------------------------------------------------------------------------
bool NetDiscHost_Open(NetDiscHost *h, uint16_t port)
{
    memset(h, 0, sizeof *h);
    h->fd = -1;
    if (!MakeUdp(&h->fd, port, &h->port, false)) { h->fd = -1; return false; }
    h->open = true;
    h->hostId = Random32();
    h->tokens = NET_DISC_REPLY_BURST;
    h->lastRefillMs = -1.0;
    return true;
}

uint16_t NetDiscHost_Port(const NetDiscHost *h) { return h->port; }

void NetDiscHost_SetInfo(NetDiscHost *h, const char *name, uint16_t gamePort, int players, int maxPlayers)
{
    char safe[NET_DISC_NAME_MAX + 1];
    size_t n = 0;
    for (; name && name[n] && n < NET_DISC_NAME_MAX; ++n) safe[n] = ((unsigned char)name[n] >= 0x20 && (unsigned char)name[n] <= 0x7e) ? name[n] : '?';
    if (n == 0) { memcpy(safe, "tower", 5); n = 5; }
    safe[n] = '\0';
    memcpy(h->info.name, safe, n + 1);
    h->info.port = gamePort;
    h->info.protocol = NET_PROTOCOL_VERSION;
    h->info.maxPlayers = (uint8_t)(maxPlayers < 1 ? 1 : maxPlayers > NET_MAX_PLAYERS ? NET_MAX_PLAYERS : maxPlayers);
    h->info.players = (uint8_t)(players < 0 ? 0 : players > h->info.maxPlayers ? h->info.maxPlayers : players);
}

int NetDiscHost_Poll(NetDiscHost *h, double nowMs)
{
    if (!h->open) return 0;
    if (h->lastRefillMs < 0.0) h->lastRefillMs = nowMs;
    h->tokens += (nowMs - h->lastRefillMs) / 1000.0 * NET_DISC_REPLY_RATE;
    if (h->tokens > NET_DISC_REPLY_BURST) h->tokens = NET_DISC_REPLY_BURST;
    h->lastRefillMs = nowMs;
    int sent = 0;
    for (int reads = 0; reads < 64; ++reads) {                                    // a flood is left in the socket (and dropped by the system), not worked through at once
        uint8_t buf[NET_MTU];
        struct sockaddr_in from;
        socklen_t fromLen = sizeof from;
        const ssize_t n = recvfrom(h->fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fromLen);
        if (n < 0) break;
        ++h->received;
        NetDiscAsk ask;
        if (h->info.port == 0 || !NetDisc_DecodeAsk(buf, (size_t)n, &ask)) { ++h->ignored; continue; }
        if (h->tokens < 1.0) { ++h->throttled; continue; }
        h->tokens -= 1.0;
        NetDiscAnnounce a = h->info;
        a.nonce = ask.nonce;
        a.hostId = h->hostId;
        uint8_t reply[NET_DISC_ASK_SIZE];
        const size_t len = NetDisc_EncodeAnnounce(&a, reply, sizeof reply);
        if (len == 0) { ++h->ignored; continue; }
        if (sendto(h->fd, reply, len, 0, (struct sockaddr *)&from, fromLen) == (ssize_t)len) { ++h->replies; ++sent; }
    }
    return sent;
}

void NetDiscHost_Close(NetDiscHost *h)
{
    if (h->open && h->fd >= 0) close(h->fd);
    h->open = false;
    h->fd = -1;
}

// ---- the searcher ---------------------------------------------------------------------------------------------------------------------------
bool NetDiscClient_Open(NetDiscClient *c, uint16_t discoveryPort)
{
    memset(c, 0, sizeof *c);
    c->fd = -1;
    if (!MakeUdp(&c->fd, 0, NULL, true)) { c->fd = -1; return false; }
    c->open = true;
    c->port = discoveryPort;
    c->nonce = Random32();
    NetLobbies_Init(&c->lobbies);
    return true;
}

int NetDiscClient_Search(NetDiscClient *c, double nowMs)
{
    if (!c->open) return 0;
    if (c->asked && nowMs - c->lastAskMs < NET_DISC_INTERVAL_MS) return 0;
    c->asked = true;
    c->lastAskMs = nowMs;
    uint8_t pkt[NET_DISC_ASK_SIZE];
    const NetDiscAsk ask = { c->nonce };
    const size_t len = NetDisc_EncodeAsk(&ask, pkt, sizeof pkt);
    if (len == 0) return 0;
    uint32_t dest[2 + 16];
    int nd = 0;
    dest[nd++] = 0xFFFFFFFFu;                                                     // the limited broadcast address
    NetDiscIface ifs[16];
    const int ni = NetDisc_ListInterfaces(ifs, 16);
    for (int i = 0; i < ni; ++i) {
        if (!ifs[i].canBroadcast || ifs[i].mask == 0xFFFFFFFFu || ifs[i].mask == 0xFFFFFFFEu) continue;
        const uint32_t b = NetDisc_DirectedBroadcast(ifs[i].ip, ifs[i].mask);
        bool dup = false;
        for (int k = 0; k < nd; ++k) if (dest[k] == b) dup = true;
        if (!dup && nd < 2 + 16 - 1) dest[nd++] = b;
    }
    dest[nd++] = 0x7F000001u;                                                     // a host on this same device
    int sent = 0;
    for (int i = 0; i < nd; ++i) if (SendTo(c->fd, dest[i], c->port, pkt, len)) { ++sent; ++c->asksSent; }
    return sent;
}

bool NetDiscClient_Probe(NetDiscClient *c, const char *ipv4, uint16_t port)
{
    uint32_t ip;
    if (!c->open || !NetDisc_ParseIp(ipv4, &ip) || port == 0) return false;
    uint8_t pkt[NET_DISC_ASK_SIZE];
    const NetDiscAsk ask = { c->nonce };
    const size_t len = NetDisc_EncodeAsk(&ask, pkt, sizeof pkt);
    if (len == 0 || !SendTo(c->fd, ip, port, pkt, len)) return false;
    ++c->asksSent;
    return true;
}

int NetDiscClient_Poll(NetDiscClient *c, double nowMs)
{
    if (!c->open) return 0;
    int changed = 0;
    for (int reads = 0; reads < 64; ++reads) {
        uint8_t buf[NET_MTU];
        struct sockaddr_in from;
        socklen_t fromLen = sizeof from;
        const ssize_t n = recvfrom(c->fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fromLen);
        if (n < 0) break;
        NetDiscAnnounce a;
        if (!NetDisc_DecodeAnnounce(buf, (size_t)n, &a) || a.nonce != c->nonce) { ++c->ignored; continue; }      // not an answer to OUR question
        ++c->answersSeen;
        const NetLobbyResult r = NetLobbies_Update(&c->lobbies, ntohl(from.sin_addr.s_addr), &a, nowMs);
        if (r == NETLOBBY_NEW || r == NETLOBBY_CHANGED) ++changed;
    }
    NetLobbies_Expire(&c->lobbies, nowMs, NET_DISC_TIMEOUT_MS);
    return changed;
}

void NetDiscClient_Close(NetDiscClient *c)
{
    if (c->open && c->fd >= 0) close(c->fd);
    c->open = false;
    c->fd = -1;
}

#else   // no UDP on this platform (a browser): every function says so

int  NetDisc_ListInterfaces(NetDiscIface *out, int max) { (void)out; (void)max; return 0; }
bool NetDiscHost_Open(NetDiscHost *h, uint16_t port) { (void)port; memset(h, 0, sizeof *h); h->fd = -1; return false; }
uint16_t NetDiscHost_Port(const NetDiscHost *h) { return h->port; }
void NetDiscHost_SetInfo(NetDiscHost *h, const char *name, uint16_t gamePort, int players, int maxPlayers) { (void)h; (void)name; (void)gamePort; (void)players; (void)maxPlayers; }
int  NetDiscHost_Poll(NetDiscHost *h, double nowMs) { (void)h; (void)nowMs; return 0; }
void NetDiscHost_Close(NetDiscHost *h) { h->open = false; }
bool NetDiscClient_Open(NetDiscClient *c, uint16_t discoveryPort) { (void)discoveryPort; memset(c, 0, sizeof *c); c->fd = -1; NetLobbies_Init(&c->lobbies); return false; }
int  NetDiscClient_Search(NetDiscClient *c, double nowMs) { (void)c; (void)nowMs; return 0; }
bool NetDiscClient_Probe(NetDiscClient *c, const char *ipv4, uint16_t port) { (void)c; (void)ipv4; (void)port; return false; }
int  NetDiscClient_Poll(NetDiscClient *c, double nowMs) { (void)c; (void)nowMs; return 0; }
void NetDiscClient_Close(NetDiscClient *c) { c->open = false; }

#endif
