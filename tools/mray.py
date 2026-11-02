#!/usr/bin/env python3
import os
import re
import sys
import stat
import tarfile
import time
import zipfile
import subprocess
import shutil

RAYROOT = os.path.expanduser("~/m_raylib")
CENTRAL_LIB = os.path.expanduser("~/raylib-test/raylib")

TEMPLATE_CODE = """#include "raylib.h"
int main(void) {
    InitWindow(800, 450, "Raylib WASM App");
    SetTargetFPS(60);
    while (!WindowShouldClose())
    {
        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawText("Raylib subdirs & menu update working!", 80, 200, 20, DARKGRAY);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
"""

INVISIBLE = dict.fromkeys(map(ord, "\u200b\u200c\u200d\u2060\ufeff\u00ad"), None)
SAFE_NAME = re.compile(r"^[A-Za-z0-9._-]+$")

# What a fresh project's .gitignore keeps out of GitHub (the build mray makes, the Android build, compiled test programs).
DEFAULT_GITIGNORE = """# Written by mray.py
/index.html
/index.js
/index.wasm
/index.data
/build/
/*_headless
*.o
.DS_Store
*.swp
"""

def clean_text(text):
    return text.translate(INVISIBLE).strip()

def valid_name(name):
    return bool(SAFE_NAME.match(name)) and name not in (".", "..")

def list_projects():
    os.makedirs(RAYROOT, exist_ok=True)
    return sorted([d.strip() for d in os.listdir(RAYROOT) if os.path.isdir(os.path.join(RAYROOT, d))])

def select_project():
    projects = list_projects()
    if not projects:
        print("\nNo projects found yet.")
        return None
    print("\n--- Select a Project ---")
    for idx, p in enumerate(projects, 1):
        print(f" {idx}) {p}")
    print(" 0) Cancel")
    choice = input("Enter project number: ").strip()
    try:
        c_idx = int(choice)
        if c_idx == 0:
            return None
        if 1 <= c_idx <= len(projects):
            return projects[c_idx - 1]
    except ValueError:
        pass
    print("Invalid selection.")
    return None

def get_project_files(pdir):
    files_list = []
    for root, dirs, files in os.walk(pdir):
        if "raylib" in root.split(os.sep) or ".git" in root.split(os.sep):
            continue
        for file in files:
            rel_path = os.path.relpath(os.path.join(root, file), pdir).strip()
            if not rel_path.startswith("index."):
                files_list.append(rel_path)
    return sorted(files_list)

def provision_raylib(pdir):
    target_dir = os.path.join(pdir, "raylib", "src")
    target_lib = os.path.join(target_dir, "libraylib.a")
    if os.path.exists(target_lib):
        print("raylib already present in the project.")
        return
    src_lib_dir = os.path.join(CENTRAL_LIB, "src")
    if not os.path.exists(src_lib_dir):
        print("Warning: Base raylib folder not found at ~/raylib-test/raylib.")
        return
    os.makedirs(target_dir, exist_ok=True)
    for f in os.listdir(src_lib_dir):
        if f.endswith(".h"):
            shutil.copy(os.path.join(src_lib_dir, f), os.path.join(target_dir, f))
    web_lib = os.path.join(src_lib_dir, "libraylib.web.a")
    reg_lib = os.path.join(src_lib_dir, "libraylib.a")
    if os.path.exists(web_lib):
        shutil.copy(web_lib, target_lib)
    elif os.path.exists(reg_lib):
        shutil.copy(reg_lib, target_lib)
    print("raylib headers and library copied into the project.")

def create_project():
    pname = clean_text(input("Enter new project name: "))
    if not valid_name(pname):
        print("Invalid project name (letters, digits, dot, underscore, dash only).")
        return
    pdir = os.path.join(RAYROOT, pname)
    if os.path.exists(pdir):
        print("Project already exists.")
        return
    os.makedirs(pdir, exist_ok=True)
    os.makedirs(os.path.join(pdir, "src"), exist_ok=True)
    with open(os.path.join(pdir, "main.c"), "w") as f:
        f.write(TEMPLATE_CODE)
    provision_raylib(pdir)
    print(f"Project '{pname}' created successfully!")

