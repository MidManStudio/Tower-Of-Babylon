# Config: weapon and projectile numbers in DixScript files

## Overview

Every number that describes a weapon, a projectile or the flashlight lives in three DixScript files, `assets/config/weapons.mdix`, `assets/config/projectiles.mdix` and `assets/config/flashlight.mdix`, the way a Unity ScriptableObject holds numbers and enums. The game reads them through the DixScript WebAssembly module (`assets/mdix/`, the same build that is published as the npm package `@midmanstudio/mdix`). No JSON is involved anywhere: the files are read with typed getters by path (`weapons[2].damage`).

The wasm only exists in the browser (the C package, `mdix-c`, is a wrapper over a native library and has no Emscripten build, and was never published). So there are two sources behind one set of calls, and the game is playable from the first frame on either:

1. **The baked copy** (`src/config/config_baked.c`): every value of the three files, already evaluated, as a sorted table. A tool (`tools/mdix_bake.mjs`) writes it from the files with the same wasm. It needs nothing at run time, so it is what the native tests read, and what the game starts with.
2. **The files themselves** (web only): once the wasm has started the game reads `assets/config/*.mdix` from the bundled assets, parses them, and replaces the baked values. Edit a number, rebuild with mray, and the new number is in the game without running anything else.

If anything about the second goes wrong, the baked values stay, and the diagnostics overlay (tap the FPS counter) says why on its `config:` line.

## Files

| File | What it holds |
|---|---|
| `assets/config/weapons.mdix` | One row per weapon, in the order of `WeaponId`: hands, pipe, pistol, shotgun, rifle. |
| `assets/config/projectiles.mdix` | One row per projectile config; a gun names one by `id`. |
| `assets/config/flashlight.mdix` | One table, `flashlight`: capacity, battery size, when it fails, the beam. See `flashlight.md`. A single object, read as `flashlight.range`, not a group array. |
| `assets/mdix/mdix_wasm_bg.wasm`, `mdix_wasm_bg.js` | The DixScript engine and its glue, copied from the npm package (`@midmanstudio/mdix` 1.0.2, `wasm-pkg/`). 3.5 MB. |
| `src/config/config_baked.c` | Generated. Do not edit. |
| `tools/mdix_bake.mjs` | Checks the three files and writes `config_baked.c`. Node 18+, no npm install. |

mray bundles the whole `assets/` folder, so no change to `mray.py` was needed.

## How the numbers get into the game

1. `main()` calls `GameConfig_Init()` first thing after the window opens. It installs the baked copy (so `Weapon_Def()` is valid at once) and starts the wasm.
2. Starting the wasm does not wait. `MdixWeb_BootStart` reads the glue and the wasm from the bundled files, hands the bytes to JavaScript, and returns; the browser compiles the module in the background. This is a plain `EM_JS` function that starts a promise, not an `EM_ASYNC_JS`, so the build does not depend on `ASYNCIFY`.
3. `Frame()` calls `GameConfig_Poll()` each frame. While the wasm is loading it does nothing. When it is ready it reads the three `.mdix` files, parses them through the wasm, builds the tables with the same checks the tool applies, and only if everything is valid installs them (`source` becomes "files"). The hash of each file is compared with the hash the baked copy was made from: if they differ the overlay says "baked copy is older than the files" (the game is using your edit; run the tool before relying on the baked copy).
4. If the wasm fails to start, a file cannot be read, a file does not parse, or a value is missing, of the wrong type or out of range, the baked values stay and the overlay line shows the first problem in orange. Nothing is half-installed.

The swap happens a moment after the first frame, while the player has nothing in hand, so nothing visible changes except the numbers. Tables are copied into their static storage in place, so pointers returned by `Weapon_Def` stay valid across the swap.

## Writing the .mdix files

What was learned by running the real engine (published npm package, Node 22) against files written for this game:

