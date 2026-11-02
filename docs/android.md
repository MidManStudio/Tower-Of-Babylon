# android

## Overview

The Android build turns the same C99 + raylib source that mray compiles for the browser into a sideloadable APK for 32-bit ARM (`armeabi-v7a`, the only ABI the Galaxy A13 installs; the sister project Terminal Viscosity established that). It is built by GitHub Actions, so nothing is needed on the phone beyond a browser: open the Actions tab, run **Android APK**, download the artifact (a zip with one `.apk` inside), unzip, install.

The pipeline is five stages in `scripts/build_android.sh`: build raylib 6.0 from source for Android; bake `assets/config/*.mdix` into `src/config/config_baked.c`; compile `main.c` and every `src/**/*.c` with the NDK; link them with raylib into `libmain.so`; package, align and sign the APK. There is no Java code (`android:hasCode="false"`): the activity is Android's `NativeActivity`, which loads `libmain.so`, and raylib's `android_main` calls the game's own `main()`.

## What is the same and what is different from the browser build

- The game is drawn at 800 x 450 and raylib scales it. raylib 6.0's Android backend keeps `GetScreenWidth()` at 800 and `GetScreenHeight()` at 450, makes the render buffer the shape of the real screen, centres the 800 x 450 picture with black bars (a 20:9 phone is wider than 16:9, so the bars are at the sides) and maps every touch back into 800 x 450 (`rcore_android.c`, `SetupFramebuffer` and the touch code, read in the 6.0 source). So the HUD rectangles and touch zones in `main.c` work unchanged.
- OpenGL ES 2.0 is the graphics API in both. `src/ui/view_clip.c` (the depth clear between world and hands, the depth and MSAA readout) and `src/ui/highp_shader.c` (the highp program and the flashlight beam program) used to run only in the browser; they now run wherever the platform has the capability `TOWER_GLES2` (web and Android, see `platforms.md`). Without that the hands would be drawn without a depth clear and there would be no beam. See `docs/lighting.md`.
- Config comes from the baked copy only. The DixScript wasm is a browser thing (`src/config/mdix_web.c` says "no" off the web, which the diagnostics overlay shows as `config: baked copy (wasm off)` with an orange note; that is expected on Android). The bake stage makes sure the baked copy matches the `.mdix` files in the checkout.
- Files are opened as `assets/config/...`, `assets/shaders/...` exactly as in the code. raylib on Android reads them from the APK's asset folder, so the project's `assets/` goes into the APK as `assets/assets/`. `assets/mdix/` (the 3.5 MB browser wasm) is left out.
- Audio is the same code. raylib's Android build carries its sound backend (`-lOpenSLES` is already in the link line), and `Audio_Update` opens the sound card on the first frame instead of waiting for a touch (that wait is the browser's rule: the capability `TOWER_AUDIO_GESTURE_UNLOCK`, see `platforms.md`). The limiter, the loops and the tap-to-audition line in the diagnostics work as in the browser. Not heard on a device yet; see `docs/audio.md`.

## Modules

### `scripts/build_android.sh`
**What it does:** the five stages above. `bash scripts/build_android.sh` runs all of them; `bash scripts/build_android.sh compile link package` runs some (the raylib stage is the slow one and its checkout stays in `build/android/raylib`). Settings by environment: `TOWER_ANDROID_API` (29), `TOWER_ANDROID_TARGET_SDK` (34), `TOWER_ANDROID_BUILD_TOOLS` (34.0.0), `TOWER_ANDROID_PLATFORM` (34), `TOWER_RAYLIB_TAG` (6.0), `TOWER_OPT` (-O2), `TOWER_VERSION_CODE`, `TOWER_VERSION_NAME`.

**Decisions:**
- raylib is built from source at tag 6.0, and the game compiles against the headers of **that** checkout (`-I build/android/raylib/src`), not against `raylib/src/` of the project, whose `libraylib.a` is the web build and whose headers are 6.1-dev. The library and the headers therefore always agree. All 36 game sources were compiled on the host against the 6.0 headers with no error and no implicit declaration, so 6.0 has every function the game calls. `-Werror=implicit-function-declaration` keeps that true: a raylib call that a future raylib lacks fails at compile time with its name, not at the link.
- The raylib build line, the link flags and the library list (`-llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm -ldl -lc`, `-u ANativeActivity_onCreate`, `-Wl,--wrap=fopen`) are the ones Terminal Viscosity's `scripts/build_android.sh` uses, which is known to run on the phone. `--wrap=fopen` is required by raylib's Android file loading; `--no-undefined` turns a missing symbol into a link error.
- `-std=gnu99` rather than `-std=c99`: the tests build the code with strict `-std=c99`, so it is plain C99; gnu99 only avoids surprises in bionic's headers.
- The bake stage runs `node tools/mdix_bake.mjs` and then `--check`. It rewrites `src/config/config_baked.c` in the CI checkout (not committed), so a `.mdix` edit that was never baked still reaches the APK.
- The package stage uses `aapt`, `zipalign` and `apksigner` the same way Terminal Viscosity does, with a throwaway debug key (`debug.keystore`, alias `towerdebug`, password `android`). It prints the badging (package, SDK, launcher activity, native code) at the end.

