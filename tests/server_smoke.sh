#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/net.md, section "The server program"
# ============================================================================
# Builds the server and the bot as separate programs (every connection through mid-net's driver) and runs them against each other over real UDP on this machine: one server, two bots,
# 10% of every packet lost on the way out. Passes when both bots finish playing (exit 0) and the server saw both. While they play, a second
# server announces itself and a third bot LISTS it, then FINDS it and joins it with no address given (net/net_discovery.h); once that server
# is gone a bot finds nothing and says so (exit 3). About fifteen seconds.
#   bash tests/server_smoke.sh      (from the project root, or give the folder as the first argument)
cd "${1:-$(dirname "$0")/..}" || exit 1
# The server and the bot speak mid-net's driver (a Rust library): cargo and a mid-engine checkout (MID_ENGINE, or ./mid-engine, or ../mid-engine) are needed; otherwise exit 77 (SKIP).
command -v cargo >/dev/null || { echo "SKIP: cargo is not installed (the server and the bot go through mid-net's driver)"; exit 77; }
me="${MID_ENGINE:-}"
if [ -z "$me" ]; then for d in ./mid-engine ../mid-engine; do [ -f "$d/crates/mid-net/src/driver_ffi.rs" ] && { me="$d"; break; }; done; fi
[ -n "$me" ] && [ -f "$me/crates/mid-net/src/driver_ffi.rs" ] || { echo "SKIP: no mid-engine checkout with crates/mid-net/src/driver_ffi.rs (set MID_ENGINE)"; exit 77; }
export MID_ENGINE="$me"
dir=$(mktemp -d /tmp/tower_smoke_XXXX)
trap 'kill $srv 2>/dev/null; rm -rf "$dir"' EXIT
bash scripts/build_server.sh "$dir" >"$dir/build.txt" 2>&1 || { echo "build failed"; tail -8 "$dir/build.txt"; exit 1; }
"$dir/tower_server" --port 0 --max-seconds 11 --loss 10 --quiet --no-discovery >"$dir/server.txt" 2>&1 &
srv=$!
port=""
for i in $(seq 1 50); do
  port=$(sed -n 's/.*listening on UDP port \([0-9]*\).*/\1/p' "$dir/server.txt" | head -1)
  [ -n "$port" ] && break
  sleep 0.1
done
[ -z "$port" ] && { echo "the server did not start"; cat "$dir/server.txt"; exit 1; }
"$dir/tower_server" --port 0 --discovery-port 0 --name smoke --max-seconds 9 --quiet >"$dir/server2.txt" 2>&1 &
srv2=$!
dport=""
for i in $(seq 1 50); do
  dport=$(sed -n 's/.*announcing.*on UDP port \([0-9]*\).*/\1/p' "$dir/server2.txt" | head -1)
  [ -n "$dport" ] && break
  sleep 0.1
done
[ -z "$dport" ] && { echo "the announcing server did not start"; cat "$dir/server2.txt"; exit 1; }
"$dir/tower_bot" 127.0.0.1 "$port" --seconds 5 --name steve --loss 10 --quiet >"$dir/bot1.txt" 2>&1 &
b1=$!
"$dir/tower_bot" 127.0.0.1 "$port" --seconds 5 --name brou --loss 10 --quiet >"$dir/bot2.txt" 2>&1 &
b2=$!
"$dir/tower_bot" --list --wait 1.5 --discovery-port "$dport" >"$dir/list.txt" 2>&1; rl=$?
"$dir/tower_bot" --find --wait 4 --discovery-port "$dport" --seconds 3 --name finder --quiet >"$dir/find.txt" 2>&1; rf=$?
wait $b1; r1=$?
wait $b2; r2=$?
wait $srv2
"$dir/tower_bot" --find --wait 1 --discovery-port "$dport" >"$dir/none.txt" 2>&1; rn=$?
wait $srv; rs=$?
echo "server: $(tail -1 "$dir/server.txt")"
echo "bot 1 ($r1): $(tail -1 "$dir/bot1.txt")"
echo "bot 2 ($r2): $(tail -1 "$dir/bot2.txt")"
echo "list ($rl): $(head -1 "$dir/list.txt")"
echo "find ($rf): $(grep found "$dir/find.txt" | head -1); $(tail -1 "$dir/find.txt")"
echo "after the server is gone ($rn): $(tail -1 "$dir/none.txt")"
if [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && grep -q "peak_players=2" "$dir/server.txt" \
   && [ $rl -eq 0 ] && grep -q '"smoke"' "$dir/list.txt" && [ $rf -eq 0 ] && grep -q 'found "smoke"' "$dir/find.txt" && grep -q "tower_bot: ok" "$dir/find.txt" \
   && grep -q "peak_players=1" "$dir/server2.txt" && [ $rn -eq 3 ]; then echo "server smoke test passed"; exit 0; fi
echo "server smoke test FAILED"; exit 1
