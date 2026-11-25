#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/net.md, section "tests/net_session_check.sh"
# ============================================================================
# The real games of tests/net_session_headless.c (hosting, joining, walking together, a link that goes quiet, a full game, leaving, the host leaving,
# two hosts) over real UDP sockets, with every connection going through mid-net's driver: builds the Rust library (from the mid-engine checkout:
# MID_ENGINE, or ./mid-engine, or ../mid-engine), compiles the test with the build line in its own header plus -DTOWER_NETDRIVER=1, under the
# address and undefined-behaviour sanitizers (which also report memory that was never freed: a connection that ended by itself must free its driver),
# and runs it. Exits 77 (run_tests.sh reports SKIP) when there is no cargo or no mid-engine with the driver. About ten seconds once the library is built.
cd "$(dirname "$0")/.." || exit 1
command -v cargo >/dev/null || { echo "SKIP: cargo is not installed"; exit 77; }
me="${MID_ENGINE:-}"
if [ -z "$me" ]; then for d in ./mid-engine ../mid-engine; do [ -f "$d/crates/mid-net/src/driver_ffi.rs" ] && { me="$d"; break; }; done; fi
[ -n "$me" ] && [ -f "$me/crates/mid-net/src/driver_ffi.rs" ] || { echo "SKIP: no mid-engine checkout with crates/mid-net/src/driver_ffi.rs (set MID_ENGINE)"; exit 77; }
dir=$(mktemp -d /tmp/tower_netsession_XXXX)
trap 'rm -rf "$dir"' EXIT
(cd "$me" && cargo build --release -p mid-net >"$dir/cargo.txt" 2>&1) || { echo "building mid-net failed"; tail -15 "$dir/cargo.txt"; exit 1; }
lib="${CARGO_TARGET_DIR:-$me/target}/release/libmid_net.a"
[ -f "$lib" ] || { echo "cargo finished but $lib is missing (is CARGO_TARGET_DIR set somewhere else?)"; exit 1; }
line=$(grep -m1 -E '^//   cc ' tests/net_session_headless.c | sed 's|^//   ||')
[ -n "$line" ] || { echo "no build line in tests/net_session_headless.c"; exit 1; }
san=""
echo 'int main(void){return 0;}' > "$dir/s.c"
cc -fsanitize=address,undefined "$dir/s.c" -o "$dir/s" 2>/dev/null && "$dir/s" 2>/dev/null && san="-fsanitize=address,undefined -fno-omit-frame-pointer"
line=${line/cc -O2 -std=c99/cc -O1 -g -std=c99 -Wall -Wextra -Wno-unused-function $san -DTOWER_NETDRIVER=1}
line=${line/-lm -o */\"$lib\" -lm -lpthread -ldl -o $dir/net_session}
eval "$line" 2>"$dir/cc.txt" || { echo "build failed"; head -20 "$dir/cc.txt"; exit 1; }
[ -s "$dir/cc.txt" ] && { echo "compiler warnings:"; head -10 "$dir/cc.txt"; exit 1; }
"$dir/net_session" >"$dir/out.txt" 2>&1; rc=$?
cat "$dir/out.txt"
[ $rc -eq 0 ] || { echo "FAILED (exit $rc)"; exit 1; }
grep -q "\[FAIL\]" "$dir/out.txt" && { echo "a check failed"; exit 1; }
# the lobby list in a build WITH the driver (net_discovery.c only reads the platform's flags: no library is needed to compile it)
dline=$(grep -m1 -E '^//   cc ' tests/net_discovery_headless.c | sed 's|^//   ||')
if [ -n "$dline" ]; then
  dline=${dline/cc -O2/cc -O1 -DTOWER_NETDRIVER=1}
  dline=${dline//-o */-o $dir/net_discovery}
  eval "$dline" 2>"$dir/cc2.txt" || { echo "the discovery test did not build with the driver"; head -10 "$dir/cc2.txt"; exit 1; }
  "$dir/net_discovery" >"$dir/out2.txt" 2>&1 || { echo "the discovery test failed with the driver"; tail -8 "$dir/out2.txt"; exit 1; }
  echo "  ok: the lobby list offers games to a build that has the driver (tests/net_discovery_headless.c with -DTOWER_NETDRIVER=1)"
fi
# and the lobby screen's backend over a real search, in a build WITH the driver (a game is joinable there; without it the entry says CANNOT CONNECT)
lline=$(grep -m1 -E '^//   cc ' tests/lobby_headless.c | sed 's|^//   ||')
if [ -n "$lline" ]; then
  lline=${lline/cc -O2/cc -O1 -DTOWER_NETDRIVER=1}
  lline=${lline//-o */-o $dir/lobby}
  eval "$lline" 2>"$dir/cc3.txt" || { echo "the lobby test did not build with the driver"; head -10 "$dir/cc3.txt"; exit 1; }
  "$dir/lobby" >"$dir/out3.txt" 2>&1 || { echo "the lobby test failed with the driver"; grep FAIL "$dir/out3.txt" | head -5; exit 1; }
  echo "  ok: the lobby's real search lists a joinable game in a build that has the driver (tests/lobby_headless.c with -DTOWER_NETDRIVER=1)"
fi
echo "net_session check passed"
