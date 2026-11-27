#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/platforms.md, section "tests/platform_check.sh"
# ============================================================================
# Compiles (syntax only) main.c and every src/**/*.c three times, as the web, Android and desktop builds would see them, and
# checks that src/platform/platform.h says what docs/platforms.md says for each. Catches the usual platform mistakes: a gate
# that hides a name another platform needs, a typo in a TOWER_ macro (-Wundef), a web-only call in common code. It does NOT
# link, run, or use the real toolchains: tests/stubs/ stands in for Emscripten's and OpenGL ES's headers.
#   bash tests/platform_check.sh        (from the project root, or give the folder as the first argument)
cd "${1:-$(dirname "$0")/..}" || exit 1
fail=0
sources=(main.c $(find src -name '*.c' | sort))

# name | compiler flags | expected: WEB ANDROID DESKTOP GLES2 AUDIO_GESTURE_UNLOCK MDIX_WASM_BRIDGE MAIN_LOOP_CALLBACK INPUT_TOUCH INPUT_KEYBOARD INPUT_POINTER_LOCK NET_UDP
configs=(
  "web|-D__EMSCRIPTEN__ -Itests/stubs|1 0 0 1 1 1 1 1 1 0 0"
  "android|-D__ANDROID__ -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -Itests/stubs|0 1 0 1 0 0 0 1 0 0 1"
  "desktop|-DPLATFORM_DESKTOP|0 0 1 0 0 0 0 0 1 1 1"
)
for c in "${configs[@]}"; do
  IFS='|' read -r name flags want <<<"$c"
  read -r w a d g u m l it ik ip nu <<<"$want"
  probe=$(mktemp /tmp/platform_probe_XXXX.c)
  cat > "$probe" <<PROBE
#include "platform/platform.h"
typedef char exactly_one_platform[(TOWER_PLATFORM_WEB + TOWER_PLATFORM_ANDROID + TOWER_PLATFORM_DESKTOP == 1) ? 1 : -1];
typedef char web_is_$w[(TOWER_PLATFORM_WEB == $w) ? 1 : -1];
typedef char android_is_$a[(TOWER_PLATFORM_ANDROID == $a) ? 1 : -1];
typedef char desktop_is_$d[(TOWER_PLATFORM_DESKTOP == $d) ? 1 : -1];
typedef char gles2_is_$g[(TOWER_GLES2 == $g) ? 1 : -1];
typedef char audio_unlock_is_$u[(TOWER_AUDIO_GESTURE_UNLOCK == $u) ? 1 : -1];
typedef char wasm_bridge_is_$m[(TOWER_MDIX_WASM_BRIDGE == $m) ? 1 : -1];
typedef char main_loop_cb_is_$l[(TOWER_MAIN_LOOP_CALLBACK == $l) ? 1 : -1];
typedef char input_touch_is_$it[(TOWER_INPUT_TOUCH == $it) ? 1 : -1];
typedef char input_keyboard_is_$ik[(TOWER_INPUT_KEYBOARD == $ik) ? 1 : -1];
typedef char input_pointer_lock_is_$ip[(TOWER_INPUT_POINTER_LOCK == $ip) ? 1 : -1];
typedef char net_udp_is_$nu[(TOWER_NET_UDP == $nu) ? 1 : -1];
PROBE
  if ! gcc -std=gnu99 -fsyntax-only -Isrc $flags "$probe" 2>/tmp/platform_err.txt; then
    echo "FAIL  platform $name: platform.h does not say what docs/platforms.md says"; head -4 /tmp/platform_err.txt; fail=1; rm -f "$probe"; continue
  fi
  rm -f "$probe"
  bad=0
  for f in "${sources[@]}"; do
    # -Wundef: a name used in #if that is not defined counts as 0 without a word. Only our own names matter here (raylib's headers do it too).
    if ! gcc -std=gnu99 -fsyntax-only -Wall -Wno-unused-function -Wundef -Werror=implicit-function-declaration -Isrc -Iraylib/src $flags "$f" 2>/tmp/platform_err.txt; then
      echo "FAIL  platform $name: $f does not compile"; head -5 /tmp/platform_err.txt; bad=1
    elif grep -q "TOWER_.*is not defined" /tmp/platform_err.txt; then
      echo "FAIL  platform $name: $f uses an undefined TOWER_ macro"; grep "TOWER_" /tmp/platform_err.txt | head -3; bad=1
    fi
  done
  if [ $bad -eq 0 ]; then echo "  ok: $name: ${#sources[@]} files compile and platform.h matches the table"; else fail=1; fi
done
# TOWER_MIDNET (platform.h: the build linked the mid-engine QUIC library): the QUIC wrapper and its users must compile for real on the native
# platforms, and the web build must refuse the option outright
for flags in "-D__ANDROID__ -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -Itests/stubs" "-DPLATFORM_DESKTOP"; do
  for f in src/net/net_mid.c src/net/net_discovery.c; do
    if ! gcc -std=gnu99 -fsyntax-only -Wall -Wundef -Isrc -Iraylib/src $flags -DTOWER_MIDNET=1 "$f" 2>/tmp/platform_err.txt; then
      echo "FAIL  TOWER_MIDNET=1: $f does not compile ($flags)"; head -5 /tmp/platform_err.txt; fail=1
    fi
  done
