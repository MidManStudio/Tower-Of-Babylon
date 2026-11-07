// Brought to u by MidManStudio
// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/config.md, section "tools/mdix_bake.mjs"
// ============================================================================
// Reads assets/config/*.mdix with the same DixScript wasm the game loads (assets/mdix/), checks them, and
// writes src/config/config_baked.c: every value, already evaluated, as a table the C code can read without the wasm.
//
//   node tools/mdix_bake.mjs            check the files, then write src/config/config_baked.c
//   node tools/mdix_bake.mjs --check    check the files and that config_baked.c is up to date; changes nothing
//
// Needs Node 18 or newer and nothing else (no npm install): the wasm and its glue are in assets/mdix/.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const OUT = path.join(root, "src/config/config_baked.c");
const checkOnly = process.argv.includes("--check");

// ---- load the engine ---------------------------------------------------------------------------------------
const glue = await import(pathToFileURL(path.join(root, "assets/mdix/mdix_wasm_bg.js")).href);
const wasmBytes = fs.readFileSync(path.join(root, "assets/mdix/mdix_wasm_bg.wasm"));
const { instance } = await WebAssembly.instantiate(wasmBytes, { "./mdix_wasm_bg.js": glue });
glue.__wbg_set_wasm(instance.exports);
instance.exports.__wbindgen_start();

// The engine logs its progress to the console; keep this tool's output to its own lines.
const realLog = console.log, realInfo = console.info;
console.log = console.info = () => {};
const say = (...a) => realLog(...a);

// ---- what the C code expects (mirrors src/weapons/weapons.h and src/projectiles/projectile_types.h) ----------------
const ENUMS = {
  Motion:   { STRAIGHT: 0, GUIDED: 2, TELEPORT: 3, WAVE: 4, CIRCULAR: 5 },
  Piercing: { NONE: 0, PIERCER: 1, RANDOM: 2 },
  Kind:     { UNARMED: 0, MELEE: 1, GUN: 2 },
  FireMode: { SEMI: 0, AUTO: 1, PUMP: 2 },
  Ammo:     { NONE: 0, PISTOL: 1, SHELLS: 2, RIFLE: 3 },
  Pattern:  { CONE: 0, FAN: 1 },
};
// field: [type, enum name when type is "enum"]
const PROJECTILE_FIELDS = {
  id: ["string"], motion: ["enum", "Motion"], speed_min: ["float"], speed_max: ["float"], lifetime: ["float"],
  max_range: ["float"], gravity: ["float"], radius: ["float"], falloff_start: ["float"], falloff_end: ["float"],
  falloff_min: ["float"], piercing: ["enum", "Piercing"], max_hits: ["int"], trail_length: ["float"],
  trail_width: ["float"], color_core: ["int"], color_glow: ["int"],
};
const WEAPON_COMMON = { id: ["string"], name: ["string"], kind: ["enum", "Kind"], damage: ["float"], cooldown: ["float"], duration: ["float"] };
const WEAPON_MELEE = { windup: ["float"], reach: ["float"], arc_deg: ["float"], knockback: ["float"] };
const WEAPON_GUN = {
  fire_mode: ["enum", "FireMode"], ammo: ["enum", "Ammo"], ammo_per_shot: ["int"], projectile: ["string"], pellets: ["int"],
  pattern: ["enum", "Pattern"], spread_deg: ["float"], spread_max_deg: ["float"], heat_per_shot: ["float"], heat_recover: ["float"],
  kick: ["float"], noise_metres: ["float"], noise_secs: ["float"], pickup_ammo: ["int"], ammo_box: ["int"],
};
const WEAPON_IDS = ["hands", "pipe", "pistol", "shotgun", "rifle"];   // the order of WeaponId

const FLASHLIGHT_FIELDS = {
  capacity_secs: ["float"], start_secs: ["float"], battery_secs: ["float"], low_fraction: ["float"], beam_inner_deg: ["float"],
  beam_outer_deg: ["float"], range: ["float"], strength: ["float"], lag: ["float"], tint: ["int"],
};

