#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/net.md, section "tests/net_driver_check.sh"
# ============================================================================
# Builds mid-net (the Rust library with the driver, from the mid-engine checkout: MID_ENGINE, or ./mid-engine, or ../mid-engine), then compiles
# tests/net_driver_test.c against it under the address and undefined-behaviour sanitizers and runs it; and checks that the build WITHOUT the driver
# (the default, TOWER_NETDRIVER=0) compiles and says no to everything. Exits 77 (run_tests.sh reports SKIP) when there is no cargo or no mid-engine
# with the driver (crates/mid-net/src/driver_ffi.rs). The first build of the library takes a minute; later runs take seconds.
cd "$(dirname "$0")/.." || exit 1
command -v cargo >/dev/null || { echo "SKIP: cargo is not installed"; exit 77; }
me="${MID_ENGINE:-}"
if [ -z "$me" ]; then for d in ./mid-engine ../mid-engine; do [ -f "$d/crates/mid-net/src/driver_ffi.rs" ] && { me="$d"; break; }; done; fi
[ -n "$me" ] && [ -f "$me/crates/mid-net/src/driver_ffi.rs" ] || { echo "SKIP: no mid-engine checkout with crates/mid-net/src/driver_ffi.rs (set MID_ENGINE)"; exit 77; }
dir=$(mktemp -d /tmp/tower_netdriver_XXXX)
trap 'rm -rf "$dir"' EXIT
(cd "$me" && cargo build --release -p mid-net >"$dir/cargo.txt" 2>&1) || { echo "building mid-net failed"; tail -15 "$dir/cargo.txt"; exit 1; }
lib="${CARGO_TARGET_DIR:-$me/target}/release/libmid_net.a"
[ -f "$lib" ] || { echo "cargo finished but $lib is missing (is CARGO_TARGET_DIR set somewhere else?)"; exit 1; }
src=(tests/net_driver_test.c src/net/net_driver.c src/net/net_mid.c src/net/net_wire.c src/net/net_proto.c src/net/net_loopback.c src/net/net_move_sim.c src/net/net_server.c src/net/net_history.c src/net/net_client.c
     src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c)
san=""
echo 'int main(void){return 0;}' > "$dir/s.c"
cc -fsanitize=address,undefined "$dir/s.c" -o "$dir/s" 2>/dev/null && "$dir/s" 2>/dev/null && san="-fsanitize=address,undefined -fno-omit-frame-pointer"
cc -O1 -g -std=c99 -Wall -Wextra $san -DNET_DRIVER_TEST -DTOWER_NETDRIVER=1 -Isrc -Iraylib/src "${src[@]}" "$lib" -lm -lpthread -ldl -o "$dir/net_driver_test" 2>"$dir/cc.txt" || { echo "build failed"; head -20 "$dir/cc.txt"; exit 1; }
[ -s "$dir/cc.txt" ] && { echo "compiler warnings:"; head -10 "$dir/cc.txt"; exit 1; }
"$dir/net_driver_test" >"$dir/out.txt" 2>&1; rc=$?
cat "$dir/out.txt"
[ $rc -eq 0 ] || { echo "FAILED (exit $rc)"; exit 1; }
# the build without the driver: every function says no, and nothing needs the library
cc -O1 -std=c99 -Wall -Wextra -Wno-unused-function -DNET_DRIVER_STUB_CHECK -Isrc -Iraylib/src -c src/net/net_driver.c -o "$dir/stub.o" 2>"$dir/stub.txt" || { echo "the stub build failed"; cat "$dir/stub.txt"; exit 1; }
if nm "$dir/stub.o" | grep -q "mid_net_driver_"; then echo "the build without TOWER_NETDRIVER still refers to the Rust library"; exit 1; fi
echo "net_driver check passed"
