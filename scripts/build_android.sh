#!/usr/bin/env bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/android.md, section "scripts/build_android.sh"
# ============================================================================
# Builds a sideloadable debug APK of Tower of Babylon for 32-bit ARM Android (armeabi-v7a), the ABI a Galaxy A13 installs.
#
# Needs on PATH or in the environment:
#   git, make, curl, unzip, a JDK 17 (java and keytool), node 18+ (only for the bake stage),
#   cargo and rustc (only for the QUIC library, stage midnet)
#   ANDROID_NDK_HOME  Android NDK (r26d is what the workflow installs)
#   ANDROID_HOME      Android SDK with platform-tools, build-tools and a platform
#
# Stages (all by default, or name some): raylib midnet bake compile link package
#   bash scripts/build_android.sh
#   bash scripts/build_android.sh compile link package
#
# The midnet stage builds mid-engine's QUIC library (Rust, crates/mid-net/transport-quinn-ffi) for armv7 with cargo and the NDK's clang;
# compile and link then build with -DTOWER_MIDNET=1 and link it. Needs cargo and the rust target armv7-linux-androideabi (rustup adds it).
# mid-engine comes from MID_ENGINE, ./mid-engine, ../mid-engine or build/android/mid-engine, and is cloned from GitHub when none has it.
# If the library cannot be built the APK is still built, without QUIC, with a loud warning (TOWER_MIDNET_REQUIRED=1 makes that an error).
#
# Settings (environment): TOWER_ANDROID_API (29), TOWER_ANDROID_TARGET_SDK (34), TOWER_ANDROID_BUILD_TOOLS (34.0.0),
# TOWER_ANDROID_PLATFORM (34), TOWER_RAYLIB_TAG (6.0), TOWER_OPT (-O2), TOWER_VERSION_CODE (1), TOWER_VERSION_NAME (0.1.0),
# TOWER_MIDNET (1; 0 = never build or link QUIC), TOWER_MIDNET_REQUIRED (0), TOWER_MIDNET_CRYPTO (ring, or aws-lc-rs),
# TOWER_MIDENGINE_REF (branch or tag to clone, default: the default branch), MID_ENGINE (path to a mid-engine checkout)
set -euo pipefail
# If any command stops the script (set -e), say which one and where: a silent exit 1 is what cost the first CI run of the QUIC stage an hour.
set -E
trap 'printf "error: %s stopped at line %s: %s\n" "$(basename "$0")" "$LINENO" "$BASH_COMMAND" >&2' ERR

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

API="${TOWER_ANDROID_API:-29}"
TARGET_SDK="${TOWER_ANDROID_TARGET_SDK:-34}"
BUILD_TOOLS="${TOWER_ANDROID_BUILD_TOOLS:-34.0.0}"
PLATFORM="${TOWER_ANDROID_PLATFORM:-34}"
RAYLIB_TAG="${TOWER_RAYLIB_TAG:-6.0}"
OPT="${TOWER_OPT:--O2}"
VERSION_CODE="${TOWER_VERSION_CODE:-1}"
VERSION_NAME="${TOWER_VERSION_NAME:-0.1.0}"
ABI="armeabi-v7a"
TRIPLE="armv7-linux-androideabi"
OUT="$root/build/android"
MIDNET="${TOWER_MIDNET:-1}"
MIDNET_REQUIRED="${TOWER_MIDNET_REQUIRED:-0}"
MIDNET_CRYPTO="${TOWER_MIDNET_CRYPTO:-ring}"
MIDENGINE_URL="${TOWER_MIDENGINE_URL:-https://github.com/Mid-D-Man/mid-engine.git}"
MIDENGINE_REF="${TOWER_MIDENGINE_REF:-}"
MIDNET_DIR="$OUT/midnet"
MIDNET_LIB="$MIDNET_DIR/libmid_net_quic.a"

NDK="${ANDROID_NDK_HOME:-${NDK_HOME:-}}"
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"