const FLOOR_FIELDS = {
  console_hold_secs: ["float"], console_drain_per_sec: ["float"], console_reach: ["float"], monsters_on_start: ["int"], monster_gap_secs: ["float"],
  countdown_secs: ["float"], collapse_secs: ["float"], collapse_ease: ["int"], rage_speed: ["float"],
};

const SWARM_FIELDS = {
  worms: ["int"], dots_per_worm: ["int"], iterations: ["int"], first_floor: ["int"], every_n_floors: ["int"], show_core: ["int"],
  dot_radius: ["float"], link_length: ["float"], crawl_speed: ["float"], speed_spread: ["float"], weave: ["float"], gravity: ["float"],
  damping: ["float"], squirm: ["float"], sense_range: ["float"], spawn_min_dist: ["float"], core_health: ["float"], core_half: ["float"],
  death_secs: ["float"], bite_damage: ["float"], bite_windup: ["float"], bite_interval: ["float"], bite_cap: ["float"], bite_reach: ["float"],
};

const CHARACTER_FIELDS = Object.fromEntries(["id", "name", "call", "role", "overview", "backstory", "passive", "activatable", "special"].map((k) => [k, ["string"]]));

const SOUND_IDS = ["footstep", "jump", "land", "slide", "pipe_swing", "pipe_hit", "pistol", "shotgun", "shotgun_pump", "rifle", "dry_fire",
  "pickup", "use_item", "flash_click", "battery", "hurt", "death", "hit_confirm", "kill_confirm", "skitter", "alert", "door", "heartbeat",
  "ambient", "ui_click"];                                 // the order of SoundId in src/audio/sound_bank.h

// "array": a group array of objects (weapons[2].damage); "object": one table (flashlight.range).
const FILES = [
  { file: "weapons.mdix", array: "weapons", symbol: "g_bakedWeapons", shape: "array" },
  { file: "projectiles.mdix", array: "projectiles", symbol: "g_bakedProjectiles", shape: "array" },
  { file: "flashlight.mdix", array: "flashlight", symbol: "g_bakedFlashlight", shape: "object" },
  { file: "sounds.mdix", array: "sounds", symbol: "g_bakedSounds", shape: "array" },
  { file: "floor.mdix", array: "stair_clock", symbol: "g_bakedFloor", shape: "object" },
  { file: "characters.mdix", array: "characters", symbol: "g_bakedCharacters", shape: "array" },
  { file: "swarm.mdix", array: "swarm", symbol: "g_bakedSwarm", shape: "object" },
];

// ---- helpers -----------------------------------------------------------------------------------------------
const errors = [];
const bad = (m) => errors.push(m);

function fnv1a(buf) {
  let h = 0x811c9dc5;
  for (const b of buf) { h ^= b; h = Math.imul(h, 0x01000193) >>> 0; }
  return h >>> 0;
}

function floatText(v) {
  const f = Math.fround(v);
  for (let p = 1; p <= 9; ++p) { const s = f.toPrecision(p); if (Math.fround(parseFloat(s)) === f) return String(parseFloat(s)); }
  return String(f);
}

// Walks one array of objects, calling add(path, type, text) for every value.
function walk(db, array, add) {
  const n = db.getArrayLength(array);
  add(`${array}[]`, "i", String(n));
  for (let i = 0; i < n; ++i) {
    const prefix = `${array}[${i}]`;
    for (const key of db.getKeys(prefix)) {
      const p = `${prefix}.${key}`;
      const t = db.getValueType(p);
      if (t === "int")        add(p, "i", String(db.getInt(p)));
      else if (t === "enum")  add(p, "i", String(db.getInt(p)));
      else if (t === "float") add(p, "f", floatText(db.getFloat(p)));
      else if (t === "bool")  add(p, "b", db.getBool(p) ? "true" : "false");
      else if (t === "string") {
        const s = db.getString(p);
        if (!/^[\x20-\x7e]*$/.test(s)) bad(`${p}: only plain ASCII text is allowed in a string`);
        add(p, "s", s);
      } else bad(`${p}: a value of type ${t} is not supported (use int, float, bool, string or an enum)`);
    }
  }
}