- **Header.** The first line is `// Brought to u by MidManStudio`. `@CONFIG` has `version`, `author` and a `features` list that must name every optional section the file uses (`enums`, `quickfuncs`, `data`); using `@ENUMS` without `enums` in the list is a parse error ("@ENUMS is not allowed with current feature settings").
- **Enums are written out by value** (`SEMI = 0, AUTO = 1, PUMP = 2`). The C side mirrors them, and `game_config.c` has a compile-time check that the C enums have the same numbers. An enum value reads as its ordinal through `getInt`. Inside an object in a group array it keeps its enum type.
- **Floats carry an `f`** (`0.30f`). The engine's getters are strict: `getInt` of a float and `getFloat` of an int are errors. The game reads numbers with the lenient `getDouble`, so `30` would load, but the tool and `weapons.mdix`'s own comments say to write floats as floats: the tool refuses an int where the schema says float, because that is nearly always a typo.
- **Rows are objects in a group array**, written `weapons:: { ... }, { ... }` with commas between fields and between rows. Comments are allowed anywhere inside `@DATA`.
- **Paths** are `array[index].field` (`weapons[3].pellets`); an array's length is `getArrayLength("weapons")`. `array.1.field` does not work.
- **Strings** must be plain ASCII (the baked table is C source).
- **QuickFuncs** (`@QUICKFUNCS`) are evaluated at load, and work: a test row `gun("rifle", ...)` with a computed `dps = damage * pellets` came back as 96. They are not used here, because named fields read better than a 16 argument call, but they are available (add `quickfuncs` to `features`).
- Not used: `@IMPORTS`, `@PROFILE`, the other value types. `getJson` and `toJson` exist but nothing here calls them.

### Adding things

- **A field on a weapon or projectile:** add it to the row type in `weapons.h` or `projectile_types.h`, read it in `BuildWeapons` or `BuildProjectiles` in `game_config.c` (with a range), add it to the field table at the top of `tools/mdix_bake.mjs`, add it to every row in the `.mdix`, run the tool.
- **A projectile config:** a new row in `projectiles.mdix` (at most 16), then name it from a gun's `projectile`.
- **A weapon:** a new `WeaponId` before `WEAPON_COUNT`, its id string in `k_ids` in `weapons.c` and in `WEAPON_IDS` in the tool, a row in `weapons.mdix` in the same position, a part list in `viewmodel.c`, an item (and icon), a pickup (and model), and the muzzle row in `k_muzzle`.

## Modules (all in `src/config/`)

### `mdix_doc.h` and `mdix_doc.c`

