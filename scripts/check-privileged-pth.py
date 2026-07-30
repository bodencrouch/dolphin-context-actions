#!/usr/bin/env python3
"""Scan for the local-root-escalation class of bug this project hit twice:

A `.pth` file in a root-owned site-packages directory (created by `sudo pip
install -e .` / `pkexec pip install --user -e .`) inserts a *non-root-owned*
directory into every root Python process's `sys.path`. Anything that can
write to that directory can then get code to run as root the next time any
system tool invokes `sudo python3` / `pkexec ... python3` / a root-run script
that imports a module the injected path shadows.

This is not specific to dolphin-context-actions -- it is a general hazard of
`pip install --user -e .` (or plain `-e .`) ever being run as root, for any
project, anywhere on the system. Run this after any `sudo pip install` /
`pkexec pip install`, or periodically as a sanity check.

Usage:
    scripts/check-privileged-pth.py            # report only
    scripts/check-privileged-pth.py --fix       # also remove hazardous .pth
                                                 # files and their dist-info
"""

import argparse
import glob
import os
import stat
import sys

# /usr* is world-traversable by default, so these glob patterns work for any
# caller. /root/.local is the common case for `pkexec pip install --user -e .`
# (HOME=/root), but /root itself is typically mode 750: not just its .pth
# files but the directory listing itself is invisible to a non-root caller,
# so glob() on a wildcard through it silently returns empty rather than
# raising -- that would make a non-root run of this script falsely report
# "clean" on exactly the location the original incident used. Handled as an
# explicit, un-globbed path instead, so the permission failure is visible.
WORLD_TRAVERSABLE_GLOBS = [
    "/usr/lib*/python3*/site-packages",
    "/usr/local/lib*/python3*/site-packages",
]
ROOT_HOME_SITE_PACKAGES_GLOB = "/root/.local/lib/python3*/site-packages"


def find_pth_files() -> tuple[list[str], list[str]]:
    """Returns (pth_files_found, locations_that_could_not_be_inspected)."""
    found = []
    unreadable = []

    for pattern in WORLD_TRAVERSABLE_GLOBS:
        for site_dir in glob.glob(pattern):
            if not os.access(site_dir, os.R_OK | os.X_OK):
                unreadable.append(site_dir)
                continue
            found.extend(glob.glob(os.path.join(site_dir, "*.pth")))

    if os.access("/root", os.R_OK | os.X_OK):
        for site_dir in glob.glob(ROOT_HOME_SITE_PACKAGES_GLOB):
            found.extend(glob.glob(os.path.join(site_dir, "*.pth")))
    else:
        unreadable.append("/root/.local (root's home directory is not accessible to this user)")

    return sorted(set(found)), unreadable


def pth_target_paths(pth_file: str) -> list[str]:
    """Paths a .pth file adds to sys.path, resolved the way Python resolves them."""
    base = os.path.dirname(pth_file)
    targets = []
    try:
        with open(pth_file, "r", errors="replace") as f:
            for line in f:
                line = line.rstrip("\n")
                if not line or line.startswith("#") or line.startswith("import "):
                    continue
                path = line if os.path.isabs(line) else os.path.join(base, line)
                targets.append(os.path.normpath(path))
    except OSError:
        pass
    return targets


