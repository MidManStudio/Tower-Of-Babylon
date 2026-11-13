// Headless test of LAN discovery (src/net/net_discovery.h): the two packets (strict, canonical, never longer than the question), the table of
// lobbies a searcher builds (new, changed, expired, full), the address helpers (directed broadcast, hotspot subnets, which address to tell
// people), and then real UDP on this machine's loopback: a host and a searcher in one process, strangers' packets, a flood, a host that goes
// quiet. No window, no raylib calls. Run from the project root:
//   cc -O2 -std=c99 -DNET_DISCOVERY_HEADLESS_TEST -Isrc -Iraylib/src tests/net_discovery_headless.c src/net/net_discovery.c src/net/net_wire.c -o net_discovery_headless
#ifdef NET_DISCOVERY_HEADLESS_TEST

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "net/net_discovery.h"
#include "platform/platform.h"

static int g_failures = 0, g_checks = 0;
static void Check(int ok, const char *what) { ++g_checks; printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

static uint32_t g_rng = 12345;
static uint32_t Rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static void SleepMs(int ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }
#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static NetDiscAnnounce Ann(const char *name, uint16_t port, int players, int max)
{
    NetDiscAnnounce a;
    memset(&a, 0, sizeof a);
    a.nonce = 0xdeadbeefu; a.hostId = 77; a.protocol = NET_PROTOCOL_VERSION; a.port = port; a.players = (uint8_t)players; a.maxPlayers = (uint8_t)max;
    strcpy(a.name, name);
    return a;
}

// A plain UDP socket for the test's own mischief: it sends what it likes to a port and reads what comes back.
static int RawSocket(void)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    bind(fd, (struct sockaddr *)&a, sizeof a);
    return fd;
}
static void RawSend(int fd, uint16_t port, const void *data, size_t len)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port);
    sendto(fd, data, len, 0, (struct sockaddr *)&a, sizeof a);
}
static int RawRecv(int fd, uint8_t *buf, size_t cap, int waitMs)       // length received, or -1 after the wait
{
    for (int t = 0; t <= waitMs; t += 5) {
        const ssize_t n = recvfrom(fd, buf, cap, MSG_DONTWAIT, NULL, NULL);
        if (n >= 0) return (int)n;
        SleepMs(5);
    }
    return -1;
}

