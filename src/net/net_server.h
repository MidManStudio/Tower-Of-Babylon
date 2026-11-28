// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_server.h and net_server.c"
// ============================================================================
#ifndef NET_SERVER_H
#define NET_SERVER_H

#include "net_history.h"
#include "net_move_sim.h"
#include "net_proto.h"
#include "net_transport.h"
#include "../dungeon/dungeon_types.h"
#include "../weapons/weapons.h"

// The authoritative side: it owns the floor and every player, runs them at NET_TICK_RATE from the commands the clients send (never from
// positions the clients claim), and tells each client the result in a snapshot every tick. The caller drives time: NetServer_Poll as
// often as it likes (it only reads what has arrived), NetServer_Tick once every NET_TICK_SECS.
#define NET_INPUT_RING   32      // commands waiting for their tick, per client
#define NET_MAX_LEAD     20      // a command more than this many ticks ahead of the server is refused (a clock far ahead, or a flood)
#define NET_MAX_REPEAT   5       // a command that did not arrive is replaced by the last one for this many ticks, then by doing nothing
#define NET_TIMEOUT_MS   5000    // a client not heard from for this long is dropped
#define NET_HELLO_TIMEOUT_MS 3000  // a connection that has not said Hello by then is dropped (it would hold a place for nothing)
#define NET_MAX_PACKETS_PER_POLL 64   // per client: the rest wait for the next poll, so a flood cannot hold the server up

typedef struct {
    bool        used, welcomed, started;
    NetTransport tp;
    uint8_t     id;
    char        character[NET_CHARACTER_LEN];
    double      lastHeardMs, connectedMs;
    PlayerController player;
    Arsenal     arsenal;             // what the player holds and how its attacks are timed (weapons.h): hands only for now; the fire button works it (RunHands, net_server.c)
    NetCmd      ring[NET_INPUT_RING];
    uint32_t    ringTick[NET_INPUT_RING];
    bool        ringHave[NET_INPUT_RING];
    uint32_t    startTick;           // the tick of the first command; the player is not run before it
    NetCmd      lastCmd;
    int         repeats;
    int8_t      margin;              // newest command's tick minus the tick about to be run: how early it came
    // What happened, for the tests and for tuning.
    unsigned    cmdsAccepted, cmdsLate, cmdsTooFar, cmdsDuplicate, ticksFed, ticksRepeated, ticksNeutral, badPackets;
} NetServerClient;

typedef struct {
    FloorMap    map;
    int         floorNumber;
    uint32_t    floorSeed;
    uint32_t    tick;                // ticks run so far
    double      nowMs;
    NetServerClient c[NET_MAX_PLAYERS];
    unsigned    snapshotsSent;
    NetHistory  hist;                // where everyone was at each of the last ticks (net_history.h)
    NetMonHistory mhist;             // and where each creature was, for the same reason: a punch is judged where the puncher saw the creature
    struct GvexWorld *gvex;          // the creatures of the game (src/gvex/gvex_world.h, docs/gvex_session.md): NULL in a build without TOWER_GVEX
} NetServer;

void NetServer_Init(NetServer *s, uint32_t floorSeed, int floorNumber);                 // makes the floor from the seed, the way every client will (and, with TOWER_GVEX, the creatures)
void NetServer_Free(NetServer *s);                                                       // frees what Init made that is not in the struct (the creatures' world); safe twice
int  NetServer_AddClient(NetServer *s, NetTransport tp, double nowMs);                  // the slot, or -1 when full (the newcomer is told)
void NetServer_Drop(NetServer *s, int slot, uint8_t reason);                            // tells the client, frees the slot
void NetServer_Poll(NetServer *s, double nowMs);                                         // reads what has arrived; drops clients that went quiet
void NetServer_Tick(NetServer *s, double nowMs);                                         // runs one tick for everyone and sends the snapshots
int  NetServer_PlayerCount(const NetServer *s);
// Where player `id` was when a shooter whose command is for the tick being run saw the world `viewLag` ticks before (clamped to NET_REWIND_MAX):
// the pose a shot is judged against. False if the player was not there. Call it while running that tick's commands (the server's `tick`).
bool NetServer_RewoundPose(const NetServer *s, uint8_t viewLag, int id, Vector3 *pos, float *yaw);
// The creatures a melee attack of `def` hits: a swing from `eye`, facing `yaw`, by a player whose screen showed the creatures `viewLag` ticks before the
// tick being run (clamped to NET_REWIND_MAX). Each creature is taken where that screen had it (the shared rule of enemy_plan.h, EnemyMelee_Reaches: the
// same as the single-player game's); one with no frame kept for that tick (a lag of 0, or older than the ring) is taken where it is now. Writes up to
// `max` ids and returns how many. 0 in a build without TOWER_GVEX. It judges and changes nothing.
int NetServer_MeleeTargets(const NetServer *s, uint8_t viewLag, Vector3 eye, float yaw, const WeaponDef *def, uint16_t *ids, int max);

#endif