// Walks one table of values, calling add(path, type, text) for every value.
function walkObject(db, prefix, add) {
  for (const key of db.getKeys(prefix)) {
    const p = `${prefix}.${key}`;
    const t = db.getValueType(p);
    if (t === "int" || t === "enum") add(p, "i", String(db.getInt(p)));
    else if (t === "float") add(p, "f", floatText(db.getFloat(p)));
    else if (t === "bool") add(p, "b", db.getBool(p) ? "true" : "false");
    else if (t === "string") {
      const s = db.getString(p);
      if (!/^[\x20-\x7e]*$/.test(s)) bad(`${p}: only plain ASCII text is allowed in a string`);
      add(p, "s", s);
    } else bad(`${p}: a value of type ${t} is not supported (use int, float, bool, string or an enum)`);
  }
}

function checkFields(db, prefix, fields) {
  for (const [key, [type, enumName]] of Object.entries(fields)) {
    const p = `${prefix}.${key}`;
    if (!db.exists(p)) { bad(`${p}: missing`); continue; }
    const t = db.getValueType(p);
    if (t !== type) { bad(`${p}: is ${t}, expected ${type}${type === "float" ? " (write it with an f, like 1.0f)" : ""}`); continue; }
    if (type === "enum") {
      const en = db.getEnumName(p), ord = db.getInt(p);
      if (en !== enumName) bad(`${p}: is a ${en}, expected a ${enumName}`);
      else if (!Object.values(ENUMS[enumName]).includes(ord)) bad(`${p}: ordinal ${ord} is not one of ${enumName}'s values the C code knows`);
    }
  }
}

// ---- load, check, collect ------------------------------------------------------------------------------------
const baked = [];
const dbs = {};
for (const f of FILES) {
  const full = path.join(root, "assets/config", f.file);
  const bytes = fs.readFileSync(full);
  let db;
  try { db = glue.MdixDatabase.loadStr(bytes.toString("utf8")); }
  catch (e) { bad(`${f.file}: ${String(e).replace(/^Error: /, "")}`); continue; }
  dbs[f.array] = db;
  const entries = [];
  if (f.shape === "array") walk(db, f.array, (p, t, v) => entries.push({ p, t, v }));
  else walkObject(db, f.array, (p, t, v) => entries.push({ p, t, v }));
  entries.sort((a, b) => (a.p < b.p ? -1 : a.p > b.p ? 1 : 0));   // same order as C's strcmp for plain ASCII
  baked.push({ ...f, hash: fnv1a(bytes), entries });
}