### `android/AndroidManifest.xml`
**What it does:** declares the app as a native activity with no Java. Package `com.midmanstudio.towerofbabylon`, landscape, OpenGL ES 2.0 required, `@MIN_SDK@`, `@TARGET_SDK@`, `@VERSION_CODE@` and `@VERSION_NAME@` filled in by the script (defaults 29, 34, 1 and 0.1.0; the workflow passes the run number).

**Decisions:** the same structure as Terminal Viscosity's manifest, which is known to install and run on the phone, with only the names changed. `android:exported="true"` is required on Android 12 and newer. `configChanges` stops the activity being recreated on rotation or resize.

### `.github/workflows/android.yml`
**What it does:** manual only (`workflow_dispatch`). Java 17, Node 20 (for the bake), the Android SDK (platform-tools, build-tools 34.0.0, platform 34), NDK r26d, the debug key from the Actions cache, then `scripts/build_android.sh`, then uploads `build/android/tower-of-babylon-armv7-debug.apk` as the artifact `tower-of-babylon-armv7-debug-apk`.

**Decisions:**
- The tool setup copies Terminal Viscosity's `android.yml`, which builds on GitHub today, minus Rust, Odin and the DixScript library, none of which this game uses.
- **The debug key is cached** (`actions/cache`, key `tower-debug-keystore-v1`). A phone only updates an installed app in place when the new APK has the same signing key; with a key regenerated on every run you would have to uninstall the old build each time. With the cache, later builds install over the old one. GitHub drops cache entries that have gone unused for 7 days, and then the next APK has a new key: uninstall the old app first (there is no save data to lose). The key is a throwaway with a public password; it protects nothing.
- `versionCode` is the run number, so each build is newer than the last.

## Installing on the phone

1. Actions tab, **Android APK**, Run workflow. It takes a few minutes, most of it raylib.
2. Download the artifact; it is a zip, unzip it (a file manager does it).
3. Open the `.apk`. Android asks once to allow installs from that app (the file manager or the browser).
4. If it says the app conflicts with an existing package, uninstall the old Tower of Babylon first (different signing key, see above).
5. To read the game's log: `adb logcat -s raylib` over USB or wireless debugging. The lines that matter: `DISPLAY: depth buffer ... MSAA samples ...`, `SHADER: high precision shader loaded`, `SHADER: flashlight shader loaded`, and raylib's `DISPLAY: Upscaling required` with the viewport offsets.

## Not verified (the sandbox has no NDK, no SDK, no Android phone)

- None of the NDK, SDK, `aapt`, `apksigner` or raylib-for-Android steps have run. The script was checked with `bash -n` and shellcheck, the workflows parse as YAML, and the C compiles on the host against the 6.0 headers (syntax and declarations only, not an ARM build). The Android-only branches in `view_clip.c` and `highp_shader.c` were syntax-checked against a stand-in `GLES2/gl2.h`.
- The first real run will show whether anything in the raylib 6.0 Android build differs from what Terminal Viscosity needs; it uses the same Makefile invocation.
- How the picture looks on the screen (the bars, the touch zones lining up with the drawn buttons), whether the highp and beam shaders compile on the Mali's driver, how fast it runs against the browser build, and whether `EGL_SAMPLES 4` (MSAA from `FLAG_MSAA_4X_HINT`) is accepted by the device. The diagnostics overlay (tap the FPS counter) and `adb logcat` answer these.
- Only `armeabi-v7a` is built. A 64-bit phone could run it too if it allows 32-bit apps; an `arm64-v8a` build would need `ANDROID_ARCH=arm64`, the `aarch64-linux-android` compiler and its own `lib/arm64-v8a/` folder, and has not been tried.

## Fixes and Problems

### `scripts/build_android.sh`
- None recorded yet.

### `android/AndroidManifest.xml`
- None recorded yet.

### `.github/workflows/android.yml`
- None recorded yet.

### `src/dungeon/dungeon_generator.c`
- `PATH_MAX` and `PATH_MIN` (the allowed route length to the stairs, 260 and 52 tiles) are now `ROUTE_MAX` and `ROUTE_MIN`. `PATH_MAX` is also a libc macro (`limits.h`, 4096 on Android), and defining it again changes the meaning silently or warns, depending on include order. Same numbers, same behaviour; every test still passes.
