#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/net.md, section "The server program"
# ============================================================================
# Builds the dedicated server and the test bot for the machine this runs on (Termux, Linux, macOS): plain C, no raylib library, no
# window. From the project root. The programs land in the folder given (default: build/).
#   bash scripts/build_server.sh                 ->  build/tower_server  build/tower_bot       (plain UDP under the driver)
#   bash scripts/build_server.sh --quic [outdir]     also builds mid-net's native transport (QUIC) and links it: the programs then speak QUIC by
#                                                    default (--transport udp for the plain one). Needs cargo (Termux: pkg install rust).
#     MID_ENGINE=/path/to/mid-engine   where the mid-engine repository is (otherwise ./mid-engine, ../mid-engine or <outdir>/mid-engine; if none
#                                      has it, it is cloned into <outdir>/mid-engine from github.com/Mid-D-Man/mid-engine)
#     MIDNET_CRYPTO=ring               the crypto backend, if aws-lc will not build on this machine (default: aws-lc-rs)
# Always -O2: the player controller uses raymath's inline functions, which need the optimiser.
cd "$(dirname "$0")/.." || exit 1
quic=0
if [ "$1" = "--quic" ]; then quic=1; shift; fi
out="${1:-build}"
mkdir -p "$out" || exit 1
common=(src/net/net_wire.c src/net/net_proto.c src/net/net_reliable.c src/net/net_udp.c src/net/net_discovery.c src/net/net_mid.c src/net/net_move_sim.c src/net/net_driver.c
        src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c)
cc=${CC:-cc}
# Every connection of the game goes through mid-net's driver (src/net/net_driver.c), which is Rust: cargo is always needed. Plain UDP is still what
# carries the packets (or QUIC, with --quic), but the driver is on top of both, on the server and on the bot, so the two ends always agree.
command -v cargo >/dev/null || { echo "build_server: needs cargo (Rust): every connection goes through mid-net's driver, a Rust library. Termux: pkg install rust"; exit 1; }
me="${MID_ENGINE:-}"
if [ -z "$me" ]; then
  for d in ./mid-engine ../mid-engine "$out/mid-engine"; do
    if [ -f "$d/crates/mid-net/src/driver_ffi.rs" ]; then me="$d"; break; fi
  done
fi
if [ -z "$me" ]; then
  echo "build_server: mid-engine not found; cloning it into $out/mid-engine"
  git clone --depth 1 https://github.com/Mid-D-Man/mid-engine "$out/mid-engine" || exit 1
  me="$out/mid-engine"
fi
[ -f "$me/crates/mid-net/src/driver_ffi.rs" ] || { echo "build_server: $me has no mid-net driver (crates/mid-net/src/driver_ffi.rs: an older mid-engine?)"; exit 1; }
feat=()
if [ $quic -eq 1 ]; then
  [ -d "$me/crates/mid-net/transport-quinn-ffi" ] || { echo "build_server: $me has no crates/mid-net/transport-quinn-ffi (an older mid-engine?)"; exit 1; }
  # one Rust library per program: the QUIC library must carry the driver too (it depends on the facade), or the two would not link together
  grep -q '^mid-net *= *{' "$me/crates/mid-net/transport-quinn-ffi/Cargo.toml" || { echo "build_server: the QUIC library of $me does not carry the driver (an older mid-engine: update it, see docs/net.md)"; exit 1; }
  [ "${MIDNET_CRYPTO:-}" = "ring" ] && feat=(--no-default-features --features ring)
  pkg=mid-net-transport-quinn-ffi; libname=libmid_net_quic.a; defs=(-DTOWER_NETDRIVER=1 -DTOWER_MIDNET=1)
  echo "build_server: building mid-net's QUIC and driver library with cargo (the first time takes a few minutes)"
else
  pkg=mid-net; libname=libmid_net.a; defs=(-DTOWER_NETDRIVER=1)
  echo "build_server: building mid-net's driver library with cargo (the first time takes a minute)"
fi
(cd "$me" && cargo build --release -p "$pkg" "${feat[@]}") || exit 1
lib="${CARGO_TARGET_DIR:-$me/target}/release/$libname"
[ -f "$lib" ] || { echo "build_server: cargo finished but $lib is missing (is CARGO_TARGET_DIR set somewhere else?)"; exit 1; }
libs=("$lib" -lm -lpthread -ldl)
set -e
$cc -O2 -std=c99 -Wall -Wextra "${defs[@]}" -DTOWER_SERVER_MAIN -Isrc -Iraylib/src src/server/server_main.c src/net/net_host.c src/net/net_server.c src/net/net_history.c "${common[@]}" "${libs[@]}" -o "$out/tower_server"
$cc -O2 -std=c99 -Wall -Wextra "${defs[@]}" -DTOWER_BOT_MAIN -Isrc -Iraylib/src src/server/bot_main.c src/net/net_client.c src/net/net_dial.c "${common[@]}" "${libs[@]}" -o "$out/tower_bot"
echo "built $out/tower_server and $out/tower_bot$([ $quic -eq 1 ] && echo ' (with QUIC)')"
