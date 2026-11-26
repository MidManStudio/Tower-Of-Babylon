// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/dungeon.md, section "main.c"
// ============================================================================
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "platform/platform.h"

#if TOWER_MAIN_LOOP_CALLBACK
#include <emscripten/emscripten.h>
#endif

#include "dungeon/dungeon_types.h"
#include "dungeon/floor_window.h"
#include "dungeon/dungeon_renderer.h"
#include "dungeon/staircase.h"
#include "obstacles/obstacles.h"
#include "player/player_controller.h"
#include "projectiles/projectiles.h"
#include "config/game_config.h"
#include "flashlight/flashlight.h"
#include "weapons/gunfire.h"
#include "enemies/enemy_manager.h"
#include "ui/screen_fx.h"
#include "weapons/weapons.h"
#include "weapons/pickups.h"
#include "weapons/viewmodel.h"
#include "lighting/lighting.h"
#include "inventory/inventory.h"
#include "inventory/inventory_ui.h"
#include "ui/view_clip.h"
#include "ui/highp_shader.h"
#include "audio/audio.h"
#include "audio/audio_events.h"
#include "input/input.h"
#include "hud/hud.h"
#include "floor/floor_game.h"
#include "core/fixed_step.h"
#include "input/input_latch.h"
#include "characters/characters.h"
#include "characters/character_select.h"
#include "characters/character_select_ui.h"
#include "lobby/lobby.h"
#include "lobby/lobby_ui.h"
#include "lobby/lobby_net.h"
#include "lobby/session_ui.h"
#include "lobby/play_ui.h"
#include "net/net_session.h"
#include "net/net_play.h"
#include "swarm/swarm.h"
#include "swarm/swarm_config.h"
#include "sim/game_sim.h"

// The rules of the game: one player on the tower and everything the rules need (src/sim/game_sim.h). It draws nothing, plays nothing and
// reads no clock. This file gives it commands 30 times a second, plays what it reports, and draws what it holds (docs/sim.md).
static GameSim g_sim;

// The depth range of the 3D view lives in ui/view_clip.h: the phone's browser may give a 16 bit depth
// buffer, so the world uses a near plane of 0.25 and the hands get a range of their own.

// What the browser gave the page, shown in the diagnostic overlay; -1 means unknown.
static int g_depthBits = -1, g_msaaSamples = -1;
// The "3d" stage split into parts, in milliseconds: the worst of recent frames for each, so a spike is not missed.
enum { SUB_LIGHT, SUB_WORLD, SUB_ENTS, SUB_HANDS, SUB_COUNT };
static float subMs[SUB_COUNT];
static void SubNote(int which, double t0, double t1)
{
    const float ms = (float)((t1 - t0) * 1000.0);
    subMs[which] = ms > subMs[which] ? ms : subMs[which] * 0.985f + ms * 0.015f;
}

// Game state. File scope, because on the web the frame function is called by the browser.
static const int screenWidth = 800;
static const int screenHeight = 450;

// Screen effects. They are the picture's, not the rules': the simulation reports "a hit landed", "the player came back", "a medkit
// was used" as events, and these are set from them (ApplySimEvents) and fade by the clock, not by the step.
static float healFlash = 0.0f;
static float fade = 0.0f;          // fade in after arriving
static float hitMarker = 0.0f, killMarker = 0.0f;
#define HIT_MARKER_SECS  0.25f
#define KILL_MARKER_SECS 0.50f

// The largest look movement accepted in one frame, in canvas pixels.
#define MAX_LOOK_PX 90.0f

// What the rules reported since the last call, as sounds, messages and effects. The simulation never plays or draws anything itself.
static void ApplySimEvents(void)
{
    SimEvent ev;
    while (GameSim_PopEvent(&g_sim, &ev)) {
        switch (ev.kind) {
            case SIMEV_TOAST:       Hud_Toast(ev.text); break;
            case SIMEV_HIT:         hitMarker = 1.0f; break;
            case SIMEV_KILL:        killMarker = 1.0f; break;
            case SIMEV_FLOOR:       Lighting_BuildForFloor(g_sim.map, ev.arg); break;
            case SIMEV_FADE_IN:     fade = 1.0f; break;
            case SIMEV_RESET_AUDIO: AudioEvents_Reset(); break;
            case SIMEV_HEAL_FLASH:  healFlash = 1.0f; break;
            case SIMEV_SOUND:
                switch ((SimSound)ev.sound) {
                    case SIMSND_HURT:          AudioEvents_Hurt(); break;
                    case SIMSND_DOOR:          AudioEvents_Door(); break;
                    case SIMSND_SHOT:          AudioEvents_Shot(ev.arg); break;
                    case SIMSND_DRYFIRE:       AudioEvents_DryFire(); break;
                    case SIMSND_MELEE:         AudioEvents_Melee(ev.arg != 0); break;
                    case SIMSND_HITMARKER:     AudioEvents_HitMarker(ev.arg != 0); break;
                    case SIMSND_PICKUP:        AudioEvents_Pickup(); break;
                    case SIMSND_USEITEM:       AudioEvents_UseItem(); break;
                    case SIMSND_BATTERY:       AudioEvents_Battery(); break;
                    case SIMSND_FLASHLIGHT:    AudioEvents_Flashlight(ev.arg != 0); break;
                    case SIMSND_CONSOLE_START: AudioEvents_ConsoleStart(); break;
                    case SIMSND_UNLOCKED:      AudioEvents_Unlocked(); break;
                    case SIMSND_COUNT_TICK:    AudioEvents_CountTick(ev.arg); break;
                    case SIMSND_COLLAPSE:      AudioEvents_Collapse(); break;
                    case SIMSND_RELOAD:        AudioEvents_Reload(ev.arg); break;
                    case SIMSND_BREAK:         AudioEvents_Break(); break;
                }
                break;
        }
    }
}

