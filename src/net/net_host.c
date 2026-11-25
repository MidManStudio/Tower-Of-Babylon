// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_host.h and net_host.c"
// ============================================================================
#define _POSIX_C_SOURCE 200809L
#include "net_host.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "net_driver.h"

void NetHostConfig_Default(NetHostConfig *c)
{
    memset(c, 0, sizeof *c);
    c->seed = 12345;
    c->floorNumber = 1;
    c->name = "TOWER";
    c->transport = NET_TRANSPORT_UDP;
    c->port = 7777;
    c->anyPortIfBusy = true;
    c->discoveryPort = NET_DISC_PORT;
    c->discovery = true;
}

static bool Listen(NetHost *h, uint16_t port)
{
    return h->transport == NET_TRANSPORT_QUIC ? NetMidHost_Listen(&h->mid, port) : NetUdpHost_Listen(&h->udp, port);
}

static void Announce(NetHost *h, int players)
{
    NetDiscHost_SetInfo(&h->disc, h->name, h->port, players, NET_MAX_PLAYERS, (uint8_t)h->transport);
    h->announced = players;
}

bool NetHost_Open(NetHost *h, const NetHostConfig *cfg, double nowMs)
{
    memset(h, 0, sizeof *h);
    h->transport = cfg->transport;
    if (h->transport != NET_TRANSPORT_UDP && h->transport != NET_TRANSPORT_QUIC) { snprintf(h->error, sizeof h->error, "unknown transport"); return false; }
    if (h->transport == NET_TRANSPORT_QUIC && !NetMid_Available()) { snprintf(h->error, sizeof h->error, "this build has no QUIC"); return false; }
    if (!NetDriver_Available()) { snprintf(h->error, sizeof h->error, "this build has no multiplayer library (mid-net's driver: see docs/net.md)"); return false; }
    snprintf(h->name, sizeof h->name, "%s", cfg->name && NetDisc_NameOk(cfg->name) ? cfg->name : "TOWER");

    NetServer_Init(&h->server, cfg->seed, cfg->floorNumber);
    if (!Listen(h, cfg->port)) {
        if (!(cfg->anyPortIfBusy && cfg->port != 0 && Listen(h, 0))) {
            snprintf(h->error, sizeof h->error, "cannot listen on UDP port %u (is another server using it?)", (unsigned)cfg->port);
            return false;
        }
    }
    h->port = h->transport == NET_TRANSPORT_QUIC ? NetMidHost_Port(&h->mid) : NetUdpHost_Port(&h->udp);
    h->tickMs = 1000.0 / (double)NET_TICK_RATE;
    h->nextTickMs = nowMs + h->tickMs;
    h->announced = -1;
    if (cfg->discovery && NetDiscHost_Open(&h->disc, cfg->discoveryPort)) {
        h->discoveryOpen = true;
        Announce(h, 0);
    }
    h->open = true;
    return true;
}

void NetHost_Update(NetHost *h, double nowMs)
{
    if (!h->open) return;
    NetTransport tp;
    while (h->transport == NET_TRANSPORT_QUIC ? NetMidHost_Accept(&h->mid, &tp) : NetUdpHost_Accept(&h->udp, &tp)) {
        NetTransport driven;                                                           // every player's connection is one of mid-net's driver, over the socket's own
        if (!NetDriver_Server(tp, (uint32_t)(h->accepted + h->refused + 1), &driven)) { if (tp.close) tp.close(tp.self); ++h->refused; continue; }
        if (NetServer_AddClient(&h->server, driven, nowMs) >= 0) ++h->accepted; else ++h->refused;
    }
    NetServer_Poll(&h->server, nowMs);
    int ran = 0;
    while (nowMs >= h->nextTickMs && ran < 5) { NetServer_Tick(&h->server, nowMs); h->nextTickMs += h->tickMs; ++ran; }
    if (nowMs >= h->nextTickMs) h->nextTickMs = nowMs + h->tickMs;                      // far behind: do not try to catch up
    const int players = NetServer_PlayerCount(&h->server);
    if (players > h->peak) h->peak = players;
    if (h->discoveryOpen) {
        if (players != h->announced) Announce(h, players);
        NetDiscHost_Poll(&h->disc, nowMs);
    }
}

int NetHost_Players(const NetHost *h) { return h->open ? NetServer_PlayerCount(&h->server) : 0; }

static void PumpFor(NetHost *h, int ms)
{
    for (int waited = 0; waited < ms; waited += 5) {
        if (h->transport == NET_TRANSPORT_QUIC) NetMidHost_Pump(&h->mid); else NetUdpHost_Pump(&h->udp);
        struct timespec ts = { 0, 5 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

void NetHost_Close(NetHost *h, int lingerMs)
{
    if (!h->open) return;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) NetServer_Drop(&h->server, i, NET_DISC_NORMAL);
    if (lingerMs > 0) PumpFor(h, lingerMs);
    if (h->discoveryOpen) NetDiscHost_Close(&h->disc);
    if (h->transport == NET_TRANSPORT_QUIC) NetMidHost_Close(&h->mid); else NetUdpHost_Close(&h->udp);
    h->discoveryOpen = false;
    h->open = false;
}
