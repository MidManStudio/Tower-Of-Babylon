// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_server.h and net_server.c"
// ============================================================================
#include "net_server.h"

#include <math.h>
#include <string.h>

#include "../dungeon/dungeon_generator.h"
#include "../enemies/enemy_plan.h"
#include "../gvex/gvex_world.h"
#include "../platform/platform.h"
#include "net_driver.h"

static void Send(NetTransport *tp, const NetMsg *m, bool reliable)
{
    uint8_t buf[NET_MTU];
    const size_t n = NetProto_Encode(m, buf, sizeof buf);
    if (n == 0) return;
    if (reliable) tp->sendReliable(tp->self, buf, n); else tp->sendDatagram(tp->self, buf, n);
}

void NetServer_Init(NetServer *s, uint32_t floorSeed, int floorNumber)
{
    memset(s, 0, sizeof *s);
    s->floorSeed = floorSeed;
    s->floorNumber = floorNumber;
    GenerateFloorMap(&s->map, floorNumber, floorSeed);
#if TOWER_GVEX
    s->gvex = GvexWorld_Create(&s->map, floorNumber);                 // the creatures of the floor, where the single-player game would put its cylinders
#endif
}

void NetServer_Free(NetServer *s)
{
#if TOWER_GVEX
    GvexWorld_Destroy(s->gvex);
#endif
    s->gvex = NULL;
}

bool NetServer_RewoundPose(const NetServer *s, uint8_t viewLag, int id, Vector3 *pos, float *yaw)
{
    return NetHistory_PoseAt(&s->hist, NetHistory_RewindTick(s->tick, viewLag), id, pos, yaw);
}

int NetServer_MeleeTargets(const NetServer *s, uint8_t viewLag, Vector3 eye, float yaw, const WeaponDef *def, uint16_t *ids, int max)
{
#if TOWER_GVEX
    if (!s->gvex || !def || !ids || max < 1) return 0;
    GvexRecord recs[GVEX_MAX_CREATURES];
    const int n = GvexWorld_Records(s->gvex, recs, GVEX_MAX_CREATURES);
    const uint32_t when = NetHistory_RewindTick(s->tick, viewLag);
    const float fx = sinf(yaw), fz = cosf(yaw);                                       // forward is sin, cos, as for the players and the creatures
    int hits = 0;
    for (int i = 0; i < n && hits < max; ++i) {
        Vector3 pos = { recs[i].x, recs[i].y, recs[i].z };
        NetMonHistory_PosAt(&s->mhist, when, recs[i].id, &pos);                      // where the puncher saw it; where it is now when that tick is not kept
        float height, radius; Color tint;
        EnemyLook(EnemyHash32(s->map.seed + (uint32_t)recs[i].id * 0x9e3779b1U), &height, &radius, &tint);   // the body the screens draw
        if (EnemyMelee_Reaches(eye, fx, fz, def->reach, def->arcDeg, pos, radius, height)) ids[hits++] = recs[i].id;
    }
    return hits;
#else
    (void)s; (void)viewLag; (void)eye; (void)yaw; (void)def; (void)ids; (void)max;
    return 0;
#endif
}

int NetServer_PlayerCount(const NetServer *s) { int n = 0; for (int i = 0; i < NET_MAX_PLAYERS; ++i) if (s->c[i].used && s->c[i].welcomed) ++n; return n; }

int NetServer_AddClient(NetServer *s, NetTransport tp, double nowMs)
{
    for (int i = 0; i < NET_MAX_PLAYERS; ++i)
        if (!s->c[i].used) {
            memset(&s->c[i], 0, sizeof s->c[i]);
            s->c[i].used = true; s->c[i].tp = tp; s->c[i].id = (uint8_t)i; s->c[i].lastHeardMs = nowMs; s->c[i].connectedMs = nowMs;
            return i;
        }
    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_DISCONNECT; m.u.disc.reason = NET_DISC_FULL;
    Send(&tp, &m, true);
    if (tp.close) tp.close(tp.self);
    return -1;
}

