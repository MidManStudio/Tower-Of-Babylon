// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/platforms.md, section "src/platform/platform.h"
// ============================================================================
#ifndef TOWER_PLATFORM_H
#define TOWER_PLATFORM_H

// The one place that knows which platform this is built for. Nothing else in the game tests __EMSCRIPTEN__, __ANDROID__ or
// a raylib PLATFORM_ macro: code asks for what it needs (a CAPABILITY below) and this file says which platforms have it.
// Every macro here is always defined, as 1 or 0, and used as `#if TOWER_x` (never `#ifdef`): an undefined name in an `#if`
// silently counts as 0, which is how a typo turns a feature off without a word. The build is warned about that
// (-Wundef, see tests/platform_check.sh).
//
// The compilers set these themselves: emcc defines __EMSCRIPTEN__, the NDK's clang defines __ANDROID__; a desktop compiler
// defines neither.

// ---- which platform (exactly one is 1) -----------------------------------------------------------------------------
#if defined(__EMSCRIPTEN__)
    #define TOWER_PLATFORM_WEB     1
    #define TOWER_PLATFORM_ANDROID 0
    #define TOWER_PLATFORM_DESKTOP 0
    #define TOWER_PLATFORM_NAME    "web"
#elif defined(__ANDROID__)
    #define TOWER_PLATFORM_WEB     0
    #define TOWER_PLATFORM_ANDROID 1
    #define TOWER_PLATFORM_DESKTOP 0
    #define TOWER_PLATFORM_NAME    "android"
#else
    #define TOWER_PLATFORM_WEB     0
    #define TOWER_PLATFORM_ANDROID 0
    #define TOWER_PLATFORM_DESKTOP 1
    #define TOWER_PLATFORM_NAME    "desktop"
#endif

// ---- capabilities --------------------------------------------------------------------------------------------------
// The graphics driver is OpenGL ES 2.0 (a browser's WebGL 1, or the phone's own driver): <GLES2/gl2.h> exists, raylib's
// default vertex shader is mediump (the cause of the old glitches), and GLSL ES 1.00 shaders (#version 100) compile.
// Desktop raylib is OpenGL 3.3 core: none of that is true there.
#if TOWER_PLATFORM_WEB || TOWER_PLATFORM_ANDROID
    #define TOWER_GLES2 1
#else
    #define TOWER_GLES2 0
#endif

// The browser keeps the sound card shut until the player touches the page, so audio opens on the first touch or key.
#define TOWER_AUDIO_GESTURE_UNLOCK TOWER_PLATFORM_WEB

// The DixScript wasm is reached from C through an EM_JS bridge (src/config/mdix_web.c), so the .mdix files can be read
// live. Elsewhere the config is the baked copy only.
#define TOWER_MDIX_WASM_BRIDGE TOWER_PLATFORM_WEB

// Touch controls (the floating stick, the fire pad, the buttons) exist and are drawn. The browser has them because it runs on
// phones; the Android build is a phone. Desktop has no touch screen to design for.
#if TOWER_PLATFORM_WEB || TOWER_PLATFORM_ANDROID
    #define TOWER_INPUT_TOUCH 1
#else
    #define TOWER_INPUT_TOUCH 0
#endif

// A keyboard is read (WASD, Shift, Space and the rest). The browser may be on a PC; the Android build is not played with one.
#if TOWER_PLATFORM_WEB || TOWER_PLATFORM_DESKTOP
    #define TOWER_INPUT_KEYBOARD 1
#else
    #define TOWER_INPUT_KEYBOARD 0
#endif

// The mouse is locked to the window and turns the camera with no button held (a first person shooter on a PC).
#define TOWER_INPUT_POINTER_LOCK TOWER_PLATFORM_DESKTOP

// UDP sockets (socket, bind, sendto, recvfrom) exist, for the multiplayer transport in src/net/net_udp.c. The Android and desktop
// builds have them; a browser has no UDP at all (it will reach a server through WebTransport or WebSockets, by its own bridge).
#if TOWER_PLATFORM_ANDROID || TOWER_PLATFORM_DESKTOP
    #define TOWER_NET_UDP 1
#else
    #define TOWER_NET_UDP 0
#endif

// ---- Build options (not platform capabilities: no table column, the build says it) --------------------------------------------------
// TOWER_MIDNET: the build links the mid-engine QUIC library (crates/mid-net/transport-quinn-ffi, built with cargo), so the game can use mid-net's
// transport (src/net/net_mid.c) as well as plain UDP. Off unless the build passes -DTOWER_MIDNET=1; only native builds can have it.
#ifndef TOWER_MIDNET
    #define TOWER_MIDNET 0
#endif
#if TOWER_MIDNET && !TOWER_NET_UDP
    #error "TOWER_MIDNET needs a native platform (the QUIC library is not built for the browser)"
#endif
// TOWER_NETDRIVER: the build links mid-net's driver (handshake, keepalive with NO SIGNAL, channels, priorities: crates/mid-net/src/driver.rs, through
// its C interface, src/net/net_driver.c). It lives in the same Rust static library as the QUIC transport (mid-net-transport-quinn-ffi re-exports it), so
// a build that has one library has both; but it is asked for on its own (a build may have only the facade library). Off unless the build passes
// -DTOWER_NETDRIVER=1; only native builds can have it.
#ifndef TOWER_NETDRIVER
    #define TOWER_NETDRIVER 0
#endif
#if TOWER_NETDRIVER && !TOWER_NET_UDP
    #error "TOWER_NETDRIVER needs a native platform (the library is not built for the browser)"
#endif

// The browser drives the frame loop (emscripten_set_main_loop); everywhere else main() runs its own while loop.
#define TOWER_MAIN_LOOP_CALLBACK TOWER_PLATFORM_WEB

#endif
