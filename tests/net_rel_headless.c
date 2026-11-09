// Headless test of the reliable-message layer: whole messages, in order, exactly once, over a path that loses, doubles and reorders
// packets; both directions at once; the sequence numbers going round their end; the round trip it measures; the window; giving up on a
// dead path; and what it does with packets that are not its own. Two ends of the layer are joined by the pretend network's datagrams.
// No window, no raylib, no sockets. Run from the project root:
//   cc -O2 -std=c99 -DNET_REL_HEADLESS_TEST -Isrc tests/net_rel_headless.c src/net/net_reliable.c src/net/net_loopback.c -lm -o net_rel_headless
#ifdef NET_REL_HEADLESS_TEST

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/net_loopback.h"
#include "net/net_reliable.h"

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

// ---- two ends over one pretend link ----------------------------------------------------------------------------------------
typedef struct { NetRel rel; NetTransport tp; } End;
static NetLoopback g_link;
static End g_a, g_b;

static void Emit(void *user, const uint8_t *pkt, size_t len) { End *e = (End *)user; e->tp.sendDatagram(e->tp.self, pkt, len); }

// For the test of one lost message: each end's output is kept in a list instead of being sent, so the test decides which packets get through.
static uint8_t aOut[8][600], bOut[8][600];
static size_t aLen[8], bLen[8];
static int aN, bN;
static void CollectA(void *u, const uint8_t *p, size_t n) { (void)u; if (aN < 8) { memcpy(aOut[aN], p, n); aLen[aN] = n; ++aN; } }
static void CollectB(void *u, const uint8_t *p, size_t n) { (void)u; if (bN < 8) { memcpy(bOut[bN], p, n); bLen[bN] = n; ++bN; } }

static void Setup(NetLinkCfg cfg, uint32_t seed)
{
    NetLoopback_Init(&g_link, seed);
    NetLoopback_SetBoth(&g_link, cfg);
    NetRel_Init(&g_a.rel); NetRel_Init(&g_b.rel);
    g_a.tp = NetLoopback_End(&g_link, 0); g_b.tp = NetLoopback_End(&g_link, 1);
}
static NetLinkCfg Link(float latency, float jitter, float loss, float dup) { NetLinkCfg c = { latency, jitter, loss, dup }; return c; }

// Moves time on by 1 ms: packets arrive, timers fire.
static void Step(void)
{
    NetLoopback_Advance(&g_link, g_link.now + 1.0);
    uint8_t buf[NET_MTU]; int n;
    while ((n = g_a.tp.recvDatagram(g_a.tp.self, buf, sizeof buf)) > 0) NetRel_OnPacket(&g_a.rel, buf, (size_t)n, g_link.now, Emit, &g_a);
    while ((n = g_b.tp.recvDatagram(g_b.tp.self, buf, sizeof buf)) > 0) NetRel_OnPacket(&g_b.rel, buf, (size_t)n, g_link.now, Emit, &g_b);
    NetRel_Tick(&g_a.rel, g_link.now, Emit, &g_a);
    NetRel_Tick(&g_b.rel, g_link.now, Emit, &g_b);
}

// Sends `count` numbered messages from `from` (as the window allows) and collects what `to` receives, until all arrive or `limitMs` passes.
// Returns how many arrived in order and exactly once (stops counting at the first wrong one).
static int Transfer(End *from, End *to, int count, double limitMs, int *wrong)
{
    int sent = 0, got = 0;
    *wrong = 0;
    for (double t = 0; t < limitMs && got < count; t += 1.0) {
        while (sent < count) {
            uint8_t m[6] = { 'M', (uint8_t)sent, (uint8_t)(sent >> 8), (uint8_t)(sent >> 16), (uint8_t)(sent >> 24), (uint8_t)(sent * 7) };
            if (!NetRel_Send(&from->rel, m, sizeof m, g_link.now, Emit, from)) break;
            ++sent;
        }
        Step();
        uint8_t rx[NET_REL_MAX_MSG]; int n;
        while ((n = NetRel_Recv(&to->rel, rx, sizeof rx)) > 0) {
            const int id = rx[1] | (rx[2] << 8) | (rx[3] << 16) | (rx[4] << 24);
            if (n != 6 || rx[0] != 'M' || id != got || rx[5] != (uint8_t)(got * 7)) ++*wrong;
            ++got;
        }
    }
    return got;
}