log() { printf '\n== %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
# warn: shows in the log and, on GitHub Actions, as an annotation on the run page. summary: a line on the run's summary page (Actions only).
# Both are if-statements that end in "return 0" on purpose: a function whose last command is a failed test would trip "set -e".
warn() {
    printf 'warning: %s\n' "$*" >&2
    if [ -n "${GITHUB_ACTIONS:-}" ]; then printf '::warning::%s\n' "$*"; fi
    return 0
}
summary() {
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then printf '%s\n' "$*" >> "$GITHUB_STEP_SUMMARY"; fi
    return 0
}

require_tools() {
    if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
        die "ANDROID_NDK_HOME is not set to an existing NDK directory"
    fi
    TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
    [ -d "$TOOLCHAIN" ] || die "no linux-x86_64 toolchain under $NDK"
    CC="$TOOLCHAIN/bin/armv7a-linux-androideabi${API}-clang"
    NM="$TOOLCHAIN/bin/llvm-nm"
    READELF="$TOOLCHAIN/bin/llvm-readelf"
    [ -x "$CC" ] || die "missing compiler $CC (is API $API supported by this NDK?)"
}

require_sdk() {
    if [ -z "$SDK" ] || [ ! -d "$SDK" ]; then
        die "ANDROID_HOME is not set to an existing SDK directory"
    fi
    BT="$SDK/build-tools/$BUILD_TOOLS"
    ANDROID_JAR="$SDK/platforms/android-$PLATFORM/android.jar"
    for f in "$BT/aapt" "$BT/zipalign" "$BT/apksigner" "$ANDROID_JAR"; do
        [ -e "$f" ] || die "missing $f (install build-tools;$BUILD_TOOLS and platforms;android-$PLATFORM)"
    done
}

# raylib is built from source for Android, because the libraylib.a in raylib/src is the web build. The headers the game
# compiles against come from the same checkout, so the library and the headers can never disagree.
stage_raylib() {
    log "raylib $RAYLIB_TAG for Android armv7 (API $API)"
    require_tools
    if [ ! -d "$OUT/raylib" ]; then
        mkdir -p "$OUT"
        git clone --depth 1 --branch "$RAYLIB_TAG" https://github.com/raysan5/raylib.git "$OUT/raylib"
    fi
    make -C "$OUT/raylib/src" clean >/dev/null 2>&1 || true
    make -C "$OUT/raylib/src" \
        PLATFORM=PLATFORM_ANDROID \
        ANDROID_NDK="$NDK" \
        ANDROID_ARCH=arm \
        ANDROID_API_VERSION="$API" \
        RAYLIB_LIBTYPE=STATIC \
        -j"$(nproc)"
    [ -f "$OUT/raylib/src/libraylib.a" ] || die "raylib did not produce libraylib.a"
    ls -la "$OUT/raylib/src/libraylib.a"
}

# ---- mid-net's QUIC library ----------------------------------------------------------------------------------------------------------
# On when the stage has built libmid_net_quic.a (the compile and link stages look for the file, so they can be run alone after this one).
midnet_active() { [ "$MIDNET" != "0" ] && [ -f "$MIDNET_LIB" ]; }

# The library could not be built (the stage deleted any copy from an earlier run when it started, so none is left to be linked): either stop
# (TOWER_MIDNET_REQUIRED=1) or carry on without QUIC and say so loudly.
midnet_fail() {
    if [ "$MIDNET_REQUIRED" = "1" ]; then die "QUIC library: $1"; fi
    warn "the APK is built WITHOUT QUIC: $1 (and so WITHOUT MULTIPLAYER: every connection of the game goes through the driver in this library)"
    summary "- **QUIC: not in this APK, and no multiplayer** (the driver is in the same library). $1"
    return 0
}

# Builds only the static library (cargo rustc --crate-type staticlib: no cdylib to link, which this build does not need). The NDK's clang is
# handed to cargo through the variables cargo and the cc crate read, which is all cargo-ndk would do for a crate whose dependencies are C
# only through ring. ring is the default crypto because aws-lc-sys wants cmake and its own Android handling (not tried here; see docs/android.md).
stage_midnet() {
    log "mid-net QUIC library for Android armv7 (API $API)"
    rm -f "$MIDNET_LIB"
    mkdir -p "$MIDNET_DIR"
    if [ "$MIDNET" = "0" ]; then
        echo "TOWER_MIDNET=0: not building the QUIC library, the APK uses plain UDP only"
        summary "- QUIC: off (TOWER_MIDNET=0)"
        return 0
    fi
    require_tools
    local tool
    for tool in cargo rustc; do
        if ! command -v "$tool" >/dev/null 2>&1; then midnet_fail "$tool not found (install Rust, for example with rustup)"; return 0; fi
    done

    local feat=(--no-default-features --features ring)
    case "$MIDNET_CRYPTO" in
        ring) ;;
        aws-lc-rs) feat=() ;;
        *) midnet_fail "TOWER_MIDNET_CRYPTO must be ring or aws-lc-rs, not '$MIDNET_CRYPTO'"; return 0 ;;
    esac

    local me="${MID_ENGINE:-}" d
    if [ -z "$me" ]; then
        for d in "$root/mid-engine" "$root/../mid-engine" "$OUT/mid-engine"; do
            if [ -d "$d/crates/mid-net/transport-quinn-ffi" ]; then me="$d"; break; fi
        done
    fi
    if [ -z "$me" ]; then
        local ref=()
        if [ -n "$MIDENGINE_REF" ]; then ref=(--branch "$MIDENGINE_REF"); fi
        echo "mid-engine not found, cloning $MIDENGINE_URL into $OUT/mid-engine"
        rm -rf "$OUT/mid-engine"
        if ! git clone --depth 1 ${ref[@]+"${ref[@]}"} "$MIDENGINE_URL" "$OUT/mid-engine"; then
            midnet_fail "could not clone $MIDENGINE_URL"
            return 0
        fi
        me="$OUT/mid-engine"
    fi
    if [ ! -d "$me/crates/mid-net/transport-quinn-ffi" ]; then
        midnet_fail "$me has no crates/mid-net/transport-quinn-ffi (an older mid-engine?)"
        return 0
    fi
    local commit
    commit="$(git -C "$me" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "mid-engine: $me (commit $commit)"

    # Rust is asked from inside the mid-engine checkout on purpose: its rust-toolchain.toml names the toolchain (stable), and rustup picks the
    # toolchain by folder, so the target has to be installed for the toolchain that folder uses, not for whichever one is the machine's default.
    local sysroot
    echo "$(cd "$me" && rustc --version 2>&1 || true), $(cd "$me" && cargo --version 2>&1 || true)"
    if ! sysroot="$(cd "$me" && rustc --print sysroot 2>/dev/null)"; then
        midnet_fail "rustc does not run inside $me (does its rust-toolchain.toml name a toolchain that is not installed?)"
        return 0
    fi
    if [ ! -d "$sysroot/lib/rustlib/$TRIPLE/lib" ]; then
        echo "rust target $TRIPLE is not installed, adding it"
        if ! command -v rustup >/dev/null 2>&1 || ! (cd "$me" && rustup target add "$TRIPLE"); then
            midnet_fail "the rust target $TRIPLE is not installed and rustup could not add it"
            return 0
        fi
    fi

    # mid-engine commits no Cargo.lock, so without one cargo takes the newest versions of everything on every run. android/midnet.Cargo.lock
    # holds the versions the library was built and tested with; it is put in place for the build and taken away again (a mid-engine checkout
    # that has its own Cargo.lock keeps it).
    local own_lock=0
    if [ ! -f "$me/Cargo.lock" ] && [ -f "$root/android/midnet.Cargo.lock" ]; then
        if cp "$root/android/midnet.Cargo.lock" "$me/Cargo.lock"; then
            own_lock=1
            echo "dependency versions: android/midnet.Cargo.lock"
        else
            warn "could not copy android/midnet.Cargo.lock into $me: cargo takes the newest versions"
        fi
    fi

    local tdir="${TOWER_MIDNET_TARGET_DIR:-$MIDNET_DIR/target}" rc=0
    mkdir -p "$tdir"
    echo "cargo build, release, $TRIPLE, crypto $MIDNET_CRYPTO (full log in build/android/midnet.log; the first build takes a few minutes)"
    if ! (
        cd "$me"
        export CC_armv7_linux_androideabi="$CC"
        export AR_armv7_linux_androideabi="$TOOLCHAIN/bin/llvm-ar"
        export CARGO_TARGET_ARMV7_LINUX_ANDROIDEABI_LINKER="$CC"
        export ANDROID_NDK_HOME="$NDK"
        export CARGO_TARGET_DIR="$tdir"
        cargo rustc -p mid-net-transport-quinn-ffi --lib --crate-type staticlib --release --target "$TRIPLE" ${feat[@]+"${feat[@]}"}
    ) 2>&1 | tee "$OUT/midnet.log"; then
        rc=1
    fi
    if [ "$own_lock" = "1" ]; then rm -f "$me/Cargo.lock"; fi
    if [ "$rc" -ne 0 ]; then midnet_fail "cargo failed, the reason is in build/android/midnet.log"; return 0; fi

    local built="$tdir/$TRIPLE/release/libmid_net_quic.a"
    if [ ! -f "$built" ]; then midnet_fail "cargo finished but $built is missing"; return 0; fi

    # Every function the game's header declares must be in the library: a header and a library that disagree (src/net/mid_net_quic.h is a copy of
    # the one in mid-engine) would otherwise show up as a link error far from its cause, or not at all.
    # The NDK's llvm-nm is older than the LLVM inside rustc, and exits 1 with "Unknown attribute kind" on some of the library's members (the
    # bitcode rustc embeds in them) while still listing every symbol it can read: its exit status and complaints are ignored (kept in midnet-nm.log).
    # This run found it out: the exit status stopped the whole script, silently.
    local syms fn missing=""
    syms="$("$NM" --defined-only "$built" 2>"$OUT/midnet-nm.log" || true)"
    if [ -z "$syms" ]; then
        warn "llvm-nm could not read the QUIC library (build/android/midnet-nm.log): the check against src/net/mid_net_quic.h is skipped; the link still needs every function the game calls"
    else
        for fn in $(grep -o 'mid_net_quic_[a-z_]*(' "$root/src/net/mid_net_quic.h" | tr -d '(' | sort -u) $(grep -o 'mid_net_[a-z0-9_]*(' "$root/src/net/mid_net_driver.h" | tr -d '(' | sort -u); do
            if ! grep -qw "$fn" <<<"$syms"; then missing="$missing $fn"; fi
        done
        if [ -n "$missing" ]; then midnet_fail "the library lacks functions that src/net/mid_net_quic.h or src/net/mid_net_driver.h declares:$missing"; return 0; fi
    fi

    cp "$built" "$MIDNET_LIB"
    ls -la "$MIDNET_LIB"
    summary "- QUIC: built (mid-engine $commit, crypto $MIDNET_CRYPTO), libmid_net_quic.a $(du -h "$MIDNET_LIB" | cut -f1) before linking"
}

