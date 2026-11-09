#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/net.md, section "The server program"
# ============================================================================
# Builds the dedicated server and the test bot for the machine this runs on (Termux, Linux, macOS): plain C, no raylib library, no
# window. From the project root. The programs land in the folder given (default: build/).
#   bash scripts/build_server.sh            ->  build/tower_server  build/tower_bot
# Always -O2: the player controller uses raymath's inline functions, which need the optimiser.
cd "$(dirname "$0")/.." || exit 1
out="${1:-build}"
mkdir -p "$out" || exit 1
common=(src/net/net_wire.c src/net/net_proto.c src/net/net_reliable.c src/net/net_udp.c src/net/net_move_sim.c
        src/player/player_controller.c src/dungeon/staircase.c src/dungeon/stair_collision.c src/dungeon/dungeon_generator.c src/dungeon/dungeon_probes.c)
cc=${CC:-cc}
set -e
$cc -O2 -std=c99 -Wall -Wextra -DTOWER_SERVER_MAIN -Isrc -Iraylib/src src/server/server_main.c src/net/net_server.c "${common[@]}" -lm -o "$out/tower_server"
$cc -O2 -std=c99 -Wall -Wextra -DTOWER_BOT_MAIN -Isrc -Iraylib/src src/server/bot_main.c src/net/net_client.c "${common[@]}" -lm -o "$out/tower_bot"
echo "built $out/tower_server and $out/tower_bot"
