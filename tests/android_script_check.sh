#!/bin/bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/testing.md, section "tests/android_script_check.sh"
# ============================================================================
# Runs the midnet, compile and link stages of scripts/build_android.sh against a FAKE toolchain (shell scripts that stand in for the NDK's clang,
# llvm-nm and llvm-readelf, and for cargo, rustc and rustup) in a temporary project. It checks the script's own logic: the cargo command line and
# environment, the fallback to an APK without QUIC, TOWER_MIDNET_REQUIRED, stale libraries, the checks after the link. It does NOT build anything for ARM.
#   bash tests/android_script_check.sh        (from the project root, or give the folder as the first argument)
cd "${1:-$(dirname "$0")/..}" || exit 1
real="$(pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
export FT="$T" FAKE_HDR="$real/src/net/mid_net_quic.h"
fail=0
ok()  { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fail=1; }
check() { local what="$1"; shift; if "$@"; then ok "$what"; else bad "$what"; fi; }
has()  { grep -q -- "$2" "$1" 2>/dev/null; }
lacks() { ! grep -q -- "$2" "$1" 2>/dev/null; }

# ---- the fake toolchain ---------------------------------------------------------------------------------------------------------------
bin="$T/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
mkdir -p "$bin" "$T/bin" "$T/log" "$T/sysroot/lib/rustlib/armv7-linux-androideabi/lib"
cat > "$bin/armv7a-linux-androideabi29-clang" <<'EOF'
#!/bin/bash
echo "$*" >> "$FT/log/cc.log"
case "$*" in *-shared*libmid_net_quic.a*) if [ -n "${FAKE_LINK_FAIL_QUIC:-}" ]; then echo "ld.lld: error: undefined symbol: _Unwind_Resume" >&2; exit 1; fi ;; esac
prev=""; for a in "$@"; do if [ "$prev" = "-o" ]; then : > "$a"; fi; prev="$a"; done
exit 0
EOF
cat > "$bin/llvm-nm" <<'EOF'
#!/bin/bash
last=""; for a in "$@"; do last="$a"; done
case "$last" in
  *.a) if [ -z "${FAKE_NM_EMPTY:-}" ]; then
         for fn in $(grep -o 'mid_net_quic_[a-z_]*(' "$FAKE_HDR" | tr -d '(' | sort -u); do
           if [ "$fn" != "${FAKE_NM_MISSING:-}" ]; then echo "0000 T $fn"; fi; done
       fi
       # the real llvm-nm 17 on a library made by a newer rustc: complains about some members, exits 1, still lists the rest
       if [ -n "${FAKE_NM_ERRORS:-}" ] || [ -n "${FAKE_NM_EMPTY:-}" ]; then
         echo "llvm-nm: error: x.rcgu.o: Unknown attribute kind (102) (Producer: 'LLVM23.1.1-rust-1.99.1-stable' Reader: 'LLVM 17.0.2')" >&2; exit 1; fi ;;
  *libmain.so) echo "0000 T ANativeActivity_onCreate"
       if [ -z "${FAKE_SO_NO_QUIC:-}" ] && grep -- '-shared' "$FT/log/cc.log" | tail -1 | grep -q libmid_net_quic.a; then echo "0000 T mid_net_quic_connect"; fi ;;
esac
exit 0
EOF
cat > "$bin/llvm-readelf" <<'EOF'
#!/bin/bash
for n in libc.so libm.so libdl.so liblog.so libandroid.so libEGL.so libGLESv2.so libOpenSLES.so ${FAKE_NEEDED_EXTRA:-}; do
  echo " 0x0001 (NEEDED)             Shared library: [$n]"; done