def build_project():
    pname = select_project()
    if not pname:
        return
    pdir = os.path.join(RAYROOT, pname)
    provision_raylib(pdir)
    
    # Gather all .c files recursively, excluding raylib/ and .git/
    c_files = []
    for root, dirs, files in os.walk(pdir):
        if "raylib" in root.split(os.sep) or ".git" in root.split(os.sep):
            continue
        for file in files:
            if file.endswith(".c"):
                c_files.append(os.path.relpath(os.path.join(root, file), pdir))
    
    if not c_files:
        print("No .c files found in project.")
        return

    print(f"Compiling {pname} with sources: {c_files} ...")
    
    out_html = os.path.join(pdir, "index.html")
    raylib_src = os.path.join(pdir, "raylib", "src")
    libraylib = os.path.join(raylib_src, "libraylib.a")
    
    cmd = [
        "emcc"
    ] + c_files + [
        libraylib,
        "-o", out_html,
        "-Wall",
        "-Wno-missing-braces",
        "-O2",
        "-std=c99",
        f"-I{raylib_src}",
        "-I./src",
        "-I.",
        "-s", "USE_GLFW=3",
        "-s", "ASYNCIFY=1",
        "-s", "ALLOW_MEMORY_GROWTH=1",
        "-s", "WASM=1",
    ]
    
    shell_file = os.path.join(raylib_src, "shell.html")
    if os.path.exists(shell_file):
        cmd.extend(["--shell-file", shell_file])
    
    # Check if assets folder exists and preload it
    assets_dir = os.path.join(pdir, "assets")
    if os.path.isdir(assets_dir):
        cmd.extend(["--preload-file", "assets"])
        print("Detected 'assets/' folder -> bundling into WASM virtual filesystem.")

    res = subprocess.run(cmd, cwd=pdir)
    if res.returncode == 0:
        print("Build successful! Generated index.html, index.js, index.wasm")
    else:
        print("Build failed.")

def serve_project():
    pname = select_project()
    if not pname:
        return
    pdir = os.path.join(RAYROOT, pname)
    if not os.path.exists(os.path.join(pdir, "index.html")):
        print("Build output (index.html) not found. Please build the project first.")
        return
    print(f"Serving {pname} at http://localhost:8080 ... Press Ctrl+C to stop.")
    os.chdir(pdir)
    subprocess.run([sys.executable, "-m", "http.server", "8080"])

def edit_project():
    pname = select_project()
    if not pname:
        return
    pdir = os.path.join(RAYROOT, pname)
    
    while True:
        print(f"\n--- Editing Project: {pname} ---")
        print("1) Edit main.c in Nano")
        print("2) Paste new code to create/replace a file directly")
        print("3) List all project files and relative paths")
        print("4) View file contents in terminal")
        print("5) Delete a file")
        print("0) Back to main menu")
        opt = input("Choose option (0-5): ").strip()
        
        if opt == "1":
            main_path = os.path.join(pdir, "main.c")
            subprocess.run(["nano", main_path])
        elif opt == "2":
            rel_path = input("Enter relative path for file (e.g., src/world.c or main.c): ").strip()
            if rel_path:
                full_path = os.path.join(pdir, rel_path)
                os.makedirs(os.path.dirname(full_path), exist_ok=True)
                print(f"Paste your code for {rel_path}. Press Enter then Ctrl+D when finished:")
                content = sys.stdin.read()
                with open(full_path, "w") as f:
                    f.write(content)
                print(f"{rel_path} saved successfully!")
        elif opt == "3":
            files = get_project_files(pdir)
            print(f"\n--- Files in Project ({pname}) ---")
            for f in files:
                print(f" -> {f}")
            print(f"Total files found: {len(files)}")
        elif opt == "4":
            rel_path = input("Enter relative path to view (e.g., main.c or src/world.h): ").strip()
            if rel_path:
                full_path = os.path.join(pdir, rel_path)
                if os.path.exists(full_path) and os.path.isfile(full_path):
                    print(f"\n--- Contents of {rel_path} ---")
                    with open(full_path, "r") as f:
                        print(f.read())
                    print("--------------------------------")
                else:
                    print("File not found.")
        elif opt == "5":
            rel_path = input("Enter relative path of file to delete: ").strip()
            if rel_path:
                full_path = os.path.join(pdir, rel_path)
                if os.path.exists(full_path) and os.path.isfile(full_path):
                    if input(f"Are you sure you want to delete {rel_path}? (y/n): ").strip().lower() == "y":
                        os.remove(full_path)
                        print("File deleted.")
                else:
                    print("File not found or invalid path.")
        elif opt == "0":
            break
        else:
            print("Invalid option.")