void NetServer_Drop(NetServer *s, int slot, uint8_t reason)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS || !s->c[slot].used) return;
    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_DISCONNECT; m.u.disc.reason = reason;
    Send(&s->c[slot].tp, &m, true);
    if (s->c[slot].tp.close) s->c[slot].tp.close(s->c[slot].tp.self);
    s->c[slot].used = false;
}

static void OnHello(NetServer *s, NetServerClient *cl, const NetHello *h)
{
    if (cl->welcomed) return;                                                         // a second Hello changes nothing
    if (h->version != NET_PROTOCOL_VERSION) { NetServer_Drop(s, cl->id, NET_DISC_VERSION); return; }
    memcpy(cl->character, h->character, sizeof cl->character);
    NetSim_Spawn(&cl->player, s->map.startX, s->map.startZ);
    Arsenal_Init(&cl->arsenal);                                                       // hands only: a player starts with nothing else
    cl->welcomed = true;
    NetMsg m; memset(&m, 0, sizeof m);
    m.kind = NET_MSG_WELCOME;
    m.u.welcome.version = NET_PROTOCOL_VERSION; m.u.welcome.tickRate = NET_TICK_RATE; m.u.welcome.playerId = cl->id; m.u.welcome.maxPlayers = NET_MAX_PLAYERS;
    m.u.welcome.floorSeed = s->floorSeed; m.u.welcome.floorNumber = s->floorNumber; m.u.welcome.serverTick = s->tick;
    m.u.welcome.spawnX = s->map.startX; m.u.welcome.spawnZ = s->map.startZ;
    Send(&cl->tp, &m, true);
}

static void OnInput(NetServer *s, NetServerClient *cl, const NetInputMsg *in)
{
    if (!cl->welcomed) { ++cl->badPackets; return; }
    const uint32_t next = s->tick + 1;                                                // the tick about to be run
    {
        long m = (long)in->tick - (long)next;
        cl->margin = (int8_t)(m > 127 ? 127 : (m < -128 ? -128 : m));
    }
    uint32_t earliest = 0; bool any = false;
    for (int i = 0; i < in->count; ++i) {
        if (in->tick < (uint32_t)i) break;
        const uint32_t t = in->tick - (uint32_t)i;
        if (t < next) { if (i == 0) ++cl->cmdsLate; continue; }                         // too late for its tick; the copies of older ones in later packets are expected, so only the newest is counted
        if (t > next + NET_MAX_LEAD) { if (i == 0) ++cl->cmdsTooFar; continue; }
        const int slot = (int)(t % NET_INPUT_RING);
        if (cl->ringHave[slot] && cl->ringTick[slot] == t) { if (i == 0) ++cl->cmdsDuplicate; continue; }       // the first to arrive for a tick is the one used
        cl->ring[slot] = in->cmds[i]; cl->ringTick[slot] = t; cl->ringHave[slot] = true;
        ++cl->cmdsAccepted;
        if (!any || t < earliest) { earliest = t; any = true; }
    }
    if (any && cl->startTick == 0) cl->startTick = earliest;
}

static void Drain(NetServer *s, int slot, double nowMs)
{
    NetServerClient *cl = &s->c[slot];
    uint8_t buf[NET_MTU];
    NetMsg m;
    int n;
    for (int k = 0; k < NET_MAX_PACKETS_PER_POLL && cl->used && (n = cl->tp.recvReliable(cl->tp.self, buf, sizeof buf)) != 0; ++k) {
        if (n < 0 || !NetProto_Decode(buf, (size_t)n, &m)) { ++cl->badPackets; continue; }
        cl->lastHeardMs = nowMs;
        if (m.kind == NET_MSG_HELLO) OnHello(s, cl, &m.u.hello);
        else if (m.kind == NET_MSG_DISCONNECT) { if (cl->tp.close) cl->tp.close(cl->tp.self); cl->used = false; }
        else ++cl->badPackets;                                                                    // a datagram kind on the reliable channel
    }
    for (int k = 0; k < NET_MAX_PACKETS_PER_POLL && cl->used && (n = cl->tp.recvDatagram(cl->tp.self, buf, sizeof buf)) != 0; ++k) {
        if (n < 0 || !NetProto_Decode(buf, (size_t)n, &m)) { ++cl->badPackets; continue; }
        cl->lastHeardMs = nowMs;
        if (m.kind == NET_MSG_INPUT) OnInput(s, cl, &m.u.input);
        else ++cl->badPackets;
    }
}