done
if gcc -std=gnu99 -fsyntax-only -Isrc -Iraylib/src -D__EMSCRIPTEN__ -Itests/stubs -DTOWER_MIDNET=1 src/net/net_mid.c 2>/tmp/platform_err.txt || ! grep -q "TOWER_MIDNET needs a native platform" /tmp/platform_err.txt; then
  echo "FAIL  the web build accepted TOWER_MIDNET=1 (it must be refused: the QUIC library is not built for the browser)"; fail=1
else echo "  ok: TOWER_MIDNET=1 compiles for android and desktop and is refused for the web"; fi
# TOWER_NETDRIVER (platform.h: the build links mid-net's driver): the adapter must compile for real on the native platforms (against the library's header),
# the build without it must compile everywhere, and the web build must refuse the option outright
for flags in "-D__ANDROID__ -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -Itests/stubs" "-DPLATFORM_DESKTOP"; do
  if ! gcc -std=gnu99 -fsyntax-only -Wall -Wundef -Isrc -Iraylib/src $flags -DTOWER_NETDRIVER=1 src/net/net_driver.c 2>/tmp/platform_err.txt; then
    echo "FAIL  TOWER_NETDRIVER=1: src/net/net_driver.c does not compile ($flags)"; head -5 /tmp/platform_err.txt; fail=1
  fi
done
for flags in "-D__EMSCRIPTEN__ -Itests/stubs" "-D__ANDROID__ -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -Itests/stubs" "-DPLATFORM_DESKTOP"; do
  if ! gcc -std=gnu99 -fsyntax-only -Wall -Wundef -Isrc -Iraylib/src $flags src/net/net_driver.c 2>/tmp/platform_err.txt; then
    echo "FAIL  the build without the driver: src/net/net_driver.c does not compile ($flags)"; head -5 /tmp/platform_err.txt; fail=1
  fi
done
if gcc -std=gnu99 -fsyntax-only -Isrc -Iraylib/src -D__EMSCRIPTEN__ -Itests/stubs -DTOWER_NETDRIVER=1 src/net/net_driver.c 2>/tmp/platform_err.txt || ! grep -q "TOWER_NETDRIVER needs a native platform" /tmp/platform_err.txt; then
  echo "FAIL  the web build accepted TOWER_NETDRIVER=1 (it must be refused: the library is not built for the browser)"; fail=1
else echo "  ok: TOWER_NETDRIVER=1 compiles for android and desktop, the build without it compiles everywhere, and the web build refuses it"; fi
# TOWER_GVEX (platform.h: the server of a session runs the creatures' world, src/gvex/gvex_world.c on Flecs): it must compile for real on the native platforms,
# the build without it must compile everywhere (an empty unit), and the web build must refuse the option outright
for flags in "-D__ANDROID__ -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -Itests/stubs" "-DPLATFORM_DESKTOP"; do
  if ! gcc -std=gnu99 -fsyntax-only -Wall -Wundef -Isrc -Iraylib/src -Ithird_party/flecs $flags -DTOWER_GVEX=1 src/gvex/gvex_world.c 2>/tmp/platform_err.txt; then
    echo "FAIL  TOWER_GVEX=1: src/gvex/gvex_world.c does not compile ($flags)"; head -5 /tmp/platform_err.txt; fail=1
  fi
done
for flags in "-D__EMSCRIPTEN__ -Itests/stubs" "-D__ANDROID__ -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -Itests/stubs" "-DPLATFORM_DESKTOP"; do
  if ! gcc -std=gnu99 -fsyntax-only -Wall -Wundef -Isrc -Iraylib/src $flags src/gvex/gvex_world.c 2>/tmp/platform_err.txt; then
    echo "FAIL  the build without creatures: src/gvex/gvex_world.c does not compile ($flags)"; head -5 /tmp/platform_err.txt; fail=1
  fi
done
if gcc -std=gnu99 -fsyntax-only -Isrc -Iraylib/src -Ithird_party/flecs -D__EMSCRIPTEN__ -Itests/stubs -DTOWER_GVEX=1 src/gvex/gvex_world.c 2>/tmp/platform_err.txt || ! grep -q "TOWER_GVEX needs a native platform" /tmp/platform_err.txt; then
  echo "FAIL  the web build accepted TOWER_GVEX=1 (it must be refused: sessions are not played in the browser)"; fail=1
else echo "  ok: TOWER_GVEX=1 compiles for android and desktop, the build without it compiles everywhere, and the web build refuses it"; fi
# a raw platform test outside platform.h is how this goes wrong again
raw=$(grep -rn "__EMSCRIPTEN__\|__ANDROID__" main.c src --include=*.c --include=*.h | grep -v "^src/platform/platform.h")
if [ -n "$raw" ]; then echo "FAIL  a raw __EMSCRIPTEN__ or __ANDROID__ test outside src/platform/platform.h (use a TOWER_ capability):"; echo "$raw" | head -5; fail=1; fi
[ $fail -eq 0 ] && echo "all platforms passed"
exit $fail