static void DoInventoryTap(Vector2 tap)
{
    const InvUiResult r = InvUI_Tap(&g_sim.inv, tap);
    switch (r.action) {
        case INVUI_CLOSE:  g_sim.invOpen = false; break;
        case INVUI_SELECT: g_sim.inv.selected = (g_sim.inv.selected == r.slot) ? -1 : r.slot; break;
        case INVUI_USE:    GameSim_UseSlot(&g_sim, r.slot); break;
        case INVUI_EQUIP:  GameSim_EquipSlot(&g_sim, r.slot); break;
        case INVUI_DROP:   GameSim_DropSlot(&g_sim, r.slot); break;
        default: break;
    }
    ApplySimEvents();
}

// Longest step the simulation takes in one frame. A hitch or a tab switch must not
// turn into a jump across the map.
#define MAX_FRAME_DT 0.05f

// ---- The rules run 30 times a second ---------------------------------------------------------------------------
// The screen is drawn at whatever pace the display runs (60, 144 frames a second). The rules (movement, weapons, monsters, shots, the
// stair clock) step at a fixed 30 a second, so the game plays the same at any frame rate and a server can run the same steps.
//   every frame:  input, UI taps, turning the camera (aim never waits for a step), the sound and the flashlight, drawing
//   every step:   GameSim_Step(): everything that decides what happens (src/sim/game_sim.c)
// A press that comes and goes between two steps is kept until the next step takes it (input/input_latch.h), and the picture blends
// between the last two steps (the camera, the monsters, the shots). See docs/sim.md.
static FixedStep g_simStep;
static SimCmd g_simCmd;

// ---- The screens before the game: the lobby, and the character screen it opens -------------------------------------------------------
// The game starts on the lobby (src/lobby, docs/lobby.md). Nothing of the game runs behind these screens (no steps, no monsters, no clock): they
// are only buttons, cards and text. The lobby's CHOOSE CHARACTER opens the character screen (src/characters, docs/characters.md); choosing there,
// or BACK, returns to the lobby; PLAY SOLO starts the game as if it had just been loaded.
typedef enum { SCREEN_LOBBY = 0, SCREEN_CHARACTER, SCREEN_NET, SCREEN_GAME } AppScreen;
static AppScreen g_screen = SCREEN_LOBBY;
static Lobby g_lobby;
static LobbyBackend g_lobbyNet;
static CharSelect g_charSel;
static NetSession g_session;                 // a hosted or joined game (net/net_session.h); idle until the lobby starts one
static NetPlay g_netPlay;                    // walking in it: the fingers' commands and the view (net/net_play.h)
static bool g_walkBegun;                     // the walk's first frame has built the session's lighting
static bool g_netLightingDirty;              // the lights were built for a session's floor: a new run rebuilds solo's
static float g_leaveArmed;                   // seconds left of a first press of the keyboard's leave

// What the player is called in a game they host or join: the chosen character's id ("" when there is no roster), and "STEVE'S TOWER" for the lobby.
static void ChosenCharacterId(char *out, size_t cap)
{
    const CharacterDef *c = Characters_Selected() >= 0 ? Characters_Get(Characters_Selected()) : NULL;
    snprintf(out, cap, "%s", c ? c->id : "");
}

static void HostedGameName(char *out, size_t cap)
{
    const CharacterDef *c = Characters_Selected() >= 0 ? Characters_Get(Characters_Selected()) : NULL;
    if (c) snprintf(out, cap, "%.15s'S TOWER", c->call); else snprintf(out, cap, "TOWER");
}

// The sound card opens after a touch or a key, and the first one happens on these screens.
static void MenuAudio(float dt, bool gesture)
{
    float lfx, lfz;
    GameSim_FlatForward(&g_sim, &lfx, &lfz);
    Audio_Update(dt, g_sim.player.camera.position, lfx, lfz, gesture);
}

static void BeginRun(void)
{
    g_screen = SCREEN_GAME;
    Staircase_SetFrame(g_sim.map->w, g_sim.map->h);   // the stair code follows the floor in play (a session may have set 45)
    if (g_netLightingDirty) { Lighting_BuildForFloor(g_sim.map, g_sim.window.currentFloor); g_netLightingDirty = false; }   // a session's floor was the last one lit
    FixedStep_Init(&g_simStep, SIM_STEP_SECS, SIM_MAX_STEPS_PER_FRAME);   // the game starts from a clean clock
    SimCmd_Init(&g_simCmd);
    fade = 1.0f;                                                          // and fades in, as after a door
}

