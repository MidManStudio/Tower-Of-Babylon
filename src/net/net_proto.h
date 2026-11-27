// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_proto.h and net_proto.c"
// ============================================================================
#ifndef NET_PROTO_H
#define NET_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The game's messages between a client and the server, as bytes. Two channels: unreliable datagrams (Input, Snapshot: a lost one is
// replaced by the next, and the client repeats its last few commands in every Input so that a lost one is rarely missed) and a reliable,
// ordered channel (Hello, Welcome, Disconnect). Every packet starts with 'T' 'B' and a kind byte. Decoding checks everything (lengths,
// counts, finite numbers, known flag bits, leftover bytes) and refuses what is wrong; nothing a stranger sends can run off a buffer.
#define NET_PROTOCOL_VERSION 5             // 5: snapshots carry the creatures of the server's world (nMonster and the NetMonster list, 19 bytes each); 4: every connection goes through mid-net's driver (net_driver.h), a different wire format that a build of version 3 cannot speak (it shows in the lobby as another version, and is refused with VERSION); 3: every command says how far behind the server the player saw the others (viewLag, for lag compensation); 2: the buttons are 16 bits and the owner's state carries a dive
#define NET_TICK_RATE        30
#define NET_TICK_SECS        (1.0f / 30.0f)
#define NET_MAX_PLAYERS      4
#define NET_MTU              1200          // the most a datagram may hold
#define NET_INPUT_REDUNDANCY 4             // an Input carries this many commands: the newest tick and the three before it
#define NET_CHARACTER_LEN    24            // a character id, as in the roster (characters/characters.h), with its end

enum {                                     // what the player's hands are doing at a tick (16 bits on the wire: the first eight were full)
    NET_BTN_SPRINT = 1, NET_BTN_USEHELD = 2, NET_BTN_JUMP = 4, NET_BTN_FIRE = 8,
    NET_BTN_USE = 16, NET_BTN_SWAP = 32, NET_BTN_LIGHT = 64, NET_BTN_SLIDE = 128,
    NET_BTN_DIVE = 256, NET_BTN_RELOAD = 512
};

// One tick of the player's wishes, already rounded to what the wire carries: both ends run the game from this rounded command, so
// the client's prediction and the server's simulation start from the same numbers.
// viewLag: the command's tick minus the server tick the others were drawn at on the player's screen when it was made (0 to 255; the client sets it,
// NetCmd_Make leaves it 0). The server rewinds the others by it to judge a shot (net_history.h).
typedef struct { int8_t moveX, moveY; uint16_t yaw; int16_t pitch; uint16_t buttons; uint8_t viewLag; } NetCmd;

NetCmd NetCmd_Make(float moveX, float moveY, float yawRad, float pitchRad, uint16_t buttons);
void   NetCmd_Move(const NetCmd *c, float *x, float *y);        // -1 to 1 each, the length held to 1 (a stranger's 127, 127 is not a faster walk)
float  NetCmd_Yaw(const NetCmd *c);                             // radians, 0 up to a full turn
float  NetCmd_Pitch(const NetCmd *c);                           // radians, a little short of straight up and down at most

// What the owner is told about their own player: enough to put the movement back exactly (full floats, not rounded: a rounded
// state could not be run on from).
enum { NET_OWN_GROUNDED = 1, NET_OWN_EXHAUSTED = 2, NET_OWN_SPRINTING = 4, NET_OWN_SLIDING = 8,
       NET_OWN_DIVING = 16, NET_OWN_ROLLING = 32, NET_OWN_LYING = 64, NET_OWN_JUMPED_SPRINTING = 128 };
typedef struct {
    float px, py, pz, velY, moveVx, moveVz, knockX, knockZ;
    float stamina, staminaDelay, slideT, slideSpeed0, slideCooldown, slideDirX, slideDirZ;
    float diveT, diveDirX, diveDirZ, lieStickX, lieStickY;          // the dive, its roll and lying down after it
    uint8_t flags;
} NetOwnerState;