exit 0
EOF
cat > "$bin/llvm-ar" <<'EOF'
#!/bin/bash
exit 0
EOF
cat > "$T/bin/cargo" <<'EOF'
#!/bin/bash
if [ "$1" = "--version" ]; then echo "cargo 1.99.1 (fake)"; exit 0; fi
{ echo "ARGS: $*"; echo "CC=$CC_armv7_linux_androideabi"; echo "AR=$AR_armv7_linux_androideabi"
  echo "LINKER=$CARGO_TARGET_ARMV7_LINUX_ANDROIDEABI_LINKER"; echo "NDK=$ANDROID_NDK_HOME"; echo "TARGETDIR=$CARGO_TARGET_DIR"; echo "CWD=$(pwd)"
  if [ -f Cargo.lock ]; then echo "LOCK=present"; else echo "LOCK=absent"; fi; } >> "$FT/log/cargo.log"
if [ "${FAKE_CARGO:-}" = "fail" ]; then echo "error: fake cargo failure" >&2; exit 101; fi
mkdir -p "$CARGO_TARGET_DIR/armv7-linux-androideabi/release"; echo FAKE-LIB-CONTENT > "$CARGO_TARGET_DIR/armv7-linux-androideabi/release/libmid_net_quic.a"
exit 0
EOF
cat > "$T/bin/rustc" <<'EOF'
#!/bin/bash
if [ -n "${FAKE_RUSTC_FAIL:-}" ]; then echo "error: toolchain 'stable' is not installed" >&2; exit 1; fi
echo "RUSTC$1-CWD=$(pwd)" >> "$FT/log/rustc.log"
case "$1" in --version) echo "rustc 1.99.1 (fake)" ;; --print) echo "$FT/sysroot" ;; esac
exit 0
EOF
chmod +x "$bin"/* "$T/bin"/*

# ---- a throwaway project around the real script ---------------------------------------------------------------------------------------
reset_project() {
  rm -rf "$T/proj" "$T/me" "$T/log"/* "$T/sysroot/lib/rustlib/armv7-linux-androideabi"
  mkdir -p "$T/proj/scripts" "$T/proj/src/net" "$T/proj/android" "$T/proj/build/android/raylib/src" "$T/me/crates/mid-net/transport-quinn-ffi" \
           "$T/sysroot/lib/rustlib/armv7-linux-androideabi/lib"
  cp "$real/scripts/build_android.sh" "$T/proj/scripts/"
  cp "$real/src/net/mid_net_quic.h" "$T/proj/src/net/"
  echo "int main(void){return 0;}" > "$T/proj/main.c"; echo "int a(void){return 1;}" > "$T/proj/src/a.c"; echo "int b(void){return 2;}" > "$T/proj/src/net/b.c"
  : > "$T/proj/build/android/raylib/src/raylib.h"; : > "$T/proj/build/android/raylib/src/libraylib.a"
  echo "# fake lock" > "$T/proj/android/midnet.Cargo.lock"
}
# run <stages> with the environment given in the variables before it: sets rc and $T/out.txt
run() { ( cd "$T/proj" && env PATH="$T/bin:$PATH" ANDROID_NDK_HOME="$T/ndk" MID_ENGINE="${ME-$T/me}" "${@:2}" bash scripts/build_android.sh $1 ) > "$T/out.txt" 2>&1; rc=$?; }
linkline() { grep -- '-shared' "$T/log/cc.log" | tail -1; }
objs() { grep -c -- ' -c ' "$T/log/cc.log"; }
withflag() { grep -- ' -c ' "$T/log/cc.log" | grep -c -- "$1"; }

echo "== QUIC built: the cargo command, the environment, the flags"
reset_project; run "midnet compile link"
check "all three stages succeed" test $rc -eq 0
check "cargo builds only the static library of the FFI crate for armv7 with ring" has "$T/log/cargo.log" "ARGS: rustc -p mid-net-transport-quinn-ffi --lib --crate-type staticlib --release --target armv7-linux-androideabi --no-default-features --features ring"
check "cargo gets the NDK clang as CC, linker and a llvm-ar" bash -c "grep -q 'CC=$bin/armv7a-linux-androideabi29-clang' '$T/log/cargo.log' && grep -q 'LINKER=$bin/armv7a-linux-androideabi29-clang' '$T/log/cargo.log' && grep -q 'AR=$bin/llvm-ar' '$T/log/cargo.log'"
check "cargo is told where the NDK is (for crates that look for it themselves)" has "$T/log/cargo.log" "NDK=$T/ndk"
check "cargo runs inside mid-engine with a target folder under build/android/midnet" bash -c "grep -q 'CWD=$T/me' '$T/log/cargo.log' && grep -q 'TARGETDIR=$T/proj/build/android/midnet/target' '$T/log/cargo.log'"
check "the pinned lock file is in place during the build" has "$T/log/cargo.log" "LOCK=present"
check "and gone from the mid-engine checkout afterwards" test ! -e "$T/me/Cargo.lock"
check "the library cargo built (not an empty file) is copied to build/android/midnet" has "$T/proj/build/android/midnet/libmid_net_quic.a" FAKE-LIB-CONTENT
check "every game source is compiled with -DTOWER_MIDNET=1" bash -c "test \"\$(grep -c -- ' -c ' '$T/log/cc.log')\" -ge 3 && test \"\$(grep -- ' -c ' '$T/log/cc.log' | grep -c -- '-DTOWER_MIDNET=1')\" -eq \"\$(grep -c -- ' -c ' '$T/log/cc.log')\""
check "the QUIC link strips the debug info of Rust's precompiled parts" bash -c "grep -- '-shared' '$T/log/cc.log' | tail -1 | grep -q -- '--strip-debug'"
check "the link line has the QUIC library, before raylib" bash -c "linkline=\$(grep -- '-shared' '$T/log/cc.log' | tail -1); case \"\$linkline\" in *libmid_net_quic.a*libraylib.a*) true ;; *) false ;; esac"
check "the link stage reports the library is inside and nothing extra is needed" has "$T/out.txt" "QUIC library linked in"

echo "== crypto choice"
reset_project; run "midnet" TOWER_MIDNET_CRYPTO=aws-lc-rs
check "aws-lc-rs: the default features are left alone" lacks "$T/log/cargo.log" "no-default-features"
reset_project; run "midnet" TOWER_MIDNET_CRYPTO=openssl
check "an unknown crypto name falls back, no cargo call" bash -c "test $rc -eq 0 && test ! -e '$T/log/cargo.log' && grep -q 'WITHOUT QUIC' '$T/out.txt'"

echo "== the library cannot be built: the APK is still built, without QUIC, loudly"
reset_project; run "midnet compile link" FAKE_CARGO=fail
check "the stages still succeed" test $rc -eq 0
check "a warning says the APK is built WITHOUT QUIC" has "$T/out.txt" "WITHOUT QUIC"
check "cargo's output is kept in build/android/midnet.log" has "$T/proj/build/android/midnet.log" "fake cargo failure"
check "no library, so compile says -DTOWER_MIDNET=0" bash -c "test ! -e '$T/proj/build/android/midnet/libmid_net_quic.a' && grep -- ' -c ' '$T/log/cc.log' | grep -q -- '-DTOWER_MIDNET=0' && ! grep -- ' -c ' '$T/log/cc.log' | grep -q -- '-DTOWER_MIDNET=1'"
check "and the link line has no QUIC library and no --strip-debug" bash -c "! grep -- '-shared' '$T/log/cc.log' | tail -1 | grep -q 'libmid_net_quic\\|strip-debug'"
check "the lock file is taken away even when cargo failed" test ! -e "$T/me/Cargo.lock"

reset_project; run "midnet" FAKE_CARGO=fail TOWER_MIDNET_REQUIRED=1
check "TOWER_MIDNET_REQUIRED=1: the same failure stops the build" bash -c "test $rc -ne 0 && grep -q 'QUIC library' '$T/out.txt'"

reset_project; mkdir -p "$T/proj/build/android/midnet"; : > "$T/proj/build/android/midnet/libmid_net_quic.a"
run "midnet compile" FAKE_CARGO=fail
check "a stale library from an earlier run is deleted, not linked" bash -c "test ! -e '$T/proj/build/android/midnet/libmid_net_quic.a' && grep -- ' -c ' '$T/log/cc.log' | grep -q -- '-DTOWER_MIDNET=0'"

reset_project; mkdir -p "$T/proj/build/android/midnet"; : > "$T/proj/build/android/midnet/libmid_net_quic.a"
run "midnet compile" TOWER_MIDNET=0
check "TOWER_MIDNET=0: cargo is not called and a stale library is deleted" bash -c "test ! -e '$T/log/cargo.log' && test ! -e '$T/proj/build/android/midnet/libmid_net_quic.a' && grep -- ' -c ' '$T/log/cc.log' | grep -q -- '-DTOWER_MIDNET=0'"

reset_project; mkdir -p "$T/proj/build/android/midnet"; : > "$T/proj/build/android/midnet/libmid_net_quic.a"
run "compile" TOWER_MIDNET=0
check "compile alone with TOWER_MIDNET=0 ignores a library that is lying around" bash -c "grep -- ' -c ' '$T/log/cc.log' | grep -q -- '-DTOWER_MIDNET=0' && ! grep -- ' -c ' '$T/log/cc.log' | grep -q -- '-DTOWER_MIDNET=1'"
reset_project; run "midnet"; rm -f "$T/log/cc.log"; run "compile link"
check "compile and link alone, after an earlier midnet stage, pick the library up" bash -c "test $rc -eq 0 && grep -- ' -c ' '$T/log/cc.log' | grep -q -- '-DTOWER_MIDNET=1' && grep -- '-shared' '$T/log/cc.log' | tail -1 | grep -q libmid_net_quic.a"

echo "== llvm-nm older than rustc's LLVM (the first CI run died of this)"
reset_project; run "midnet compile link" FAKE_NM_ERRORS=1
check "llvm-nm exits 1 with complaints but lists the symbols: the QUIC library is still built and linked" bash -c "test $rc -eq 0 && test -f '$T/proj/build/android/midnet/libmid_net_quic.a' && ! grep -q 'WITHOUT QUIC' '$T/out.txt' && grep -- '-shared' '$T/log/cc.log' | tail -1 | grep -q libmid_net_quic.a"
check "its complaints are kept in build/android/midnet-nm.log" has "$T/proj/build/android/midnet-nm.log" "Unknown attribute kind"
reset_project; run "midnet compile link" FAKE_NM_EMPTY=1
check "llvm-nm gives nothing: a warning, the header check is skipped, the build goes on with QUIC" bash -c "test $rc -eq 0 && grep -q 'could not read the QUIC library' '$T/out.txt' && test -f '$T/proj/build/android/midnet/libmid_net_quic.a'"

echo "== something nobody expected fails inside the QUIC stage"
reset_project; mkdir -p "$T/proj/build/android/midnet/libmid_net_quic.a"
run "midnet compile link"
check "rm cannot delete the library (it is a folder): the stage dies, the build goes on without QUIC" bash -c "test $rc -eq 0 && grep -q 'stopped unexpectedly' '$T/out.txt' && grep -q 'WITHOUT QUIC' '$T/out.txt' && grep -- ' -c ' '$T/log/cc.log' | grep -q -- '-DTOWER_MIDNET=0'"
check "the log names the command and the line that failed" bash -c "grep -q 'build_android.sh stopped at line [0-9]*: rm -f' '$T/out.txt'"
reset_project; mkdir -p "$T/proj/build/android/midnet/libmid_net_quic.a"
run "midnet" TOWER_MIDNET_REQUIRED=1
check "the same with TOWER_MIDNET_REQUIRED=1 stops the build" bash -c "test $rc -ne 0 && grep -q 'stopped unexpectedly' '$T/out.txt'"

echo "== checks on what was built"
reset_project; run "midnet" FAKE_NM_MISSING=mid_net_quic_conn_state
check "a function the header declares but the library lacks: fallback, and it names the function" bash -c "test $rc -eq 0 && grep -q 'mid_net_quic_conn_state' '$T/out.txt' && grep -q 'WITHOUT QUIC' '$T/out.txt' && test ! -e '$T/proj/build/android/midnet/libmid_net_quic.a'"
lastlink_has_quic() { grep -- '-shared' "$T/log/cc.log" | tail -1 | grep -q libmid_net_quic.a; }
lastcompile_flag() { grep -- ' -c ' "$T/log/cc.log" | tail -1 | grep -q -- "$1"; }
reset_project; run "midnet compile link" FAKE_SO_NO_QUIC=1
check "QUIC linked but libmain.so does not contain it: APK without QUIC, with the reason and a warning" bash -c "test $rc -eq 0 && grep -q 'does not contain the QUIC library' '$T/out.txt' && grep -q 'WITHOUT QUIC' '$T/out.txt'"
reset_project; run "midnet compile link" FAKE_NEEDED_EXTRA=libc++_shared.so
check "libmain.so needs a library the phone lacks: APK without QUIC, the library is named" bash -c "test $rc -eq 0 && grep -q 'libc++_shared.so' '$T/out.txt' && grep -q 'WITHOUT QUIC' '$T/out.txt'"
reset_project; run "midnet compile link" FAKE_NEEDED_EXTRA=libc++_shared.so TOWER_MIDNET_REQUIRED=1
check "the same with TOWER_MIDNET_REQUIRED=1 stops the build" bash -c "test $rc -ne 0 && grep -q 'libc++_shared.so' '$T/out.txt'"
reset_project; run "midnet compile link" FAKE_LINK_FAIL_QUIC=1
check "the link with QUIC fails: the sources are compiled again with -DTOWER_MIDNET=0 and linked without the library" bash -c "test $rc -eq 0 && grep -q 'WITHOUT QUIC' '$T/out.txt' && ! grep -- '-shared' '$T/log/cc.log' | tail -1 | grep -q libmid_net_quic.a && grep -- ' -c ' '$T/log/cc.log' | tail -1 | grep -q -- '-DTOWER_MIDNET=0' && test \$(grep -- ' -c ' '$T/log/cc.log' | grep -c -- '-DTOWER_MIDNET=1') -eq 3 && test \$(grep -- ' -c ' '$T/log/cc.log' | grep -c -- '-DTOWER_MIDNET=0') -eq 3"
check "the linker's complaint is kept in build/android/midnet-link.log and the library is deleted" bash -c "grep -q '_Unwind_Resume' '$T/proj/build/android/midnet-link.log' && test ! -e '$T/proj/build/android/midnet/libmid_net_quic.a'"
reset_project; run "midnet compile link" FAKE_LINK_FAIL_QUIC=1 TOWER_MIDNET_REQUIRED=1
check "the link with QUIC fails and TOWER_MIDNET_REQUIRED=1: the build stops" bash -c "test $rc -ne 0 && grep -q 'linking the QUIC library failed' '$T/out.txt'"
reset_project; run "midnet compile link" FAKE_NEEDED_EXTRA=libc++_shared.so TOWER_MIDNET=0
check "the same library without QUIC is not this stage's business (the old behaviour)" test $rc -eq 0

echo "== where mid-engine comes from"
reset_project; echo "[workspace]" > "$T/me/Cargo.lock"; run "midnet"
check "a Cargo.lock that mid-engine has itself is used and left in place" bash -c "grep -q workspace '$T/me/Cargo.lock' && grep -q 'LOCK=present' '$T/log/cargo.log' && ! grep -q 'fake lock' '$T/me/Cargo.lock'"
reset_project; ME=/nonexistent run "midnet"
check "MID_ENGINE pointing at a folder without the crate: fallback with the reason" bash -c "test $rc -eq 0 && grep -q 'no crates/mid-net/transport-quinn-ffi' '$T/out.txt'"
if command -v git >/dev/null 2>&1; then
  reset_project; o="$T/origin"; rm -rf "$o"; mkdir -p "$o/crates/mid-net/transport-quinn-ffi"
  ( cd "$o" && git init -q -b main . && : > crates/mid-net/transport-quinn-ffi/x && git add . && git -c user.email=t@t -c user.name=t commit -q -m one \
      && git checkout -q -b devref && : > marker_devref && git add . && git -c user.email=t@t -c user.name=t commit -q -m two && git checkout -q main )
  ME= run "midnet" TOWER_MIDENGINE_URL="file://$o"
  check "no mid-engine anywhere: it is cloned into build/android/midnet's neighbour build/android/mid-engine" bash -c "test $rc -eq 0 && test -d '$T/proj/build/android/mid-engine/crates/mid-net/transport-quinn-ffi' && grep -q 'CWD=$T/proj/build/android/mid-engine' '$T/log/cargo.log'"
  check "the default branch is cloned when no ref is given" test ! -e "$T/proj/build/android/mid-engine/marker_devref"
  reset_project; ME= run "midnet" TOWER_MIDENGINE_URL="file://$o" TOWER_MIDENGINE_REF=devref
  check "TOWER_MIDENGINE_REF picks the branch" test -e "$T/proj/build/android/mid-engine/marker_devref"
  reset_project; ME= run "midnet" TOWER_MIDENGINE_URL="file://$T/does-not-exist"
  check "a clone that fails: fallback" bash -c "test $rc -eq 0 && grep -q 'could not clone' '$T/out.txt'"
else echo "  skip: git not installed (the clone cases)"; fi

echo "== the rust target"
reset_project; rm -rf "$T/sysroot/lib/rustlib/armv7-linux-androideabi"
cat > "$T/bin/rustup" <<'EOF'
#!/bin/bash
echo "rustup $* (cwd $(pwd))" >> "$FT/log/rustup.log"; mkdir -p "$FT/sysroot/lib/rustlib/armv7-linux-androideabi/lib"; exit 0
EOF
chmod +x "$T/bin/rustup"
run "midnet"
check "a missing rust target is added with rustup, then the build goes ahead" bash -c "grep -q 'target add armv7-linux-androideabi' '$T/log/rustup.log' && test -f '$T/proj/build/android/midnet/libmid_net_quic.a'"
check "rustup and rustc are run inside the mid-engine checkout (its rust-toolchain.toml picks the toolchain)" bash -c "grep -q 'cwd $T/me' '$T/log/rustup.log' && grep -q 'RUSTC--print-CWD=$T/me' '$T/log/rustc.log' && grep -q 'RUSTC--version-CWD=$T/me' '$T/log/rustc.log'"
reset_project; rm -rf "$T/sysroot/lib/rustlib/armv7-linux-androideabi"; echo 'exit 1' >> "$T/bin/rustup"; sed -i '2i exit 1' "$T/bin/rustup"
run "midnet"
check "rustup failing to add it: fallback with the reason" bash -c "test $rc -eq 0 && grep -q 'rustup could not add it' '$T/out.txt'"
rm -f "$T/bin/rustup"

reset_project; run "midnet" FAKE_RUSTC_FAIL=1
check "rustc not working inside mid-engine (toolchain file names one that is missing): fallback with the reason" bash -c "test $rc -eq 0 && grep -q 'rustc does not run inside' '$T/out.txt' && test ! -e '$T/log/cargo.log'"

echo "== the script itself"
check "bash -n" bash -n scripts/build_android.sh
check "--help lists the midnet stage and its settings" bash -c "bash scripts/build_android.sh --help | grep -q 'raylib midnet bake compile link package' && bash scripts/build_android.sh --help | grep -q TOWER_MIDNET_REQUIRED"
check "an unknown stage name is refused" bash -c "! bash scripts/build_android.sh nonsense >/dev/null 2>&1"
if command -v shellcheck >/dev/null 2>&1; then check "shellcheck (warnings)" shellcheck -S warning scripts/build_android.sh; else echo "  skip: shellcheck not installed"; fi
exit $fail