// A hosted or joined game starts with the player walking: the commands come from the fingers (net/net_play.h), the view starts facing the way the
// player does when they appear (yaw 0, as the floor's spawn faces), and the leave key has not been pressed.
static void BeginNetPlay(void)
{
    Staircase_SetFrame(MAP_WIDTH, MAP_HEIGHT);     // online floors are 45 by 45 whatever TOWER_FLOOR_TILES is (src/net makes them)
    NetPlay_Init(&g_netPlay, 0.0f);
    NetSession_SetCommands(&g_session, NetPlay_CmdFn, &g_netPlay);
    g_leaveArmed = 0.0f;
    g_walkBegun = false;
}

static void LobbyFrame(void)
{
    float dt = GetFrameTime();
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    GameConfig_Poll();                                    // the web build swaps the real characters.mdix in when the wasm is up
    Lobby_SetContext(&g_lobby, Characters_Count(), Characters_Selected() >= 0, NetSession_CanHost());

    InputMenu menu;
    Input_Menu(&menu);
    Vector2 tap = { 0 };
    if (Input_NewTap(&tap)) Lobby_Tap(&g_lobby, tap.x, tap.y);
    if (menu.step != 0) Lobby_Move(&g_lobby, menu.step);
    if (menu.confirm) Lobby_Press(&g_lobby);
    Lobby_Update(&g_lobby, &g_lobbyNet, GetTime() * 1000.0);
    MenuAudio(dt, menu.gesture);

    // A join in progress: the host answers (the game is joined), or does not (back to the list, with the reason).
    const double netNow = NetUdp_NowMs();
    if (g_lobby.screen == LOBBY_CONNECTING) {
        NetSession_Update(&g_session, netNow);
        if (g_session.state == NETS_PLAYING) { Lobby_BackHome(&g_lobby); g_screen = SCREEN_NET; }
        else if (g_session.state == NETS_FAILED) { Lobby_ConnectFailed(&g_lobby, g_session.error); NetSession_Leave(&g_session); }
    }

    char who[64], gameName[64];
    ChosenCharacterId(who, sizeof who);
    switch (Lobby_TakeWant(&g_lobby)) {
        case LOBBY_WANT_CHARACTER:
            CharSelect_Open(&g_charSel, Characters_Count(), Characters_Selected());
            g_screen = SCREEN_CHARACTER;
            break;
        case LOBBY_WANT_PLAY:
            BeginRun();
            break;
        case LOBBY_WANT_HOST: {
            HostedGameName(gameName, sizeof gameName);
            uint32_t seed = (uint32_t)netNow * 2654435761u;
            if (seed == 0) seed = 1;
            if (NetSession_Host(&g_session, NET_TRANSPORT_UDP, gameName, who, seed, netNow)) { BeginNetPlay(); g_screen = SCREEN_NET; }
            else { Lobby_SetNotice(&g_lobby, g_session.error); NetSession_Leave(&g_session); }
            break;
        }
        case LOBBY_WANT_JOIN: {
            LobbyEntry target;
            if (!Lobby_JoinTarget(&g_lobby, &target)) break;
            if (NetSession_Join(&g_session, target.transportId, target.address, target.port, target.name, who, netNow)) { BeginNetPlay(); Lobby_BeginConnect(&g_lobby, target.name); }
            else { Lobby_SetNotice(&g_lobby, g_session.error); NetSession_Leave(&g_session); }
            break;
        }
        case LOBBY_WANT_CANCEL:
            NetSession_Leave(&g_session);
            break;
        default: break;
    }

    BeginDrawing();
        LobbyUI_Draw(&g_lobby, Input_Scheme() == INPUT_SCHEME_KEYBOARD);
    EndDrawing();
}

// A hosted or joined game. Until the game answers (the host's own connection to itself takes a frame or two) the screen says CONNECTING; then the
// player walks in the session's floor, sees the others, and leaves with the LEAVE button (or Tab twice on a keyboard). The game ending by itself
// (the host left, the connection was lost) returns to the lobby with the reason in red.
static void EndNet(bool notice)
{
    if (notice) Lobby_SetNotice(&g_lobby, g_session.error);
    NetSession_Leave(&g_session);
    Lobby_BackHome(&g_lobby);
    g_walkBegun = false;
    g_screen = SCREEN_LOBBY;
}

static void NetWaitFrame(void)
{
    float dt = GetFrameTime();
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    InputMenu menu;
    Input_Menu(&menu);
    bool leave = menu.confirm;
    Vector2 tap = { 0 };
    if (Input_NewTap(&tap) && UiRect_Contains(SessionUI_Leave(), tap.x, tap.y)) leave = true;
    MenuAudio(dt, menu.gesture);
    NetSession_Update(&g_session, NetUdp_NowMs());

    if (leave) EndNet(false);
    else if (g_session.state == NETS_FAILED || g_session.state == NETS_ENDED) EndNet(true);

    BeginDrawing();
        SessionUI_Draw(&g_session, Input_Scheme() == INPUT_SCHEME_KEYBOARD);
    EndDrawing();
}

