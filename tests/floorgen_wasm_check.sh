#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/floorgen.md, section "floorgen_wasm_check.sh"
# ============================================================================
# The floor generator must make the same floor in the browser as on the server and the phone (docs/design/08, section 12). This builds
# tests/floorgen_hash_headless.c twice, natively and as WebAssembly, runs both with "dump" (the 40 golden floors: hash, attempts, counts,
# route) and fails if the two outputs differ by a single byte.
# The WebAssembly build is made with Emscripten (emcc, run with node) when both are installed, which is the toolchain of the real web
# build; otherwise with Zig's C compiler and wasmtime from PyPI (pip install ziglang wasmtime), which is how it was first checked. The
# Zig build is given the 64 KB stack that Emscripten 3.1.27 and later default to, so it also fails if the generator ever needs more
# stack than the web build has. With no wasm toolchain at all it exits 77, which run_tests.sh reports as SKIP.
#   bash tests/floorgen_wasm_check.sh        (from the project root, or anywhere: it changes to the project root itself)
cd "$(dirname "$0")/.." || exit 1
test_c=tests/floorgen_hash_headless.c
line=$(grep -m1 -E '^//   cc ' "$test_c" | sed 's|^//   ||')
[ -n "$line" ] || { echo "no build line in $test_c"; exit 1; }
command -v cc >/dev/null || { echo "SKIP: no C compiler (cc) for the native build"; exit 77; }

# Which wasm toolchain, before anything is built.
how=""
if command -v emcc >/dev/null && command -v node >/dev/null; then how="emcc"
elif command -v python3 >/dev/null && python3 -c "import ziglang, wasmtime" >/dev/null 2>&1; then how="zig"
fi
[ -n "$how" ] || { echo "SKIP: no WebAssembly toolchain (emcc with node, or python3 with ziglang and wasmtime from PyPI)"; exit 77; }

dir=$(mktemp -d /tmp/floorgen_wasm_XXXX)
trap 'rm -rf "$dir"' EXIT

# The native reference, built with the build line in the test's own header.
native_line=${line//-o */-o $dir/native}
eval "$native_line" >"$dir/native_build.txt" 2>&1 || { echo "the native build failed"; head -10 "$dir/native_build.txt"; exit 1; }
"$dir/native" dump >"$dir/native.txt" 2>"$dir/native_err.txt" || { echo "the native run failed"; head -5 "$dir/native_err.txt"; exit 1; }
[ -s "$dir/native.txt" ] || { echo "the native run printed nothing"; exit 1; }

# The same sources and flags as the build line, without its output name and without -lm (both toolchains bring their own libm).
args=${line#cc }
args=${args%% -o *}
args=${args// -lm/}

if [ "$how" = "emcc" ]; then
  # Emscripten's defaults (64 KB stack) on purpose: that is what the web build of the game uses.
  emcc $args -o "$dir/fg.js" >"$dir/wasm_build.txt" 2>&1 || { echo "the emcc build failed"; head -15 "$dir/wasm_build.txt"; exit 1; }
  node "$dir/fg.js" dump >"$dir/wasm.txt" 2>"$dir/wasm_err.txt" || { echo "the WebAssembly run failed (node)"; head -8 "$dir/wasm_err.txt"; exit 1; }
  via="Emscripten and node"
else
  python3 -m ziglang cc -target wasm32-wasi $args -D_WASI_EMULATED_PROCESS_CLOCKS -lwasi-emulated-process-clocks -Wl,-z,stack-size=65536 \
      -o "$dir/fg.wasm" >"$dir/wasm_build.txt" 2>&1 || { echo "the Zig build failed"; head -15 "$dir/wasm_build.txt"; exit 1; }
  python3 - "$dir/fg.wasm" "$dir/wasm.txt" >"$dir/wasm_err.txt" 2>&1 <<'PY' || { echo "the WebAssembly run failed (wasmtime)"; head -8 "$dir/wasm_err.txt"; exit 1; }
import sys
import wasmtime
wasm_path, out_path = sys.argv[1], sys.argv[2]
engine = wasmtime.Engine()
store = wasmtime.Store(engine)
module = wasmtime.Module.from_file(engine, wasm_path)
linker = wasmtime.Linker(engine)
linker.define_wasi()
cfg = wasmtime.WasiConfig()
cfg.argv = ["floorgen_hash_headless", "dump"]
cfg.stdout_file = out_path
store.set_wasi(cfg)
instance = linker.instantiate(store, module)
try:
    instance.exports(store)["_start"](store)
except wasmtime.ExitTrap as e:        # main returning ends in proc_exit; only a non-zero code is a failure
    if e.code != 0:
        print("exit code", e.code)
        sys.exit(1)
PY
  via="Zig (wasm32-wasi, 64 KB stack) and wasmtime"
fi

[ -s "$dir/wasm.txt" ] || { echo "the WebAssembly run printed nothing"; exit 1; }
if ! cmp -s "$dir/native.txt" "$dir/wasm.txt"; then
  echo "FAILED: the WebAssembly build ($via) makes different floors than the native build"
  diff "$dir/native.txt" "$dir/wasm.txt" | head -12
  exit 1
fi
echo "floorgen_wasm_check: $(wc -l < "$dir/native.txt") golden floors, native and WebAssembly ($via) give identical output"
exit 0