A document is something you read by path. `MdixDoc_FromBaked(table)` reads a baked table; `MdixDoc_Parse(text)` parses text through the wasm. Both answer the same calls: `Has`, `ArrayLen` (-1 when it is not an array), `Int` (an int or an enum's ordinal), `Float` (a float, or an int widened), `Bool`, `Str` (false when it does not fit; never cut). They return `false` for "not there or not that type" and never throw. The baked table is sorted by `strcmp` and looked up by binary search; an array's length is stored as an entry named `weapons[]`. `Mdix_Hash` is 32 bit FNV-1a, the same as the tool uses, so the game can tell whether the baked copy is of the file it is reading. `Mdix_WasmStart`, `Mdix_WasmState` (idle, loading, ready, failed) and `Mdix_WasmError` front the wasm start.

### `mdix_web.c`

The bridge. On a non-Emscripten build every call says no ("the DixScript wasm only runs in the web build"). On Emscripten, the JavaScript lives in `EM_JS` functions: `mdix_js_boot` copies the glue and the wasm out of the C heap at once (so C frees them straight away), loads the glue as an ES module from a Blob URL, instantiates the wasm by hand with the glue as its only import (the steps a bundler performs for the npm package), calls `__wbg_set_wasm` and `__wbindgen_start`, and records ready or failed. `import()` is written inside `new Function(...)` so the Emscripten toolchain's JavaScript parser never has to read it. Documents are kept in a JavaScript `Map` by integer handle; getters write their result into the C heap (`HEAP32`, `HEAPF64`, `HEAPU8`) and return 1 or 0. Only `HEAPU8`, `HEAP32`, `HEAPF64` and `Module` are used from the Emscripten runtime; strings are encoded with `TextEncoder`/`TextDecoder` so nothing depends on which runtime helper functions the toolchain happened to include.

Modelled on `com.midmanstudio.mdix.webgl` in the DixScript-Rust repository, which does the same job for a Unity WebGL build.

### `game_config.h` and `game_config.c`

`GameConfig_LoadFrom(weaponsDoc, projectilesDoc, flashlightDoc, err, cap)` builds the tables (the flashlight table's values are checked for type and range, and its start charge and battery may not exceed its capacity, nor its inner beam reach its outer), validating every value (type and range, projectile ids exist and are unique, every weapon id is a known weapon and appears once, a gun has an ammo type and a real projectile, hot spread is not below cold spread) and installs them only if all three built. `GameConfig_LoadBaked`, `GameConfig_Init` and `GameConfig_Poll` are described above. `GameConfig_Status()` returns the source, the wasm state, whether the baked copy is stale, and the first problem found.

### `tools/mdix_bake.mjs`

`node tools/mdix_bake.mjs` loads the three files with the wasm in `assets/mdix/` (so it runs the same engine as the game), checks them against the schema (every field present and of the type the C side reads; enum values are ones the C code knows; ids match `WeaponId` order; every gun's projectile exists; ranges for pellets and falloff; the flashlight's start charge and battery within its capacity, its inner beam inside its outer, its range and strength), and writes `src/config/config_baked.c`. `--check` changes nothing and exits 2 if the generated file is out of date, 1 if the files are invalid. Tested with deliberately broken files: an int where a float belongs, an unknown projectile, a wrong id, a missing `features` entry, and a stale baked file are each refused with the path named.

### `tests/config_headless.c`

Run from the project root; build line in the file. Checks that all three baked tables are in `strcmp` order, that the `.mdix` files on disk hash to what the baked copy was made from (so a forgotten re-bake fails a test), every getter on the baked table including the type strictness and the NULL document, the weapon and projectile tables built from the baked copy (hands and pipe equal the numbers they had before the config existed; pistol 25 damage, 0.3 s, hold to repeat; shotgun pump, shells, 8 pellets in 4.5 degrees; rifle automatic, 0.12 s, spread opening to 5 degrees; ammo boxes 8, 6 and 24), nine kinds of bad document (each refused and named, with the installed tables untouched after all of them), a good document of its own taking effect, the flashlight table (read back as written; four bad values and a missing one refused and named), and the native path (the wasm start fails politely, the status says baked, the weapons still work).

### `tests/mdix_bridge_test.mjs`

`node tests/mdix_bridge_test.mjs`. Extracts every `EM_JS` body from `mdix_web.c` as it is, runs them against a simulated WebAssembly heap and the real wasm in `assets/mdix/`, and reads the game's own `.mdix` files through them: boot, ready, both files loaded, array lengths, a non-array, has, an int, an enum's ordinal, a float, an int widened to a float, a string, a string too long for its buffer (refused), wrong-type reads (refused), a file that does not parse (handle 0 and a reason), a freed handle, an unknown handle. It also zeroes the heap bytes after boot to prove the JavaScript copied them. It cannot cover Emscripten's own plumbing (that `EM_JS` and `HEAPU8` behave as documented in the toolchain on the phone) or the Blob URL import, which Node does not support (a `data:` URL stands in for it).

## Fixes and Problems

- The first plan (a built-in C reader for a subset of DixScript, because the C package could not be linked) was dropped when it turned out the wasm build that publishes the npm package could be loaded by hand: `mdix_wasm_bg.js` has no imports of its own and the wasm imports only that file. This also lifts the "no QuickFuncs" restriction the small reader would have had.
- The first design started the wasm with `EM_ASYNC_JS`, which needs `ASYNCIFY`. It now starts a promise and the game polls, so the build has no new requirement and the game does not wait for a 3.5 MB module to compile before the first frame.
- A rule found by a test before it ran: a gun only heats if a shot adds more heat than the gun loses between shots (`cooldown * heat_recover` below `heat_per_shot`). The rifle's first numbers lost more than they gained at full auto, so it would never have opened its spread; fixed in the file and written down in its comments.

## Tuning

Everything in the two `.mdix` files, with its meaning in the comments at the top of each. Ranges that the C side accepts are in `BuildWeapons` and `BuildProjectiles`.

## Known limits

- **Not run in a browser.** Everything on the web path that is JavaScript was run in Node against the real wasm, and every C file passes a syntax check against stand-in Emscripten headers, but the first load of `mdix_wasm_bg.js` through a Blob URL, and the `HEAPU8` and `Module` names inside `EM_JS` on the phone's toolchain, have not been seen working. If it fails, the game plays on the baked values and the overlay says why.
- The wasm adds 3.5 MB to `index.data`.
- Items (`item_types.c`) are still a table in C; moving them to a `.mdix` is the next step the same machinery supports. `tests/mdix_bridge_test.mjs` still loads only the weapons and projectiles files through the bridge; the flashlight file goes through the same code and is checked by the bake tool, but not by that test.
- A `.mdix` cannot be changed while the game runs (there is no file watching in a browser build); edit, rebuild, reload.