// What others are told about a player: rounded (millimetres, a 65536th of a turn), enough to draw them.
enum { NET_REM_SPRINTING = 1, NET_REM_SLIDING = 2, NET_REM_GROUNDED = 4 };
typedef struct { uint8_t id; int32_t px, py, pz; uint16_t yaw; int16_t pitch; uint8_t flags; } NetRemote;

// A creature of the server's world (src/gvex/gvex_world.h, G.V.E.X.): what a client needs to draw it. 19 bytes on the wire.
#define NET_MAX_MONSTERS 32                 // one snapshot carries at most this many (GVEX_MAX_CREATURES)
#define NET_MON_KINDS    16                 // a kind byte at or above this is refused: the registry of docs/design/10-gvex-dossier.md has room for 16 to start with
// flags: bits 0-1 the state (0 idle, 1 chase, 2 attack), bits 2-3 the variant (a Ghoul's mobility grade, 0 to 2). Nothing else is set.
#define NET_MON_STATE(f)   ((f) & 3)
#define NET_MON_VARIANT(f) (((f) >> 2) & 3)
#define NET_MON_FLAGS(state, variant) ((uint8_t)(((state) & 3) | (((variant) & 3) << 2)))
typedef struct {
    uint16_t id;                            // the spawn index: the same on every machine; it seeds the creature's looks (enemy_plan.h)
    uint8_t  kind, flags;
    int32_t  px, py, pz;                    // millimetres, like the players'
    uint16_t yaw;                           // 0 to 65535 for 0 to 2 pi
    uint8_t  health;                        // 0 to 255 for 0 to its full health
} NetMonster;

enum { NET_DISC_NORMAL = 0, NET_DISC_VERSION = 1, NET_DISC_FULL = 2, NET_DISC_TIMEOUT = 3, NET_DISC_BAD = 4 };

typedef struct { uint8_t version; char character[NET_CHARACTER_LEN]; } NetHello;
typedef struct {
    uint8_t version, tickRate, playerId, maxPlayers;
    uint32_t floorSeed; int32_t floorNumber; uint32_t serverTick;
    float spawnX, spawnZ;
} NetWelcome;
typedef struct {
    uint32_t tick;                          // the tick of cmds[0]
    uint32_t ackServerTick;                 // the newest snapshot the client has seen
    uint8_t  count;                         // 1 to NET_INPUT_REDUNDANCY; cmds[i] is the command for tick - i
    NetCmd   cmds[NET_INPUT_REDUNDANCY];
} NetInputMsg;
typedef struct {
    uint32_t serverTick;                    // the tick this is the state after
    uint8_t  started;                       // 1 once the server has begun running the recipient's player
    int8_t   margin;                        // how many ticks before it was needed the recipient's newest command arrived (negative: late)
    NetOwnerState owner;
    uint8_t  nRemote;
    NetRemote remote[NET_MAX_PLAYERS - 1];
    uint8_t  nMonster;                      // the creatures of the server's world (0 in a build without TOWER_GVEX)
    NetMonster monster[NET_MAX_MONSTERS];
} NetSnapshot;
typedef struct { uint8_t reason; } NetDisconnect;

typedef enum { NET_MSG_NONE = 0, NET_MSG_INPUT, NET_MSG_SNAPSHOT, NET_MSG_HELLO, NET_MSG_WELCOME, NET_MSG_DISCONNECT } NetMsgKind;
typedef struct {
    NetMsgKind kind;
    union { NetHello hello; NetWelcome welcome; NetInputMsg input; NetSnapshot snap; NetDisconnect disc; } u;
} NetMsg;

// Writes the message into buf; returns its length, or 0 when it does not fit or is not valid to send.
size_t NetProto_Encode(const NetMsg *m, uint8_t *buf, size_t cap);
// Reads a packet. False (and *out unusable) when anything is wrong with it.
bool NetProto_Decode(const uint8_t *buf, size_t len, NetMsg *out);

#endif