void NetServer_Poll(NetServer *s, double nowMs)
{
    s->nowMs = nowMs;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        if (!s->c[i].used) continue;
        Drain(s, i, nowMs);
        if (!s->c[i].used) continue;
        NetLinkInfo li;
        if (s->c[i].tp.linkInfo && s->c[i].tp.linkInfo(s->c[i].tp.self, &li)) {
            // The transport watches its own connection (NO SIGNAL after a second and a half, the end after ten seconds or at the other side's
            // goodbye): the silence rule below is its business, not ours.
            if (li.state == NETLINK_ENDED) NetServer_Drop(s, i, NetDriver_EndToDisc(&li));
        } else if (nowMs - s->c[i].lastHeardMs > NET_TIMEOUT_MS) NetServer_Drop(s, i, NET_DISC_TIMEOUT);
        if (s->c[i].used && !s->c[i].welcomed && nowMs - s->c[i].connectedMs > NET_HELLO_TIMEOUT_MS) NetServer_Drop(s, i, NET_DISC_TIMEOUT);
    }
}

// The command for tick t, if it arrived. Takes it out of the ring.
static bool TakeCmd(NetServerClient *cl, uint32_t t, NetCmd *out)
{
    const int slot = (int)(t % NET_INPUT_RING);
    if (!cl->ringHave[slot] || cl->ringTick[slot] != t) return false;
    *out = cl->ring[slot];
    cl->ringHave[slot] = false;
    return true;
}

#if TOWER_GVEX
// The creatures' tick: they sense the players that are in the game (started, with the noise each makes) and move; what they are after it goes to the clients.
static int StepCreatures(NetServer *s, NetMonster *out)
{
    if (!s->gvex) return 0;
    GvexTarget targets[GVEX_MAX_TARGETS];
    memset(targets, 0, sizeof targets);
    for (int i = 0; i < NET_MAX_PLAYERS && i < GVEX_MAX_TARGETS; ++i) {
        const NetServerClient *cl = &s->c[i];
        if (!cl->used || !cl->welcomed || !cl->started) continue;
        targets[i] = (GvexTarget){ true, cl->player.position.x, cl->player.position.y, cl->player.position.z, PlayerNoise(&cl->player) };
    }
    GvexWorld_Step(s->gvex, targets, GVEX_MAX_TARGETS, NET_TICK_SECS);
    GvexRecord recs[NET_MAX_MONSTERS];
    const int n = GvexWorld_Records(s->gvex, recs, NET_MAX_MONSTERS);
    for (int i = 0; i < n; ++i) {
        const GvexRecord *r = &recs[i];
        float yaw = fmodf(r->yaw, 6.28318531f); if (yaw < 0.0f) yaw += 6.28318531f;
        float hp = r->maxHealth > 0.0f ? r->health / r->maxHealth : 0.0f;
        if (hp < 0.0f) hp = 0.0f;
        if (hp > 1.0f) hp = 1.0f;
        out[i] = (NetMonster){ r->id, r->kind, NET_MON_FLAGS(r->state, r->variant),
                               (int32_t)lroundf(r->x * 1000.0f), (int32_t)lroundf(r->y * 1000.0f), (int32_t)lroundf(r->z * 1000.0f),
                               (uint16_t)((uint32_t)lroundf(yaw / 6.28318531f * 65536.0f) & 0xFFFFu), (uint8_t)lroundf(hp * 255.0f) };
    }
    return n;
}
#endif