static void NetWalkFrame(void)
{
    float dt = GetFrameTime();
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    if (!g_walkBegun) {                                   // the first frame of the walk: the lights of the session's floor
        PlayUI_Begin(&g_session);
        g_netLightingDirty = true;
        g_walkBegun = true;
    }
    const PlayerController *me = &g_session.client.player;

    // What the player wants this frame: the same controls as solo, with the buttons the walk has. LEAVE only takes taps.
    InputContext ictx = { 0 };
    ictx.dt = dt;
    ictx.aimMode = TC_DEFAULT_AIM_MODE;
    ictx.slideAvailable = NetPlay_SlideShown(me);
    ictx.diveAvailable = NetPlay_DiveShown(me);
    { const UiRect lv = SessionUI_Leave(); ictx.blocked[ictx.blockedCount++] = lv; }
    InputFrame in;
    Input_Update(&ictx, &in);
    NetPlay_Input(&g_netPlay, &in, MAX_LOOK_PX);          // the view turns now; the commands go out at the tick

    bool leave = false;
    Vector2 tap = { 0 };
    if (Input_NewTap(&tap) && UiRect_Contains(SessionUI_Leave(), tap.x, tap.y)) leave = true;
    if (in.inventoryToggle) { if (g_leaveArmed > 0.0f) leave = true; else g_leaveArmed = 3.0f; }     // Tab twice (nothing else uses it here)
    if (g_leaveArmed > 0.0f) g_leaveArmed -= dt;

    const double nowMs = NetUdp_NowMs();
    NetSession_Update(&g_session, nowMs);                 // runs the ticks that are due, asking NetPlay for each command
    if (leave) { EndNet(false); return; }
    if (g_session.state == NETS_FAILED || g_session.state == NETS_ENDED) { EndNet(true); return; }

    Camera3D cam;
    NetPlay_Camera(&g_netPlay, &g_session.client, &cam);
    {
        const float fl = sqrtf((cam.target.x - cam.position.x) * (cam.target.x - cam.position.x) + (cam.target.z - cam.position.z) * (cam.target.z - cam.position.z));
        const float fx = fl > 0.0001f ? (cam.target.x - cam.position.x) / fl : 0.0f, fz = fl > 0.0001f ? (cam.target.z - cam.position.z) / fl : 1.0f;
        Audio_Update(dt, cam.position, fx, fz, in.gesture);
        AudioEvents_Movement(dt, PlayerSpeed(me), me->isGrounded, me->velocityY, me->sprinting, me->sliding, me->exhausted, me->health.isAlive);
    }
    Lighting_UpdateAround((float)GetTime(), cam.position.x, cam.position.z);
    BeginDrawing();
        ClearBackground(BLACK);
        Highp_Begin();
        BeginMode3D(cam);
            ViewClip_Apply(&cam, WORLD_NEAR, WORLD_FAR);
            PlayUI_Draw3D(&g_session, &cam, nowMs);
            Highp_FlashlightOff();
        EndMode3D();
        PlayUI_DrawTags(&g_session, &cam, nowMs);
        PlayUI_DrawOverlay(&g_session, Input_Scheme() == INPUT_SCHEME_KEYBOARD, g_leaveArmed > 0.0f);
        Highp_End();
    EndDrawing();
}

static void NetFrame(void)
{
    if (g_session.state == NETS_PLAYING) NetWalkFrame();
    else NetWaitFrame();
}

static void SelectFrame(void)
{
    float dt = GetFrameTime();
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    GameConfig_Poll();                                    // the web build swaps the real characters.mdix in when the wasm is up
    CharSelect_Resync(&g_charSel, Characters_Count());

    InputMenu menu;
    Input_Menu(&menu);
    Vector2 tap = { 0 };
    if (Input_NewTap(&tap)) CharSelect_Tap(&g_charSel, tap.x, tap.y);
    if (menu.step != 0) CharSelect_Move(&g_charSel, menu.step);
    if (menu.confirm) CharSelect_Press(&g_charSel);
    MenuAudio(dt, menu.gesture);

    if (g_charSel.done) {
        const CharacterDef *c = Characters_Get(g_charSel.focus);
        if (c) Characters_Select(c->id);
        g_screen = SCREEN_LOBBY;
    } else if (g_charSel.back) {
        g_screen = SCREEN_LOBBY;
    }

    BeginDrawing();
        CharSelectUI_Draw(&g_charSel, Input_Scheme() == INPUT_SCHEME_KEYBOARD);
    EndDrawing();
}