if (dbs.projectiles) {
  const n = dbs.projectiles.getArrayLength("projectiles");
  const ids = new Set();
  for (let i = 0; i < n; ++i) {
    checkFields(dbs.projectiles, `projectiles[${i}]`, PROJECTILE_FIELDS);
    const id = dbs.projectiles.exists(`projectiles[${i}].id`) ? dbs.projectiles.getString(`projectiles[${i}].id`) : `#${i}`;
    if (ids.has(id)) bad(`projectiles[${i}]: the id "${id}" is used twice`);
    ids.add(id);
    const g = (k) => (dbs.projectiles.exists(`projectiles[${i}].${k}`) ? dbs.projectiles.getFloat(`projectiles[${i}].${k}`) : NaN);
    if (!(g("speed_min") > 0 && g("speed_max") >= g("speed_min"))) bad(`projectiles[${i}] (${id}): speed_min must be above 0 and no more than speed_max`);
    if (!(g("lifetime") > 0 && g("max_range") > 0)) bad(`projectiles[${i}] (${id}): lifetime and max_range must be above 0`);
    if (!(g("falloff_end") >= g("falloff_start") && g("falloff_min") >= 0 && g("falloff_min") <= 1)) bad(`projectiles[${i}] (${id}): falloff needs start <= end and min between 0 and 1`);
  }
  if (n > 16) bad("projectiles: more than 16 configs (MAX_PROJECTILE_CONFIGS)");
  if (dbs.weapons) {
    const w = dbs.weapons.getArrayLength("weapons");
    if (w !== WEAPON_IDS.length) bad(`weapons: ${w} rows, the game has ${WEAPON_IDS.length} weapons (${WEAPON_IDS.join(", ")})`);
    for (let i = 0; i < w; ++i) {
      const p = `weapons[${i}]`;
      checkFields(dbs.weapons, p, WEAPON_COMMON);
      if (!dbs.weapons.exists(`${p}.kind`)) continue;
      const kind = dbs.weapons.getInt(`${p}.kind`);
      const id = dbs.weapons.getString(`${p}.id`);
      if (id !== WEAPON_IDS[i]) bad(`${p}: the id is "${id}" but row ${i} must be "${WEAPON_IDS[i]}" (the order of WeaponId)`);
      if (kind === ENUMS.Kind.GUN) {
        checkFields(dbs.weapons, p, WEAPON_GUN);
        const pr = dbs.weapons.exists(`${p}.projectile`) ? dbs.weapons.getString(`${p}.projectile`) : "";
        if (!ids.has(pr)) bad(`${p} (${id}): the projectile "${pr}" is not in projectiles.mdix`);
        const pellets = dbs.weapons.exists(`${p}.pellets`) ? dbs.weapons.getInt(`${p}.pellets`) : 0;
        if (pellets < 1 || pellets > 32) bad(`${p} (${id}): pellets must be 1 to 32`);
        if (dbs.weapons.exists(`${p}.ammo`) && dbs.weapons.getInt(`${p}.ammo`) === 0) bad(`${p} (${id}): a gun needs an ammo type`);
      } else {
        checkFields(dbs.weapons, p, WEAPON_MELEE);
      }
    }
  }
}

if (dbs.flashlight) {
  const d = dbs.flashlight;
  checkFields(d, "flashlight", FLASHLIGHT_FIELDS);
  const g = (k) => (d.exists(`flashlight.${k}`) ? d.getFloat(`flashlight.${k}`) : NaN);
  if (!(g("capacity_secs") > 0)) bad("flashlight.capacity_secs: must be above 0");
  if (!(g("start_secs") >= 0 && g("start_secs") <= g("capacity_secs"))) bad("flashlight.start_secs: must be between 0 and capacity_secs");
  if (!(g("battery_secs") > 0 && g("battery_secs") <= g("capacity_secs"))) bad("flashlight.battery_secs: must be above 0 and no more than capacity_secs");
  if (!(g("low_fraction") >= 0 && g("low_fraction") <= 1)) bad("flashlight.low_fraction: must be between 0 and 1");
  if (!(g("beam_inner_deg") > 0 && g("beam_outer_deg") > g("beam_inner_deg") && g("beam_outer_deg") <= 80)) bad("flashlight: beam_inner_deg must be above 0 and below beam_outer_deg, which is at most 80");
  if (!(g("range") > 0 && g("range") <= 100)) bad("flashlight.range: must be above 0 and at most 100");
  if (!(g("strength") >= 0 && g("strength") <= 4)) bad("flashlight.strength: must be between 0 and 4");
  if (!(g("lag") > 0)) bad("flashlight.lag: must be above 0");
}

if (dbs.stair_clock) {
  // The ranges are checked by the C loader (src/floor/floor_config.c), which tests/floor_run_headless.c runs on this very table.
  checkFields(dbs.stair_clock, "stair_clock", FLOOR_FIELDS);
}

if (dbs.swarm) {
  // The ranges are checked by the C loader (src/swarm/swarm_config.c), which tests/swarm_headless.c runs on this very table.
  checkFields(dbs.swarm, "swarm", SWARM_FIELDS);
}