def export_project_zip():
    pname = select_project()
    if not pname:
        return
    pdir = os.path.join(RAYROOT, pname)
    downloads_dir = "/storage/emulated/0/Download"
    if not os.path.exists(downloads_dir):
        downloads_dir = os.path.expanduser("~/storage/downloads")
    os.makedirs(downloads_dir, exist_ok=True)
    zip_path = os.path.join(downloads_dir, f"{pname}_export.zip")
    
    print(f"Exporting {pname} to {zip_path} ...")
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for root, dirs, files in os.walk(pdir):
            if "raylib" in root.split(os.sep) or ".git" in root.split(os.sep):
                continue
            for file in files:
                fullpath = os.path.join(root, file)
                relpath = os.path.relpath(fullpath, pdir)
                zipf.write(fullpath, relpath)
    print("Export complete! Saved to Android Downloads folder.")

def show_projects_list():
    projects = list_projects()
    print("\n--- Existing Projects ---")
    if projects:
        for idx, p in enumerate(projects, 1):
            print(f" {idx}) {p}")
    else:
        print("No projects found yet.")
    print("-------------------------")

# ---------------------------------------------------------------------------------------------------------------------
# 8) Apply an update archive (the .tar.gz / .zip a chat hands you) to a project
# ---------------------------------------------------------------------------------------------------------------------
ARCHIVE_EXTS = (".tar.gz", ".tgz", ".tar", ".zip")

def download_dirs():
    # Termux shows the phone's Download folder under several paths (~/storage/downloads and ~/storage/shared/Download are
    # links to /storage/emulated/0/Download). Keep one path per real folder, or every file is listed once per path.
    found, real = [], set()
    for d in ("/storage/emulated/0/Download", os.path.expanduser("~/storage/downloads"), os.path.expanduser("~/storage/shared/Download")):
        if os.path.isdir(d) and os.path.realpath(d) not in real:
            real.add(os.path.realpath(d))
            found.append(d)
    return found

def find_archives(limit=12):
    found, seen = [], set()
    for d in download_dirs():
        try:
            names = os.listdir(d)
        except OSError:
            continue
        for n in names:
            if n.lower().endswith(ARCHIVE_EXTS):
                p = os.path.join(d, n)
                if os.path.isfile(p) and os.path.realpath(p) not in seen:
                    seen.add(os.path.realpath(p))
                    found.append((os.path.getmtime(p), p))
    found.sort(reverse=True)
    return [p for _, p in found[:limit]]

def safe_member_path(name):
    # A path inside the project, or None when it is absolute or climbs out of it.
    name = name.replace("\\", "/")
    while name.startswith("./"):
        name = name[2:]
    if not name or name.startswith("/") or re.match(r"^[A-Za-z]:", name):
        return None
    parts = [x for x in name.split("/") if x not in ("", ".")]
    if not parts or ".." in parts:
        return None
    return "/".join(parts)

def read_archive_entries(path):
    # Returns (entries, skipped). entries: list of (relative path, mode, bytes). Only plain files are taken.
    entries, skipped = [], []
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as zf:
            for info in zf.infolist():
                if info.filename.endswith("/"):
                    continue
                rel = safe_member_path(info.filename)
                mode = (info.external_attr >> 16) & 0o7777
                if rel is None:
                    skipped.append((info.filename, "path leaves the project"))
                elif stat.S_ISLNK(info.external_attr >> 16):
                    skipped.append((info.filename, "symbolic link"))
                else:
                    entries.append((rel, mode or 0o644, zf.read(info)))
    else:
        with tarfile.open(path) as tf:
            for m in tf.getmembers():
                if m.isdir():
                    continue
                rel = safe_member_path(m.name)
                if rel is None:
                    skipped.append((m.name, "path leaves the project"))
                elif not m.isreg():
                    skipped.append((m.name, "not a plain file"))
                else:
                    entries.append((rel, m.mode & 0o7777, tf.extractfile(m).read()))
    return entries, skipped

