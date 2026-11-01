#!/usr/bin/env bash
# Brought to u by MidManStudio
# ============================================================================
# NOTICE: Full documentation, design decisions, and fix history for this file
# live in docs/android.md, section "scripts/build_android.sh"
# ============================================================================
# Builds a sideloadable debug APK of Tower of Babylon for 32-bit ARM Android (armeabi-v7a), the ABI a Galaxy A13 installs.
#
# Needs on PATH or in the environment:
#   git, make, curl, unzip, a JDK 17 (java and keytool), node 18+ (only for the bake stage)
#   ANDROID_NDK_HOME  Android NDK (r26d is what the workflow installs)
#   ANDROID_HOME      Android SDK with platform-tools, build-tools and a platform
#
# Stages (all by default, or name some): raylib bake compile link package
#   bash scripts/build_android.sh
#   bash scripts/build_android.sh compile link package
#
# Settings (environment): TOWER_ANDROID_API (29), TOWER_ANDROID_TARGET_SDK (34), TOWER_ANDROID_BUILD_TOOLS (34.0.0),
# TOWER_ANDROID_PLATFORM (34), TOWER_RAYLIB_TAG (6.0), TOWER_OPT (-O2), TOWER_VERSION_CODE (1), TOWER_VERSION_NAME (0.1.0)
set -euo pipefail

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
OUT="$root/build/android"

NDK="${ANDROID_NDK_HOME:-${NDK_HOME:-}}"
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"

log() { printf '\n== %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

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
    local sources=(main.c)
    while IFS= read -r f; do sources+=("$f"); done < <(find src -name '*.c' | sort)
    local f
    for f in "${sources[@]}"; do
        "$CC" "${cflags[@]}" -c "$f" -o "$OUT/obj/${f//\//__}.o"
    done
    echo "${#sources[@]} sources compiled"
}

stage_link() {
    log "link libmain.so"
    require_tools
    mkdir -p "$OUT/stage/lib/$ABI"
    # Flags follow raylib's own Android template. The NDK's native app glue is already inside libraylib.a, and
    # -u ANativeActivity_onCreate pulls it in. raylib's android_main calls the game's main() (main.c) directly.
    # --wrap=fopen is required by raylib: its Android backend redirects fopen to read from the APK's assets, and the
    # wrapper calls __real_fopen, which only exists when the final link passes this flag. --no-undefined makes missing
    # symbols fail here instead of when the app starts.
    "$CC" -shared -o "$OUT/stage/lib/$ABI/libmain.so" \
        "$OUT"/obj/*.o \
        "$OUT/raylib/src/libraylib.a" \
        -u ANativeActivity_onCreate \
        -Wl,--wrap=fopen \
        -Wl,-soname,libmain.so -Wl,--exclude-libs,libatomic.a -Wl,--build-id \
        -Wl,--no-undefined -Wl,--gc-sections \
        -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -Wl,--warn-shared-textrel \
        -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm -ldl -lc
    ls -la "$OUT/stage/lib/$ABI/libmain.so"
    local exports
    exports="$("$NM" -D --defined-only "$OUT/stage/lib/$ABI/libmain.so")"
    grep -q "ANativeActivity_onCreate" <<<"$exports" || die "ANativeActivity_onCreate is not exported from libmain.so"
    echo "exported entry point: ANativeActivity_onCreate"
    echo "needs: $("$READELF" -d "$OUT/stage/lib/$ABI/libmain.so" | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | tr '\n' ' ')"
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
}

stages=("$@")
if [ "${#stages[@]}" -eq 0 ]; then
    stages=(raylib bake compile link package)
fi
for s in "${stages[@]}"; do
    case "$s" in
        raylib|bake|compile|link|package) "stage_$s" ;;
        -h|--help) sed -n '2,22p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) die "unknown stage '$s' (raylib bake compile link package)" ;;
    esac
done
log "done"