int main(void)
{
    printf("a clean path\n");
    {
        Setup(Link(10.0f, 0.0f, 0.0f, 0.0f), 1u);
        int wrong;
        const int got = Transfer(&g_a, &g_b, 200, 3000.0, &wrong);
        Check(got == 200 && wrong == 0, "200 messages arrive, in order, once each");
        Check(g_a.rel.retransmits == 0 && g_b.rel.duplicates == 0, "with no resending and no repeats");
        for (int i = 0; i < 100; ++i) Step();
        Check(g_a.rel.sendBase == g_a.rel.sendNext, "and every one is acknowledged (once the last acknowledgement has had time to get back)");
        Check(g_a.rel.haveRtt && g_a.rel.srtt > 18.0 && g_a.rel.srtt < 24.0, "the round trip it measured is the real one (20 ms)");
    }

    printf("\na path that loses, doubles and reorders\n");
    {
        Setup(Link(25.0f, 40.0f, 0.3f, 0.1f), 2u);                       // 30% lost, 10% doubled, up to 40 ms of swing (so reordering) each way
        int wrong;
        const int got = Transfer(&g_a, &g_b, 500, 120000.0, &wrong);
        printf("    (%u messages sent, %u resent, %u repeats thrown away, round trip %.0f ms, wait %.0f ms)\n", g_a.rel.sent, g_a.rel.retransmits, g_b.rel.duplicates, g_a.rel.srtt, g_a.rel.rto);
        Check(got == 500 && wrong == 0, "500 messages still arrive, in order, exactly once each");
        Check(g_a.rel.retransmits > 100 && g_b.rel.duplicates > 0, "(the path really did lose and double them)");
        Check(!g_a.rel.dead, "and the connection is not called dead");

        Setup(Link(25.0f, 40.0f, 0.3f, 0.1f), 3u);
        int wrongAB = 0, wrongBA = 0, gotAB = 0, gotBA = 0, sentAB = 0, sentBA = 0;
        for (double t = 0; t < 120000.0 && (gotAB < 300 || gotBA < 300); t += 1.0) {
            while (sentAB < 300) { uint8_t m[4] = { 'A', (uint8_t)sentAB, (uint8_t)(sentAB >> 8), 0 }; if (!NetRel_Send(&g_a.rel, m, 4, g_link.now, Emit, &g_a)) break; ++sentAB; }
            while (sentBA < 300) { uint8_t m[4] = { 'B', (uint8_t)sentBA, (uint8_t)(sentBA >> 8), 0 }; if (!NetRel_Send(&g_b.rel, m, 4, g_link.now, Emit, &g_b)) break; ++sentBA; }
            Step();
            uint8_t rx[64]; int n;
            while ((n = NetRel_Recv(&g_b.rel, rx, sizeof rx)) > 0) { if (rx[0] != 'A' || (rx[1] | (rx[2] << 8)) != gotAB) ++wrongAB; ++gotAB; }
            while ((n = NetRel_Recv(&g_a.rel, rx, sizeof rx)) > 0) { if (rx[0] != 'B' || (rx[1] | (rx[2] << 8)) != gotBA) ++wrongBA; ++gotBA; }
        }
        Check(gotAB == 300 && gotBA == 300 && wrongAB == 0 && wrongBA == 0, "both ends sending at once: 300 each way, each in order");
    }

    printf("\nsequence numbers going round\n");
    {
        Setup(Link(1.0f, 1.0f, 0.02f, 0.02f), 4u);
        int wrong;
        const int got = Transfer(&g_a, &g_b, 70000, 4000000.0, &wrong);
        Check(got == 70000 && wrong == 0, "70000 messages (more than the 65536 sequence numbers) arrive in order, once each");
        Check(g_a.rel.sendNext < g_a.rel.sent, "(the sequence number really did wrap)");
    }

    printf("\none lost message among several\n");
    {
        aN = bN = 0;
        NetRel A, B; NetRel_Init(&A); NetRel_Init(&B);
        for (uint8_t i = 0; i < 5; ++i) { const uint8_t m[3] = { 'M', i, i }; NetRel_Send(&A, m, 3, 0.0, CollectA, NULL); }
        for (int i = 0; i < aN; ++i) if (i != 1) NetRel_OnPacket(&B, aOut[i], aLen[i], 10.0, CollectB, NULL);          // message 1 is lost on the way
        for (int i = 0; i < bN; ++i) NetRel_OnPacket(&A, bOut[i], bLen[i], 20.0, CollectA, NULL);
        int freed = 0; for (int s = 2; s <= 4; ++s) if (!A.out[s % NET_REL_WINDOW].used) ++freed;
        Check(freed == 3 && A.out[1].used && A.sendBase == 1, "the acknowledgements say which came after the gap, so 2, 3 and 4 are done and only 1 is still owed");
        uint8_t rx[16];
        Check(NetRel_Recv(&B, rx, sizeof rx) == 3 && rx[1] == 0 && NetRel_Recv(&B, rx, sizeof rx) == 0, "and the receiver holds 2, 3 and 4 back until 1 arrives (only 0 is handed over)");
        const unsigned before = A.retransmits;
        aN = 0;
        NetRel_Tick(&A, 1000.0, CollectA, NULL);
        Check(A.retransmits == before + 1 && aN == 1, "when the wait is up, one message is sent again: only the lost one");
        NetRel_OnPacket(&B, aOut[0], aLen[0], 1010.0, CollectB, NULL);
        int got = 0, inOrder = 1; while (NetRel_Recv(&B, rx, sizeof rx) > 0) { if (rx[1] != got + 1) inOrder = 0; ++got; }
        Check(got == 4 && inOrder, "and when it arrives 1, 2, 3 and 4 follow, in order");
    }

    printf("\nthe window, sizes\n");
    {
        Setup(Link(10.0f, 0.0f, 1.0f, 0.0f), 5u);                        // nothing gets through
        uint8_t m[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        int accepted = 0;
        for (int i = 0; i < 100; ++i) if (NetRel_Send(&g_a.rel, m, sizeof m, g_link.now, Emit, &g_a)) ++accepted;
        Check(accepted == NET_REL_WINDOW, "with nothing acknowledged, exactly one window of messages is taken and the rest refused");
        uint8_t big[NET_REL_MAX_MSG + 1] = { 0 };
        Setup(Link(1.0f, 0.0f, 0.0f, 0.0f), 6u);
        Check(NetRel_Send(&g_a.rel, big, NET_REL_MAX_MSG, 0.0, Emit, &g_a) && !NetRel_Send(&g_a.rel, big, NET_REL_MAX_MSG + 1, 0.0, Emit, &g_a) && !NetRel_Send(&g_a.rel, big, 0, 0.0, Emit, &g_a), "a message of the largest size goes, one byte more or none does not");
        for (int i = 0; i < 20; ++i) Step();
        uint8_t rx[NET_REL_MAX_MSG];
        Check(NetRel_Recv(&g_b.rel, rx, sizeof rx) == NET_REL_MAX_MSG, "and the large one arrives whole");
        NetRel_Send(&g_a.rel, m, sizeof m, g_link.now, Emit, &g_a);
        for (int i = 0; i < 20; ++i) Step();
        uint8_t tiny[4];
        Check(NetRel_Recv(&g_b.rel, tiny, sizeof tiny) == -1 && NetRel_Recv(&g_b.rel, tiny, sizeof tiny) == 0, "a message that does not fit the reader's buffer is dropped, not cut, and the next one is not held up");
    }

    printf("\nthe window of a receiver that does not read\n");
    {
        Setup(Link(5.0f, 0.0f, 0.0f, 0.0f), 7u);
        int sent = 0;
        for (double t = 0; t < 3000.0; t += 1.0) {
            uint8_t m[4] = { 'M', (uint8_t)sent, (uint8_t)(sent >> 8), 0 };
            if (sent < 100 && NetRel_Send(&g_a.rel, m, 4, g_link.now, Emit, &g_a)) ++sent;
            Step();                                                      // b never calls Recv
        }
        Check(sent == 2 * NET_REL_WINDOW && g_b.rel.delivered == 0 && g_b.rel.outOfWindow > 0, "a receiver that never reads stops the sender at two windows (one the receiver holds, one it refuses and the sender keeps): nothing runs away, nothing is lost");
        uint8_t rx[16]; int got = 0, inOrder = 1;
        for (double t = 0; t < 5000.0 && got < 100; t += 1.0) {
            uint8_t m[4] = { 'M', (uint8_t)sent, (uint8_t)(sent >> 8), 0 };
            if (sent < 100 && NetRel_Send(&g_a.rel, m, 4, g_link.now, Emit, &g_a)) ++sent;
            Step();
            int n;
            while ((n = NetRel_Recv(&g_b.rel, rx, sizeof rx)) > 0) { if ((rx[1] | (rx[2] << 8)) != got) inOrder = 0; ++got; }
        }
        Check(got == 100 && inOrder, "when it starts reading, all arrive, in order");
    }

    printf("\na dead path\n");
    {
        Setup(Link(10.0f, 0.0f, 1.0f, 0.0f), 8u);
        uint8_t m[4] = { 1, 2, 3, 4 };
        NetRel_Send(&g_a.rel, m, 4, 0.0, Emit, &g_a);
        for (int i = 0; i < 90000 && !g_a.rel.dead; ++i) Step();
        printf("    (called dead after %.1f s, %u resends)\n", g_link.now / 1000.0, g_a.rel.retransmits);
        Check(g_a.rel.dead && g_a.rel.retransmits <= NET_REL_GIVEUP, "a path that answers nothing is given up on, after a bounded number of resends");
        Check(g_link.now > 3000.0 && g_link.now < 20000.0, "not at once (a few seconds of trying), and not for ever");
    }

    printf("\npackets that are not its own\n");
    {
        Setup(Link(1.0f, 0.0f, 0.0f, 0.0f), 9u);
        uint8_t m[4] = { 9, 9, 9, 9 };
        NetRel_Send(&g_a.rel, m, 4, 0.0, Emit, &g_a);
        for (int i = 0; i < 10; ++i) Step();
        uint8_t rx[16];
        Check(NetRel_Recv(&g_b.rel, rx, sizeof rx) == 4 && NetRel_Recv(&g_b.rel, rx, sizeof rx) == 0, "one message arrives once");
        const uint8_t old[7] = { 1, 0, 0, 9, 9, 9, 9 };                  // the same sequence number again, long after
        NetRel_OnPacket(&g_b.rel, old, sizeof old, g_link.now, Emit, &g_b);
        Check(NetRel_Recv(&g_b.rel, rx, sizeof rx) == 0 && g_b.rel.duplicates == 1, "an old copy is not delivered again (it is counted, and answered)");
        const uint8_t ackFuture[7] = { 2, 0xFF, 0x7F, 0, 0, 0, 0 };     // an acknowledgement of things never sent
        const uint16_t base = g_a.rel.sendBase;
        NetRel_OnPacket(&g_a.rel, ackFuture, sizeof ackFuture, g_link.now, Emit, &g_a);
        Check(g_a.rel.sendBase == base, "an acknowledgement of messages never sent changes nothing");
        {                                                                    // the same, with three messages really in flight: a believed false acknowledgement would lose them
            Setup(Link(10.0f, 0.0f, 1.0f, 0.0f), 10u);
            for (int i = 0; i < 3; ++i) NetRel_Send(&g_a.rel, m, 4, 0.0, Emit, &g_a);
            const uint8_t bogus[7] = { 2, 5, 0, 0, 0, 0, 0 };                // "I have everything before 5", when only 0, 1 and 2 were ever sent
            NetRel_OnPacket(&g_a.rel, bogus, sizeof bogus, 1.0, Emit, &g_a);
            Check(g_a.rel.out[0].used && g_a.rel.out[1].used && g_a.rel.out[2].used && g_a.rel.sendBase == 0, "an acknowledgement of messages never sent does not free the ones that are in flight");
        }
        Check(!NetRel_OnPacket(&g_a.rel, (const uint8_t *)"", 0, 0.0, Emit, &g_a) && !NetRel_OnPacket(&g_a.rel, (const uint8_t *)"\x07xyz", 4, 0.0, Emit, &g_a) && !NetRel_OnPacket(&g_a.rel, ackFuture, 5, 0.0, Emit, &g_a) && !NetRel_OnPacket(&g_a.rel, old, 3, 0.0, Emit, &g_a), "empty, unknown, short and wrongly sized packets are refused");
        uint32_t rs = 77u; int crashed = 0;
        for (int it = 0; it < 200000; ++it) {
            uint8_t p[600]; size_t n = (size_t)(rs = rs * 1664525u + 1013904223u) % 600;
            for (size_t i = 0; i < n; ++i) p[i] = (uint8_t)(rs = rs * 1664525u + 1013904223u);
            if (n && it % 2) p[0] = (uint8_t)(1 + (it / 2) % 2);
            NetRel_OnPacket(&g_b.rel, p, n, g_link.now, Emit, &g_b);
            uint8_t sink[NET_REL_MAX_MSG]; while (NetRel_Recv(&g_b.rel, sink, sizeof sink) != 0) { }
        }
        Check(!crashed, "200000 random packets: nothing crashes or overruns");
    }

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