static void Frame(void)
{
    // A session has no props yet (the server has none), so while one is on, the player's movement must not meet solo's: see obstacles.h.
    Obstacles_Suspend(g_session.state == NETS_CONNECTING || g_session.state == NETS_PLAYING);
    if (g_screen == SCREEN_LOBBY) { LobbyFrame(); return; }
    if (g_screen == SCREEN_CHARACTER) { SelectFrame(); return; }
    if (g_screen == SCREEN_NET) { NetFrame(); return; }
    const double tFrame0 = GetTime();
    const float rawDt = GetFrameTime();
    float dt = rawDt;
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    GameConfig_Poll();                // swaps in the .mdix files as soon as the DixScript wasm has started (web builds)

    int nearDoor, nearPickup; bool nearConsole;
    GameSim_FindNear(&g_sim, &nearDoor, &nearPickup, &nearConsole);

    // Taps this frame: the STATS button and popup, the INV button, the inventory screen.
    const bool wasOpen = g_sim.invOpen;
    Vector2 tap = { 0 };
    const bool tapped = Input_NewTap(&tap);
    if (tapped && Hud_HandleTap(tap, wasOpen)) { /* the HUD used it */ }
    else if (tapped && !wasOpen && CheckCollisionPointRec(tap, InvUI_OpenButton())) g_sim.invOpen = true;
    else if (tapped && wasOpen) DoInventoryTap(tap);
    if (g_sim.invOpen) Hud_SetStatsOpen(false);
    Hud_Update(dt);
    ApplySimEvents();                 // what a tap on the inventory did

    bool useAvailable = (nearDoor >= 0 || nearPickup >= 0 || nearConsole) && !g_sim.invOpen;

    // What the player wants this frame, from whatever the platform has: fingers, mouse, keyboard (src/input). The context says
    // which controls are live: nothing while a door opens or the player is dead, only walking with the inventory open,
    // and a button only when it has something to act on.
    InputContext ictx = { 0 };
    ictx.dt = dt;                                     // the joystick style of fire pad aims at a rate, so it needs the frame's time
    ictx.aimMode = TC_DEFAULT_AIM_MODE;               // how the fire pad aims (a setting, once there is a settings screen)
    ictx.locked = g_sim.seq != SEQ_NONE || !g_sim.player.health.isAlive;
    ictx.walkOnly = g_sim.invOpen;
    ictx.useAvailable = useAvailable;
    ictx.swapAvailable = Arsenal_OwnedCount(&g_sim.arsenal) > 1;
    ictx.lightAvailable = Inventory_Has(&g_sim.inv, ITEM_FLASHLIGHT);
    ictx.slideAvailable = GameSim_SlideButtonShown(&g_sim);
    ictx.reloadAvailable = Arsenal_CanReload(&g_sim.arsenal) && g_sim.player.health.isAlive;
    ictx.diveAvailable = GameSim_DiveButtonShown(&g_sim) && !g_sim.invOpen;       // in the air after a sprinting jump
    ictx.blockedCount = Hud_BlockedRects(ictx.blocked, TC_MAX_BLOCKED - 1);
    {
        const Rectangle ib = InvUI_OpenButton();
        ictx.blocked[ictx.blockedCount++] = (UiRect){ ib.x, ib.y, ib.width, ib.height };
    }
    InputFrame in;
    Input_Update(&ictx, &in);
    if (in.inventoryToggle) g_sim.invOpen = !g_sim.invOpen;
    if (in.statsToggle && !g_sim.invOpen) Hud_ToggleStats();
    SimCmd_Add(&g_simCmd, &in);                       // the rules take it at the next step

    // Aim: turned every frame, so it never waits for a step. Size of this frame's look movement as the input gave it, for the
    // frame graph, then capped.
    Vector2 lookDelta = in.look;
    const float lookRaw = sqrtf(lookDelta.x * lookDelta.x + lookDelta.y * lookDelta.y);
    if (lookRaw > MAX_LOOK_PX) lookDelta = Vector2Scale(lookDelta, MAX_LOOK_PX / lookRaw);
    FrameGraph_Push(rawDt * 1000.0f, lookRaw);
    if (g_sim.seq == SEQ_NONE && g_sim.player.health.isAlive) PlayerApplyLook(&g_sim.player, lookDelta);
    PlayerSyncCamera(&g_sim.player);                        // the camera's direction follows; its position is as the last step left it

    const double tInput = GetTime();
    // The rules: as many fixed steps as the time since the last frame is worth (a stall is capped, and the rest dropped).
    const int steps = FixedStep_Advance(&g_simStep, rawDt);
    for (int i = 0; i < steps; ++i) GameSim_Step(&g_sim, SimCmd_Take(&g_simCmd));
    ApplySimEvents();
    const double tSim = GetTime();
    const float alpha = FixedStep_Alpha(&g_simStep);  // how far the next step has got: the picture is blended that far

    // What the steps changed, as the screen and the speakers see it.
    GameSim_FindNear(&g_sim, &nearDoor, &nearPickup, &nearConsole);
    useAvailable = (nearDoor >= 0 || nearPickup >= 0 || nearConsole) && !g_sim.invOpen;

    // The flashlight: drains while on, flickers when low, and its beam follows where the camera looks (every frame, so the beam
    // is never behind the view; its charge drains by time, so the frame rate does not change it).
    GameSim_TorchFrame(&g_sim, dt);
    ApplySimEvents();

    // Sound: where the listener is, then what the body is doing (footsteps, jump, landing, slide, heartbeat).
    {
        float lfx, lfz;
        GameSim_FlatForward(&g_sim, &lfx, &lfz);
        Audio_Update(dt, g_sim.player.camera.position, lfx, lfz, in.gesture);             // a browser opens the sound card only after a touch or a key
        AudioEvents_Movement(dt, sqrtf(g_sim.player.moveVel.x * g_sim.player.moveVel.x + g_sim.player.moveVel.z * g_sim.player.moveVel.z), g_sim.player.isGrounded, g_sim.player.velocityY,
                             g_sim.player.sprinting, g_sim.player.sliding, g_sim.player.exhausted, g_sim.player.health.isAlive);
        AudioEvents_Health(g_sim.player.health.hp / g_sim.player.health.maxHp, g_sim.player.health.isAlive);
        AudioEvents_Ambience(g_sim.player.health.isAlive);
        Vector3 awakeAt = { 0 };
        float awakeDist = 0.0f;
        const bool awake = g_sim.player.health.isAlive && Enemies_NearestAwake(g_sim.player.position, 40.0f, &awakeAt, &awakeDist);
        AudioEvents_Enemies(dt, awake, awakeAt, awakeDist);
    }

    // Screen effects that fade by the clock, not by the step.
    if (fade > 0.0f) fade -= dt * 1.6f;
    hitMarker -= dt / HIT_MARKER_SECS;   if (hitMarker < 0.0f) hitMarker = 0.0f;
    killMarker -= dt / KILL_MARKER_SECS; if (killMarker < 0.0f) killMarker = 0.0f;
    if (healFlash > 0.0f) healFlash -= dt * 2.0f;
    {   // where the collapse of the floor has got to, for the renderer
        float cx, cz, cr;
        FloorGame_CollapseWave(&cx, &cz, &cr);
        DrawFloorMapCollapse(cx, cz, cr);
    }

    // Drawn between the last two steps: the camera, the monsters and the shots are blended that far from where the last step began.
    Enemies_SetRenderAlpha(alpha);
    Projectiles_SetRenderLag((1.0f - alpha) * SIM_STEP_SECS);
    Camera3D viewCam = g_sim.player.camera;
    // Aiming down sights and leaning are blended between steps like the eye: the field of view, the tilt and the hands' pose all
    // move smoothly through the 0.2 s they take, not in 30 Hz jumps.
    const float aimView = g_sim.prevAim + (g_sim.player.aimBlend - g_sim.prevAim) * alpha;
    const float leanView = g_sim.prevLean + (g_sim.player.leanBlend - g_sim.prevLean) * alpha;
    {
        const Vector3 dir = Vector3Subtract(g_sim.player.camera.target, g_sim.player.camera.position);
        viewCam.position = Vector3Lerp(g_sim.prevEye, g_sim.player.camera.position, alpha);
        viewCam.target = Vector3Add(viewCam.position, dir);
        viewCam.fovy = PlayerViewFov(&g_sim.player, aimView);
        viewCam.up = PlayerViewUp(Vector3Normalize(dir), leanView);
    }
    const AimFrame viewAim = Aim_FromCamera(&viewCam);

    const double tAi = GetTime();
    const double tLight0 = GetTime();
    Lighting_UpdateAround((float)GetTime(), viewCam.position.x, viewCam.position.z);
    SubNote(SUB_LIGHT, tLight0, GetTime());
    BeginDrawing();
        ClearBackground(BLACK);
        Highp_Begin();                    // from here on, vertex math is 32 bit even on a phone (see ui/highp_shader.h)

        BeginMode3D(viewCam);
            ViewClip_Apply(&viewCam, WORLD_NEAR, WORLD_FAR);
            // Only what is inside a cone around where the camera looks is drawn, up or down as well as sideways.
            Vector3 look = { viewCam.target.x - viewCam.position.x, viewCam.target.y - viewCam.position.y,
                             viewCam.target.z - viewCam.position.z };
            // The flashlight: held a little to the right of and below the eye. The renderer keeps the dark surfaces the beam reaches,
            // and the beam shader lights them (or, with no shader, a soft glow stands in).
            const Vector3 torchPos = { viewCam.position.x + viewAim.forward.x * 0.10f + viewAim.right.x * 0.12f - viewAim.up.x * 0.08f,
                                       viewCam.position.y + viewAim.forward.y * 0.10f + viewAim.right.y * 0.12f - viewAim.up.y * 0.08f,
                                       viewCam.position.z + viewAim.forward.z * 0.10f + viewAim.right.z * 0.12f - viewAim.up.z * 0.08f };
            if (g_sim.torch.level > 0.0f) {
                const FlashlightConfig *fc = Flashlight_Config();
                Highp_SetFlashlight(torchPos, g_sim.torch.dir, cosf(fc->beamInnerDeg * DEG2RAD), cosf(fc->beamOuterDeg * DEG2RAD), fc->range, fc->strength * g_sim.torch.level, fc->tint);
            }
            DrawFloorMapFlashlight(&g_sim.torch, torchPos, !Highp_FlashlightAvailable());
            const double tWorld0 = GetTime();
            DrawFloorMapView(g_sim.map, viewCam.position, look, 0.0f);
            SubNote(SUB_WORLD, tWorld0, GetTime());
            const double tEnts0 = GetTime();
            DrawObstaclesSystem();
            Pickups_Draw(g_sim.player.position, (float)GetTime());
            DrawEnemiesSystem(g_sim.player.position); // Render the enemies, with attack and hit animations
            Swarm_Draw(&g_sim.swarm, viewCam.position, alpha);     // the worm swarm and, as a thin red frame, its health box

            DrawStairs(g_sim.map, g_sim.doorOpen, g_sim.player.position);
            FloorGame_Draw(g_sim.player.position, (float)GetTime());
            Highp_FlashlightOff();                              // the tracers, the hands and the screen are not lit by the beam
            DrawProjectilesSystem(viewCam.position);      // light that adds to what is behind it: after everything opaque
            SubNote(SUB_ENTS, tEnts0, GetTime());
            // The hands and what they hold, over everything else in the 3D view.
            const double tHands0 = GetTime();
            if (g_sim.player.health.isAlive) {
                const float walk01 = PlayerSpeed(&g_sim.player) / PLAYER_WALK_SPEED > 1.5f ? 1.5f : PlayerSpeed(&g_sim.player) / PLAYER_WALK_SPEED;
                // Aimed and moving left or right, the gun tilts a little toward the move (eased over a tenth of a second).
                static float strafeSway = 0.0f;
                strafeSway = Viewmodel_StrafeSwayStep(strafeSway, PlayerStrafe01(&g_sim.player), dt);
                Viewmodel_DrawAimedSway(&g_sim.arsenal, &viewAim, g_sim.player.isGrounded ? walk01 : 0.0f, (float)GetTime(), Lighting_AtPoint(g_sim.player.position),
                                        PlayerAimEase(aimView), strafeSway);
            }
            SubNote(SUB_HANDS, tHands0, GetTime());
        EndMode3D();
        const double tScene = GetTime();

        ScreenFx_DrawHealthEffects(screenWidth, screenHeight, g_sim.player.health.hp / g_sim.player.health.maxHp,
                                   g_sim.player.hitFlash, (float)GetTime());
        if (healFlash > 0.0f) DrawRectangle(0, 0, screenWidth, screenHeight, Fade((Color){ 60, 220, 100, 255 }, 0.22f * healFlash));
        ScreenFx_DrawCrosshairAimed(screenWidth, screenHeight, PlayerAimEase(aimView));
        ScreenFx_DrawHitMarker(screenWidth, screenHeight, hitMarker, killMarker);

        if (fade > 0.0f) DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, fade > 1.0f ? 1.0f : fade));
        if (g_sim.fadeOut > 0.0f) DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, g_sim.fadeOut > 1.0f ? 1.0f : g_sim.fadeOut));

        if (!g_sim.player.health.isAlive) {
            const float a = g_sim.deathT / DEATH_FADE_SECS;
            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, a > 1.0f ? 1.0f : a));
            if (a > 0.5f) {
                const int tw = MeasureText("YOU DIED", 48);
                DrawText("YOU DIED", (screenWidth - tw) / 2, screenHeight / 2 - 30, 48, Fade((Color){ 190, 20, 20, 255 }, a > 1.0f ? 1.0f : (a - 0.5f) * 2.0f));
            }
        }

        // Everything drawn over the view that is not a screen effect: the touch controls, the STATS button and popup, the prompt, the toast.
        {
            HudView hv = { 0 };
            hv.alive = g_sim.player.health.isAlive; hv.sprinting = g_sim.player.sprinting; hv.sliding = g_sim.player.sliding; hv.exhausted = g_sim.player.exhausted;
            hv.hp = g_sim.player.health.hp; hv.maxHp = g_sim.player.health.maxHp;
            hv.stamina01 = g_sim.player.stamina / PLAYER_STAMINA_MAX;
            hv.stim01 = g_sim.player.stimLeft / PLAYER_STIM_SECS;
            hv.exhaustRecover01 = STAMINA_EXHAUST_RECOVER / PLAYER_STAMINA_MAX;
            hv.tooTiredToSlide = g_sim.player.stamina < STAMINA_SLIDE_COST;
            const WeaponDef *held = Weapon_Def(g_sim.arsenal.current);
            hv.weaponName = held->name; hv.weaponIsGun = held->kind == WEAPON_KIND_GUN; hv.weaponIsMelee = held->kind == WEAPON_KIND_MELEE;
            hv.ammo = g_sim.arsenal.ammo; hv.weaponDry = g_sim.arsenal.dry > 0.0f; hv.weaponsOwned = Arsenal_OwnedCount(&g_sim.arsenal);
            hv.magSize = held->kind == WEAPON_KIND_GUN ? held->magSize : 0;
            hv.mag = hv.magSize > 0 ? g_sim.arsenal.mag[g_sim.arsenal.current] : 0;
            hv.reloading = g_sim.arsenal.reloading; hv.reloadFrac = Arsenal_ReloadFraction(&g_sim.arsenal);
            hv.magEmpty = hv.magSize > 0 && hv.mag <= 0;
            hv.diveShown = ictx.diveAvailable;
            hv.tooTiredToDive = g_sim.player.exhausted || g_sim.player.stamina < STAMINA_DIVE_COST;
            hv.reloadShown = g_sim.player.health.isAlive && !g_sim.invOpen && (Arsenal_CanReload(&g_sim.arsenal) || g_sim.arsenal.reloading);
            hv.weaponWears = held->kind == WEAPON_KIND_MELEE && held->durability > 0;
            hv.weaponCondition = Arsenal_Condition(&g_sim.arsenal, g_sim.arsenal.current);
            hv.durabilityLeft = hv.weaponWears ? held->durability - g_sim.arsenal.wear[g_sim.arsenal.current] : 0;
            hv.hasTorch = Inventory_Has(&g_sim.inv, ITEM_FLASHLIGHT); hv.torchOn = g_sim.torch.on; hv.torchLow = Flashlight_IsLow(&g_sim.torch);
            hv.torchFlat = g_sim.torch.charge <= 0.0f; hv.torchFraction = Flashlight_Fraction(&g_sim.torch);
            hv.torchSecs = g_sim.torch.charge; hv.torchLevel = g_sim.torch.level;
            hv.floorNumber = g_sim.window.currentFloor; hv.enemies = CountActiveEnemies() + (Swarm_Alive(&g_sim.swarm) ? 1 : 0);
            char floorInfo[112];
            snprintf(floorInfo, sizeof floorInfo, "try %d route %d walls %d pillars %d open %d%% gen %uus%s", g_sim.map->attempts, g_sim.map->pathLength,
                     g_sim.map->partitions, g_sim.map->pillars, (int)(g_sim.map->walkableFraction * 100.0f), (unsigned)g_sim.map->genMicros, g_sim.map->usedFallback ? "  FALLBACK" : "");
            hv.floorInfo = floorInfo;
            hv.useShown = useAvailable;
            if (useAvailable) {
                hv.useLocked = nearDoor >= 0 && Staircase_DoorLocked((DoorId)nearDoor, g_sim.map);
                hv.useLabel = hv.useLocked ? "LOCKED" : (nearDoor == DOOR_UP ? "DOOR\nUP" : "DOOR\nDOWN");
                if (nearDoor < 0 && nearPickup >= 0) {
                    static char pick[40];
                    snprintf(pick, sizeof pick, "PICKUP\n%s", Pickup_Label(Pickups_Get(nearPickup)->kind));
                    hv.useLabel = pick;
                } else if (nearDoor < 0) {
                    hv.useLabel = "HOLD\nUNLOCK";                  // the stair clock's console
                }
            }
            hv.slideShown = GameSim_SlideButtonShown(&g_sim); hv.invOpen = g_sim.invOpen;
            hv.characterName = Characters_SelectedCall();
            hv.stairPhase = FloorGame_Phase(); hv.consoleProgress = FloorGame_ConsoleProgress(); hv.consoleHeld = FloorGame_ConsoleHeld();
            hv.stairCountdown = FloorGame_CountdownLeft(); hv.collapseLeft01 = FloorGame_CollapseLeft01();
            hv.debug = (HudDebug){ g_depthBits, g_msaaSamples, { subMs[SUB_LIGHT], subMs[SUB_WORLD], subMs[SUB_ENTS], subMs[SUB_HANDS] } };
            Hud_Draw(&hv);
        }

        if (g_sim.invOpen) {
            InvLoadout lo = { g_sim.arsenal.current, 0u, Arsenal_EquippedCount(&g_sim.arsenal), ARSENAL_MAX_EQUIPPED };
            for (int w = WEAPON_HANDS + 1; w < WEAPON_COUNT; ++w) if (g_sim.arsenal.owned[w]) lo.equippedMask |= 1u << w;
            InvUI_Draw(&g_sim.inv, &lo);
        }
        else         InvUI_DrawOpenButton(&g_sim.inv, false);

    const double tUi = GetTime();
    Highp_End();
    EndDrawing();
    const double tEnd = GetTime();

    // Where our own time went this frame, in ms. Late frames with small numbers here were
    // held up outside the game.
    const float stages[FRAME_STAGES] = {
        (float)((tInput - tFrame0) * 1000.0), (float)((tSim - tInput) * 1000.0), (float)((tAi - tSim) * 1000.0),
        (float)((tScene - tAi) * 1000.0), (float)((tUi - tScene) * 1000.0), (float)((tEnd - tUi) * 1000.0)
    };
    FrameGraph_PushStages(stages);

}