def apply_update():
    pname = select_project()
    if not pname:
        return
    pdir = os.path.join(RAYROOT, pname)
    archives = find_archives()
    print("\n--- Update archives in Downloads (newest first) ---")
    for idx, a in enumerate(archives, 1):
        age = int((time.time() - os.path.getmtime(a)) / 60)
        print(f" {idx}) {os.path.basename(a)}   ({age} min old)")
    if not archives:
        print(" (none found; type a full path below)")
    print(" 0) Cancel")
    choice = input("Archive number, or a full path [1]: ").strip().strip("'\"")
    if choice == "0":
        return
    if choice == "":
        choice = "1"
    if choice.isdigit():
        i = int(choice)
        if not (1 <= i <= len(archives)):
            print("Invalid selection.")
            return
        archive = archives[i - 1]
    else:
        archive = os.path.expanduser(clean_text(choice))
    if not os.path.isfile(archive) or not archive.lower().endswith(ARCHIVE_EXTS):
        print("Not a .tar.gz, .tgz, .tar or .zip file.")
        return
    try:
        entries, skipped = read_archive_entries(archive)
    except (tarfile.TarError, zipfile.BadZipFile, OSError, EOFError) as e:
        print(f"Could not read the archive: {e}")
        return

    new, changed, same, writes = [], [], 0, []
    for rel, mode, data in entries:
        if rel == ".git" or rel.startswith(".git/"):
            skipped.append((rel, "inside .git"))
            continue
        dest = os.path.join(pdir, rel)
        if os.path.isfile(dest):
            with open(dest, "rb") as f:
                if f.read() == data:
                    same += 1
                    continue
            changed.append(rel)
        else:
            new.append(rel)
        writes.append((rel, dest, mode, data))
    print(f"\n{os.path.basename(archive)}: {len(entries)} files -> {len(new)} new, {len(changed)} changed, {same} already identical")
    for label, names in (("new", new), ("changed", changed)):
        for n in names[:25]:
            print(f"   {label:8s} {n}")
        if len(names) > 25:
            print(f"   ... and {len(names) - 25} more {label}")
    for n, why in skipped[:10]:
        print(f"   SKIPPED  {n} ({why})")
    if not writes:
        print("Nothing to write.")
        return
    if input(f"Write {len(writes)} files into {pname}? (y/n): ").strip().lower() != "y":
        print("Cancelled.")
        return
    root_real = os.path.realpath(pdir)
    done = 0
    for rel, dest, mode, data in writes:
        if not os.path.realpath(dest).startswith(root_real + os.sep):
            print(f"   SKIPPED  {rel} (resolves outside the project)")
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)
        os.chmod(dest, (mode | 0o600) & 0o777)
        done += 1
    print(f"Done: {done} files written. Build it (2), or publish it with the GitHub menu (7).")

# ---------------------------------------------------------------------------------------------------------------------
# 9) GitHub: status, connect, commit + push, pull. Uses the git (and gh login) already in Termux.
# ---------------------------------------------------------------------------------------------------------------------
def git_available():
    if shutil.which("git"):
        return True
    print("git is not installed. Run:  pkg install git")
    return False

def run_git(pdir, *args, quiet=False):
    # Returns (exit code, stdout, stderr). quiet=True captures instead of printing.
    try:
        if quiet:
            r = subprocess.run(["git"] + list(args), cwd=pdir, capture_output=True, text=True)
            return r.returncode, (r.stdout or "").strip(), (r.stderr or "").strip()
        r = subprocess.run(["git"] + list(args), cwd=pdir)
        return r.returncode, "", ""
    except OSError as e:
        return 127, "", str(e)

def is_repo(pdir):
    return os.path.isdir(os.path.join(pdir, ".git"))

def has_commits(pdir):
    return run_git(pdir, "rev-parse", "--verify", "HEAD", quiet=True)[0] == 0

