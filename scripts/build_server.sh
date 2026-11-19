#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/net.md, section "The server program"
# ============================================================================
# Builds the dedicated server and the test bot for the machine this runs on (Termux, Linux, macOS): plain C, no raylib library, no
# window. From the project root. The programs land in the folder given (default: build/).
#   bash scripts/build_server.sh                 ->  build/tower_server  build/tower_bot       (plain UDP only)
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
common=(src/net/net_wire.c src/net/net_proto.c src/net/net_reliable.c src/net/net_udp.c src/net/net_discovery.c src/net/net_mid.c src/net/net_move_sim.c
        src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c)
cc=${CC:-cc}
defs=()
libs=(-lm)
if [ $quic -eq 1 ]; then
  command -v cargo >/dev/null || { echo "build_server: --quic needs cargo (Rust). Termux: pkg install rust"; exit 1; }
  me="${MID_ENGINE:-}"
  if [ -z "$me" ]; then
    for d in ./mid-engine ../mid-engine "$out/mid-engine"; do
      if [ -d "$d/crates/mid-net/transport-quinn-ffi" ]; then me="$d"; break; fi
    done
  fi
  if [ -z "$me" ]; then
    echo "build_server: mid-engine not found; cloning it into $out/mid-engine"
    git clone --depth 1 https://github.com/Mid-D-Man/mid-engine "$out/mid-engine" || exit 1
    me="$out/mid-engine"
  fi
  [ -d "$me/crates/mid-net/transport-quinn-ffi" ] || { echo "build_server: $me has no crates/mid-net/transport-quinn-ffi (an older mid-engine?)"; exit 1; }
  feat=()
  [ "${MIDNET_CRYPTO:-}" = "ring" ] && feat=(--no-default-features --features ring)
  echo "build_server: building mid-net's QUIC library with cargo (the first time takes a few minutes)"
  (cd "$me" && cargo build --release -p mid-net-transport-quinn-ffi "${feat[@]}") || exit 1
  lib="${CARGO_TARGET_DIR:-$me/target}/release/libmid_net_quic.a"
  [ -f "$lib" ] || { echo "build_server: cargo finished but $lib is missing (is CARGO_TARGET_DIR set somewhere else?)"; exit 1; }
  defs=(-DTOWER_MIDNET=1)
  libs=("$lib" -lm -lpthread -ldl)
fi
set -e
$cc -O2 -std=c99 -Wall -Wextra "${defs[@]}" -DTOWER_SERVER_MAIN -Isrc -Iraylib/src src/server/server_main.c src/net/net_host.c src/net/net_server.c src/net/net_history.c "${common[@]}" "${libs[@]}" -o "$out/tower_server"
$cc -O2 -std=c99 -Wall -Wextra "${defs[@]}" -DTOWER_BOT_MAIN -Isrc -Iraylib/src src/server/bot_main.c src/net/net_client.c "${common[@]}" "${libs[@]}" -o "$out/tower_bot"
echo "built $out/tower_server and $out/tower_bot$([ $quic -eq 1 ] && echo ' (with QUIC)')"
