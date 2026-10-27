#!/usr/bin/env python3
"""
new_project.py - export a standalone firmware project from ak-mcu-base.

The repository also holds the web site, the old base (legacy/) and the CI
set-up. A product project needs none of that, so this script copies the base
alone into one self-contained folder:

  python tools/new_project.py D:/work/my-product --board my-board

  my-product/
    src/              kernel/ hal/ common/ services/ boot/ app/ demo/ port/
    tests/ tools/ docs/
    third_party/      nanoMODBUS, SPL + CMSIS of the STM32L1 (only what the build uses)
    BASE_VERSION      which ak-mcu-base the project started from

Nothing is written into the base. The destination must not exist or be empty.
After the export:  cd <dest>  ->  pio run   (or the CMake targets in Makefile).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time

BASE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
REPO = BASE

# not part of a product project: build output, the old base, repository plumbing
SKIP_DIRS = {"build", ".pio", ".gradle", "__pycache__", "legacy", ".git", ".github", ".vscode", ".superpowers", ".cache"}
# below docs/: the web site and its pictures stay behind, the Markdown comes along
SKIP_DOCS = {"play", "superpowers"}
SKIP_FILES = {"ak_sim_flash.bin", ".mcp.json", "README.en.md"}
SKIP_EXT = {".img", ".pyc"}


def fail(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(1)


def base_version():
    """Tag/commit of the base, for BASE_VERSION. Works without git too."""
    try:
        d = subprocess.run(["git", "-C", BASE, "describe", "--tags", "--always", "--dirty",
                            "--match", "ak-mcu-base-v*"],
                           capture_output=True, text=True, timeout=10)
        if d.returncode == 0 and d.stdout.strip():
            return d.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return "unknown (no git)"


def copy_base(dest):
    n = 0
    for root, dirs, files in os.walk(BASE):
        rel = os.path.relpath(root, BASE)
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not (rel == "docs" and d in SKIP_DOCS)]
        out = dest if rel == "." else os.path.join(dest, rel)
        os.makedirs(out, exist_ok=True)
        for f in files:
            if f in SKIP_FILES or os.path.splitext(f)[1] in SKIP_EXT:
                continue
            if rel == "docs" and f.endswith(".html"):       # pages of the web site
                continue
            shutil.copy2(os.path.join(root, f), os.path.join(out, f))
            n += 1
    return n


def edit(path, subs):
    """Apply (pattern, replacement) pairs; every pattern must match exactly once."""
    with open(path, encoding="utf-8", newline="") as f:
        s = f.read()
    for pat, rep in subs:
        s, k = re.subn(pat, rep, s, count=1, flags=re.M)
        if k != 1:
            fail("%s: pattern not found: %s" % (os.path.basename(path), pat))
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dest", help="new project folder (must not exist or be empty)")
    ap.add_argument("--board", help="board name stored in the image header, max 15 chars "
                                    "(default: keep the base's name). Images built for another "
                                    "board name are refused by the bootloader.")
    a = ap.parse_args(argv)

    dest = os.path.abspath(a.dest)
    if os.path.commonpath([dest, REPO]) == REPO:
        fail("destination is inside the base repository")
    if os.path.exists(dest) and os.listdir(dest):
        fail("destination exists and is not empty: " + dest)
    if a.board and not re.fullmatch(r"[A-Za-z0-9_.-]{1,15}", a.board):
        fail("--board: 1..15 characters of A-Z a-z 0-9 _ . -")
    if not os.path.isdir(os.path.join(BASE, "third_party", "stm32l1", "STM32L1xx_StdPeriph_Driver")):
        fail("SPL not found below " + os.path.join(BASE, "third_party", "stm32l1"))

    # third_party/ (SPL + CMSIS, nanoMODBUS) is part of the base and comes along;
    # platformio.ini and port.cmake already point at them
    n = copy_base(dest)
    if a.board:
        edit(os.path.join(dest, "src", "port", "stm32l151", "port_cfg.h"), [
            (r'^(#define PORT_BOARD_NAME\s+)"[^"]*"', r'\1"%s"' % a.board),
        ])

    ver = base_version()
    with open(os.path.join(dest, "BASE_VERSION"), "w", encoding="utf-8", newline="\n") as f:
        f.write("base: ak-mcu-base %s\n" % ver)
        f.write("source: https://github.com/hohoanganh/ak-mcu-base \n")
        f.write("exported: %s\n" % time.strftime("%Y-%m-%d"))
        if a.board:
            f.write("board name: %s\n" % a.board)

    print("exported %d files (base + vendor SPL/CMSIS) to %s" % (n, dest))
    print("base version: " + ver)
    print("next: cd \"%s\" && pio run        (bootloader: pio run -e boot)" % dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
