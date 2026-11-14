# platforms

## Overview

The game builds for three kinds of platform. **Web** (Emscripten, WebGL 1, built by `mray.py`) and **Android** (NDK, OpenGL ES 2.0, built by `scripts/build_android.sh`) exist today. **Desktop** (OpenGL 3.3 on Windows, Linux or macOS) is planned: the code compiles for it, nothing builds or runs it yet. This document is the single list of every place the platforms differ, and the rule for adding another.

C has the same preprocessor as C#: `#if`, `#elif`, `#else`, `#endif`, and `-D` on the command line works like a C# define symbol. Two differences matter. A name that is **not defined** counts as `0` inside `#if` without any error, so a typo silently turns a feature off. And the symbols are not set by a project file: the compiler sets them (`emcc` defines `__EMSCRIPTEN__`, the NDK's clang defines `__ANDROID__`, a desktop compiler defines neither).

## The rules

1. **One file decides the platform: `src/platform/platform.h`.** Nothing else tests `__EMSCRIPTEN__`, `__ANDROID__` or raylib's `PLATFORM_*` macros. `tests/platform_check.sh` fails if one appears.
2. **Gate on a capability, not on a platform.** Code says what it needs (`#if TOWER_GLES2`), `platform.h` says which platforms have it. A new platform is then one edit in one file, not a hunt through the code, and the reason for each gate is written where it is defined.
3. **Every macro is always defined, as `1` or `0`, and tested with `#if`, never `#ifdef`.** An `#ifdef` cannot tell "off" from "typo". The check compiles with `-Wundef` and fails on any undefined `TOWER_` name.
4. **A gate guards the smallest thing it can** (one call, one block), and the other branch is a deliberate "no" or the portable version, never a missing function: callers never have to know which platform they are on. (`ViewClip_ClearDepth` is a no-op without `TOWER_GLES2`; `MdixWeb_*` all return "not available" without `TOWER_MDIX_WASM_BRIDGE`.)
5. **Anything a platform needs that the others cannot compile goes behind a capability, and the capability goes in the table below** with what each platform does and whether it was seen working.

## The capabilities

| Capability | Meaning | Web | Android | Desktop |
|---|---|---|---|---|
| `TOWER_PLATFORM_WEB`, `_ANDROID`, `_DESKTOP` | Exactly one is 1. `TOWER_PLATFORM_NAME` is the word. Use these only to **report** (a log line), not to decide behaviour. | 1 / 0 / 0 | 0 / 1 / 0 | 0 / 0 / 1 |
| `TOWER_GLES2` | The driver is OpenGL ES 2.0: `<GLES2/gl2.h>` exists, raylib's default vertex shader is mediump, `#version 100` shaders compile. | 1 | 1 | 0 |
| `TOWER_AUDIO_GESTURE_UNLOCK` | The sound card may only be opened after a touch or key. | 1 | 0 | 0 |
| `TOWER_MDIX_WASM_BRIDGE` | The DixScript wasm is reachable (an `EM_JS` bridge), so `.mdix` files can be read live. | 1 | 0 | 0 |
| `TOWER_MAIN_LOOP_CALLBACK` | The browser calls the frame function; `main()` must not loop itself. | 1 | 0 | 0 |
| `TOWER_INPUT_TOUCH` | The touch controls (floating stick, fire pad, buttons) are compiled and drawn. | 1 | 1 | 0 |
| `TOWER_INPUT_KEYBOARD` | The keyboard is read (WASD and the rest). | 1 | 0 | 1 |
| `TOWER_INPUT_POINTER_LOCK` | The mouse is locked to the window and turns the camera with no button held. | 0 | 0 | 1 |
| `TOWER_NET_UDP` | UDP sockets exist, for the multiplayer transport. A browser has none. | 0 | 1 | 1 |

## Where each is used, and what the other side does

| Capability | File | With it | Without it |
|---|---|---|---|
| `TOWER_GLES2` | `src/ui/view_clip.c` `ViewClip_ClearDepth` | clears the depth buffer between the world and the hands (`glClear`) | nothing is cleared. **Desktop gap:** the hands can clip into walls; the fix is a depth-only clear through rlgl, not written |
| `TOWER_GLES2` | `src/ui/view_clip.c` `ViewClip_QueryBuffers` | reads depth bits and MSAA samples (`glGetIntegerv`) | returns -1, the overlay says so |
| `TOWER_GLES2` | `src/ui/highp_shader.c` `Highp_Init` | builds the highp program and the flashlight beam program (GLSL ES 1.00) | builds nothing; the game uses raylib's shader and the flashlight's glow fallback. **Desktop gap:** the beam needs `#version 330` versions of the two shaders |
| `TOWER_AUDIO_GESTURE_UNLOCK` | `src/audio/audio.c` `Audio_Update` | waits for the first touch, mouse button or movement key, then opens the sound card | opens it on the first frame |
| `TOWER_MDIX_WASM_BRIDGE` | `src/config/mdix_web.c` | the EM_JS bridge that boots the DixScript wasm | every call says "not available"; config is the baked copy. **Android option:** the native library Terminal Viscosity already ships could be bridged here, not done |
| `TOWER_INPUT_TOUCH` | `src/input/input.c` (fingers, the mouse as a finger), `src/hud/hud.c` (draws the controls) | the touch logic runs and the controls are drawn while the touch scheme is in use | the touch code is not compiled; the output is the keyboard and mouse only |
| `TOWER_INPUT_KEYBOARD` | `src/input/input.c` | WASD, Shift, Space, F, E, Q, C, L, I, P are read | not compiled |
| `TOWER_INPUT_POINTER_LOCK` | `src/input/input.c` | `DisableCursor()`; the mouse delta is the camera, the left button fires. **Desktop only, never run** | the camera comes from a finger or a held mouse button |
| `TOWER_NET_UDP` | `src/net/net_udp.c` | the real UDP transport (`NetUdpHost`): bind, send, receive, with the reliable layer on top, and LAN discovery's sockets and interface list (`src/net/net_discovery.c`: its packets and lobby table build everywhere) | every function is there and says "no network" (false or 0); the browser will use WebTransport or WebSockets through its own bridge (`net.md`). **The Android build declares the INTERNET permission in its manifest (`android.md`), though nothing in the app calls the network yet** |