if (dbs.characters) {
  // The fields and their types here; the lengths, the characters allowed and the unique ids are checked by the C loader
  // (src/characters/characters.c), which tests/characters_headless.c runs on this very table.
  const n = dbs.characters.getArrayLength("characters");
  if (n < 1 || n > 8) bad(`characters: ${n} entries, it needs 1 to 8 (CHARACTER_MAX)`);
  for (let i = 0; i < n; ++i) checkFields(dbs.characters, `characters[${i}]`, CHARACTER_FIELDS);
}

if (dbs.sounds) {
  // The row layout (a sound row, then its layer rows) and the order of the ids; the ranges of every value are checked by the C loader
  // (src/audio/sound_bank.c), which tests/audio_headless.c runs on this very table.
  const d = dbs.sounds;
  const n = d.getArrayLength("sounds");
  let cur = -1, layers = 0;
  const close = () => { if (cur >= 0 && (layers < 1 || layers > 4)) bad(`sounds: "${SOUND_IDS[cur]}" has ${layers} layers, it needs 1 to 4`); };
  for (let i = 0; i < n; ++i) {
    const p = `sounds[${i}]`;
    if (!d.exists(`${p}.kind`)) { bad(`${p}: missing kind`); continue; }
    const kind = d.getInt(`${p}.kind`);
    if (kind === 0) {
      close(); ++cur; layers = 0;
      const id = d.exists(`${p}.id`) ? d.getString(`${p}.id`) : "";
      if (cur >= SOUND_IDS.length) { bad(`${p}: more sounds than the game has ids for (${SOUND_IDS.length})`); break; }
      if (id !== SOUND_IDS[cur]) bad(`${p}: the id is "${id}" but sound ${cur} must be "${SOUND_IDS[cur]}" (the order of SoundId)`);
    } else if (kind === 1) {
      if (cur < 0) bad(`${p}: a layer before any sound`); else ++layers;
    } else bad(`${p}: kind must be 0 (a sound) or 1 (a layer)`);
  }
  close();
  if (cur + 1 !== SOUND_IDS.length) bad(`sounds: ${cur + 1} sounds, the game has ${SOUND_IDS.length} (${SOUND_IDS.join(", ")})`);
}

if (errors.length) {
  say(`FAILED: ${errors.length} problem${errors.length === 1 ? "" : "s"} in assets/config:`);
  for (const e of errors) say("  " + e);
  process.exit(1);
}

// ---- write -----------------------------------------------------------------------------------------------------
const esc = (s) => s.replace(/\\/g, "\\\\").replace(/"/g, '\\"');
let c = `// GENERATED by tools/mdix_bake.mjs from assets/config/*.mdix. Do not edit: change the .mdix and run
//   node tools/mdix_bake.mjs
// It holds every value of those files, already evaluated, so the game has them before (and without) the DixScript wasm.
// See docs/config.md.
#include "mdix_doc.h"

`;
for (const b of baked) {
  c += `// ${b.file}   FNV-1a 0x${b.hash.toString(16).padStart(8, "0")}\nstatic const MdixBakedEntry k_${b.array}[] = {\n`;
  for (const e of b.entries) c += `    { "${esc(e.p)}", '${e.t}', "${esc(e.v)}" },\n`;
  c += `};\nconst MdixBaked ${b.symbol} = { "${b.file}", 0x${b.hash.toString(16).padStart(8, "0")}u, k_${b.array}, (int)(sizeof k_${b.array} / sizeof k_${b.array}[0]) };\n\n`;
}
c = c.replace(/\n+$/, "\n");

if (checkOnly) {
  const now = fs.existsSync(OUT) ? fs.readFileSync(OUT, "utf8") : "";
  if (now !== c) { say("STALE: src/config/config_baked.c does not match assets/config/*.mdix. Run: node tools/mdix_bake.mjs"); process.exit(2); }
  say(`ok: the .mdix files are valid and config_baked.c is up to date (${baked.map((b) => `${b.file} ${b.entries.length} values`).join(", ")})`);
} else {
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, c);
  say(`wrote src/config/config_baked.c (${baked.map((b) => `${b.file}: ${b.entries.length} values`).join(", ")})`);
}