// The player's hands for this tick: the fire button works the arsenal (the same timing as the single-player game: a windup, then the blow connects, then a
// cooldown), and when a blow connects it is judged against the creatures as the puncher's screen showed them and does its damage and knockback.
// Hands only for now; a player at the eye height of a standing one (height * 0.4, as the single-player camera starts).
static void RunHands(NetServer *s, NetServerClient *cl, const NetCmd *cmd)
{
    ArsenalEvents ev;
    Arsenal_Update(&cl->arsenal, NET_TICK_SECS, (cmd->buttons & NET_BTN_FIRE) != 0, &ev);
#if TOWER_GVEX
    if (!ev.impact || !ev.def || !s->gvex) return;
    const Vector3 eye = { cl->player.position.x, cl->player.position.y + cl->player.height * 0.4f, cl->player.position.z };
    uint16_t ids[NET_MAX_MONSTERS];
    const int n = NetServer_MeleeTargets(s, cmd->viewLag, eye, cl->player.yaw, ev.def, ids, NET_MAX_MONSTERS);
    for (int i = 0; i < n; ++i) {
        if (GvexWorld_Damage(s->gvex, ids[i], ev.def->damage)) continue;              // killed: gone
        GvexWorld_Knock(s->gvex, ids[i], eye.x, eye.z, ev.def->knockback);
    }
#else
    (void)s; (void)ev;
#endif
}

void NetServer_Tick(NetServer *s, double nowMs)
{
    s->nowMs = nowMs;
    const uint32_t T = ++s->tick;
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        NetServerClient *cl = &s->c[i];
        if (!cl->used || !cl->welcomed) continue;
        if (!cl->started && cl->startTick != 0 && T >= cl->startTick) cl->started = true;
        if (!cl->started) continue;
        NetCmd cmd;
        if (TakeCmd(cl, T, &cmd)) {
            cl->lastCmd = cmd; cl->repeats = 0; ++cl->ticksFed;
        } else if (cl->repeats < NET_MAX_REPEAT) {                                           // did not arrive: go on as before, minus one-off presses
            cmd = cl->lastCmd;
            cmd.buttons = (uint16_t)(cmd.buttons & ~(NET_BTN_JUMP | NET_BTN_USE | NET_BTN_SWAP | NET_BTN_LIGHT | NET_BTN_SLIDE | NET_BTN_DIVE | NET_BTN_RELOAD));
            ++cl->repeats; ++cl->ticksRepeated;
        } else {                                                                              // silent for too long: stop, keeping the aim
            cmd = cl->lastCmd; cmd.moveX = 0; cmd.moveY = 0; cmd.buttons = 0;
            ++cl->ticksNeutral;
        }
        NetSim_Step(&cl->player, &cmd, &s->map);
        RunHands(s, cl, &cmd);
    }
#if TOWER_GVEX
    NetMonster mons[NET_MAX_MONSTERS];
    const int nMons = StepCreatures(s, mons);
#endif
    NetHistory_Begin(&s->hist, T);                                                          // where everyone is after this tick, for the shots of the next ones
#if TOWER_GVEX
    NetMonHistory_Begin(&s->mhist, T);                                                      // and every creature, as the snapshot below says (millimetres)
    for (int i = 0; i < nMons; ++i) NetMonHistory_Set(&s->mhist, T, mons[i].id, (Vector3){ (float)mons[i].px / 1000.0f, (float)mons[i].py / 1000.0f, (float)mons[i].pz / 1000.0f });
#endif
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        const NetServerClient *cl = &s->c[i];
        if (cl->used && cl->welcomed && cl->started) NetHistory_Set(&s->hist, T, i, cl->player.position, cl->player.yaw);
    }
    for (int i = 0; i < NET_MAX_PLAYERS; ++i) {
        NetServerClient *cl = &s->c[i];
        if (!cl->used || !cl->welcomed) continue;
        NetMsg m; memset(&m, 0, sizeof m);
        m.kind = NET_MSG_SNAPSHOT;
        m.u.snap.serverTick = T; m.u.snap.started = cl->started ? 1 : 0; m.u.snap.margin = cl->margin;
        NetSim_Capture(&cl->player, &m.u.snap.owner);
        for (int j = 0; j < NET_MAX_PLAYERS; ++j) {
            if (j == i || !s->c[j].used || !s->c[j].welcomed) continue;
            NetSim_Remote(&s->c[j].player, s->c[j].id, &m.u.snap.remote[m.u.snap.nRemote++]);
        }
#if TOWER_GVEX
        memcpy(m.u.snap.monster, mons, sizeof(NetMonster) * (size_t)nMons);
        m.u.snap.nMonster = (uint8_t)nMons;
#endif
        Send(&cl->tp, &m, false);
        ++s->snapshotsSent;
    }
}