# The Android build has no DixScript wasm (that is a browser thing), so it runs on the baked copy of assets/config/*.mdix.
# Baking here means the APK always carries the numbers the .mdix files say, even if someone edited them and forgot to bake.
stage_bake() {
    log "bake assets/config/*.mdix into src/config/config_baked.c"
    if command -v node >/dev/null 2>&1; then
        node tools/mdix_bake.mjs
        node tools/mdix_bake.mjs --check
    else
        echo "warning: node not found, using the baked copy as committed" >&2
    fi
}

stage_compile() {
    log "game sources as armv7 Android objects"
    require_tools
    [ -f "$OUT/raylib/src/raylib.h" ] || die "raylib.h not found, run the raylib stage first"
    rm -rf "$OUT/obj"
    mkdir -p "$OUT/obj"
    # Notes on the flags:
    #  -I$OUT/raylib/src   the headers of the raylib that was just built, NOT raylib/src of the project (those are the web build's).
    #  -DGRAPHICS_API_OPENGL_ES2 -DPLATFORM_ANDROID   what raylib itself was built with; rlgl.h sizes things by them.
    #  -std=gnu99          the code is plain C99 (the tests build it with -std=c99), gnu99 only avoids bionic header quirks.
    #  -Werror=implicit-function-declaration   a raylib call this raylib does not have fails here, not at the link.
    local cflags=(-std=gnu99 "$OPT" -fPIC -ffunction-sections -fdata-sections -funwind-tables -fstack-protector-strong
                  -Wall -Wno-unused-function -Werror=implicit-function-declaration
                  -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -I"$root/src" -I"$OUT/raylib/src")
    # -DTOWER_MIDNET: src/net/net_mid.c talks to the QUIC library when this is 1, and has stubs that say "not built in" when it is 0.
    # -DTOWER_NETDRIVER: every connection of the game goes through mid-net's driver (src/net/net_driver.c), which is in the SAME library (libmid_net_quic.a carries
    # QUIC and the driver: two Rust static libraries cannot be linked together). Without the library the APK has no multiplayer at all, and says so in the lobby.
    # -DTOWER_GVEX: the server of a session (a phone can host) runs the creatures' world, src/gvex/gvex_world.c on Flecs (third_party/flecs, compiled below as one unit);
    # sessions need the driver, so it comes with it.
    if midnet_active; then cflags+=(-DTOWER_MIDNET=1 -DTOWER_NETDRIVER=1 -DTOWER_GVEX=1 -I"$root/third_party/flecs"); echo "TOWER_MIDNET=1 TOWER_NETDRIVER=1 TOWER_GVEX=1 (QUIC and driver library: $MIDNET_LIB)"; else cflags+=(-DTOWER_MIDNET=0 -DTOWER_NETDRIVER=0); echo "TOWER_MIDNET=0 TOWER_NETDRIVER=0 (no QUIC library: the APK has no multiplayer)"; fi
    local sources=(main.c)
    while IFS= read -r f; do sources+=("$f"); done < <(find src -name '*.c' | sort)
    local f
    for f in "${sources[@]}"; do
        "$CC" "${cflags[@]}" -c "$f" -o "$OUT/obj/${f//\//__}.o"
    done
    # Flecs is one translation unit of its own (4 MB of C); warnings are Flecs's to have (-w), but an implicit declaration is still an error.
    if midnet_active; then
        "$CC" "${cflags[@]}" -w -c third_party/flecs/tob_flecs.c -o "$OUT/obj/third_party__flecs__tob_flecs.c.o"
        sources+=(third_party/flecs/tob_flecs.c)
    fi
    echo "${#sources[@]} sources compiled"
}