**Build options** (not platform capabilities: they have no column, and the build says them): `TOWER_MIDNET` (default 0) means the build linked the mid-engine QUIC library (`net/net_mid.c`; `scripts/build_server.sh --quic` for the server and bot, and `scripts/build_android.sh` for the APK, which passes it when its midnet stage built the library). It needs a native platform: with the web configuration it is an `#error` (`tests/platform_check.sh` checks that, and that the real QUIC wrapper compiles for Android and desktop).
| `TOWER_MAIN_LOOP_CALLBACK` | `main.c` (include and the end of `main`) | `emscripten_set_main_loop(Frame, 0, 1)` | `SetTargetFPS(60)` and `while (!WindowShouldClose()) Frame();` |

## What each platform is built with

| | Web | Android | Desktop |
|---|---|---|---|
| Build | `mray.py` option 2 (`emcc`, the project's `raylib/src/libraylib.a`, which is raylib's web build) | `scripts/build_android.sh` (NDK clang, raylib 6.0 built from source, `docs/android.md`) | none yet |
| Defines the compiler sets | `__EMSCRIPTEN__` | `__ANDROID__` | none |
| Defines the build passes | none | `-DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2` (what raylib itself is built with) | would be raylib's `-DPLATFORM_DESKTOP` |
| raylib headers | `raylib/src` (6.1-dev) | the headers of the raylib it just built (6.0) | the headers of the library it links |
| Config | wasm (live) with the baked copy as the start and the fallback | baked copy | baked copy |
| Audio | opens after a touch | opens at once | opens at once |
| Controls | touch (drawn until a key is pressed) and keyboard | touch | keyboard and a locked mouse |
| Last seen working | yes (the user's phone) | **not yet run** | not built |

## Adding a platform (or a difference)

1. **A new difference between existing platforms:** name the capability after what the code needs, add it to `platform.h` with a one-line comment, add a column value for each platform in the tables above and in the `configs` list of `tests/platform_check.sh`, then use `#if TOWER_YOUR_NAME` in the code.
2. **A new platform** (desktop first, perhaps iOS later): add a branch to the platform block of `platform.h` that sets the three `TOWER_PLATFORM_*` and `TOWER_PLATFORM_NAME` and every capability, add its row to `configs` in `tests/platform_check.sh`, add a stub directory to `tests/stubs/` if it has headers the others lack, add a build script, and fill its column in the tables above, including the gaps.
3. **Run** `bash tests/platform_check.sh` (it is part of `tools/run_tests.sh`).

## Modules

### `src/platform/platform.h`
**What it does:** sets the three platform macros and the seven capabilities from what the compiler defines. Nothing here includes anything or defines anything but macros, so it is safe to include anywhere, first or last.

**Decisions:** capabilities are macros with the values 1 and 0 (not `#ifdef` symbols) so `-Wundef` can catch a misspelling. The names carry the `TOWER_` prefix because raylib already defines `PLATFORM_ANDROID`, `PLATFORM_WEB` and `PLATFORM_DESKTOP` for its own build, and the project's web build does not pass them: relying on them would have been a different answer on different builds.

### `tests/platform_check.sh`
**What it does:** for each of web, Android and desktop, checks that `platform.h` produces exactly the values in the table (a compile-time assertion for each), then compiles `main.c` and every source file under `src/` (47 files), syntax only, with `-Wall -Wundef -Werror=implicit-function-declaration` and that platform's defines. It fails on: a file that does not compile for one platform (a name only another platform has), a call to a function no header declares (a web-only call in common code), an undefined `TOWER_` macro, and any raw `__EMSCRIPTEN__` or `__ANDROID__` outside `platform.h`. It was checked by breaking each of those on purpose.

**What it cannot see:** it does not link, run, or use the real toolchains. `tests/stubs/` stands in for `emscripten.h` and `GLES2/gl2.h`, so a mistake inside an `EM_JS` body or in how a real GL call is used is invisible to it. A platform that compiles here can still fail to link or to run.

## Fixes and Problems

- The first Android work (`docs/android.md`) gated with `#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)` written out in four files, and the audio code with `__EMSCRIPTEN__` directly. That worked but spread the decision over the code, and a desktop build would have had to find every one of them. Replaced by the capabilities above; behaviour is unchanged on both existing platforms (all tests pass, and the three-platform check was run before and after).
