// Brought to u by MidManStudio
// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "tests/mdix_bridge_test.mjs"
// ============================================================================
// Runs the JavaScript inside src/config/mdix_web.c (the EM_JS bodies, copied out as they are) against a simulated
// WebAssembly heap and the real DixScript wasm in assets/mdix/, then reads the game's own .mdix files through it, the
// way the game does on the phone. What it cannot cover: Emscripten's own plumbing (EM_JS itself, HEAPU8 growth) and the
// Blob URL import, which Node does not support: a data: URL stands in for it here.
//   node tests/mdix_bridge_test.mjs
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const src = fs.readFileSync(path.join(root, "src/config/mdix_web.c"), "utf8");
const web = src.slice(src.indexOf("#if defined(__EMSCRIPTEN__)"), src.indexOf("#else"));

// ---- pull the EM_JS functions out of the C file ------------------------------------------------------------------
const fns = {};
const re = /EM_JS\(\s*(\w+)\s*,\s*(\w+)\s*,\s*\(([^)]*)\)\s*,\s*\{/g;
let m;
while ((m = re.exec(web))) {
  let depth = 1, i = re.lastIndex;
  while (depth > 0 && i < web.length) { const ch = web[i++]; if (ch === "{") depth++; else if (ch === "}") depth--; }
  const body = web.slice(re.lastIndex, i - 1);
  const params = m[3].trim() === "void" ? [] : m[3].split(",").map((p) => p.trim().split(/[\s*]+/).pop());
  fns[m[2]] = { params, body };
}

// ---- a simulated Emscripten heap and Module ---------------------------------------------------------------------------
const heap = new ArrayBuffer(16 * 1024 * 1024);
const HEAPU8 = new Uint8Array(heap), HEAP32 = new Int32Array(heap), HEAPF64 = new Float64Array(heap);
let bump = 1024;
const alloc = (n, align = 8) => { bump = (bump + align - 1) & ~(align - 1); const p = bump; bump += n; return p; };
const cstr = (s) => { const b = new TextEncoder().encode(s); const p = alloc(b.length + 1); HEAPU8.set(b, p); HEAPU8[p + b.length] = 0; return p; };
const readCstr = (p) => { let e = p; while (HEAPU8[e]) e++; return new TextDecoder().decode(HEAPU8.subarray(p, e)); };
const Module = {};

// Node cannot import() a blob: URL. A data: URL carries the same text.
URL.createObjectURL = () => "data:text/javascript;base64," + Buffer.from(globalThis.__lastBlob).toString("base64");
URL.revokeObjectURL = () => {};
const RealBlob = globalThis.Blob;
globalThis.Blob = class extends RealBlob { constructor(parts, o) { super(parts, o); globalThis.__lastBlob = Buffer.concat(parts.map((p) => Buffer.from(p))); } };

const C = {};
for (const [name, { params, body }] of Object.entries(fns)) {
  const f = new Function("HEAPU8", "HEAP32", "HEAPF64", "Module", ...params, body);
  C[name] = (...args) => f(HEAPU8, HEAP32, HEAPF64, Module, ...args);
}

let failed = 0;
const check = (ok, what) => { console.log(`  ${ok ? "ok" : "FAIL"}: ${what}`); if (!ok) failed++; };

console.log("Bridge: the C file has every function the game needs");
for (const n of ["mdix_js_boot", "mdix_js_state", "mdix_js_error", "mdix_js_load", "mdix_js_free", "mdix_js_has", "mdix_js_len", "mdix_js_int", "mdix_js_float", "mdix_js_bool", "mdix_js_str"])
  check(typeof C[n] === "function", `${n} found and its body is valid JavaScript`);

console.log("Boot: the wasm and glue from assets/mdix, as the game hands them over");
const glueBytes = fs.readFileSync(path.join(root, "assets/mdix/mdix_wasm_bg.js"));
const wasmBytes = fs.readFileSync(path.join(root, "assets/mdix/mdix_wasm_bg.wasm"));
const pg = alloc(glueBytes.length), pw = alloc(wasmBytes.length);
HEAPU8.set(glueBytes, pg); HEAPU8.set(wasmBytes, pw);
check(C.mdix_js_state() === 0, "before the boot the state is 0 (loading or idle)");
const realLog = console.log; console.log = () => {};       // the engine prints its own progress lines
C.mdix_js_boot(pg, glueBytes.length, pw, wasmBytes.length);
HEAPU8.fill(0, pg, pg + glueBytes.length);                 // the game frees these at once: the JavaScript must have copied them
HEAPU8.fill(0, pw, pw + wasmBytes.length);
for (let i = 0; i < 200 && C.mdix_js_state() === 0; ++i) await new Promise((r) => setTimeout(r, 10));
console.log = realLog;
if (C.mdix_js_state() !== 1) { const e = alloc(200); C.mdix_js_error(e, 200); console.log("  boot error:", readCstr(e)); }
check(C.mdix_js_state() === 1, "the module becomes ready");

console.log("Documents: the game's own files");
const out = alloc(4, 4), outD = alloc(8, 8), buf = alloc(256);
const load = (text) => { console.log = () => {}; const h = C.mdix_js_load(cstr(text)); console.log = realLog; return h; };
const weapons = fs.readFileSync(path.join(root, "assets/config/weapons.mdix"), "utf8");
const projectiles = fs.readFileSync(path.join(root, "assets/config/projectiles.mdix"), "utf8");
const hw = load(weapons), hp = load(projectiles);
check(hw > 0 && hp > 0 && hw !== hp, "weapons.mdix and projectiles.mdix each get their own handle");
check(C.mdix_js_len(hw, cstr("weapons")) === 5 && C.mdix_js_len(hp, cstr("projectiles")) === 3, "array lengths 5 and 3");
check(C.mdix_js_len(hw, cstr("weapons[0].id")) === -1, "a non-array has length -1");
check(C.mdix_js_has(hw, cstr("weapons[3].pellets")) === 1 && C.mdix_js_has(hw, cstr("weapons[3].nope")) === 0 && C.mdix_js_has(hw, cstr("weapons[9].id")) === 0, "has: yes, no, no");
check(C.mdix_js_int(hw, cstr("weapons[3].pellets"), out) === 1 && HEAP32[out >> 2] === 8, "an int: the shotgun has 8 pellets");
check(C.mdix_js_int(hw, cstr("weapons[4].fire_mode"), out) === 1 && HEAP32[out >> 2] === 1, "an enum reads as its ordinal: the rifle is AUTO = 1");
check(C.mdix_js_float(hw, cstr("weapons[2].damage"), outD) === 1 && HEAPF64[outD >> 3] === 25, "a float: the pistol does 25");
check(C.mdix_js_float(hw, cstr("weapons[3].pellets"), outD) === 1 && HEAPF64[outD >> 3] === 8, "an int widens to a float");
check(Math.abs((C.mdix_js_float(hp, cstr("projectiles[1].speed_max"), outD), HEAPF64[outD >> 3]) - 52) < 1e-6, "projectile speed 52");
check(C.mdix_js_str(hw, cstr("weapons[3].projectile"), buf, 256) === 1 && readCstr(buf) === "buckshot", "a string: the shotgun's projectile is buckshot");
check(C.mdix_js_str(hw, cstr("weapons[3].projectile"), buf, 4) === 0, "a string that does not fit is refused, not cut");
check(C.mdix_js_int(hw, cstr("weapons[2].damage"), out) === 0, "an int read of a float says no");
check(C.mdix_js_int(hw, cstr("weapons[2].id"), out) === 0, "an int read of a string says no");
check(C.mdix_js_bool(hw, cstr("weapons[2].damage"), out) === 0, "a bool read of a float says no");

console.log("Errors and cleanup");
const hb = load('@CONFIG( version -> "1.0.0" features -> "data" )\n@ENUMS( A { X } )\n@DATA( n = 1 )');
check(hb === 0, "text that does not parse gives handle 0");
C.mdix_js_error(buf, 256);
check(readCstr(buf).length > 5, `and a reason: "${readCstr(buf).slice(0, 70)}"`);
C.mdix_js_free(hw);
check(C.mdix_js_has(hw, cstr("weapons[0].id")) === 0 && C.mdix_js_len(hw, cstr("weapons")) === -1, "a freed handle answers no and -1");
check(C.mdix_js_free(9999) === undefined, "freeing an unknown handle does nothing");

console.log(failed ? `\n${failed} FAILED` : "\nall passed");
process.exit(failed ? 1 : 0);
