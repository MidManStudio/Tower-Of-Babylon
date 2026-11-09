#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/net.md, section "The server program"
# ============================================================================
# Builds the server and the bot as separate programs and runs them against each other over real UDP on this machine: one server, two bots,
# 10% of every packet lost on the way out. Passes when both bots finish playing (exit 0) and the server saw both. About fifteen seconds.
#   bash tests/server_smoke.sh      (from the project root, or give the folder as the first argument)
cd "${1:-$(dirname "$0")/..}" || exit 1
dir=$(mktemp -d /tmp/tower_smoke_XXXX)
trap 'kill $srv 2>/dev/null; rm -rf "$dir"' EXIT
bash scripts/build_server.sh "$dir" >"$dir/build.txt" 2>&1 || { echo "build failed"; tail -8 "$dir/build.txt"; exit 1; }
"$dir/tower_server" --port 0 --max-seconds 11 --loss 10 --quiet >"$dir/server.txt" 2>&1 &
srv=$!
port=""
for i in $(seq 1 50); do
  port=$(sed -n 's/.*listening on UDP port \([0-9]*\).*/\1/p' "$dir/server.txt" | head -1)
  [ -n "$port" ] && break
  sleep 0.1
done
[ -z "$port" ] && { echo "the server did not start"; cat "$dir/server.txt"; exit 1; }
"$dir/tower_bot" 127.0.0.1 "$port" --seconds 5 --name steve --loss 10 --quiet >"$dir/bot1.txt" 2>&1 &
b1=$!
"$dir/tower_bot" 127.0.0.1 "$port" --seconds 5 --name brou --loss 10 --quiet >"$dir/bot2.txt" 2>&1 &
b2=$!
wait $b1; r1=$?
wait $b2; r2=$?
wait $srv; rs=$?
echo "server: $(tail -1 "$dir/server.txt")"
echo "bot 1 ($r1): $(tail -1 "$dir/bot1.txt")"
echo "bot 2 ($r2): $(tail -1 "$dir/bot2.txt")"
if [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && grep -q "peak_players=2" "$dir/server.txt"; then echo "server smoke test passed"; exit 0; fi
echo "server smoke test FAILED"; exit 1