def origin_url(pdir):
    code, out, _ = run_git(pdir, "remote", "get-url", "origin", quiet=True)
    return out if code == 0 else ""

def ensure_identity(pdir):
    if run_git(pdir, "config", "user.name", quiet=True)[1] and run_git(pdir, "config", "user.email", quiet=True)[1]:
        return True
    print("git does not know who you are yet (needed once, for the commit author).")
    name = clean_text(input("Your name: "))
    email = clean_text(input("Your email (the one on GitHub, or the noreply one): "))
    if not name or not email:
        print("Cancelled.")
        return False
    run_git(pdir, "config", "--global", "user.name", name, quiet=True)
    run_git(pdir, "config", "--global", "user.email", email, quiet=True)
    return True

def git_status_summary(pdir):
    if not is_repo(pdir):
        print("Not a git repository yet. Use option 2 to connect it to GitHub.")
        return
    run_git(pdir, "status", "-sb")
    url = origin_url(pdir)
    print(f"origin: {url if url else '(not set, use option 2)'}")

def git_connect(pdir):
    if not is_repo(pdir):
        code, _, _ = run_git(pdir, "init", "-b", "main", quiet=True)
        if code != 0:
            run_git(pdir, "init", quiet=True)
            run_git(pdir, "checkout", "-b", "main", quiet=True)
        print("Created a git repository here.")
    current = origin_url(pdir)
    if current:
        print(f"origin is already {current}")
    url = clean_text(input("GitHub repo URL (https://github.com/<owner>/<repo>.git), blank to keep: "))
    if url:
        if " " in url or not (url.startswith("https://") or url.startswith("git@") or url.startswith("/") or url.startswith("file://")):
            print("That does not look like a repository URL.")
            return
        run_git(pdir, "remote", "set-url" if current else "add", "origin", url, quiet=True)
        current = url
    if not current:
        return
    if has_commits(pdir):
        print("This project already has commits here; the remote is set. Use 3 to push, 4 to pull.")
        return
    if input("Fetch what is on GitHub and line this folder up with it (keeps your files)? (y/n): ").strip().lower() != "y":
        return
    code, _, err = run_git(pdir, "fetch", "origin", quiet=True)
    if code != 0:
        print(f"Fetch failed: {err}")
        print("If it asks for a login: pkg install gh, then  gh auth login --scopes workflow  and  gh auth setup-git")
        return
    for branch in ("main", "master"):
        if run_git(pdir, "rev-parse", "--verify", f"origin/{branch}", quiet=True)[0] == 0:
            run_git(pdir, "reset", "-q", f"origin/{branch}", quiet=True)
            print(f"Lined up with origin/{branch}. Your files are untouched; option 1 shows how they differ.")
            return
    print("The GitHub repository is empty, so there is nothing to line up with. Use 3 to push.")

