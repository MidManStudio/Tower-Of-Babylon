# repo

## Overview

How the GitHub repository is set up and how changes get into it from a phone. The repository uses the three kinds of `.mdix` template from `Mid-D-Man/mdix-scaffold` (the reusable workflow `generate-structure.yml@main` does the work); this project uses two of them:

- **structure** (`.mdix/project_structure/project_structure.mdix`): the folders and a README. Existing files are never touched.
- **replacements** (`.mdix/replacements/replacements.mdix`): a tarball or zip dropped next to the manifest is unpacked and its files are written to their places in the repo. This is the phone-friendly route: commit one file through the GitHub web page, run a workflow, done.

There is no `patch.mdix`; a patch is a one-off, so it is written when one is needed (it is always `.mdix/patches/patch.mdix` with its own `run-patch.yml`).

## First setup

**From Termux** (git is already there): apply the update tarball with mray option 8, then in the project folder `git init`, `git add .`, `git commit`, add the new repository as `origin` and push. `.gitignore` keeps the web build (`index.*`) and `build/` out.

**From the browser only:** create the repository on GitHub, then commit these files with "Add file, Upload files":
1. `.github/workflows/run-replacements.yml` and `.github/workflows/run-structure.yml` (the browser upload keeps a folder name you type into the path box),
2. `.mdix/replacements/replacements.mdix` and the full project tarball `tower_full_project.tar.gz`, both into `.mdix/replacements/`,
3. `.mdix/project_structure/project_structure.mdix` and `.mdix/env/mappings.mdix`.

Then Actions, **Generate project structure**, dry run `false`, to make the folders and README; then **Apply replacements**, dry run `false`, to unpack the tarball into the repo. Do each once with dry run `true` first: it prints every file and where it would go. After a clean apply, run **Apply replacements** again with `delete_processed_archives` set to `true`, or delete the tarball by hand, so a later run does not apply it again over newer work. Finally add `.github/workflows/android.yml`, `.github/workflows/tests.yml`, `.gitignore` and `LICENSE`: these are dot-paths or workflow files and are **not** applied from a tarball (see below), so they are committed by hand.

If an apply fails when it tries to commit, open the repository's Settings, Actions, General, and set Workflow permissions to Read and write.

## Updating

Make the update tarball the way mray option 8 expects it (paths relative to the project root, no top folder) and put it in `.mdix/replacements/`, next to `replacements.mdix`. Run **Apply replacements** (dry run first). Every file inside the tarball that sits in a folder goes to that same path under the repo root; the manifest names the one file at the top of the tarball, `main.c`.

## Publishing from Termux (`tools/mray.py`)

A copy of the mray script is kept in `tools/mray.py`; it lives at `~/mray.py` on the phone. Its menu has two entries beyond the build tools (and Exit is last, 9): **8) Apply Update Archive** lists the `.tar.gz`, `.zip` and `.tar` files in Downloads (newest first), shows how many files are new, changed and identical, skips anything with a path that leaves the project and anything inside `.git`, and writes the rest after you confirm; **7) GitHub** shows status, connects a project folder to a repository (fetches and lines the folder up with what is already on GitHub without touching your files), commits and pushes (it shows the changed files first, refuses a file over 50 MB, writes a `.gitignore` if there is none), pulls fast-forward only, and lists recent commits. Login is the one done once with `gh auth login --scopes workflow` and `gh auth setup-git`.

## Modules

### `.mdix/project_structure/project_structure.mdix`
**What it does:** declares the folder skeleton (`src/<module>/`, `assets/`, `docs/design/`, `tests/`, `tools/`, `scripts/`, `android/`) with a `.gitkeep` in each, and a root `README.md` filled from the mappings.

**Decisions:** only what a fresh repository needs. The 130-odd real files are not declared one by one; they arrive through replacements, so a source file is never described in two places. A new module folder under `src/` is one more `src.<name>::` group.