# One link of libmain.so (with the QUIC library when there is one). Returns 1 instead of stopping the script, so stage_link can fall back.
# Flags follow raylib's own Android template. The NDK's native app glue is already inside libraylib.a, and
# -u ANativeActivity_onCreate pulls it in. raylib's android_main calls the game's main() (main.c) directly.
# --wrap=fopen is required by raylib: its Android backend redirects fopen to read from the APK's assets, and the
# wrapper calls __real_fopen, which only exists when the final link passes this flag. --no-undefined makes missing
# symbols fail here instead of when the app starts.
link_so() {
    # --strip-debug with the QUIC library: the parts of Rust's standard library that rustc ships precompiled carry debug info (the library is 18 MB
    # before linking on the host). The game itself is built without -g, so this drops only theirs.
    local qlib=() strip=()
    if midnet_active; then qlib=("$MIDNET_LIB"); strip=("-Wl,--strip-debug"); fi
    "$CC" -shared -o "$OUT/stage/lib/$ABI/libmain.so" \
        "$OUT"/obj/*.o \
        ${qlib[@]+"${qlib[@]}"} \
        ${strip[@]+"${strip[@]}"} \
        "$OUT/raylib/src/libraylib.a" \
        -u ANativeActivity_onCreate \
        -Wl,--wrap=fopen \
        -Wl,-soname,libmain.so -Wl,--exclude-libs,libatomic.a -Wl,--build-id \
        -Wl,--no-undefined -Wl,--gc-sections \
        -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -Wl,--warn-shared-textrel \
        -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm -ldl -lc || return 1
    return 0
}

# After a link with the QUIC library: Rust must be inside libmain.so, and must not have pulled in a library the phone lacks (libc++_shared.so and
# libunwind.so are not in the APK and not on the phone: the app would fail to start with a message nobody sees). Only the system libraries named
# in the link line are allowed. Says why on stderr and returns 1 instead of stopping the script.
quic_so_ok() {
    local so="$OUT/stage/lib/$ABI/libmain.so" all needed n bad=""
    all="$("$NM" --defined-only "$so")"
    if ! grep -qw mid_net_quic_connect <<<"$all"; then echo "libmain.so does not contain the QUIC library" >&2; return 1; fi
    needed="$("$READELF" -d "$so" | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | tr '\n' ' ')"
    for n in $needed; do
        case "$n" in
            libc.so|libm.so|libdl.so|liblog.so|libandroid.so|libEGL.so|libGLESv2.so|libOpenSLES.so|libstdc++.so) ;;
            *) bad="$bad $n" ;;
        esac
    done
    if [ -n "$bad" ]; then echo "libmain.so needs libraries the phone does not have:$bad" >&2; return 1; fi
    echo "QUIC library linked in, no extra libraries needed"
    return 0
}

stage_link() {
    log "link libmain.so"
    require_tools
    mkdir -p "$OUT/stage/lib/$ABI"
    if midnet_active; then
        # The QUIC library is the one part of this link that has never run on the real NDK. If it does not link, or links in something wrong,
        # the APK is still built without it (compile again with -DTOWER_MIDNET=0, link again), unless the build says QUIC is required.
        if ! { link_so && quic_so_ok; } 2>&1 | tee "$OUT/midnet-link.log"; then
            if [ "$MIDNET_REQUIRED" = "1" ]; then die "linking the QUIC library failed, the reason is in build/android/midnet-link.log"; fi
            warn "the APK is built WITHOUT QUIC: the library was built but linking it into libmain.so failed (build/android/midnet-link.log)"
            summary "- **QUIC: not in this APK.** The library was built but did not link into libmain.so (log: midnet-link.log)."
            rm -f "$MIDNET_LIB"
            stage_compile
            link_so
        fi
    else
        link_so
    fi
    ls -la "$OUT/stage/lib/$ABI/libmain.so"
    local exports needed
    exports="$("$NM" -D --defined-only "$OUT/stage/lib/$ABI/libmain.so")"
    grep -q "ANativeActivity_onCreate" <<<"$exports" || die "ANativeActivity_onCreate is not exported from libmain.so"
    echo "exported entry point: ANativeActivity_onCreate"
    needed="$("$READELF" -d "$OUT/stage/lib/$ABI/libmain.so" | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | tr '\n' ' ')"
    echo "needs: $needed"
}

stage_package() {
    log "package, align, sign"
    require_sdk
    command -v keytool >/dev/null 2>&1 || die "keytool not found, install a JDK"
    local stage="$OUT/stage" unsigned="$OUT/unsigned.apk" aligned="$OUT/aligned.apk"
    local ks="$OUT/debug.keystore" apk="$OUT/tower-of-babylon-armv7-debug.apk"
    [ -f "$stage/lib/$ABI/libmain.so" ] || die "libmain.so not built yet (run the link stage)"

    sed -e "s/@MIN_SDK@/$API/" -e "s/@TARGET_SDK@/$TARGET_SDK/" \
        -e "s/@VERSION_CODE@/$VERSION_CODE/" -e "s/@VERSION_NAME@/$VERSION_NAME/" \
        android/AndroidManifest.xml > "$stage/AndroidManifest.xml"

    # The game opens its files as "assets/config/weapons.mdix", "assets/shaders/..." and so on. On Android raylib reads
    # them from the APK's asset folder, whose root is the folder given to aapt, so the project's assets/ goes in as
    # assets/assets/ and every path in the code works as written. assets/mdix is the browser's DixScript wasm (3.5 MB),
    # which the phone build does not use.
    rm -rf "$stage/assets"
    mkdir -p "$stage/assets"
    cp -r assets "$stage/assets/assets"
    rm -rf "$stage/assets/assets/mdix"

    rm -f "$unsigned" "$aligned" "$apk"
    "$BT/aapt" package -f -M "$stage/AndroidManifest.xml" -I "$ANDROID_JAR" \
        -A "$stage/assets" -F "$unsigned"
    (cd "$stage" && "$BT/aapt" add "$unsigned" "lib/$ABI/libmain.so")
    "$BT/zipalign" -f -p 4 "$unsigned" "$aligned"

    # A throwaway debug key, no secrets involved. A phone only updates an installed app in place when the new APK is
    # signed with the same key, so the workflow keeps this file in the Actions cache between runs.
    if [ ! -f "$ks" ]; then
        echo "no debug keystore yet, making one (the first APK signed with it needs the old app uninstalled)"
        keytool -genkeypair -keystore "$ks" -storepass android -keypass android \
            -alias towerdebug -keyalg RSA -keysize 2048 -validity 10000 \
            -dname "CN=Tower of Babylon Debug"
    else
        echo "using the existing debug keystore"
    fi
    "$BT/apksigner" sign --ks "$ks" --ks-pass pass:android --key-pass pass:android \
        --ks-key-alias towerdebug --out "$apk" "$aligned"
    "$BT/apksigner" verify --verbose "$apk"
    ls -la "$apk"
    "$BT/aapt" dump badging "$apk" | grep -E "^package|^sdkVersion|^targetSdkVersion|^launchable-activity|^native-code" || true
    echo "contents:"
    unzip -l "$apk" | grep -E "AndroidManifest|libmain|assets/config|assets/shaders" || true
    local quic="no" all
    all="$("$NM" --defined-only "$stage/lib/$ABI/libmain.so" 2>/dev/null || true)"
    if grep -qw mid_net_quic_connect <<<"$all"; then quic="yes"; fi
    echo "QUIC in this APK: $quic"
    summary "- **APK:** $(basename "$apk"), $(du -h "$apk" | cut -f1), QUIC in it: **$quic**"
}

stages=("$@")
if [ "${#stages[@]}" -eq 0 ]; then
    stages=(raylib midnet bake compile link package)
fi
if [ "${stages[0]}" != "midnet_inner" ]; then summary "### Tower of Babylon, Android armv7 build"; fi
for s in "${stages[@]}"; do
    case "$s" in
        raylib|bake|compile|link|package) "stage_$s" ;;
        midnet)
            # The Rust stage runs as a child process, because the shell stops at the first failing command and a command nobody expected to fail
            # (like llvm-nm above) would take the whole APK down with it. As a child it can only cost the APK its QUIC library.
            if ! bash "${BASH_SOURCE[0]}" midnet_inner; then
                rm -f "$MIDNET_LIB" 2>/dev/null || true
                midnet_fail "the QUIC stage stopped unexpectedly (the failing command is named in the log above)"
            fi ;;
        midnet_inner) stage_midnet; exit 0 ;;
        -h|--help) sed -n '2,27p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) die "unknown stage '$s' (raylib midnet bake compile link package)" ;;
    esac
done
log "done"
