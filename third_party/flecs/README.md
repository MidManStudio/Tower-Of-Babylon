# Flecs (vendored)

The Flecs entity component system, <https://github.com/SanderMertens/flecs>, MIT licence (`LICENSE`).

- **Version:** 4.2.0, the amalgamated distribution (`distr/flecs.c` and `distr/flecs.h`) of the commit `7d1bc7d` (2026-11-24). The two files are unchanged.
- **Use it through `tob_flecs.h`**, never `flecs.h`: the game builds Flecs as a custom build (`FLECS_CUSTOM_BUILD` plus only the addons it uses) and the same macros must be defined where `flecs.c` is compiled and where `flecs.h` is included. `tob_flecs.c` is the one translation unit that compiles `flecs.c`.
- **Compile:** `cc -std=gnu99 -O2 -c third_party/flecs/tob_flecs.c` (about 10 seconds on one core, a 1.4 MB object). Link with `-lm -lpthread`.
- **Refresh:** copy the two files from a newer `distr/`, check that the addons listed in `tob_flecs.h` still exist, run `tests/gvex_check.sh`. Record the new version and commit here.
- **Why it is in the repository:** `docs/gvex_session.md`, "Flecs in the repository".
