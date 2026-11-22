// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "floorgen_plain_headless.c"
// ============================================================================
// The same golden floors as floorgen_hash_headless.c, with the generator built with plain scans (-DFG_PLAIN_SCANS: every free run,
// 3 by 3 fit and side band is looked up one tile at a time instead of with bit scans). The goldens were taken that way, so this
// is the oracle: the fast build has to give the same hashes. This file only turns the switch on and includes the other one.
// Inside a macro so mray's build, which compiles every .c file, does not pick up a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DFLOORGEN_PLAIN_HEADLESS_TEST -DFG_PLAIN_SCANS -Isrc tests/floorgen_plain_headless.c src/dungeon/dungeon_generator.c -lm -o floorgen_plain_headless
// Run:
//   ./floorgen_plain_headless
#ifdef FLOORGEN_PLAIN_HEADLESS_TEST
#define FLOORGEN_HASH_HEADLESS_TEST 1
#include "floorgen_hash_headless.c"
#endif
