// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net.md, section "net_play.h and net_play.c"
// ============================================================================
#ifndef NET_PLAY_H
#define NET_PLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "net_client.h"
#include "../input/input_latch.h"

// The player's side of walking in a networked game: what the fingers and keys say becomes one NetCmd per tick, and what the client has
// predicted becomes the camera. The look is turned every frame, at once (nothing waits for a tick or the network); the command for a tick carries
// the yaw and pitch as they are then. The buttons are the same latched presses single play uses (input_latch.h): a press between two ticks is kept
// until the next tick takes it, so nothing is lost at 30 ticks a second on a 144 Hz screen.
typedef struct {
    SimCmd           latch;                // presses and held states since the last tick
    PlayerController look;                 // only its yaw and pitch are used: the view the player is turning
} NetPlay;

void NetPlay_Init(NetPlay *n, float yaw);
// Once per frame, after Input_Update: keeps the presses and turns the view by the frame's look movement (pixels; a swipe longer than maxLookPx is cut to it).
void NetPlay_Input(NetPlay *n, const InputFrame *in, float maxLookPx);
// The command for the next tick (the latched presses are used up).
NetCmd NetPlay_Take(NetPlay *n);
// The same as a NetCmdFn: user is the NetPlay (NetSession_SetCommands, or NetClient_Update).
NetCmd NetPlay_CmdFn(void *user, uint32_t tick);
// The buttons of a SimCmd as the wire carries them.
uint16_t NetPlay_Buttons(const SimCmd *s);

// The camera for this frame: at the predicted player blended between the last two ticks, turned the way the player is looking now, at the field of
// view the walk gives (a sprint widens it). The client's own player is untouched.
void NetPlay_Camera(const NetPlay *n, const NetClient *c, Camera3D *out);

// Which touch buttons exist: SLIDE while sprinting forward or sliding, DIVE where a dive can be asked for (the same rules as single play).
bool NetPlay_SlideShown(const PlayerController *p);
bool NetPlay_DiveShown(const PlayerController *p);

#endif
