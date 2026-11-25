// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_dial.h and net_dial.c"
// ============================================================================
#define _POSIX_C_SOURCE 200809L
#include "net_dial.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "net_driver.h"

static void CloseSocket(NetDial *d)
{
    if (d->transport == NET_TRANSPORT_QUIC) NetMidHost_Close(&d->mid); else NetUdpHost_Close(&d->udp);
}

bool NetDial_Open(NetDial *d, int transport, const char *host, uint16_t port, NetTransport *out, char *err, size_t errCap)
{
    memset(d, 0, sizeof *d);
    d->transport = transport;
    if (err && errCap) err[0] = 0;
    NetTransport lower;
    memset(&lower, 0, sizeof lower);
    if (transport == NET_TRANSPORT_QUIC) {
        if (!NetMid_Available()) { if (err) snprintf(err, errCap, "THIS GAME HAS NO QUIC"); return false; }
    } else if (transport != NET_TRANSPORT_UDP) {
        if (err) snprintf(err, errCap, "UNKNOWN WAY TO CONNECT");
        return false;
    }
    // Every connection of the game goes through mid-net's driver (net_driver.h): a build without it has no multiplayer, and says so.
    if (!NetDriver_Available()) { if (err) snprintf(err, errCap, "THIS BUILD HAS NO MULTIPLAYER LIBRARY"); return false; }
    if (transport == NET_TRANSPORT_QUIC) {
        if (!NetMidHost_Connect(&d->mid, host, port, &lower)) { if (err) snprintf(err, errCap, "COULD NOT REACH %s", host); return false; }
    } else {
        if (!NetUdpHost_Connect(&d->udp, host, port, &lower)) { if (err) snprintf(err, errCap, "COULD NOT REACH %s", host); return false; }
    }
    if (!NetDriver_Client(lower, "", out)) {                          // nothing has been sent: the transport and the socket just go
        if (lower.close) lower.close(lower.self);
        CloseSocket(d);
        if (err) snprintf(err, errCap, "COULD NOT START THE CONNECTION");
        return false;
    }
    d->open = true;
    return true;
}

void NetDial_Close(NetDial *d, int lingerMs)
{
    if (!d->open) return;
    for (int waited = 0; waited < lingerMs; waited += 5) {
        if (d->transport == NET_TRANSPORT_QUIC) NetMidHost_Pump(&d->mid); else NetUdpHost_Pump(&d->udp);
        struct timespec ts = { 0, 5 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    CloseSocket(d);
    d->open = false;
}
