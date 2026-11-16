// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_dial.h and net_dial.c"
// ============================================================================
#define _POSIX_C_SOURCE 200809L
#include "net_dial.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

bool NetDial_Open(NetDial *d, int transport, const char *host, uint16_t port, NetTransport *out, char *err, size_t errCap)
{
    memset(d, 0, sizeof *d);
    d->transport = transport;
    if (err && errCap) err[0] = 0;
    if (transport == NET_TRANSPORT_QUIC) {
        if (!NetMid_Available()) { if (err) snprintf(err, errCap, "THIS GAME HAS NO QUIC"); return false; }
        if (!NetMidHost_Connect(&d->mid, host, port, out)) { if (err) snprintf(err, errCap, "COULD NOT REACH %s", host); return false; }
    } else if (transport == NET_TRANSPORT_UDP) {
        if (!NetUdpHost_Connect(&d->udp, host, port, out)) { if (err) snprintf(err, errCap, "COULD NOT REACH %s", host); return false; }
    } else {
        if (err) snprintf(err, errCap, "UNKNOWN WAY TO CONNECT");
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
    if (d->transport == NET_TRANSPORT_QUIC) NetMidHost_Close(&d->mid); else NetUdpHost_Close(&d->udp);
    d->open = false;
}