def pth_import_lines(pth_file: str) -> list[str]:
    """"import ..." lines an editable-install .pth executes at interpreter startup.

    This is the more dangerous of the two .pth hazards this tool covers: a
    sys.path entry only becomes code execution when something later imports a
    shadowed module, but a "import ..." line in a .pth file runs
    unconditionally, every time any Python process starts with that
    site-packages directory on its search path -- root or not.

    Scoped to __editable__*.pth specifically, not every .pth on the system:
    an unconditional check across all .pth files flags real, benign system
    packaging the first time this ran against a live machine -- setuptools'
    own distutils-precedence.pth, namespace-package shims like
    protobuf-*-nspkg.pth and Paste-*-nspkg.pth, abrt3.pth -- all root-owned,
    distro-packaged, and reviewed as part of normal OS supply chain trust,
    not something this tool should second-guess or a bare --fix should risk
    deleting. Both incidents this tool exists to catch were `pip install -e`
    editable installs, which is exactly the naming convention setuptools uses
    (PEP 660) for the .pth it generates -- so that's the scope this narrower
    check targets, and it still catches the class of hazard used in a
    constructed test (a crafted __editable__-style .pth with an
    "import subprocess; ..." payload).
    """
    if not os.path.basename(pth_file).startswith("__editable__"):
        return []
    lines = []
    try:
        with open(pth_file, "r", errors="replace") as f:
            for line in f:
                line = line.rstrip("\n")
                if line.startswith("import "):
                    lines.append(line)
    except OSError:
        pass
    return lines


def is_hazardous(path: str) -> str | None:
    """Returns a description of the hazard, or None if the path is safe."""
    if not os.path.exists(path):
        return None
    st = os.stat(path)
    if st.st_uid != 0:
        owner = _username(st.st_uid)
        return f"owned by {owner} (uid {st.st_uid}), not root"
    if st.st_mode & (stat.S_IWGRP | stat.S_IWOTH):
        return "root-owned but group- or world-writable"
    return None


def _username(uid: int) -> str:
    try:
        import pwd
        return pwd.getpwuid(uid).pw_name
    except (ImportError, KeyError):
        return f"uid {uid}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fix", action="store_true", help="Remove hazardous .pth files (needs root for system dirs)")
    args = parser.parse_args()

    hazards: list[tuple[str, str, str]] = []  # (pth_file, target_path, reason)

    pth_files, unreadable = find_pth_files()
    for pth_file in pth_files:
        for line in pth_import_lines(pth_file):
            hazards.append((pth_file, line, "executes unconditionally at interpreter startup"))
        for target in pth_target_paths(pth_file):
            reason = is_hazardous(target)
            if reason:
                hazards.append((pth_file, target, reason))

    if unreadable:
        print("Could not inspect (re-run with sudo for a complete check):")
        for path in unreadable:
            print(f"  {path}")
        print()

    if not hazards:
        if unreadable:
            print("No hazards found in the locations that could be inspected.")
            return 2
        print("No hazardous .pth files found.")
        return 0

    print(f"Found {len(hazards)} hazardous .pth entr{'y' if len(hazards) == 1 else 'ies'}:\n")
    for pth_file, target, reason in hazards:
        print(f"  {pth_file}")
        print(f"    -> {target}")
        print(f"       {reason}")
        print(f"       Any process that can write there can get code run as root")
        print(f"       the next time anything does `sudo python3` / `pkexec ... python3`.")
        print()

    if not args.fix:
        print("Re-run with --fix to remove these .pth files (and their matching")
        print("*.dist-info/*.egg-info directories, if present).")
        return 1

    removed_any = False
    for pth_file, _target, _reason in hazards:
        _remove_pth_and_dist_info(pth_file)
        removed_any = True
    if removed_any:
        print("Removed. Re-run without --fix to confirm the machine is clean.")
    return 0


def _remove_pth_and_dist_info(pth_file: str):
    site_dir = os.path.dirname(pth_file)
    base = os.path.basename(pth_file)
    print(f"Removing {pth_file}")
    os.remove(pth_file)

    # An editable install's .pth is named __editable__.<name>-<version>.pth;
    # its dist-info directory shares the <name>-<version> stem.
    stem = base
    if stem.startswith("__editable__."):
        stem = stem[len("__editable__."):]
    stem = stem[: -len(".pth")] if stem.endswith(".pth") else stem

    for suffix in (".dist-info", ".egg-info"):
        candidate = os.path.join(site_dir, stem + suffix)
        if os.path.isdir(candidate):
            print(f"Removing {candidate}")
            import shutil
            shutil.rmtree(candidate)


if __name__ == "__main__":
    sys.exit(main())