int main(void)
{
    // Smooths polygon edges and the thin corner lines, which crawl visibly while turning.
    // A phone's browser may ignore the request; nothing breaks if it does.
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Tower of Babylon - Horror Game Show");
    Input_Init();
    Hud_Init();
    Audio_Init();                     // hands sounds.mdix to the config layer, so it must come before GameConfig_Init
    FloorGame_Register();             // and floor.mdix, the stair clock's numbers
    Characters_Register();            // and characters.mdix, the roster
    Swarm_Register();                 // and swarm.mdix, the worm swarm
    GameConfig_Init();                // weapon and projectile numbers: the baked copy now, the .mdix files once the wasm is up
    CharSelect_Init(&g_charSel, Characters_Count());
    Lobby_Init(&g_lobby);                          // the game starts on the lobby
    g_lobbyNet = LobbyNet_Backend();
    ScreenFx_Init();
    Highp_Init();
    ViewClip_QueryBuffers(&g_depthBits, &g_msaaSamples);
    TraceLog(LOG_INFO, "DISPLAY: depth buffer %d bits, MSAA samples %d", g_depthBits, g_msaaSamples);

    // Seed with system time so every launch creates a unique tower layout.
    uint32_t towerSeed = (uint32_t)time(NULL);
    GameSim_Init(&g_sim, towerSeed);

    InitObstaclesSystem();
    InitProjectilesSystem();
    InitEnemiesSystem();

    // The first floor: its monsters, its swarm, its pickups, the stair clock, and the player at the foot of the stairs.
    GameSim_Begin(&g_sim);
    ApplySimEvents();
    FixedStep_Init(&g_simStep, SIM_STEP_SECS, SIM_MAX_STEPS_PER_FRAME);
    SimCmd_Init(&g_simCmd);

#if TOWER_MAIN_LOOP_CALLBACK
    // The browser drives the loop from requestAnimationFrame (fps 0), so frames are paced
    // by the display. No SetTargetFPS, no sleeping, and no Asyncify needed for the loop.
    emscripten_set_main_loop(Frame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) Frame();
#endif

    NetSession_Leave(&g_session);
    LobbyNet_Shutdown();
    Audio_Shutdown();
    ScreenFx_Unload();
    UnloadProjectilesSystem();
    UnloadEnemiesSystem();
    CloseWindow();
    return 0;
}
