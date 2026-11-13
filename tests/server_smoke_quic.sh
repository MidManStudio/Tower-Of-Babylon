#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/net.md, section "tests/server_smoke_quic.sh"
# ============================================================================
# The same as server_smoke.sh but over mid-net's QUIC (the Rust crate mid-net-transport-quinn-ffi in the mid-engine repository): builds the
# server and bot WITH the QUIC library, then runs one server, two bots playing for 5 s over QUIC, and a third bot that LISTS the server and
# FINDS and joins it with no address given; the lobby must say QUIC, and once the server is gone nothing is found (exit 3).
# Needs cargo and a mid-engine checkout (MID_ENGINE, or ./mid-engine, or ../mid-engine); otherwise exits 77, which run_tests.sh reports as SKIP.
# The first build compiles quinn, rustls and a crypto library: several minutes; later runs take seconds.
cd "$(dirname "$0")/.." || exit 1
command -v cargo >/dev/null || { echo "SKIP: cargo is not installed"; exit 77; }
me="${MID_ENGINE:-}"
if [ -z "$me" ]; then for d in ./mid-engine ../mid-engine; do [ -d "$d/crates/mid-net/transport-quinn-ffi" ] && { me="$d"; break; }; done; fi
[ -n "$me" ] && [ -d "$me/crates/mid-net/transport-quinn-ffi" ] || { echo "SKIP: no mid-engine checkout with crates/mid-net/transport-quinn-ffi (set MID_ENGINE)"; exit 77; }
export MID_ENGINE="$me"
dir=$(mktemp -d /tmp/tower_smoke_quic_XXXX)
trap 'rm -rf "$dir"' EXIT
bash scripts/build_server.sh --quic "$dir" >"$dir/build.txt" 2>&1 || { echo "build failed"; tail -15 "$dir/build.txt"; exit 1; }
"$dir/tower_server" --port 0 --discovery-port 0 --name quicsmoke --max-seconds 11 --quiet >"$dir/server.txt" 2>&1 &
srv=$!
port=""; dport=""
for i in $(seq 1 60); do
  port=$(sed -n 's/.*listening on UDP port \([0-9]*\).*/\1/p' "$dir/server.txt" | head -1)
  dport=$(sed -n 's/.*announcing.*on UDP port \([0-9]*\).*/\1/p' "$dir/server.txt" | head -1)
  [ -n "$port" ] && [ -n "$dport" ] && break
  sleep 0.1
done
[ -z "$port" ] || [ -z "$dport" ] && { echo "the server did not start"; cat "$dir/server.txt"; exit 1; }
grep -q "QUIC" "$dir/server.txt" || { echo "the server did not come up speaking QUIC"; cat "$dir/server.txt"; exit 1; }
"$dir/tower_bot" 127.0.0.1 "$port" --seconds 5 --name steve --quiet >"$dir/bot1.txt" 2>&1 &
b1=$!
"$dir/tower_bot" 127.0.0.1 "$port" --seconds 5 --name brou --quiet >"$dir/bot2.txt" 2>&1 &
b2=$!
"$dir/tower_bot" --list --wait 1.5 --discovery-port "$dport" >"$dir/list.txt" 2>&1; rl=$?
"$dir/tower_bot" --find --wait 4 --discovery-port "$dport" --seconds 3 --name finder --quiet >"$dir/find.txt" 2>&1; rf=$?
wait $b1; r1=$?
wait $b2; r2=$?
wait $srv; rs=$?
"$dir/tower_bot" --find --wait 1 --discovery-port "$dport" >"$dir/none.txt" 2>&1; rn=$?
echo "server: $(tail -1 "$dir/server.txt")"
echo "bot 1 ($r1): $(tail -1 "$dir/bot1.txt")"
echo "bot 2 ($r2): $(tail -1 "$dir/bot2.txt")"
echo "list ($rl): $(head -1 "$dir/list.txt")"
echo "find ($rf): $(grep found "$dir/find.txt" | head -1); $(tail -1 "$dir/find.txt")"
echo "after the server is gone ($rn): $(tail -1 "$dir/none.txt")"
if [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && grep -q "peak_players=3" "$dir/server.txt" \
   && [ $rl -eq 0 ] && grep -q "QUIC" "$dir/list.txt" && [ $rf -eq 0 ] && grep -q 'found "quicsmoke"' "$dir/find.txt" && grep -q "tower_bot: ok" "$dir/find.txt" \
   && [ $rn -eq 3 ]; then echo "server smoke test over QUIC passed"; exit 0; fi
echo "FAILED"; exit 1