int main(void)
{
    printf("net_discovery_headless: finding a game on the local network\n");

    printf("\n-- the question\n");
    {
        uint8_t buf[NET_DISC_ASK_SIZE + 8];
        NetDiscAsk ask = { 0x01020304u }, back = { 0 };
        const size_t n = NetDisc_EncodeAsk(&ask, buf, sizeof buf);
        Check(n == NET_DISC_ASK_SIZE, "a question is exactly NET_DISC_ASK_SIZE bytes");
        Check(NetDisc_DecodeAsk(buf, n, &back) && back.nonce == 0x01020304u, "it decodes to the same nonce");
        Check(buf[0] == 'T' && buf[1] == 'W' && buf[2] == 'D' && buf[3] == '1', "it starts with the magic TWD1");
        Check(NetDisc_EncodeAsk(&ask, buf, NET_DISC_ASK_SIZE - 1) == 0, "a buffer too small for it gives nothing");
        int refused = 1;
        for (size_t cut = 0; cut < n; ++cut) if (NetDisc_DecodeAsk(buf, cut, &back)) refused = 0;
        Check(refused, "every shorter length is refused (a question must be as long as an answer can be)");
        Check(!NetDisc_DecodeAsk(buf, n + 1, &back), "and a longer one");
        int bad = 0;
        for (size_t i = 10; i < n; ++i) { uint8_t t[NET_DISC_ASK_SIZE]; memcpy(t, buf, n); t[i] = 1; if (NetDisc_DecodeAsk(t, n, &back)) bad = 1; }
        Check(!bad, "a non-zero byte anywhere in the padding is refused");
        uint8_t t[NET_DISC_ASK_SIZE];
        memcpy(t, buf, n); t[0] ^= 1;
        Check(!NetDisc_DecodeAsk(t, n, &back), "the wrong magic is refused");
        memcpy(t, buf, n); t[4] = NET_DISC_VERSION + 1;
        Check(!NetDisc_DecodeAsk(t, n, &back), "another version is refused");
        memcpy(t, buf, n); t[5] = 2;
        Check(!NetDisc_DecodeAsk(t, n, &back), "an answer is not a question");
    }

    printf("\n-- the answer\n");
    {
        uint8_t buf[NET_DISC_ASK_SIZE];
        NetDiscAnnounce a = Ann("steve's tower", 7777, 2, 4), back;
        size_t n = NetDisc_EncodeAnnounce(&a, buf, sizeof buf);
        Check(n == 22 + strlen(a.name), "its length is the fixed part plus the name");
        Check(NetDisc_DecodeAnnounce(buf, n, &back) && !memcmp(&back, &a, sizeof a), "it decodes to exactly what was encoded");
        a = Ann("123456789012345678901234", 1, 4, 4);
        n = NetDisc_EncodeAnnounce(&a, buf, sizeof buf);
        Check(n > 0 && n <= NET_DISC_ASK_SIZE && NetDisc_DecodeAnnounce(buf, n, &back), "the longest name fits, and the longest answer is not longer than a question");
        Check(n <= NET_DISC_ASK_SIZE, "nobody can use a host to send more than they sent it");
        a = Ann("ok", 7777, 1, 4);
        Check(NetDisc_EncodeAnnounce(&a, buf, 10) == 0, "a buffer too small gives nothing");
        a = Ann("", 7777, 1, 4);
        Check(NetDisc_EncodeAnnounce(&a, buf, sizeof buf) == 0, "an empty name is not sent");
        {
            NetDiscAnnounce *endless = (NetDiscAnnounce *)malloc(sizeof *endless);
            memset(endless, 'x', sizeof *endless);                                   // every byte, the field and the padding after it, is 'x': no end anywhere
            endless->port = 7777; endless->players = 1; endless->maxPlayers = 4;
            Check(NetDisc_EncodeAnnounce(endless, buf, sizeof buf) == 0, "a name with no end inside its field is not sent, and not read past (a sanitizer build shows the difference)");
            free(endless);
        }
        a = Ann("tab\there", 7777, 1, 4);
        Check(NetDisc_EncodeAnnounce(&a, buf, sizeof buf) == 0, "a name with a control character is not sent");
        a = Ann("caf\xc3\xa9", 7777, 1, 4);
        Check(NetDisc_EncodeAnnounce(&a, buf, sizeof buf) == 0, "a name that is not plain ASCII is not sent");
        a = Ann("ok", 0, 1, 4);
        Check(NetDisc_EncodeAnnounce(&a, buf, sizeof buf) == 0, "port 0 is not sent");
        a = Ann("ok", 7777, 5, 4);
        Check(NetDisc_EncodeAnnounce(&a, buf, sizeof buf) == 0, "more players than places is not sent");
        a = Ann("ok", 7777, 0, NET_MAX_PLAYERS + 1);
        Check(NetDisc_EncodeAnnounce(&a, buf, sizeof buf) == 0, "more places than the game has is not sent");
        a = Ann("ok", 7777, 0, 0);
        Check(NetDisc_EncodeAnnounce(&a, buf, sizeof buf) == 0, "no places at all is not sent");

        // every cut and every changed byte of a good packet: refused, or if accepted, then it encodes back to the very same bytes
        a = Ann("hello", 7777, 2, 4);
        n = NetDisc_EncodeAnnounce(&a, buf, sizeof buf);
        int cutOk = 1, flipOk = 1;
        for (size_t cut = 0; cut < n; ++cut) if (NetDisc_DecodeAnnounce(buf, cut, &back)) cutOk = 0;
        for (size_t i = 0; i < n; ++i)
            for (int bit = 0; bit < 8; ++bit) {
                uint8_t t[NET_DISC_ASK_SIZE], again[NET_DISC_ASK_SIZE];
                memcpy(t, buf, n); t[i] ^= (uint8_t)(1 << bit);
                if (NetDisc_DecodeAnnounce(t, n, &back)) { const size_t m = NetDisc_EncodeAnnounce(&back, again, sizeof again); if (m != n || memcmp(again, t, n)) flipOk = 0; }
            }
        Check(cutOk, "every shorter length of an answer is refused");
        Check(flipOk, "no single changed bit gets through unless it still means exactly what its bytes say (decode then encode gives the same bytes)");
        uint8_t t[NET_DISC_ASK_SIZE];
        memcpy(t, buf, n); t[n] = 0;
        Check(!NetDisc_DecodeAnnounce(t, n + 1, &back), "a byte left over after the name is refused");
        memcpy(t, buf, n); t[21] = 4;                                              // the name length says 4 but 5 bytes follow
        Check(!NetDisc_DecodeAnnounce(t, n, &back), "a name length that does not match the packet is refused");
        memcpy(t, buf, n); t[23] = 0;                                              // a zero byte inside the name
        Check(!NetDisc_DecodeAnnounce(t, n, &back), "a zero byte inside the name is refused");
        memcpy(t, buf, n); t[12] = 0; t[13] = 0;
        Check(!NetDisc_DecodeAnnounce(t, n, &back), "a packet that says port 0 is refused (checked on the way in, not only on the way out)");
        memcpy(t, buf, n); t[14] = 0; t[15] = 0;
        Check(!NetDisc_DecodeAnnounce(t, n, &back), "a packet that says no places is refused");
        memcpy(t, buf, n); t[15] = NET_MAX_PLAYERS + 1;
        Check(!NetDisc_DecodeAnnounce(t, n, &back), "a packet that says more places than the game has is refused");
        memcpy(t, buf, n); t[14] = 3; t[15] = 2;
        Check(!NetDisc_DecodeAnnounce(t, n, &back), "a packet that says more players than places is refused");
        {   // the transport the host speaks travels in the answer
            NetDiscAnnounce q = Ann("quic game", 7777, 1, 4);
            q.transport = NET_TRANSPORT_QUIC;
            uint8_t qb[NET_DISC_ASK_SIZE];
            const size_t qn = NetDisc_EncodeAnnounce(&q, qb, sizeof qb);
            Check(qn > 0 && NetDisc_DecodeAnnounce(qb, qn, &back) && back.transport == NET_TRANSPORT_QUIC, "the transport (QUIC) travels in the answer");
            q.transport = 9;                                                           // a transport this build has never heard of must still be listed
            Check(NetDisc_EncodeAnnounce(&q, qb, sizeof qb) == qn && NetDisc_DecodeAnnounce(qb, qn, &back) && back.transport == 9, "an unknown transport still decodes, so it can be listed as one this build cannot join");
        }
        NetDiscAsk askOnly = { 5 };
        uint8_t q[NET_DISC_ASK_SIZE];
        NetDisc_EncodeAsk(&askOnly, q, sizeof q);
        Check(!NetDisc_DecodeAnnounce(q, sizeof q, &back), "a question is not an answer");

        // random and spoilt packets: nothing crashes, and what is accepted is canonical
        int fuzzOk = 1;
        for (int i = 0; i < 300000; ++i) {
            uint8_t r[80];
            size_t len = Rnd() % 80;
            for (size_t k = 0; k < len; ++k) r[k] = (uint8_t)Rnd();
            if (i % 3 == 0 && len >= 6) { r[0] = 'T'; r[1] = 'W'; r[2] = 'D'; r[3] = '1'; r[4] = NET_DISC_VERSION; r[5] = (uint8_t)(1 + Rnd() % 2); }
            if (i % 3 == 1 && n >= 6) { memcpy(r, buf, n); len = n; r[Rnd() % n] = (uint8_t)Rnd(); }
            NetDiscAnnounce an; NetDiscAsk ak; uint8_t again[80];
            if (NetDisc_DecodeAnnounce(r, len, &an)) { const size_t m = NetDisc_EncodeAnnounce(&an, again, sizeof again); if (m != len || memcmp(again, r, len)) fuzzOk = 0; }
            if (NetDisc_DecodeAsk(r, len, &ak)) { const size_t m = NetDisc_EncodeAsk(&ak, again, sizeof again); if (m != len || memcmp(again, r, len)) fuzzOk = 0; }
        }
        Check(fuzzOk, "300000 random and spoilt packets: nothing crashes, and whatever is accepted is canonical");
    }

    printf("\n-- the lobbies a searcher has found\n");
    {
        NetLobbies t;
        NetLobbies_Init(&t);
        NetDiscAnnounce a = Ann("one", 7777, 1, 4);
        Check(NetLobbies_Update(&t, IP(192, 168, 1, 5), &a, 1000.0) == NETLOBBY_NEW && NetLobbies_Count(&t) == 1, "an answer from a new place is NEW");
        Check(NetLobbies_Update(&t, IP(192, 168, 1, 5), &a, 3000.0) == NETLOBBY_SAME && NetLobbies_Count(&t) == 1, "the same answer again is SAME, and there is still one lobby");
        a.players = 3;
        Check(NetLobbies_Update(&t, IP(192, 168, 1, 5), &a, 3100.0) == NETLOBBY_CHANGED, "a changed player count is CHANGED (the Unity package never reported it)");
        a.hostId = 99;
        Check(NetLobbies_Update(&t, IP(192, 168, 1, 5), &a, 3200.0) == NETLOBBY_CHANGED, "a restarted host (new host id) is CHANGED, not a second lobby");
        a = Ann("one", 7777, 3, 4);
        a.hostId = 99;
        strcpy(a.name, "renamed");
        Check(NetLobbies_Update(&t, IP(192, 168, 1, 5), &a, 3300.0) == NETLOBBY_CHANGED && !strcmp(NetLobbies_Nth(&t, 0)->info.name, "renamed"), "a new name is CHANGED and shown");
        NetDiscAnnounce b = Ann("two", 7777, 0, 4);
        b.hostId = 78;
        Check(NetLobbies_Update(&t, IP(192, 168, 1, 9), &b, 3400.0) == NETLOBBY_NEW && NetLobbies_Count(&t) == 2, "the same port on another address is another lobby");
        NetDiscAnnounce c = Ann("three", 7780, 0, 4);
        c.hostId = 79;
        Check(NetLobbies_Update(&t, IP(192, 168, 1, 5), &c, 3500.0) == NETLOBBY_NEW && NetLobbies_Count(&t) == 3, "another port on the same address is another lobby");
        Check(NetLobbies_Nth(&t, 0)->info.port == 7777 && NetLobbies_Nth(&t, 0)->addr == IP(192, 168, 1, 5) && !strcmp(NetLobbies_Nth(&t, 1)->info.name, "two") && !strcmp(NetLobbies_Nth(&t, 2)->info.name, "three"),
              "they are listed in the order they were first heard");
        Check(NetLobbies_Nth(&t, 3) == NULL && NetLobbies_Nth(&t, 100) == NULL, "past the end there is nothing");
        // the transport
        {
            Check(NetDisc_TransportSupported(NET_TRANSPORT_UDP) == (TOWER_NET_UDP != 0), "plain UDP is supported wherever the platform has UDP");
            Check(NetDisc_TransportSupported(NET_TRANSPORT_QUIC) == (TOWER_MIDNET != 0), "QUIC is supported only in a build that linked mid-net");
            Check(!NetDisc_TransportSupported(7) && !NetDisc_TransportSupported(255), "a transport this build has never heard of is not supported");
            NetLobbies tr;
            NetLobbies_Init(&tr);
            NetDiscAnnounce u = Ann("udp", 7777, 0, 4), qk = Ann("quic", 7777, 0, 4), odd = Ann("odd", 7777, 0, 4);
            u.hostId = 11; u.transport = NET_TRANSPORT_UDP;
            qk.hostId = 12; qk.transport = NET_TRANSPORT_QUIC;
            odd.hostId = 13; odd.transport = 7;
            NetLobbies_Update(&tr, IP(10, 0, 0, 1), &odd, 0.0);
            NetLobbies_Update(&tr, IP(10, 0, 0, 2), &qk, 0.0);
            NetLobbies_Update(&tr, IP(10, 0, 0, 3), &u, 0.0);
            Check(!NetLobbies_Nth(&tr, 0)->transportSupported && NetLobbies_Nth(&tr, 1)->transportSupported == (TOWER_MIDNET != 0) && NetLobbies_Nth(&tr, 2)->transportSupported == (TOWER_NET_UDP != 0),
                  "each lobby is marked with whether this build can connect the way it wants");
            const NetLobby *jf = NetLobbies_FirstJoinable(&tr);
            Check((TOWER_NET_UDP && TOWER_MIDNET) ? (jf && !strcmp(jf->info.name, "quic")) : TOWER_NET_UDP ? (jf && !strcmp(jf->info.name, "udp")) : jf == NULL,
                  "the first joinable skips a lobby this build cannot connect to (and one of a transport nobody has heard of)");
            qk.transport = NET_TRANSPORT_UDP;
            Check(NetLobbies_Update(&tr, IP(10, 0, 0, 2), &qk, 1.0) == NETLOBBY_CHANGED, "a host that changes transport is CHANGED");
            Check(NetLobbies_Nth(&tr, 1)->info.transport == NET_TRANSPORT_UDP && NetLobbies_Nth(&tr, 1)->transportSupported == (TOWER_NET_UDP != 0), "and its supported flag follows the new transport, not the old one");
        }
        // one host, two of its addresses
        {
            NetLobbies m;
            NetLobbies_Init(&m);
            NetDiscAnnounce h = Ann("multi", 7777, 1, 4);
            h.hostId = 500;
            NetLobbies_Update(&m, IP(192, 168, 1, 7), &h, 0.0);
            const NetLobbyResult r2 = NetLobbies_Update(&m, IP(192, 168, 43, 1), &h, 10.0);
            Check(r2 == NETLOBBY_SAME && NetLobbies_Count(&m) == 1 && NetLobbies_Nth(&m, 0)->addr == IP(192, 168, 1, 7) && NetLobbies_Nth(&m, 0)->lastSeenMs == 10.0,
                  "one host answering from two of its addresses (Wi-Fi and hotspot) is ONE lobby, kept at the first address and kept alive by either");
            NetDiscAnnounce other = Ann("multi", 7777, 1, 4);
            other.hostId = 501;
            Check(NetLobbies_Update(&m, IP(192, 168, 1, 8), &other, 20.0) == NETLOBBY_NEW && NetLobbies_Count(&m) == 2, "a different host (another id, another address) with the same port is a second lobby");
        }
        // joinable
        NetLobbies u;
        NetLobbies_Init(&u);
        NetDiscAnnounce full = Ann("full", 7777, 4, 4), old = Ann("old", 7777, 0, 4), good = Ann("good", 7777, 1, 4);
        full.hostId = 601; old.hostId = 602; good.hostId = 603;                    // three different hosts
        old.protocol = NET_PROTOCOL_VERSION + 1;
        NetLobbies_Update(&u, IP(10, 0, 0, 1), &full, 0.0);
        NetLobbies_Update(&u, IP(10, 0, 0, 2), &old, 0.0);
        Check(NetLobbies_FirstJoinable(&u) == NULL, "a full lobby and one of another version are not joinable");
        Check(!NetLobbies_Nth(&u, 1)->compatible && NetLobbies_Nth(&u, 0)->compatible, "the other version is kept in the list but marked incompatible, so a player can be told to update");
        NetLobbies_Update(&u, IP(10, 0, 0, 3), &good, 0.0);
        Check(NetLobbies_FirstJoinable(&u) && !strcmp(NetLobbies_FirstJoinable(&u)->info.name, "good"), "the first joinable skips them");
        // order survives a freed place being reused by a newer lobby
        {
            NetLobbies o;
            NetLobbies_Init(&o);
            NetDiscAnnounce la = Ann("A", 7001, 0, 4), lb = Ann("B", 7002, 0, 4), lc = Ann("C", 7003, 0, 4), ld = Ann("D", 7004, 0, 4);
            la.hostId = 1; lb.hostId = 2; lc.hostId = 3; ld.hostId = 4;
            NetLobbies_Update(&o, IP(10, 0, 0, 1), &la, 0.0);
            NetLobbies_Update(&o, IP(10, 0, 0, 2), &lb, 4000.0);
            NetLobbies_Update(&o, IP(10, 0, 0, 3), &lc, 4000.0);
            NetLobbies_Expire(&o, 6000.0, 5000.0);                                  // A is gone, its place is free
            NetLobbies_Update(&o, IP(10, 0, 0, 4), &ld, 6000.0);                    // D takes that place
            Check(NetLobbies_Count(&o) == 3 && !strcmp(NetLobbies_Nth(&o, 0)->info.name, "B") && !strcmp(NetLobbies_Nth(&o, 1)->info.name, "C") && !strcmp(NetLobbies_Nth(&o, 2)->info.name, "D"),
                  "the order is the order of first hearing even when a newer lobby takes an older one's freed place");
        }
        // time
        NetLobbies_Update(&t, IP(192, 168, 1, 9), &b, 9000.0);                    // two was heard again at 9 s
        Check(NetLobbies_Expire(&t, 9000.0, 5000.0) == 2 && NetLobbies_Count(&t) == 1, "what has not been heard for 5 s is dropped, what was heard again stays");
        Check(!strcmp(NetLobbies_Nth(&t, 0)->info.name, "two"), "and the survivor is the one heard again");
        Check(NetLobbies_Expire(&t, 9000.0 + 5000.0, 5000.0) == 0 && NetLobbies_Expire(&t, 14001.0, 5000.0) == 1, "exactly 5 s is not yet gone; a moment more is");
        // a full table
        NetLobbies f;
        NetLobbies_Init(&f);
        int allNew = 1;
        for (int i = 0; i < NET_DISC_MAX_LOBBIES; ++i) { NetDiscAnnounce x = Ann("x", (uint16_t)(7000 + i), 0, 4); if (NetLobbies_Update(&f, IP(10, 1, 1, 1), &x, 0.0) != NETLOBBY_NEW) allNew = 0; }
        Check(allNew && NetLobbies_Count(&f) == NET_DISC_MAX_LOBBIES, "the table takes NET_DISC_MAX_LOBBIES lobbies");
        NetDiscAnnounce extra = Ann("extra", 9000, 0, 4);
        Check(NetLobbies_Update(&f, IP(10, 1, 1, 1), &extra, 0.0) == NETLOBBY_IGNORED && NetLobbies_Count(&f) == NET_DISC_MAX_LOBBIES, "one more is ignored and nothing is overwritten");
        NetLobbies_Expire(&f, 6000.0, 5000.0);
        Check(NetLobbies_Update(&f, IP(10, 1, 1, 1), &extra, 6000.0) == NETLOBBY_NEW, "once the table has emptied there is room again");
    }

    printf("\n-- addresses\n");
    {
        Check(NetDisc_DirectedBroadcast(IP(192, 168, 1, 37), IP(255, 255, 255, 0)) == IP(192, 168, 1, 255), "192.168.1.37 /24: the broadcast address is 192.168.1.255");
        Check(NetDisc_DirectedBroadcast(IP(172, 20, 10, 2), IP(255, 255, 255, 240)) == IP(172, 20, 10, 15), "172.20.10.2 /28 (an iPhone hotspot): 172.20.10.15");
        Check(NetDisc_DirectedBroadcast(IP(10, 3, 4, 5), IP(255, 255, 0, 0)) == IP(10, 3, 255, 255), "10.3.4.5 /16: 10.3.255.255");
        Check(NetDisc_DirectedBroadcast(IP(192, 168, 43, 1), IP(255, 255, 255, 252)) == IP(192, 168, 43, 3), "a /30: the last address of its four");
        Check(NetDisc_IsHotspotRange(IP(192, 168, 43, 1)) && NetDisc_IsHotspotRange(IP(192, 168, 43, 254)), "192.168.43.x is a hotspot (Android)");
        Check(NetDisc_IsHotspotRange(IP(192, 168, 49, 7)) && NetDisc_IsHotspotRange(IP(172, 20, 10, 3)) && NetDisc_IsHotspotRange(IP(192, 168, 137, 1)), "so are 192.168.49.x (Wi-Fi Direct), 172.20.10.x (iOS) and 192.168.137.x (Windows)");
        Check(!NetDisc_IsHotspotRange(IP(192, 168, 42, 1)) && !NetDisc_IsHotspotRange(IP(192, 168, 44, 1)) && !NetDisc_IsHotspotRange(IP(172, 20, 11, 1)) && !NetDisc_IsHotspotRange(IP(172, 20, 9, 1)) && !NetDisc_IsHotspotRange(IP(192, 168, 1, 5)) && !NetDisc_IsHotspotRange(IP(10, 0, 0, 2)),
              "the neighbouring subnets and ordinary home addresses are not");
        const char *usable[] = { "wlan0", "swlan0", "eth0", "en0", "ap0", "p2p-wlan0-0", "rndis0", "wlp2s0" };
        const char *unusable[] = { "lo", "lo0", "rmnet_data0", "ccmni1", "clat4", "v4-rmnet_data0", "tun0", "dummy0", "ppp0", "wwan0", "docker0", "veth12", "" };
        int ok = 1;
        for (size_t i = 0; i < sizeof usable / sizeof usable[0]; ++i) if (!NetDisc_IfaceNameUsable(usable[i])) ok = 0;
        Check(ok, "Wi-Fi, Ethernet, hotspot, Wi-Fi Direct and USB tethering interfaces can carry a game");
        ok = 1;
        for (size_t i = 0; i < sizeof unusable / sizeof unusable[0]; ++i) if (NetDisc_IfaceNameUsable(unusable[i])) ok = 0;
        Check(ok && !NetDisc_IfaceNameUsable(NULL) && NetDisc_IfaceNameUsable("local0"), "the loopback, mobile data, tunnels and the like cannot (and a name that only starts with 'lo' can)");

        NetDiscIface ifs[6];
        memset(ifs, 0, sizeof ifs);
        uint32_t out[6];
        // a phone on Wi-Fi that also runs data and a hotspot
        strcpy(ifs[0].name, "rmnet_data0"); ifs[0].ip = IP(10, 88, 12, 4); ifs[0].up = true;
        strcpy(ifs[1].name, "swlan0");      ifs[1].ip = IP(192, 168, 43, 1); ifs[1].up = true;
        strcpy(ifs[2].name, "wlan0");       ifs[2].ip = IP(192, 168, 1, 23); ifs[2].up = true;
        strcpy(ifs[3].name, "lo");          ifs[3].ip = IP(127, 0, 0, 1); ifs[3].up = true; ifs[3].loopback = true;
        int n = NetDisc_RankHostAddresses(ifs, 4, out, 6);
        Check(n == 2 && out[0] == IP(192, 168, 1, 23) && out[1] == IP(192, 168, 43, 1), "ordinary Wi-Fi first, then the hotspot; mobile data and the loopback are left out");
        n = NetDisc_RankHostAddresses(ifs + 1, 1, out, 6);
        Check(n == 1 && out[0] == IP(192, 168, 43, 1), "with only a hotspot, the hotspot's address");
        n = NetDisc_RankHostAddresses(ifs, 1, out, 6);
        Check(n == 0, "with only mobile data there is no address to give (not a carrier address nobody can reach)");
        ifs[2].up = false;
        n = NetDisc_RankHostAddresses(ifs, 4, out, 6);
        Check(n == 1 && out[0] == IP(192, 168, 43, 1), "an interface that is down is left out");
        ifs[2].up = true; ifs[2].ip = IP(169, 254, 7, 9);
        n = NetDisc_RankHostAddresses(ifs, 4, out, 6);
        Check(n == 1 && out[0] == IP(192, 168, 43, 1), "a 169.254.x.x address (no DHCP answered) is left out");
        strcpy(ifs[4].name, "eth0"); ifs[4].ip = IP(10, 0, 0, 7); ifs[4].up = true;
        strcpy(ifs[5].name, "wlan1"); ifs[5].ip = IP(192, 168, 5, 5); ifs[5].up = true;
        n = NetDisc_RankHostAddresses(ifs + 4, 2, out, 6);
        Check(n == 2 && out[0] == IP(10, 0, 0, 7) && out[1] == IP(192, 168, 5, 5), "several ordinary addresses keep the order they were given");
        n = NetDisc_RankHostAddresses(ifs + 4, 2, out, 1);
        Check(n == 1, "and no more than asked for");
        Check(NetDisc_RankHostAddresses(ifs, 0, out, 6) == 0, "an empty list gives nothing");

        char s[16];
        NetDisc_FormatIp(IP(192, 168, 1, 5), s);
        Check(!strcmp(s, "192.168.1.5"), "192.168.1.5 is written as such");
        NetDisc_FormatIp(0, s); int z = !strcmp(s, "0.0.0.0");
        NetDisc_FormatIp(0xffffffffu, s);
        Check(z && !strcmp(s, "255.255.255.255"), "0.0.0.0 and 255.255.255.255 too");
        uint32_t ip = 0;
        Check(NetDisc_ParseIp("192.168.1.5", &ip) && ip == IP(192, 168, 1, 5) && NetDisc_ParseIp("0.0.0.0", &ip) && ip == 0 && NetDisc_ParseIp("255.255.255.255", &ip) && ip == 0xffffffffu, "a dotted address is read");
        const char *bad[] = { "", "1.2.3", "1.2.3.4.5", "256.1.1.1", "01.2.3.4", "1..2.3", "a.b.c.d", "1.2.3.4 ", " 1.2.3.4", "1.2.3.", ".1.2.3", "1.2.3.-4", "1234.1.1.1", "localhost" };
        ok = 1;
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) if (NetDisc_ParseIp(bad[i], &ip)) ok = 0;
        Check(ok && !NetDisc_ParseIp(NULL, &ip), "anything else is refused (no names, no octal, no spaces)");
        int rt = 1;
        for (int i = 0; i < 2000; ++i) { const uint32_t v = Rnd(); NetDisc_FormatIp(v, s); uint32_t w = 0; if (!NetDisc_ParseIp(s, &w) || w != v) rt = 0; }
        Check(rt, "2000 random addresses written and read back are the same");
    }

    printf("\n-- this machine's network\n");
    {
        NetDiscIface list[16];
        const int n = NetDisc_ListInterfaces(list, 16);
        int ok = n >= 0 && n <= 16;
        for (int i = 0; i < n; ++i) if (!list[i].up || list[i].loopback || !NetDisc_IfaceNameUsable(list[i].name) || list[i].mask == 0 || list[i].ip == 0) ok = 0;
        Check(ok, "the interface list holds only up, non-loopback, usable entries with an address and a mask");
        printf("  (this machine reports %d usable interface%s)\n", n, n == 1 ? "" : "s");
    }

    printf("\n-- real UDP on the loopback\n");
    {
        NetDiscHost host;
        NetDiscClient cl;
        Check(NetDiscHost_Open(&host, 0) && host.open && NetDiscHost_Port(&host) != 0, "a host takes a free port");
        const uint16_t dport = NetDiscHost_Port(&host);
        NetDiscHost second;
        Check(!NetDiscHost_Open(&second, dport), "a second host on the same port fails: the port is not shared, so nobody is answered twice");
        NetDiscHost_SetInfo(&host, "steve's tower", 7777, 1, 4, NET_TRANSPORT_UDP);
        Check(NetDiscClient_Open(&cl, dport) && cl.open, "a searcher opens on a port of its own (so it can run next to a host)");

        double now = 100000.0;
        const int sentTo = NetDiscClient_Search(&cl, now);
        Check(sentTo >= 1, "a round of questions goes out (at least to this machine itself)");
        Check(NetDiscClient_Search(&cl, now + 500.0) == 0 && NetDiscClient_Search(&cl, now + 1999.0) == 0, "and not again before the interval has passed");
        SleepMs(30);
        const int answered = NetDiscHost_Poll(&host, now);
        Check(answered >= 1, "the host answers");
        SleepMs(30);
        const int changed = NetDiscClient_Poll(&cl, now);
        Check(changed == 1 && NetLobbies_Count(&cl.lobbies) == 1, "the searcher lists exactly one lobby (the same host reached by several addresses is not listed twice)");
        const NetLobby *l = NetLobbies_Nth(&cl.lobbies, 0);
        Check(l && !strcmp(l->info.name, "steve's tower") && l->info.port == 7777 && l->info.players == 1 && l->info.maxPlayers == 4 && l->compatible && l->info.transport == NET_TRANSPORT_UDP, "with its name, game port, places, version and transport");
        NetDiscIface mine[16];
        const int nm = NetDisc_ListInterfaces(mine, 16);
        int fromHere = l && l->addr == IP(127, 0, 0, 1);
        for (int i = 0; i < nm; ++i) if (l && l->addr == mine[i].ip) fromHere = 1;
        Check(fromHere, "and the address it ANSWERED from (this machine's own: the loopback or one of its interfaces), not one the host claims");
        Check(NetLobbies_FirstJoinable(&cl.lobbies) == l, "it can be joined");

        NetDiscHost_SetInfo(&host, "steve's tower", 7777, 3, 4, NET_TRANSPORT_UDP);
        now += 2100.0;
        NetDiscClient_Search(&cl, now);
        SleepMs(30); NetDiscHost_Poll(&host, now); SleepMs(30);
        Check(NetDiscClient_Poll(&cl, now) == 1 && NetLobbies_Nth(&cl.lobbies, 0)->info.players == 3, "when a player joins, the next round shows 3 of 4");

        // a host that speaks QUIC says so, and the searcher knows whether this build can join it
        NetDiscHost_SetInfo(&host, "steve's tower", 7777, 3, 4, NET_TRANSPORT_QUIC);
        now += 2100.0;
        NetDiscClient_Search(&cl, now);
        SleepMs(30); NetDiscHost_Poll(&host, now); SleepMs(30);
        NetDiscClient_Poll(&cl, now);
        Check(NetLobbies_Nth(&cl.lobbies, 0)->info.transport == NET_TRANSPORT_QUIC && NetLobbies_Nth(&cl.lobbies, 0)->transportSupported == (TOWER_MIDNET != 0),
              "a host that announces QUIC is listed as QUIC, joinable only in a build that has it");
        NetDiscHost_SetInfo(&host, "steve's tower", 7777, 3, 4, NET_TRANSPORT_UDP);
        now += 2100.0;
        NetDiscClient_Search(&cl, now);
        SleepMs(30); NetDiscHost_Poll(&host, now); SleepMs(30);
        NetDiscClient_Poll(&cl, now);
        // strangers
        const int raw = RawSocket();
        uint8_t reply[200];
        const unsigned ignoredBefore = host.ignored;
        const uint8_t junk[5] = { 1, 2, 3, 4, 5 };
        RawSend(raw, dport, junk, sizeof junk);
        uint8_t shortAsk[NET_DISC_ASK_SIZE - 1];
        NetDiscAsk ask = { 42 };
        uint8_t full[NET_DISC_ASK_SIZE];
        NetDisc_EncodeAsk(&ask, full, sizeof full);
        memcpy(shortAsk, full, sizeof shortAsk);
        RawSend(raw, dport, shortAsk, sizeof shortAsk);
        uint8_t dirty[NET_DISC_ASK_SIZE];
        memcpy(dirty, full, sizeof dirty); dirty[40] = 9;
        RawSend(raw, dport, dirty, sizeof dirty);
        uint8_t an[NET_DISC_ASK_SIZE];
        NetDiscAnnounce fake = Ann("fake", 1234, 0, 4);
        const size_t fl = NetDisc_EncodeAnnounce(&fake, an, sizeof an);
        RawSend(raw, dport, an, fl);
        uint8_t zeros[1] = { 0 };
        RawSend(raw, dport, zeros, 0);
        SleepMs(30);
        const int rs = NetDiscHost_Poll(&host, now);
        Check(rs == 0 && RawRecv(raw, reply, sizeof reply, 60) < 0, "junk, a short question, a question with dirty padding, an answer, an empty packet: nobody is answered");
        Check(host.ignored - ignoredBefore == 5, "and the host counted them as ignored");

        // a proper question from a stranger is answered, and the answer is no longer than the question
        RawSend(raw, dport, full, sizeof full);
        SleepMs(30);
        NetDiscHost_Poll(&host, now);
        const int rl = RawRecv(raw, reply, sizeof reply, 200);
        NetDiscAnnounce got;
        Check(rl > 0 && (size_t)rl <= sizeof full && NetDisc_DecodeAnnounce(reply, (size_t)rl, &got) && got.nonce == 42 && got.hostId == host.hostId, "a proper question is answered with the asker's own nonce, in no more bytes than it sent");

        // the searcher takes only answers to its own question
        struct sockaddr_in cs; socklen_t cl2 = sizeof cs;
        getsockname(cl.fd, (struct sockaddr *)&cs, &cl2);
        const uint16_t searcherPort = ntohs(cs.sin_port);
        NetDiscAnnounce wrong = Ann("intruder", 5555, 0, 4);
        wrong.nonce = cl.nonce + 1;
        uint8_t wb[NET_DISC_ASK_SIZE];
        const size_t wl = NetDisc_EncodeAnnounce(&wrong, wb, sizeof wb);
        RawSend(raw, searcherPort, wb, wl);
        RawSend(raw, searcherPort, junk, sizeof junk);
        SleepMs(30);
        const unsigned ignoredC = cl.ignored;
        NetDiscClient_Poll(&cl, now);
        Check(NetLobbies_Count(&cl.lobbies) == 1 && cl.ignored - ignoredC == 2, "an answer to somebody else's question, and junk, never get onto the list");

        // a flood
        const unsigned repliesBefore = host.replies;
        for (int i = 0; i < 200; ++i) RawSend(raw, dport, full, sizeof full);
        SleepMs(60);
        NetDiscHost_Poll(&host, now + 1.0);
        NetDiscHost_Poll(&host, now + 1.0);
        NetDiscHost_Poll(&host, now + 1.0);
        NetDiscHost_Poll(&host, now + 1.0);
        const unsigned flooded = host.replies - repliesBefore;
        Check(flooded <= (unsigned)NET_DISC_REPLY_BURST + 2 && host.throttled > 0, "200 questions in an instant: a burst is answered, the rest is throttled (and counted)");
        while (RawRecv(raw, reply, sizeof reply, 0) >= 0) {}
        SleepMs(30);
        NetDiscHost_Poll(&host, now + 5000.0);                                    // five seconds later the allowance has come back
        RawSend(raw, dport, full, sizeof full);
        SleepMs(30);
        NetDiscHost_Poll(&host, now + 5000.0);
        Check(RawRecv(raw, reply, sizeof reply, 200) > 0, "and later the host answers again");

        // a probe straight at an address
        NetDiscClient other;
        NetDiscClient_Open(&other, dport);
        Check(!NetDiscClient_Probe(&other, "not an address", dport) && !NetDiscClient_Probe(&other, "127.0.0.1", 0), "a probe needs a real address and port");
        Check(NetDiscClient_Probe(&other, "127.0.0.1", dport), "a probe goes straight to an address (across subnets, where broadcasts are filtered)");
        SleepMs(30); NetDiscHost_Poll(&host, now + 5001.0); SleepMs(30);
        Check(NetDiscClient_Poll(&other, now + 5001.0) == 1 && NetLobbies_Count(&other.lobbies) == 1, "and its answer is listed");
        NetDiscClient_Close(&other);

        // a host that goes quiet
        NetDiscClient_Poll(&cl, now + 3000.0);
        Check(NetLobbies_Count(&cl.lobbies) == 1, "a lobby not yet 5 s old is still listed");
        Check(NetDiscClient_Poll(&cl, now + 2100.0 + NET_DISC_TIMEOUT_MS + 100.0) == 0 && NetLobbies_Count(&cl.lobbies) == 0, "a host that stops answering is gone from the list after the timeout");

        // closing frees the port
        close(raw);
        NetDiscClient_Close(&cl);
        NetDiscHost_Close(&host);
        Check(!host.open && !cl.open, "both close");
        NetDiscHost again;
        Check(NetDiscHost_Open(&again, dport), "and the port is free again");
        NetDiscHost_Close(&again);
        Check(NetDiscHost_Poll(&host, 0.0) == 0 && NetDiscClient_Poll(&cl, 0.0) == 0 && NetDiscClient_Search(&cl, 0.0) == 0, "a closed host or searcher does nothing and does not crash");

        // an unset host answers nobody
        NetDiscHost mute;
        NetDiscHost_Open(&mute, 0);
        const int r2 = RawSocket();
        RawSend(r2, NetDiscHost_Port(&mute), full, sizeof full);
        SleepMs(30);
        Check(NetDiscHost_Poll(&mute, 0.0) == 0 && RawRecv(r2, reply, sizeof reply, 60) < 0, "a host that has not said what it announces (no SetInfo) answers nobody");
        NetDiscHost_SetInfo(&mute, "bad\tname\x7f\xff-and-a-very-long-name-indeed", 7777, 9, 99, NET_TRANSPORT_UDP);
        Check(NetDisc_NameOk(mute.info.name) && strlen(mute.info.name) == NET_DISC_NAME_MAX && mute.info.maxPlayers == NET_MAX_PLAYERS && mute.info.players == NET_MAX_PLAYERS,
              "SetInfo makes any name safe (unprintable becomes '?', cut at 24) and clamps the numbers");
        NetDiscHost_SetInfo(&mute, "", 7777, -3, 0, NET_TRANSPORT_UDP);
        Check(!strcmp(mute.info.name, "tower") && mute.info.players == 0 && mute.info.maxPlayers == 1, "an empty name becomes \"tower\"");
        NetDiscHost_Close(&mute);
        close(r2);
    }

    printf("\n%s (%d failure%s of %d checks)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s", g_checks);
    return g_failures ? 1 : 0;
}
#endif