def big_files(pdir, limit_mb=50):
    code, out, _ = run_git(pdir, "diff", "--cached", "--name-only", "-z", quiet=True)
    big = []
    for n in out.split("\0"):
        if n:
            p = os.path.join(pdir, n)
            if os.path.isfile(p) and os.path.getsize(p) > limit_mb * 1024 * 1024:
                big.append((n, os.path.getsize(p) // (1024 * 1024)))
    return big

def git_commit_push(pdir):
    if not is_repo(pdir) or not origin_url(pdir):
        print("Connect the project first (option 2).")
        return
    if not ensure_identity(pdir):
        return
    if not os.path.exists(os.path.join(pdir, ".gitignore")):
        if input("There is no .gitignore. Write one that keeps the web build and build/ out? (y/n): ").strip().lower() == "y":
            with open(os.path.join(pdir, ".gitignore"), "w") as f:
                f.write(DEFAULT_GITIGNORE)
    run_git(pdir, "add", "-A", quiet=True)
    code, out, _ = run_git(pdir, "status", "--short", quiet=True)
    lines = [l for l in out.splitlines() if l.strip()]
    if not lines:
        print("Nothing to commit. If GitHub is behind, option 3 will push existing commits.")
        code, ahead, _ = run_git(pdir, "rev-list", "--count", "@{u}..HEAD", quiet=True)
        if code == 0 and ahead != "0":
            print(f"{ahead} commit(s) not pushed yet.")
            if input("Push them? (y/n): ").strip().lower() == "y":
                push_now(pdir)
        return
    print(f"\n{len(lines)} files changed:")
    for l in lines[:40]:
        print("  " + l)
    if len(lines) > 40:
        print(f"  ... and {len(lines) - 40} more")
    big = big_files(pdir)
    if big:
        for n, mb in big:
            print(f"  TOO BIG for GitHub (100 MB limit): {n} ({mb} MB)")
        print("Remove it or add it to .gitignore, then try again.")
        run_git(pdir, "reset", "-q", quiet=True)
        return
    default_msg = "Update " + time.strftime("%Y-%m-%d %H:%M")
    msg = clean_text(input(f"Commit message [{default_msg}]: ")) or default_msg
    if input("Commit and push? (y/n): ").strip().lower() != "y":
        run_git(pdir, "reset", "-q", quiet=True)
        print("Cancelled, nothing committed.")
        return
    code, out, err = run_git(pdir, "commit", "-q", "-m", msg, quiet=True)
    if code != 0:
        print(f"Commit failed: {err or out}")
        return
    print("Committed.")
    push_now(pdir)

def push_now(pdir):
    code, _, _ = run_git(pdir, "push", "-u", "origin", "HEAD")
    if code == 0:
        print("Pushed.")
        return
    print("\nThe push did not go through. The usual causes:")
    print(" - not logged in:      pkg install gh ; gh auth login --scopes workflow ; gh auth setup-git")
    print(" - workflow files:     the login needs the workflow scope: gh auth refresh -s workflow")
    print(" - GitHub is ahead:    use option 4 (Pull), then 3 again")

def git_pull(pdir):
    if not is_repo(pdir) or not origin_url(pdir):
        print("Connect the project first (option 2).")
        return
    code, _, _ = run_git(pdir, "pull", "--ff-only")
    if code != 0:
        print("Could not fast-forward. Your folder and GitHub both changed. Tell me what git said and we sort it out;")
        print("nothing was changed by this attempt.")

def github_menu():
    if not git_available():
        return
    pname = select_project()
    if not pname:
        return
    pdir = os.path.join(RAYROOT, pname)
    while True:
        print(f"\n--- GitHub: {pname} ---")
        print("1) Status")
        print("2) Connect to a GitHub repo (set origin)")
        print("3) Commit and push")
        print("4) Pull (fast-forward only)")
        print("5) Recent commits")
        print("0) Back to main menu")
        opt = input("Choose option (0-5): ").strip()
        if opt == "1":
            git_status_summary(pdir)
        elif opt == "2":
            git_connect(pdir)
        elif opt == "3":
            git_commit_push(pdir)
        elif opt == "4":
            git_pull(pdir)
        elif opt == "5":
            if is_repo(pdir) and has_commits(pdir):
                run_git(pdir, "log", "--oneline", "-n", "15")
            else:
                print("No commits yet.")
        elif opt == "0":
            break
        else:
            print("Invalid option.")

def main():
    while True:
        print("\n" + "="*38)
        print("   Raylib WASM Manager (~/m_raylib)")
        print("="*38)
        print("1) Create New Project")
        print("2) Build Project (WASM)")
        print("3) Serve & Run Project (HTTP Server)")
        print("4) Edit / View / Delete Files")
        print("5) List All Projects")
        print("6) Export Project as Zip (Downloads)")
        print("7) GitHub (status, commit, push, pull)")
        print("8) Apply Update Archive (tar.gz / zip from Downloads)")
        print("9) Exit")
        print("="*38)
        choice = input("Select an option (1-9): ").strip()
        
        if choice == "1":
            create_project()
        elif choice == "2":
            build_project()
        elif choice == "3":
            serve_project()
        elif choice == "4":
            edit_project()
        elif choice == "5":
            show_projects_list()
        elif choice == "6":
            export_project_zip()
        elif choice == "7":
            github_menu()
        elif choice == "8":
            apply_update()
        elif choice == "9":
            sys.exit(0)
        else:
            print("Invalid option, try again.")
        
        input("\nPress Enter to continue...")

if __name__ == "__main__":
    main()
