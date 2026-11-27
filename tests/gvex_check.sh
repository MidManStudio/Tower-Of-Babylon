#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/testing.md, section "tests/gvex_check.sh"
# ============================================================================
# The creatures of a session (docs/gvex_session.md, slice S1): builds Flecs (third_party/flecs, a custom build: about ten seconds), then compiles
# tests/gvex_test.c (the server's world: spawns, senses, walking, two worlds in one process, hurting) and tests/net_gvex_test.c (two clients
# over a link with latency, jitter and loss: the same creatures on both screens, the wire format, a kill) with the build line in each file's own header,
# under the address and undefined-behaviour sanitizers (which also report memory that was never freed), and runs them. Exits 77 (run_tests.sh reports
# SKIP) when there is no C compiler that takes -std=gnu99. About 20 seconds.
cd "$(dirname "$0")/.." || exit 1
cc=${CC:-cc}
command -v "$cc" >/dev/null || { echo "SKIP: no C compiler"; exit 77; }
dir=$(mktemp -d /tmp/tower_gvex_XXXX)
trap 'rm -rf "$dir"' EXIT
"$cc" -O2 -std=gnu99 -Ithird_party/flecs -c third_party/flecs/tob_flecs.c -o "$dir/tob_flecs.o" 2>"$dir/flecs.txt" || { echo "building Flecs failed"; head -15 "$dir/flecs.txt"; exit 1; }
san=""
echo 'int main(void){return 0;}' > "$dir/s.c"
"$cc" -fsanitize=address,undefined "$dir/s.c" -o "$dir/s" 2>/dev/null && "$dir/s" 2>/dev/null && san="-fsanitize=address,undefined -fno-omit-frame-pointer"
rc=0
for t in gvex_test net_gvex_test; do
  line=$(grep -m1 -E '^//   cc ' "tests/$t.c" | sed 's|^//   ||')
  [ -n "$line" ] || { echo "no build line in tests/$t.c"; exit 1; }
  line=${line/cc -O1 -std=c99/$cc -O1 -g -std=c99 -Wall -Wextra -Wno-unused-function $san}
  line=${line//third_party\/flecs\/tob_flecs.c/$dir/tob_flecs.o}
  line=${line//-o */-o $dir/$t}
  eval "$line" 2>"$dir/cc_$t.txt" || { echo "tests/$t.c did not build"; head -20 "$dir/cc_$t.txt"; exit 1; }
  [ -s "$dir/cc_$t.txt" ] && { echo "compiler warnings in tests/$t.c:"; head -10 "$dir/cc_$t.txt"; exit 1; }
  echo "== tests/$t.c"
  "$dir/$t" > "$dir/out_$t.txt" 2>&1; r=$?
  cat "$dir/out_$t.txt"
  [ $r -eq 0 ] || { echo "tests/$t.c FAILED (exit $r)"; rc=1; }
  grep -q "\[FAIL\]" "$dir/out_$t.txt" && { echo "a check failed in tests/$t.c"; rc=1; }
done
[ $rc -eq 0 ] && echo "gvex check passed"
exit $rc
