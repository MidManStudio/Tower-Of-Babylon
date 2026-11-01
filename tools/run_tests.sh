#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/testing.md, section "tools/run_tests.sh"
# ============================================================================
# Builds and runs every headless C test using the build line in its own file header, from the project root (or the folder
# given as the first argument), then the Node and Python tests that can run here. Needs gcc, and for the optional ones
# node 18+, and python3 with numpy plus Mesa's EGL/GLES2 libraries (those are skipped, with a message, when missing).
#   bash tools/run_tests.sh
# Builds and runs every headless test using the build line in its own file header.
cd "${1:-$(dirname "$0")/..}" || exit 1
fail=0
for f in tests/*_headless.c; do
  line=$(grep -m1 -E '^//   cc ' "$f" | sed 's|^//   ||')
  [ -z "$line" ] && { echo "NO BUILD LINE: $f"; fail=1; continue; }
  out=/tmp/t_$(basename "$f" .c)
  line=${line//-o */-o $out}
  if ! eval "$line" 2>/tmp/build_err.txt; then echo "BUILD FAIL: $f"; head -5 /tmp/build_err.txt; fail=1; continue; fi
  if "$out" > /tmp/run_out.txt 2>&1; then echo "PASS  $f  ($(grep -c '^  ok\|ok:' /tmp/run_out.txt) ok lines)"; else echo "FAIL  $f"; tail -5 /tmp/run_out.txt; fail=1; fi
done
if command -v node >/dev/null 2>&1; then
  if node tests/mdix_bridge_test.mjs 2>&1 | grep -v "^\[mdix\]" | tail -1 | grep -q "all passed"; then echo "PASS  tests/mdix_bridge_test.mjs"; else echo "FAIL  tests/mdix_bridge_test.mjs"; fail=1; fi
  if node tools/mdix_bake.mjs --check >/dev/null 2>&1; then echo "PASS  tools/mdix_bake.mjs --check (the baked config is up to date)"; else echo "FAIL  tools/mdix_bake.mjs --check: run node tools/mdix_bake.mjs"; fail=1; fi
else echo "SKIP  node tests (node is not installed)"; fi
if command -v python3 >/dev/null 2>&1 && python3 -c "import numpy" >/dev/null 2>&1; then
  out=$(python3 tests/shader_gles_test.py 2>&1); rc=$?
  if echo "$out" | grep -q "^SKIP"; then echo "SKIP  tests/shader_gles_test.py ($(echo "$out" | grep '^SKIP' | head -1))"; elif [ $rc -eq 0 ]; then echo "PASS  tests/shader_gles_test.py"; else echo "FAIL  tests/shader_gles_test.py"; echo "$out" | tail -5; fail=1; fi
else echo "SKIP  tests/shader_gles_test.py (python3 with numpy is not installed)"; fi
exit $fail