### `.mdix/replacements/replacements.mdix`
**What it does:** `target_root = "."` and one override, `main.c`.

**Decisions and traps (all from the scaffold skill and its resolver, `lib_replacements.py`):**
- A file inside a folder of the tarball is **nested**: its path is its destination, no search, no ambiguity.
- A file at the very top of the tarball is **flat**: the resolver searches the whole repo for a file of that name and fails when there is more than one. `main.c` is the only top-level file in the update tarballs, and it is unique, but an override pins it anyway. `README.md` is deliberately not overridden: `docs/design/README.md` has the same name, and an override on a bare name also catches nested files of that name.
- **Dot-paths are never applied.** The resolver skips any file or folder starting with a dot, so `.github/`, `.gitignore` and `.mdix/` inside a tarball are ignored (mray option 8 does unpack them, which is why they are in the update tarball too). Workflow files, `.gitignore` and the templates themselves are committed directly.
- Binary files (the `.wasm`, `libraylib.a`) are written as bytes.
- Archives are extracted in a throwaway copy; the tarball in `.mdix/replacements/` stays unless `delete_processed_archives` is `true`.

### `.mdix/env/mappings.mdix`
**What it does:** the `[[title]]`, `[[description]]` and so on that the structure template's README uses. `run-structure.yml` passes it as `mappings_file`.

### `.github/workflows/run-structure.yml` and `run-replacements.yml`
**What they do:** call the reusable workflow in `Mid-D-Man/mdix-scaffold`. Dropdowns, not free text, for every true/false and choice input. An automatic run (a push that touches the template or the replacements folder) is always a dry run: `dry_run` is forced to `true` unless the run was started by hand with the dropdown set to `false`. The default file strategy is `skip` for structure and `overwrite` for replacements (an update exists to overwrite). Nothing here ever defaults to deleting or overwriting.

**Trap:** the commit the apply makes ends in `[skip ci]`, so GitHub starts no push-triggered workflow for it, `tests.yml` included. Run **Tests** by hand after an apply.

### `.github/workflows/tests.yml`
**What it does:** `bash tools/run_tests.sh` on a push that touches `main.c`, `src/`, `tests/`, `tools/` or `assets/config/`, and on demand. It installs gcc, numpy and Mesa's software OpenGL ES for the shader test. A clean run prints 30 `PASS` lines (see `docs/testing.md`).

### `.gitignore`, `LICENSE`
`.gitignore` keeps out the web build mray writes into the project folder (`index.html`, `index.js`, `index.wasm`, `index.data`), `build/` (the Android build's raylib checkout, objects and keystore), compiled test programs and editor files. `LICENSE` is the same MidManStudio notice the Terminal Viscosity repository carries.

## Not verified

- The workflows have never run on GitHub (the sandbox cannot). The scaffold steps were run in the sandbox with the repository's own Python scripts (`generate_structure.py` and its libraries, the published `midmanstudio-mdix` package): both templates validate, the structure template creates the folders and README, and an apply of the full tarball into the generated skeleton reproduces the project tree file for file. What differs on GitHub is the runner, the permissions and the reusable-workflow call.
- `tests.yml` installs the Mesa packages by their Ubuntu 24.04 names (checked against the package index); whether the GLES shader test runs on the hosted runner, or reports `SKIP`, is untried.

## Fixes and Problems

### `tools/mray.py`
- The first version of the update was written from an older copy of the script than the one in use (the paste had no option 8), and so replaced a working build step with a broken one: the `emcc` flag `--shell-file` was added even when the project has no `shell.html`, with nothing after it, so it took the next flag as its file (`emcc: error: '--shell-file': file not found: '--preload-file'`). Now the flag is added only when the file exists. The script that was replaced is kept by the installer as `~/mray.py.bak`.
- The archive list showed every file three times: Termux shows the phone's Download folder under three paths, and they were compared as text. Now one path per real folder (`os.path.realpath`), and one entry per real file.
- Exit was option 7 with the new entries after it; it is now last (9).
