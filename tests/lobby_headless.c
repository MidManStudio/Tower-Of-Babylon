// Headless test of the lobby: the logic of both screens (the buttons and which are on, the keyboard's focus, taps, what the screen asks of the game),
// the search lifecycle against a scripted backend (it starts when FIND opens, stops when it closes, never runs twice), the list of games with its
// scrolling, the layout, and the real backend over the LAN discovery with a host answering on this machine. No window, no raylib calls. Run from the
// project root:
//   cc -O2 -std=c99 -DLOBBY_HEADLESS_TEST -Isrc -Iraylib/src tests/lobby_headless.c src/lobby/lobby.c src/lobby/lobby_net.c src/net/net_discovery.c src/net/net_wire.c src/characters/character_select.c -o lobby_headless
#ifdef LOBBY_HEADLESS_TEST
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "characters/character_select.h"
#include "lobby/lobby.h"
#include "lobby/lobby_net.h"
#include "lobby/session_ui.h"
#include "platform/platform.h"

static int g_failures = 0;
static void Check(int ok, const char *what) { printf("  [%s] %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++g_failures; }

// ---- a backend that answers from a script ------------------------------------------------------------------------------------------------
typedef struct {
    bool       failStart;
    int        starts, stops, updates, entryCalls;
    double     lastNow;
    int        n;                    // what count() says
    int        goneIndex;            // entry() says "gone" for this index (-1: none)
    LobbyEntry list[24];
} Fake;

static bool FStart(void *u) { Fake *f = u; ++f->starts; return !f->failStart; }
static void FStop(void *u) { Fake *f = u; ++f->stops; }
static void FUpdate(void *u, double now) { Fake *f = u; ++f->updates; f->lastNow = now; }
static int  FCount(void *u) { Fake *f = u; return f->n; }
static bool FEntry(void *u, int i, LobbyEntry *out) { Fake *f = u; ++f->entryCalls; if (i == f->goneIndex || i < 0 || i >= 24) return false; *out = f->list[i]; return true; }

static LobbyBackend Mk(Fake *f, bool canSearch, const char *why)
{
    LobbyBackend b;
    memset(&b, 0, sizeof b);
    b.canSearch = canSearch; b.cannotSearchReason = why;
    b.searchStart = FStart; b.searchStop = FStop; b.update = FUpdate; b.count = FCount; b.entry = FEntry; b.user = f;
    return b;
}

static void Fill(Fake *f, int n)
{
    memset(f, 0, sizeof *f);
    f->n = n; f->goneIndex = -1;
    for (int i = 0; i < n && i < 24; ++i) {
        snprintf(f->list[i].name, sizeof f->list[i].name, "GAME %d", i);
        f->list[i].players = i % 4; f->list[i].maxPlayers = 4;
        snprintf(f->list[i].transport, sizeof f->list[i].transport, "UDP");
        f->list[i].joinable = true;
        snprintf(f->list[i].address, sizeof f->list[i].address, "192.168.1.%d", 10 + i);
        f->list[i].port = (uint16_t)(7777 + i);
        f->list[i].transportId = i % 2;
    }
}

static void TapRect(Lobby *l, UiRect r) { Lobby_Tap(l, r.x + r.w * 0.5f, r.y + r.h * 0.5f); }

// A lobby on HOME with a roster of 4 and somebody chosen.
static void Home(Lobby *l, int roster, bool chosen) { Lobby_Init(l); Lobby_SetContext(l, roster, chosen, false); }
static void HomeCanHost(Lobby *l, int roster, bool chosen) { Lobby_Init(l); Lobby_SetContext(l, roster, chosen, true); }

// A lobby on FIND with the backend's games already copied in.
static void Find(Lobby *l, Fake *f, const LobbyBackend *b, double now)
{
    Home(l, 4, true);
    TapRect(l, Lobby_Button(LB_FIND));
    Lobby_Update(l, b, now);
    (void)f;
}

int main(void)
{
    printf("HOME: which buttons are on\n");
    {
        Lobby l; Lobby_Init(&l);
        Check(l.screen == LOBBY_HOME && l.want == LOBBY_WANT_NONE && l.search == LOBBY_SEARCH_OFF, "the lobby starts on HOME, wanting nothing, not searching");
        Check(l.focus == LB_PLAY, "with no roster known yet the focus is on PLAY (CHARACTER is off)");
        Lobby_SetContext(&l, 4, false, false);
        Check(Lobby_Enabled(&l, LB_CHARACTER) && !Lobby_Enabled(&l, LB_PLAY) && Lobby_Enabled(&l, LB_FIND) && !Lobby_Enabled(&l, LB_HOST), "a roster and nobody chosen: CHARACTER and FIND are on, PLAY and HOST are off");
        Check(l.focus == LB_CHARACTER, "and the focus moves to CHARACTER (the first button that is on)");
        Lobby_SetContext(&l, 4, true, false);
        Check(Lobby_Enabled(&l, LB_PLAY) && Lobby_Enabled(&l, LB_CHARACTER) && !Lobby_Enabled(&l, LB_HOST), "somebody chosen: PLAY is on, HOST still is not");
        Check(l.focus == LB_PLAY, "choosing moves the focus to PLAY, the next thing to do");
        Lobby_Move(&l, 1);
        Lobby_SetContext(&l, 4, true, false);
        Check(l.focus == LB_FIND, "but the focus is only moved when somebody was just chosen, not every frame");
        Lobby_SetContext(&l, 0, false, false);
        Check(Lobby_Enabled(&l, LB_PLAY) && !Lobby_Enabled(&l, LB_CHARACTER), "no characters at all: PLAY is on (the game starts without a choice, as it always did), CHARACTER is off");
        Lobby_SetContext(&l, -3, false, false);
        Check(l.rosterCount == 0, "a negative roster count is treated as none");
        Check(!Lobby_Enabled(&l, (LobbyButton)99) && !Lobby_Enabled(&l, LB_COUNT), "a button that does not exist is off");

        Home(&l, 4, false);
        Check(l.focus == LB_CHARACTER, "[setup] roster of 4, nobody chosen: focus on CHARACTER");
        Lobby_SetContext(&l, 0, false, false);
        Check(l.focus == LB_PLAY, "the roster vanishing under the focus moves it to a button that is on");
    }

    printf("\nHOME: the keyboard\n");
    {
        Lobby l; Home(&l, 4, true);
        l.focus = LB_CHARACTER;
        Lobby_Move(&l, 1);  Check(l.focus == LB_PLAY, "right: PLAY");
        Lobby_Move(&l, 1);  Check(l.focus == LB_FIND, "right: FIND");
        Lobby_Move(&l, 1);  Check(l.focus == LB_CHARACTER, "right: HOST is off, so it wraps to CHARACTER");
        Lobby_Move(&l, -1); Check(l.focus == LB_FIND, "left from the first wraps to the last button that is on");
        Lobby_Move(&l, 0);  Check(l.focus == LB_FIND, "no step does nothing");
        Home(&l, 4, false);
        Lobby_Move(&l, 1);  Check(l.focus == LB_FIND, "nobody chosen: right skips PLAY, which is off");
        Lobby_Move(&l, -1); Check(l.focus == LB_CHARACTER, "and left skips it again");
        Lobby_Press(&l);
        Check(l.want == LOBBY_WANT_CHARACTER && l.screen == LOBBY_HOME, "ENTER on CHARACTER asks the game to open the character screen");
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_CHARACTER && l.want == LOBBY_WANT_NONE && Lobby_TakeWant(&l) == LOBBY_WANT_NONE, "the want is handed over once");
        Home(&l, 4, true); Lobby_Press(&l);
        Check(l.focus == LB_PLAY && Lobby_TakeWant(&l) == LOBBY_WANT_PLAY, "ENTER on PLAY asks the game to start the run");
        Home(&l, 0, false); Lobby_Press(&l);
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_PLAY, "with no roster ENTER starts the run straight away");
        Home(&l, 4, true); l.focus = LB_FIND; Lobby_Press(&l);
        Check(l.screen == LOBBY_FIND && l.focus == LOBBY_FOCUS_BACK && l.want == LOBBY_WANT_NONE, "ENTER on FIND opens the list, with the focus on BACK");
    }

    printf("\nHOME: taps\n");
    {
        Lobby l; Home(&l, 4, false);
        TapRect(&l, Lobby_Button(LB_PLAY));
        Check(l.want == LOBBY_WANT_NONE && l.focus == LB_CHARACTER, "a tap on PLAY with nobody chosen does nothing (it is off)");
        TapRect(&l, Lobby_Button(LB_HOST));
        Check(l.want == LOBBY_WANT_NONE && l.screen == LOBBY_HOME, "a tap on HOST does nothing (it is off)");
        Lobby_Tap(&l, 1.0f, 1.0f); Lobby_Tap(&l, 400.0f, 400.0f); Lobby_Tap(&l, -5.0f, 900.0f);
        Check(l.want == LOBBY_WANT_NONE && l.screen == LOBBY_HOME, "a tap on nothing does nothing");
        TapRect(&l, Lobby_Button(LB_CHARACTER));
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_CHARACTER, "a tap on CHARACTER asks for the character screen");
        Lobby_SetContext(&l, 4, true, false);
        TapRect(&l, Lobby_Button(LB_PLAY));
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_PLAY && l.focus == LB_PLAY, "a tap on PLAY with somebody chosen asks for the run, and puts the focus there");
        TapRect(&l, Lobby_Button(LB_FIND));
        Check(l.screen == LOBBY_FIND && l.focus == LOBBY_FOCUS_BACK, "a tap on FIND opens the list");
    }

    printf("\nFIND: the search starts and stops with the screen\n");
    {
        Fake f; Fill(&f, 3);
        LobbyBackend b = Mk(&f, true, NULL);
        Lobby l; Home(&l, 4, true);
        Lobby_Update(&l, &b, 100.0);
        Check(f.starts == 0 && f.updates == 0 && l.search == LOBBY_SEARCH_OFF && l.entryCount == 0, "on HOME nothing searches and nothing is asked of the backend");
        TapRect(&l, Lobby_Button(LB_FIND));
        Check(f.starts == 0, "opening FIND does not touch the backend by itself (Lobby_Update does)");
        Lobby_Update(&l, &b, 200.0);
        Check(f.starts == 1 && f.updates == 1 && l.search == LOBBY_SEARCHING && f.lastNow == 200.0, "the first update starts the search and runs it with the time it was given");
        Lobby_Update(&l, &b, 300.0); Lobby_Update(&l, &b, 400.0);
        Check(f.starts == 1 && f.updates == 3 && f.stops == 0, "later updates only update: the search is not started again");
        Check(l.entryCount == 3 && strcmp(l.entries[0].name, "GAME 0") == 0 && strcmp(l.entries[2].name, "GAME 2") == 0, "the games found are copied in, in order");
        strcpy(f.list[0].name, "CHANGED");
        Check(strcmp(l.entries[0].name, "GAME 0") == 0, "the screen holds a copy: the backend's buffers can change under it");
        Lobby_Update(&l, &b, 500.0);
        Check(strcmp(l.entries[0].name, "CHANGED") == 0, "and the next update refreshes it");

        TapRect(&l, Lobby_Back());
        Check(l.screen == LOBBY_HOME && l.focus == LB_FIND && f.stops == 0, "BACK returns to HOME with the focus on FIND (the backend is told on the next update)");
        Lobby_Update(&l, &b, 600.0);
        Check(f.stops == 1 && l.search == LOBBY_SEARCH_OFF && l.entryCount == 0 && l.scroll == 0, "the next update stops the search and forgets the games");
        Lobby_Update(&l, &b, 700.0);
        Check(f.stops == 1 && f.updates == 4, "and it is only stopped once");
        TapRect(&l, Lobby_Button(LB_FIND));
        Check(l.entryCount == 0, "opening FIND again shows no games from the last time");
        Lobby_Update(&l, &b, 800.0);
        Check(f.starts == 2 && l.entryCount == 3, "and the search starts afresh");
    }

    printf("\nFIND: a search that cannot run\n");
    {
        Fake f; Fill(&f, 3);
        LobbyBackend b = Mk(&f, false, "A BROWSER CANNOT SEARCH");
        Lobby l; Find(&l, &f, &b, 10.0);
        Check(l.search == LOBBY_SEARCH_UNAVAILABLE && strcmp(l.noSearchWhy, "A BROWSER CANNOT SEARCH") == 0, "a backend that cannot search: the screen says why");
        Check(f.starts == 0 && f.updates == 0 && f.entryCalls == 0 && l.entryCount == 0, "and nothing is started or asked");
        Lobby_Update(&l, &b, 20.0);
        TapRect(&l, Lobby_Back()); Lobby_Update(&l, &b, 30.0);
        Check(f.stops == 0 && l.search == LOBBY_SEARCH_OFF, "leaving does not stop a search that never started");

        Lobby_Init(&l); Lobby_SetContext(&l, 4, true, false); TapRect(&l, Lobby_Button(LB_FIND));
        Lobby_Update(&l, NULL, 10.0);
        Check(l.search == LOBBY_SEARCH_UNAVAILABLE && strcmp(l.noSearchWhy, "NO NETWORK") == 0, "no backend at all: unavailable, with a reason of its own");
        LobbyBackend nr = Mk(&f, false, NULL);
        Lobby_Init(&l); Lobby_SetContext(&l, 4, true, false); TapRect(&l, Lobby_Button(LB_FIND));
        Lobby_Update(&l, &nr, 10.0);
        Check(strcmp(l.noSearchWhy, "NO NETWORK") == 0, "a backend with no reason given: the same one");

        Fill(&f, 3); f.failStart = true;
        LobbyBackend ok = Mk(&f, true, NULL);
        Find(&l, &f, &ok, 10.0);
        Check(l.search == LOBBY_SEARCH_FAILED && f.starts == 1 && f.updates == 0 && l.entryCount == 0, "a search that fails to start: the screen says so, and no update is made");
        Lobby_Update(&l, &ok, 20.0);
        Check(f.starts == 1, "it is not retried every frame");
        TapRect(&l, Lobby_Back()); Lobby_Update(&l, &ok, 30.0);
        Check(f.stops == 0, "leaving does not stop what never started");
        f.failStart = false;
        TapRect(&l, Lobby_Button(LB_FIND)); Lobby_Update(&l, &ok, 40.0);
        Check(f.starts == 2 && l.search == LOBBY_SEARCHING && l.entryCount == 3, "opening FIND again tries again");

        Lobby_Update(&l, NULL, 50.0);
        Check(l.search == LOBBY_SEARCHING, "a backend that vanishes mid-search is no crash (nothing changes)");
    }

    printf("\nFIND: the list of games\n");
    {
        Fake f; Fill(&f, 3);
        LobbyBackend b = Mk(&f, true, NULL);
        Lobby l; Find(&l, &f, &b, 10.0);
        Lobby_Move(&l, 1);  Check(l.focus == 0, "right from BACK: the first game");
        Lobby_Move(&l, 1);  Lobby_Move(&l, 1); Check(l.focus == 2, "right twice more: the third");
        Lobby_Move(&l, 1);  Check(l.focus == LOBBY_FOCUS_BACK, "right from the last wraps to BACK");
        Lobby_Move(&l, -1); Check(l.focus == 2, "left from BACK: the last game");
        Lobby_Press(&l);
        Check(l.screen == LOBBY_FIND && Lobby_TakeWant(&l) == LOBBY_WANT_JOIN, "ENTER on a game that can be joined asks to join it (and stays on FIND until the game says it has started)");
        Lobby_Move(&l, 1);
        Lobby_Press(&l);
        Check(l.screen == LOBBY_HOME, "ENTER on BACK goes back");

        Find(&l, &f, &b, 20.0);
        const UiRect r1 = Lobby_Row(1);
        Lobby_Tap(&l, r1.x + 20.0f, r1.y + 10.0f);
        Check(l.focus == 1, "a tap on a row highlights it");
        const UiRect r4 = Lobby_Row(4);
        Lobby_Tap(&l, r4.x + 20.0f, r4.y + 10.0f);
        Check(l.focus == 1, "a tap on an empty row does nothing");
        Lobby_Tap(&l, 5.0f, 5.0f);
        Check(l.focus == 1 && l.screen == LOBBY_FIND, "a tap on nothing does nothing");
        Check(Lobby_ShownRows(&l) == 3, "three games, three rows shown");

        Fill(&f, 0); Lobby_Update(&l, &b, 30.0);
        Check(l.entryCount == 0 && l.focus == LOBBY_FOCUS_BACK, "the games vanish while one is highlighted: the focus falls back to BACK");
        Lobby_Move(&l, 1); Lobby_Move(&l, -1);
        Check(l.focus == LOBBY_FOCUS_BACK, "with no games the ring is only BACK");

        Fill(&f, 30); Lobby_Update(&l, &b, 40.0);
        Check(l.entryCount == LOBBY_MAX_ENTRIES, "a backend with more games than there is room for: the table is full, not overrun");
        Fill(&f, 30); f.goneIndex = 3; f.entryCalls = 0; Lobby_Update(&l, &b, 45.0);
        Check(f.entryCalls == LOBBY_MAX_ENTRIES && l.entryCount == LOBBY_MAX_ENTRIES - 1, "the backend is asked about no more games than the table holds, however many it says it has");
        Fill(&f, 4); f.goneIndex = 1; Lobby_Update(&l, &b, 50.0);
        Check(l.entryCount == 3 && strcmp(l.entries[0].name, "GAME 0") == 0 && strcmp(l.entries[1].name, "GAME 2") == 0 && strcmp(l.entries[2].name, "GAME 3") == 0, "a game the backend says is gone is left out, the others keep their order");
    }

    printf("\nFIND: scrolling\n");
    {
        Fake f; Fill(&f, 8);
        LobbyBackend b = Mk(&f, true, NULL);
        Lobby l; Find(&l, &f, &b, 10.0);
        Check(Lobby_ShownRows(&l) == LOBBY_ROWS && l.scroll == 0, "eight games: five rows shown, from the first");
        TapRect(&l, Lobby_ScrollDown());
        Check(l.scroll == 1 && Lobby_ShownRows(&l) == LOBBY_ROWS, "DOWN moves the view by one");
        Lobby_Update(&l, &b, 20.0);
        Check(l.scroll == 1, "and the next update leaves it there");
        for (int i = 0; i < 6; ++i) TapRect(&l, Lobby_ScrollDown());
        Check(l.scroll == 3 && Lobby_ShownRows(&l) == 5, "DOWN stops at the end (eight games, five rows: three)");
        const UiRect r2 = Lobby_Row(2);
        Lobby_Tap(&l, r2.x + 5.0f, r2.y + 5.0f);
        Check(l.focus == 5, "a tap on a row after scrolling highlights the game on it, not the row's number");
        for (int i = 0; i < 6; ++i) TapRect(&l, Lobby_ScrollUp());
        Check(l.scroll == 0, "UP stops at the start");
        Check(l.focus == 5, "the arrows never move the highlight");

        Find(&l, &f, &b, 30.0);
        Lobby_Move(&l, -1);
        Check(l.focus == 7 && l.scroll == 3, "left from BACK: the last game, and the view moves to show it");
        Lobby_Move(&l, 1); Lobby_Move(&l, 1);
        Check(l.focus == 0 && l.scroll == 0, "right past the last: BACK, then the first game, and the view goes back to it");
        for (int i = 0; i < 5; ++i) Lobby_Move(&l, 1);
        Check(l.focus == 5 && l.scroll == 1, "stepping down the list scrolls it as the highlight passes the last row");
        Lobby_Move(&l, 1);
        Check(l.focus == 6 && l.scroll == 2, "and keeps scrolling one game at a time");

        Fill(&f, 3); Lobby_Update(&l, &b, 40.0);
        Check(l.scroll == 0 && l.focus == 2, "the list shrinks under the view: the view and the highlight are brought back inside it");
        Check(Lobby_ShownRows(&l) == 3, "and the rows shown are the games that are there");
        Check(!UiRect_Contains(Lobby_ScrollUp(), 0, 0), "[layout] the arrows are not in the corner");
    }

    printf("\nHOST\n");
    {
        Lobby l;
        HomeCanHost(&l, 4, false);
        Check(!Lobby_Enabled(&l, LB_HOST), "a platform that can host, nobody chosen: HOST is off (it hosts as the chosen character)");
        HomeCanHost(&l, 4, true);
        Check(Lobby_Enabled(&l, LB_HOST), "somebody chosen: HOST is on");
        Home(&l, 4, true);
        Check(!Lobby_Enabled(&l, LB_HOST), "a platform that cannot host (a browser): HOST is off however the rest is");
        HomeCanHost(&l, 0, false);
        Check(Lobby_Enabled(&l, LB_HOST), "no roster at all: HOST is on, like PLAY");

        HomeCanHost(&l, 4, true);
        TapRect(&l, Lobby_Button(LB_HOST));
        Check(l.want == LOBBY_WANT_HOST && l.focus == LB_HOST, "a tap on HOST asks the game to host, and puts the focus there");
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_HOST && Lobby_TakeWant(&l) == LOBBY_WANT_NONE, "handed over once");
        HomeCanHost(&l, 4, true); l.focus = LB_FIND;
        Lobby_Move(&l, 1);
        Check(l.focus == LB_HOST, "the keyboard steps from FIND to HOST (it is on now)");
        Lobby_Press(&l);
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_HOST, "ENTER on HOST asks the game to host");
        Lobby_Move(&l, 1);
        Check(l.focus == LB_CHARACTER, "and one more step wraps to CHARACTER");

        HomeCanHost(&l, 4, false);
        TapRect(&l, Lobby_Button(LB_HOST));
        Check(l.want == LOBBY_WANT_NONE && l.focus == LB_CHARACTER, "a tap on HOST with nobody chosen does nothing");
    }

    printf("\nthe red line\n");
    {
        Lobby l; HomeCanHost(&l, 4, true);
        Lobby_SetNotice(&l, "COULD NOT OPEN A PORT TO HOST ON");
        Check(strcmp(l.notice, "COULD NOT OPEN A PORT TO HOST ON") == 0, "a notice is kept");
        Lobby_Update(&l, NULL, 10.0); Lobby_SetContext(&l, 4, true, true); Lobby_Move(&l, 1);
        Check(l.notice[0] != 0, "frames and steps do not clear it");
        TapRect(&l, Lobby_Button(LB_HOST));
        Check(l.notice[0] == 0, "pressing a button that is on does");
        Lobby_SetNotice(&l, "X"); Lobby_SetNotice(&l, NULL);
        Check(l.notice[0] == 0, "NULL clears it");
        char big[200]; memset(big, 'A', sizeof big - 1); big[sizeof big - 1] = 0;
        Lobby_SetNotice(&l, big);
        Check(strlen(l.notice) == LOBBY_NOTICE_LEN - 1, "a text that is too long is cut to the room there is");
        TapRect(&l, Lobby_Button(LB_FIND));
        Check(l.notice[0] == 0 && l.screen == LOBBY_FIND, "opening FIND clears it");
        Lobby_SetNotice(&l, "Y");
        TapRect(&l, Lobby_Back());
        Check(l.notice[0] == 0 && l.screen == LOBBY_HOME, "BACK clears it");
        Lobby_SetNotice(&l, "THE HOST LEFT");
        Lobby_BackHome(&l);
        Check(strcmp(l.notice, "THE HOST LEFT") == 0 && l.screen == LOBBY_HOME && l.focus == LB_PLAY, "BackHome keeps it (a game that ended says why) and puts the focus on PLAY");
    }

    printf("\nJOIN\n");
    {
        Fake f; Fill(&f, 3);
        f.list[1].joinable = false; snprintf(f.list[1].why, sizeof f.list[1].why, "FULL");
        LobbyBackend b = Mk(&f, true, NULL);
        Lobby l; Find(&l, &f, &b, 10.0);
        LobbyEntry t;
        Check(!Lobby_CanJoin(&l) && !Lobby_JoinTarget(&l, &t), "on FIND with the focus on BACK there is nothing to join");
        TapRect(&l, Lobby_Join());
        Check(l.want == LOBBY_WANT_NONE, "a tap on JOIN with nothing highlighted does nothing");
        TapRect(&l, Lobby_Row(1));
        Check(l.focus == 1 && !Lobby_CanJoin(&l), "a full game can be highlighted but not joined");
        TapRect(&l, Lobby_Join()); Lobby_Press(&l);
        Check(l.want == LOBBY_WANT_NONE, "JOIN and ENTER do nothing on it");
        TapRect(&l, Lobby_Row(2));
        Check(Lobby_CanJoin(&l), "a game that can be joined: JOIN is on");
        Lobby_SetNotice(&l, "OLD NEWS");
        TapRect(&l, Lobby_Join());
        Check(l.want == LOBBY_WANT_JOIN && l.notice[0] == 0, "a tap on JOIN asks the game to join (and clears the red line)");
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_JOIN && Lobby_JoinTarget(&l, &t), "the game takes the want and the target");
        Check(strcmp(t.name, "GAME 2") == 0 && strcmp(t.address, "192.168.1.12") == 0 && t.port == 7779 && t.transportId == 0 && t.joinable, "the target says where and how to connect: its name, address, port and transport");
        // The list changes under the player's finger: the target stays the game that was asked for.
        Fill(&f, 3); strcpy(f.list[2].name, "SOMEONE ELSE"); Lobby_Update(&l, &b, 20.0);
        Check(Lobby_JoinTarget(&l, &t) && strcmp(t.name, "GAME 2") == 0, "the target is a copy, taken when JOIN was pressed: the list changing after does not change it");

        Find(&l, &f, &b, 30.0);
        TapRect(&l, Lobby_Row(0)); Lobby_Press(&l);
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_JOIN && Lobby_JoinTarget(&l, &t) && strcmp(t.name, "GAME 0") == 0, "ENTER on a game that can be joined joins it");
    }

    printf("\nCONNECTING\n");
    {
        Fake f; Fill(&f, 2);
        LobbyBackend b = Mk(&f, true, NULL);
        Lobby l; Find(&l, &f, &b, 10.0);
        Check(f.starts == 1 && l.entryCount == 2, "[setup] searching on FIND");
        Lobby_BeginConnect(&l, "STEVE'S TOWER");
        Check(l.screen == LOBBY_CONNECTING && strcmp(l.connectName, "STEVE'S TOWER") == 0, "a join has started: CONNECTING, with the game's name");
        Lobby_Update(&l, &b, 20.0);
        Check(f.stops == 1 && l.search == LOBBY_SEARCH_OFF && l.entryCount == 0, "the search stops while connecting, and the list is forgotten");
        Lobby_Move(&l, 1); Lobby_Tap(&l, 5.0f, 5.0f); TapRect(&l, Lobby_Button(LB_PLAY)); TapRect(&l, Lobby_Join()); TapRect(&l, Lobby_Back()); TapRect(&l, Lobby_Row(0));
        Check(l.screen == LOBBY_CONNECTING && l.want == LOBBY_WANT_NONE, "while connecting, only CANCEL does anything");
        TapRect(&l, Lobby_Cancel());
        Check(l.want == LOBBY_WANT_CANCEL && l.screen == LOBBY_FIND && l.focus == LOBBY_FOCUS_BACK, "a tap on CANCEL asks the game to give up, and returns to the list");
        Lobby_TakeWant(&l);
        Lobby_Update(&l, &b, 30.0);
        Check(f.starts == 2 && l.search == LOBBY_SEARCHING, "and the search starts again");

        Lobby_BeginConnect(&l, "X"); Lobby_Press(&l);
        Check(Lobby_TakeWant(&l) == LOBBY_WANT_CANCEL && l.screen == LOBBY_FIND, "ENTER cancels too");

        Lobby_BeginConnect(&l, "X");
        Lobby_ConnectFailed(&l, "THE GAME IS FULL");
        Check(l.screen == LOBBY_FIND && l.focus == LOBBY_FOCUS_BACK && strcmp(l.notice, "THE GAME IS FULL") == 0, "a join that failed: back to the list with the reason in red");
        Lobby_Update(&l, &b, 40.0);
        Check(l.search == LOBBY_SEARCHING && l.notice[0] != 0, "the search runs again and the reason stays up");
        Lobby_BeginConnect(&l, NULL);
        Check(l.connectName[0] == 0, "a missing name is an empty one");
        char longName[100]; memset(longName, 'N', sizeof longName - 1); longName[sizeof longName - 1] = 0;
        Lobby_BeginConnect(&l, longName);
        Check(strlen(l.connectName) == LOBBY_NAME_MAX, "a long name is cut to the room there is");
    }

    printf("\nlayout\n");
    {
        const UiRect all[] = { Lobby_CharacterPanel(), Lobby_Button(LB_CHARACTER), Lobby_Button(LB_PLAY), Lobby_Button(LB_FIND), Lobby_Button(LB_HOST),
                               Lobby_StatusBox(), Lobby_Back(), Lobby_ListPanel(), Lobby_Row(0), Lobby_Row(LOBBY_ROWS - 1), Lobby_ScrollUp(), Lobby_ScrollDown(), Lobby_Join(), Lobby_Cancel() };
        int on = 1;
        for (size_t i = 0; i < sizeof all / sizeof all[0]; ++i)
            if (all[i].x < 0 || all[i].y < 0 || all[i].x + all[i].w > 800.0f || all[i].y + all[i].h > 450.0f) on = 0;
        Check(on, "every rectangle of both screens is on the 800 by 450 canvas");

        const UiRect home[] = { Lobby_Button(LB_CHARACTER), Lobby_Button(LB_PLAY), Lobby_Button(LB_FIND), Lobby_Button(LB_HOST), Lobby_StatusBox() };
        int apart = 1;
        for (int i = 0; i < 5; ++i) for (int j = i + 1; j < 5; ++j) if (UiRect_Overlaps(home[i], home[j])) apart = 0;
        Check(apart, "on HOME the four buttons and the status box never overlap");
        const UiRect panel = Lobby_CharacterPanel(), cb = Lobby_Button(LB_CHARACTER);
        Check(cb.x >= panel.x && cb.y >= panel.y && cb.x + cb.w <= panel.x + panel.w && cb.y + cb.h <= panel.y + panel.h, "CHARACTER sits inside the character's box");
        Check(!UiRect_Overlaps(panel, Lobby_Button(LB_PLAY)) && !UiRect_Overlaps(panel, Lobby_Button(LB_FIND)) && !UiRect_Overlaps(panel, Lobby_Button(LB_HOST)) && !UiRect_Overlaps(panel, Lobby_StatusBox()), "and the right-hand column is clear of the box");
        Check(Lobby_Button(LB_PLAY).h >= 60.0f && Lobby_Button(LB_FIND).h >= 60.0f && Lobby_Button(LB_HOST).h >= 60.0f && cb.h >= 44.0f, "the buttons are big enough for a thumb");

        const UiRect lp = Lobby_ListPanel(), su = Lobby_ScrollUp(), sd = Lobby_ScrollDown(), bk = Lobby_Back();
        int rowsOk = 1;
        for (int i = 0; i < LOBBY_ROWS; ++i) {
            const UiRect r = Lobby_Row(i);
            if (r.x < lp.x || r.y < lp.y || r.x + r.w > lp.x + lp.w || r.y + r.h > lp.y + lp.h) rowsOk = 0;
            if (UiRect_Overlaps(r, su) || UiRect_Overlaps(r, sd) || r.w < 600.0f || r.h < 40.0f) rowsOk = 0;
            if (i > 0 && UiRect_Overlaps(r, Lobby_Row(i - 1))) rowsOk = 0;
        }
        Check(rowsOk, "the rows are inside the list, apart from one another and from the arrows, wide and tall enough for a thumb");
        Check(su.x >= lp.x && su.x + su.w <= lp.x + lp.w && sd.x >= lp.x && sd.y + sd.h <= lp.y + lp.h && !UiRect_Overlaps(su, sd), "the arrows are inside the list and apart");
        Check(!UiRect_Overlaps(bk, lp) && bk.w >= 80.0f && bk.h >= 28.0f, "BACK is clear of the list and big enough for a thumb");
        const UiRect jn = Lobby_Join(), cn = Lobby_Cancel();
        int joinClear = !UiRect_Overlaps(jn, lp) && !UiRect_Overlaps(jn, bk) && !UiRect_Overlaps(jn, su) && !UiRect_Overlaps(jn, sd);
        for (int i = 0; i < LOBBY_ROWS; ++i) if (UiRect_Overlaps(jn, Lobby_Row(i))) joinClear = 0;
        Check(joinClear && jn.w >= 120.0f && jn.h >= 40.0f, "JOIN is clear of the list, the arrows and BACK, and big enough for a thumb");
        Check(cn.w >= 120.0f && cn.h >= 40.0f, "CANCEL is big enough for a thumb");
        const UiRect lv = SessionUI_Leave();
        Check(lv.x >= 0 && lv.y >= 0 && lv.x + lv.w <= 800.0f && lv.y + lv.h <= 450.0f && lv.w >= 80.0f && lv.h >= 28.0f, "the session screen's LEAVE is on the canvas and big enough for a thumb");
        Check(lv.x == bk.x && lv.y == bk.y && lv.w == bk.w && lv.h == bk.h, "and in the same place as BACK, so a thumb learns one place");
        const UiRect cbk = CharSelect_Back();
        Check(bk.x == cbk.x && bk.y == cbk.y && bk.w == cbk.w && bk.h == cbk.h, "and in the same place as the BACK of the character screen");
    }

    printf("\nthe real backend (LAN discovery)\n");
    {
        NetLobby n; memset(&n, 0, sizeof n);
        LobbyEntry e;
        Check(!LobbyNet_EntryFromLobby(&n, &e) && e.name[0] == 0 && !e.joinable, "an entry not in use gives nothing (and a cleared entry)");
        Check(!LobbyNet_EntryFromLobby(NULL, &e), "NULL gives nothing");

        n.used = true; n.compatible = true; n.transportSupported = true;
        snprintf(n.info.name, sizeof n.info.name, "STEVE'S TOWER"); n.info.players = 1; n.info.maxPlayers = 4; n.info.transport = NET_TRANSPORT_UDP;
        Check(LobbyNet_EntryFromLobby(&n, &e) && strcmp(e.name, "STEVE'S TOWER") == 0 && e.players == 1 && e.maxPlayers == 4 && strcmp(e.transport, "UDP") == 0 && e.joinable && e.why[0] == 0,
              "a game this build can join: its name, its numbers, UDP, joinable, no reason");
        n.info.transport = NET_TRANSPORT_QUIC;
        Check(LobbyNet_EntryFromLobby(&n, &e) && strcmp(e.transport, "QUIC") == 0 && e.joinable, "a QUIC game that this build supports: QUIC, joinable");
        uint32_t ip = 0; NetDisc_ParseIp("192.168.43.1", &ip);
        n.addr = ip; n.info.port = 7781;
        Check(LobbyNet_EntryFromLobby(&n, &e) && strcmp(e.address, "192.168.43.1") == 0 && e.port == 7781 && e.transportId == NET_TRANSPORT_QUIC, "where and how to connect: the address it answered from, its port and its transport");
        n.info.transport = NET_TRANSPORT_UDP;
        Check(LobbyNet_EntryFromLobby(&n, &e) && e.transportId == NET_TRANSPORT_UDP, "plain UDP is transport 0");
        n.info.transport = NET_TRANSPORT_QUIC;
        n.transportSupported = false;
        Check(LobbyNet_EntryFromLobby(&n, &e) && !e.joinable && strcmp(e.why, "NEEDS QUIC") == 0, "a QUIC game when this build has no QUIC: not joinable, NEEDS QUIC");
        n.info.transport = NET_TRANSPORT_UDP;
        Check(LobbyNet_EntryFromLobby(&n, &e) && !e.joinable && strcmp(e.why, "CANNOT CONNECT") == 0, "a UDP game when this build has no UDP: not joinable, CANNOT CONNECT");
        n.transportSupported = true; n.info.players = 4;
        Check(LobbyNet_EntryFromLobby(&n, &e) && !e.joinable && strcmp(e.why, "FULL") == 0, "a full game: not joinable, FULL");
        n.compatible = false;
        Check(LobbyNet_EntryFromLobby(&n, &e) && !e.joinable && strcmp(e.why, "OTHER VERSION") == 0, "another version of the game, even when it is full: OTHER VERSION comes first");
        n.compatible = true; n.info.players = 1; n.info.transport = 9;
        Check(LobbyNet_EntryFromLobby(&n, &e) && strcmp(e.transport, "?") == 0, "a transport this build has never heard of is shown as ?");
        memset(n.info.name, 'x', sizeof n.info.name);
        Check(LobbyNet_EntryFromLobby(&n, &e) && strlen(e.name) <= LOBBY_NAME_MAX, "a name is never longer than the screen's room for it (the copy ends in a zero)");

        const LobbyBackend b = LobbyNet_Backend();
        Check(b.canSearch == (TOWER_NET_UDP != 0), "the backend can search exactly where this platform has UDP");
        Check(b.cannotSearchReason && b.cannotSearchReason[0] && b.searchStart && b.searchStop && b.update && b.count && b.entry, "and it carries a reason and every function");
        LobbyNet_Shutdown(); LobbyNet_Shutdown();
        Check(b.count(NULL) == 0 && !b.entry(NULL, 0, &e), "shut down (twice, harmlessly): it lists nothing");

#if TOWER_NET_UDP
        NetDiscHost host;
        const bool opened = NetDiscHost_Open(&host, 0);
        Check(opened, "a host opens on a free port (this machine)");
        if (opened) {
            NetDiscHost_SetInfo(&host, "STEVE'S TOWER", 7777, 1, 4, NET_TRANSPORT_UDP);
            LobbyNet_UsePort(NetDiscHost_Port(&host));
            LobbyBackend nb = LobbyNet_Backend();
            Lobby l; Home(&l, 4, true);
            TapRect(&l, Lobby_Button(LB_FIND));
            double now = 1000.0;
            for (int i = 0; i < 100 && l.entryCount == 0; ++i) {
                Lobby_Update(&l, &nb, now);
                NetDiscHost_Poll(&host, now);
                usleep(5000);
                now += 50.0;
            }
            Check(l.search == LOBBY_SEARCHING, "FIND started the real search");
            Check(l.entryCount == 1, "the host on this machine answered and is listed");
            Check(l.entryCount == 1 && strcmp(l.entries[0].name, "STEVE'S TOWER") == 0 && l.entries[0].players == 1 && l.entries[0].maxPlayers == 4 && strcmp(l.entries[0].transport, "UDP") == 0 && l.entries[0].joinable == (TOWER_NETDRIVER != 0),
                  "with its name, its players, UDP, and joinable where the build has the driver library (every connection goes through it; where it has not, the entry says CANNOT CONNECT)");
            uint32_t found = 0;
            Check(l.entryCount == 1 && NetDisc_ParseIp(l.entries[0].address, &found) && l.entries[0].port == 7777 && l.entries[0].transportId == NET_TRANSPORT_UDP, "and where to connect: an address that parses, the game port the host announced, and UDP");
            NetDiscHost_SetInfo(&host, "STEVE'S TOWER", 7777, 4, 4, NET_TRANSPORT_UDP);
            for (int i = 0; i < 100 && !(l.entryCount == 1 && l.entries[0].players == 4); ++i) {
                now += 2100.0;                                           // a new round of questions is due
                Lobby_Update(&l, &nb, now);
                NetDiscHost_Poll(&host, now);
                usleep(5000);
                Lobby_Update(&l, &nb, now + 1.0);
            }
            Check(l.entryCount == 1 && l.entries[0].players == 4 && !l.entries[0].joinable && strcmp(l.entries[0].why, TOWER_NETDRIVER ? "FULL" : "CANNOT CONNECT") == 0, "when the host fills up the entry follows: FULL, not joinable (a build without the driver library keeps saying CANNOT CONNECT)");
            TapRect(&l, Lobby_Back());
            Lobby_Update(&l, &nb, now + 10.0);
            Check(l.search == LOBBY_SEARCH_OFF && l.entryCount == 0 && nb.count(NULL) == 0, "BACK closes the real search and the list is empty");
            TapRect(&l, Lobby_Button(LB_FIND));
            Lobby_Update(&l, &nb, now + 20.0);
            Check(l.search == LOBBY_SEARCHING, "and FIND can open it again (the socket was closed cleanly and opens again)");
            LobbyNet_Shutdown();
            NetDiscHost_Close(&host);
            LobbyNet_UsePort(NET_DISC_PORT);
        }
#else
        printf("  (no UDP on this platform: the loopback case is skipped)\n");
#endif
    }

    printf("\n%s\n", g_failures == 0 ? "all passed" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
#endif
